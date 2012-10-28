#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <string>
#include <stdexcept>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "ADARA.h"
#include "EventFd.h"

#include "Logging.h"

namespace fs = boost::filesystem;

/* TODO better, common place for this */
struct header {
	uint32_t payload_len;
	uint32_t pkt_format;
	uint32_t ts_sec;
	uint32_t ts_nsec;
};

static LoggerPtr logger(Logger::getLogger("SMS.StorageManager"));

std::string StorageManager::m_baseDir;
int StorageManager::m_base_fd = -1;

StorageContainer::SharedPtr StorageManager::m_cur_container;
StorageFile::SharedPtr StorageManager::m_prologueFile;

StorageManager::ContainerSignal StorageManager::m_contChange;
StorageManager::PrologueSignal StorageManager::m_prologue;

uint32_t StorageManager::m_block_size;
uint64_t StorageManager::m_blocks_used;
uint64_t StorageManager::m_max_blocks_allowed = 0x40000000;

struct timespec StorageManager::m_scanStart;
uint64_t StorageManager::m_scannedBlocks;
std::list<StorageContainer::SharedPtr> StorageManager::m_pendingRuns;

bool StorageManager::m_ioActive = false;
EventFd *StorageManager::m_ioStartEvent;
EventFd *StorageManager::m_ioCompleteEvent;
uint64_t StorageManager::m_purgedBlocks;

/* These get passed through an eventfd(), and need to be above the
 * range of blocks possibly up for purge.
 */
#define IOCMD_PURGE_MAX	(((uint64_t) 1) << 50)
#define IOCMD_BASE	(((uint64_t) 1) << 52)
#define IOCMD_SHUTDOWN	(IOCMD_BASE + 1)
#define IOCMD_DONE	(IOCMD_BASE + 2)
#define IOCMD_INITIAL	(IOCMD_BASE + 3)

#define RUN_STORAGE_MODE 0660

const char *StorageManager::m_run_filename = "next_run";
const char *StorageManager::m_run_tempname = "next_run.temp";

boost::thread StorageManager::m_ioThread;

void StorageManager::init(const std::string &baseDir)
{
	struct stat stats;

	m_baseDir = baseDir;

	if (stat(baseDir.c_str(), &stats)) {
		int err = errno;
		std::string msg("StorageManager::init() stat() error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	m_block_size = stats.st_blksize;
	m_base_fd = open(baseDir.c_str(), O_RDONLY | O_DIRECTORY);
	if (m_base_fd < 0) {
		int err = errno;
		std::string msg("StorageManager::init() open() error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	/* m_ioStartEvent will be used by the background IO thread to wait
	 * for requests (blocking reads). m_ioCompleteEvent will be used
	 * in the event loop to let us know when the thread has completed
	 * the current request.
	 *
	 * We create these here rather than statically, as they require the
	 * EPICS fdManager to be instantiated before they can register
	 * their interest in descriptors.
	 */
	m_ioStartEvent = new EventFd();
	m_ioCompleteEvent = new EventFd(boost::bind(
					&StorageManager::ioCompleted));

	if (cleanupRunFiles())
		throw std::runtime_error("Unable to obtain initial run number");

	/* Set the fencepost for the scan; any containers with a
	 * date after this time have been generated as part of this
	 * invocation of SMS, and will already be accounted for; the
	 * scan process must skip them.
	 */
        clock_gettime(CLOCK_REALTIME, &m_scanStart);

	boost::thread io(backgroundIo);
	m_ioThread.swap(io);

	/* The IO thread immediately begins a scan of the store, so consider
	 * it active.
	 */
	m_ioActive = true;

	/* Start the initial container */
	startContainer();
}

void StorageManager::stop(void)
{
	endCurrentContainer();
	close(m_base_fd);

	if (m_ioActive)
		m_ioCompleteEvent->block();

	m_ioStartEvent->signal(IOCMD_SHUTDOWN);
	m_ioThread.join();
	m_ioActive = true;
}

uint32_t StorageManager::readRunFile(const char *name, bool notify)
{
	long run;
	char *p, buffer[16];
	ssize_t len;
	int fd, e;

	fd = openat(m_base_fd, name, O_RDONLY);
	if (fd < 0) {
		if (notify) {
			e = errno;
			ERROR("Unable to open run number storage: "
			      << strerror(e));
		}
		return 0;
	}

	len = read(fd, buffer, sizeof(buffer));
	e = errno;
	close(fd);

	if (len < 0) {
		if (notify) {
			ERROR("Unable to read run number storage: "
			      << strerror(e));
		}
		return 0;
	}

	if (len < 1 || len == sizeof(buffer)) {
		if (notify)
			ERROR("Run storage has invalid size " << len);
		return 0;
	}

	errno = 0;
	buffer[len] = 0;
	run = strtol(buffer, &p, 0);

	/* It's OK to have a newline, even if we won't write one ourselves. */
	if (isspace(*p))
		*p = 0;
	if (*p || errno || run <= 0 || run >= (1L << 32)) {
		if (notify) {
			ERROR("Run storage has invalid data '"
			      << buffer << "'");
		}
		return 0;
	}

	return (uint32_t) run;
}

uint32_t StorageManager::getNextRun(void)
{
	return readRunFile(m_run_filename, true);
}

bool StorageManager::updateNextRun(uint32_t run)
{
	std::string text = boost::lexical_cast<std::string>(run);
	int fd, rc, write_errno = 0, fsync_errno = 0, close_errno = 0;

	fd = openat(m_base_fd, m_run_tempname, O_CREAT|O_TRUNC|O_WRONLY,
		    RUN_STORAGE_MODE);
	if (fd < 0) {
		int e = errno;
		ERROR("Unable to open run number temporary: " << strerror(e));
		return true;
	}

	/* Write the new run number to temporary storage, and ensure it
	 * makes it to disk.
	 */
	rc = write(fd, text.c_str(), text.length());
	if (rc < 0)
		write_errno = errno;
	if (fsync(fd))
		fsync_errno = errno;
	if (close(fd))
		close_errno = errno;

	if (write_errno) {
		ERROR("Unable to write run number temporary: "
		      << strerror(write_errno));
		return true;
	}

	if (rc != (int) text.length()) {
		ERROR("Short write for run number temporary");
		return true;
	}

	if (fsync_errno) {
		ERROR("Unable to fsync run number temporary: "
		      << strerror(fsync_errno));
		return true;
	}

	if (close_errno) {
		ERROR("Close error for run number temporary: "
		      << strerror(close_errno));
		return true;
	}

	/* Ok, atomically rename the temporary storage to the final place
	 * to advance to the new number.
	 */
	if (renameat(m_base_fd, m_run_tempname, m_base_fd, m_run_filename)) {
		int e = errno;
		ERROR("Renaming run storage failed: " << strerror(e));
		unlinkat(m_base_fd, m_run_tempname, 0);
		return true;
	}

	/* We aren't guaranteed the new file names are safe on disk until
	 * we sync the directory that contains them.
	 */
	if (fsync(m_base_fd)) {
		int e = errno;
		ERROR("fsync on base dir for run storage failed: "
		      << strerror(e));
		return true;
	}

	return false;
}

bool StorageManager::cleanupRunFiles(void)
{
	uint32_t nextrun = readRunFile(m_run_filename, true);
	uint32_t temprun = readRunFile(m_run_tempname, false);

	/* We should always have a valid next run file */
	if (!nextrun) {
		ERROR("Missing next run information");
		return true;
	}

	/* If we had a corrupt temporary file, we can just delete it
	 * and move on. Similarly if it isn't monotonically increasing
	 * from the last value.
	 */
	if (temprun <= nextrun) {
		if (temprun) {
			WARN("Stored run number tried to go backwards ("
			     << nextrun << " vs " << temprun << ")");
		}

		if (unlinkat(m_base_fd, m_run_tempname, 0) && errno != ENOENT) {
			int e = errno;
			ERROR("Unable to clean up temp run storage: "
			      << strerror(e));
			return true;
		}

		return false;
	}

	/* Ok, we want the temporary storage to be the new run number, so
	 * complete the move as for a normal update. Just keep things around
	 * if the rename fails.
	 */
	if (renameat(m_base_fd, m_run_tempname, m_base_fd, m_run_filename)) {
		int e = errno;
		ERROR("Renaming run storage failed: " << strerror(e));
		return true;
	}

	/* We aren't guaranteed the new file names are safe on disk until
	 * we sync the directory that contains them.
	 */
	if (fsync(m_base_fd)) {
		int e = errno;
		ERROR("fsync on base dir for run storage failed: "
		      << strerror(e));
		return true;
	}

	return false;
}

void StorageManager::addBaseStorage(off_t size)
{
	off_t blocks;

	/* Now that the file is no longer being written to, we can add
	 * account for its use of blocks
	 */
	blocks = size + m_block_size - 1;
	blocks /= m_block_size;
	m_blocks_used += blocks;
}

void StorageManager::startContainer(uint32_t run)
{
	struct timespec now;

	if (m_cur_container)
		throw std::runtime_error("Already have a container");

	clock_gettime(CLOCK_REALTIME, &now);
	m_cur_container = StorageContainer::create(now, run);

	m_contChange(m_cur_container, true);
}

void StorageManager::endCurrentContainer(void)
{
	if (!m_cur_container)
		throw std::runtime_error("No container to end");

	m_cur_container->terminate();
	m_contChange(m_cur_container, false);
	m_cur_container.reset();
}

void StorageManager::fileCreated(StorageFile::SharedPtr &f)
{
	if (m_prologueFile)
		throw std::runtime_error("Recursive use of prologue files");

	m_prologueFile = f;
	m_prologue();
	m_prologueFile.reset();
}

void StorageManager::startRecording(uint32_t run)
{
	if (!run)
		throw std::runtime_error("Invalid run number");

	if (m_cur_container->runNumber())
		throw std::runtime_error("Already recording");

	endCurrentContainer();
	startContainer(run);
}

void StorageManager::stopRecording(void)
{
	endCurrentContainer();
	startContainer();
}

void StorageManager::iterateHistory(uint32_t startSeconds, FileOffSetFunc cb)
{
	/* TODO allow requests of actual historical data */

	/* Ok, we don't want any historical data, so just create a
	 * transient file to hold the current state information.
	 */

	/* Create a file for the current state, and call the prologue
	 * handlers to populate it.
	 */
	StorageFile::SharedPtr state(StorageFile::stateFile(m_cur_container,
							    "/tmp"));
	state->persist(false);
	fileCreated(state);
	cb(state, 0);

	/* Now, tell the callback about the current file we're working on.
	 */
	StorageFile::SharedPtr &f = m_cur_container->file();
	if (f)
		cb(f, f->size());
}

void StorageManager::addPacket(IoVector &iovec, bool notify)
{
	uint32_t len = validatePacket(iovec);
	off_t size, blocks;

	if (!m_cur_container)
		throw std::runtime_error("No container!");

	size = m_cur_container->write(iovec, len, notify);

	/* Is it time to initiate a purge of old data?
	 *
	 * m_blocks_used contains the size of all of our closed files,
	 * and we don't add the current file until we're done with it.
	 */
	blocks = size + m_block_size - 1;
	blocks /= m_block_size;
	if ((m_blocks_used + blocks) > m_max_blocks_allowed) {
		/* TODO start a purge of the storage pool */
	}
}

void StorageManager::addPrologue(IoVector &iovec)
{
	/* We're writing a prologue before putting event data or slow control
	 * updates into a file, so we know we have a current container.
	 */
	if (!m_prologueFile) {
		throw std::runtime_error("Invalid use of "
					 "StorageManager::addPrologue");
	}

	uint32_t len = validatePacket(iovec);
	m_prologueFile->write(iovec, len, false);
}

uint32_t StorageManager::validatePacket(const IoVector &iovec)
{
	IoVector::const_iterator it;
	uint32_t len = 0;

	/* XXX We assume there is no overflow */
	for (it = iovec.begin(); it != iovec.end(); it++)
		len += it->iov_len;

	if (iovec[0].iov_len < (2 * sizeof(uint32_t)))
		throw std::runtime_error("Initial fragment too small");

	if (len < sizeof(struct header))
		throw std::runtime_error("Packet too small");

	return len;
}

void StorageManager::scanDaily(const std::string &dir)
{
	fs::directory_iterator end, it(dir);
	StorageContainer::SharedPtr c;

	DEBUG("Scanning daily directory " << dir);

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		if (status.type() != fs::directory_file) {
			WARN("Ignoring non-directory '" << it->path() << "'");
			continue;
		}

		c = StorageContainer::scan(it->path().native());
		if (c) {
			m_scannedBlocks += c->blocks();

			/* If this is a (non-manual) run pending translation,
			 * note it for later.
			 */
			if (c->runNumber() && !c->isTranslated() &&
							!c->isManual())
				m_pendingRuns.push_back(c);
		}
	}
}

void StorageManager::scanStorage(void)
{
	fs::directory_iterator end, it(m_baseDir);

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		/* Skip over the storage for the next run number */
		if (file == m_run_filename || file == m_run_tempname)
			continue;

		if (status.type() != fs::directory_file) {
			WARN("Ignoring non-directory '" << it->path() << "'");
			continue;
		}

		/* Validate that the directory name is in the proper format
		 * for a daily directory. strptime() allows leading zeros
		 * to be omitted, so we convert back to a string to verify.
		 */
		struct tm tm;
		char *p = strptime(file.c_str(), "%Y%m%d", &tm);
		if (p && !*p) {
			char tmp[9];
			strftime(tmp, sizeof(tmp), "%Y%m%d", &tm);
			if (strcmp(file.c_str(), tmp))
				p = NULL;
		}

		if (!p || *p) {
			WARN("Daily directory '" << it->path()
			      << "' has invalid format");
			continue;
		}

		scanDaily(it->path().native());
	}

	DEBUG("Scanned " << m_scannedBlocks << " blocks, and had "
	      << m_pendingRuns.size() << " runs pending translation.");
}

void StorageManager::backgroundIo(void)
{
	/* This is the background I/O thread. It is responsible for the
	 * initial scan and verification of the data store, and for purging
	 * old data once the initial scan is complete.
	 *
	 * Communication with the main thread is handled via two EventFd
	 * objects. This thread will perform a blocking read on one, waiting
	 * for instructions from the main event loop. Only one active
	 * instruction is allowed to be outstanding at a time. When that
	 * instruction is completed, the I/O thread will signal the main
	 * loop via a second EventFd that will eventually invoke a callback
	 * function in the proper context.
	 *
	 * The main thread uses the state variable m_ioActive to track if
	 * it has asked the I/O thread to do something. When this variable
	 * is true, it may must not send additional commands, and it must
	 * not touch any of the state communication veriables.
	 *
	 * State variables:
	 * m_ioActive		main loop, track IO request is active
	 * m_ioStartEvent	main loop, signal IO thread of request
	 * m_ioCompleteEvent	IO thread, signal IO request is complete
	 * m_purgedBlocks	IO thread, indicate how many blocks were purged
	 */
	scanStorage();
	m_ioCompleteEvent->signal(IOCMD_INITIAL);

	bool alive = true;
	while (alive) {
		uint64_t cmd = m_ioStartEvent->block();

		/* We only accept two commands -- shutdown, and the
		 * minimum number of blocks to purge.
		 */
		if (cmd == IOCMD_SHUTDOWN)
			alive = false;
		else
			m_purgedBlocks = purgeData(cmd);
		m_ioCompleteEvent->signal(IOCMD_DONE);
	}
}

void StorageManager::ioCompleted(void)
{
	uint64_t val = m_ioCompleteEvent->read();

	if (val == IOCMD_INITIAL) {
		/* Initial scan is complete, so update the size of the
		 * data store, and queue any runs needing translation.
		 *
		 * We add, as we've been taking data while the initial
		 * scan progressed.
		 */
		DEBUG("ioCompleted initially scanned " << m_scannedBlocks);
		m_blocks_used += m_scannedBlocks;

		std::list<StorageContainer::SharedPtr>::iterator it;
		for (it = m_pendingRuns.begin(); it != m_pendingRuns.end();
									++it) {
			// XXX add to STSClientMgr's list
			fprintf(stderr, "Run %u pending\n", (*it)->runNumber());
		}
	} else {
		DEBUG("ioCompleted purged " << m_purgedBlocks << " blocks");
		m_blocks_used -= m_purgedBlocks;
	}

	m_ioActive = false;
}

void StorageManager::requestPurge(uint64_t goal)
{
	/* Only one I/O action at a time. */
	if (m_ioActive)
		return;

	/* In the unlikely event we're purging enough to get into our
	 * command range, just clamp the goal -- we'll pick up and
	 * try again once this purge cycle is complete.
	 */
	if (goal >= IOCMD_PURGE_MAX)
		goal = IOCMD_PURGE_MAX;

	DEBUG("Requesting purge of " << goal << " blocks");

	m_ioActive = true;
	m_ioStartEvent->signal(goal);
}

uint64_t StorageManager::purgeData(uint64_t purgeRequested)
{
	// XXX implement
	return 0;
}
