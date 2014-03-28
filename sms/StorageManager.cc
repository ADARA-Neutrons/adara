#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
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
#include "STSClientMgr.h"
#include "Logging.h"
#include "utils.h"

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
bool StorageManager::m_dailyExhausted;
std::list<std::string> StorageManager::m_dailyCache;

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
std::string StorageManager::m_stateDirPrefix("state-storage");
std::string StorageManager::m_stateDir;
uint32_t StorageManager::m_pulseTime;
uint32_t StorageManager::m_nextIndexTime;
uint32_t StorageManager::m_indexPeriod;
std::list<StorageManager::IndexEntry> StorageManager::m_stateIndex;

boost::thread StorageManager::m_ioThread;

void StorageManager::config(const boost::property_tree::ptree &conf)
{
	m_baseDir = conf.get<std::string>("storage.basedir", "");
	if (!m_baseDir.length()) {
		m_baseDir = conf.get<std::string>("sms.basedir");
		m_baseDir += "/data";
	}

	struct stat stats;
	if (stat(m_baseDir.c_str(), &stats)) {
		int err = errno;
		std::string msg("Unable to stat ");
		msg += m_baseDir;
		msg += ": ";
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	m_stateDir = m_baseDir;
	m_stateDir += "/";
	m_stateDir += m_stateDirPrefix;

	uint64_t maxSize = 0;
	std::string poolsize = conf.get<std::string>("storage.poolsize", "");
	if (poolsize.length()) {
		try {
			maxSize = parse_size(poolsize);
		} catch (std::runtime_error e) {
			std::string msg("Unable to parse storage pool size: ");
			msg += e.what();
			throw std::runtime_error(msg);
		}
	} else {
		/* If the user doesn't specify a size, we'll use a percentage
		 * of the total space, 80% by default.
		 */
		struct statfs fsstats;
		if (statfs(m_baseDir.c_str(), &fsstats)) {
			int err = errno;
			std::string msg("Unable to statfs ");
			msg += m_baseDir;
			msg += ": ";
			msg += strerror(err);
			throw std::runtime_error(msg);
		}

		int percent = conf.get<int>("storage.percent", 80);
		maxSize = fsstats.f_blocks * 100 / percent;
		maxSize *= 512;
	}

	m_block_size = stats.st_blksize;
	m_max_blocks_allowed = maxSize + m_block_size - 1;
	m_max_blocks_allowed /= m_block_size;

	m_indexPeriod = conf.get<uint32_t>("storage.index_period", 300);

	StorageFile::config(conf);
}

void StorageManager::init(void)
{
	m_base_fd = open(m_baseDir.c_str(), O_RDONLY | O_DIRECTORY);
	if (m_base_fd < 0) {
		int err = errno;
		std::string msg("StorageManager::init() open() error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	if (fchdir(m_base_fd)) {
		int err = errno;
		std::string msg("StorageManager::init() chdir() error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	/* Others should not be able to write to our files, but we'll
	 * leave them readable for now.
	 */
	umask(0002);

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

	/* If we have a stale index directory, rename it so that we may
	 * make a new one while we kill the old ones in the background.
	 *
	 * Don't kick off the background delete, we'll do that in lateInit()
	 * as part of walking the directory; this covers us in case there are
	 * other stale index directories present, and ensures the threads are
	 * part of the correct process.
	 */
	if (faccessat(m_base_fd, m_stateDirPrefix.c_str(), 0, 0) == 0) {
		if (retireIndexDir(false)) {
			throw std::runtime_error("Unable to retire stale "
						 "index");
		}
	}
}

void StorageManager::lateInit(void)
{
	/* Clean up any lingering index directories in the background. */
	if (cleanupIndexes())
		throw std::runtime_error("Unable to clean stale indexes");

	/* Set the fencepost for the scan; any containers with a
	 * date after this time have been generated as part of this
	 * invocation of SMS, and will already be accounted for; the
	 * scan process must skip them.
	 */
        clock_gettime(CLOCK_REALTIME, &m_scanStart);

	/* We need a timestamp for the initial index entry; any timestamp
	 * will do, as it will be the catch-all if we are asked to go back
	 * to the beginning of the first container. We just set it here
	 * to note that it has not been overlooked.
	 */
	m_pulseTime = m_nextIndexTime = 1;

	boost::thread io(backgroundIo);
	m_ioThread.swap(io);

	/* The IO thread immediately begins a scan of the store, so consider
	 * it active.
	 */
	m_ioActive = true;

	/* Start the initial container; we do this in lateInit() to give
	 * the Geometry, PixelMap, and any other future experiment information
	 * classes a chance to be created and registered for the prologue
	 * before creating any files -- this ensures all of the correct
	 * information is in every file we create.
	 */
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
		throw std::logic_error("Already have a container");

	if (mkdirat(m_base_fd, m_stateDirPrefix.c_str(), 0775) < 0) {
		int e = errno;
		std::string msg("Unable to create new state dir: ");
		msg += strerror(e);
		throw std::runtime_error(msg);
	}

	clock_gettime(CLOCK_REALTIME, &now);
	m_cur_container = StorageContainer::create(now, run);

	m_contChange(m_cur_container, true);

	/* Containers need to be sure to always have a file; otherwise
	 * there will be no record if we don't currently have pulses
	 * coming in. This isn't a normal situation, but we should handle
	 * it gracefully.
	 *
	 * This needs to happen after we tell interested parties about
	 * the new container, so they don't miss the notification of the
	 * new file.
	 *
	 * We'll see this file via the fileCreated() call back, and add
	 * it to the state index at that point.
	 */
	m_cur_container->newFile();
}

void StorageManager::endCurrentContainer(void)
{
	if (!m_cur_container)
		throw std::logic_error("No container to end");

	m_cur_container->terminate();
	m_contChange(m_cur_container, false);
	m_cur_container.reset();

	/* Now that we've changed containers, our index of past state
	 * snapshots is invalid; clear it out. We'll start repopulating
	 * the index when we create a new container.
	 */
	m_stateIndex.clear();
	retireIndexDir();
}

void StorageManager::stateSnapshot(StorageFile::SharedPtr &f)
{
	if (m_prologueFile)
		throw std::logic_error("Recursive use of prologue files");

	m_prologueFile = f;
	m_prologue();
	m_prologueFile.reset();
}

void StorageManager::fileCreated(StorageFile::SharedPtr &f)
{
	/* Each new file gives us an opportunity to add a state checkpoint
	 * at low cost; we do not need a separate state file as we'll be
	 * taking a snapshot as part of the file creation.
	 */
	StorageFile::SharedPtr noFile;
	indexState(noFile, f, 0);

	stateSnapshot(f);
}

void StorageManager::startRecording(uint32_t run)
{
	if (!run)
		throw std::logic_error("Invalid run number");

	if (m_cur_container->runNumber())
		throw std::logic_error("Already recording");

	endCurrentContainer();
	startContainer(run);
}

void StorageManager::stopRecording(void)
{
	endCurrentContainer();
	startContainer();
}

void StorageManager::notify(void)
{
	m_cur_container->notify();
}

void StorageManager::iterateHistory(uint32_t startSeconds, FileOffSetFunc cb)
{
	if (startSeconds) {
		if (m_stateIndex.empty())
			throw std::logic_error("State index is empty");

		/* We store newest entries to the front of the list, so
		 * we only need to go until we find a entry that has an
		 * older timestamp than we're looking for. That's our
		 * starting point, but we want the iterator to point
		 * past it so the conversion to a reverse_iterator points
		 * to the proper entry.
		 */
		std::list<IndexEntry>::iterator v, it, end;
		end = m_stateIndex.end();
		for (it = m_stateIndex.begin(), v = it++; it != end; v = it++) {
			if (v->m_key < startSeconds)
				break;
		}

		/* We've found the entry that satisfies the request; tell
		 * the callback about the state file, if any. We may not
		 * have one, if the caller's request is satisfied at the
		 * start of a data file.
		 *
		 * Once we've got the state out of the way, resume from the
		 * appropriate location in the associated data file, and
		 * walk to the start of the list, informing the callback
		 * of each data file after our starting position.
		 *
		 * This has the desired side effect of handling the currently
		 * active file without a special case -- as we index every
		 * new file as a snapshot, it will be the last file we hand
		 * to the callback.
		 */
		std::list<IndexEntry>::reverse_iterator rit(it), rend;
		rend = m_stateIndex.rend();

		if (rit->m_stateFile)
			cb(rit->m_stateFile, 0);
		cb(rit->m_dataFile, rit->m_resumeOffset);
		for (rit++; rit != rend; rit++) {
			if (rit->isDataOnly())
				cb(rit->m_dataFile, 0);
		}
	} else {
		/* Ok, we don't want any historical data, so just create a
		 * transient file to hold the current state information.
		 */
		StorageFile::SharedPtr state(StorageFile::stateFile(
						m_cur_container, "/tmp"));
		stateSnapshot(state);
		cb(state, 0);
		state->persist(false);
		state->put_fd();

		/* Now that we've snapshotted the state, inform the
		 * callback about the current file we're working on.
		 */
		StorageFile::SharedPtr &f = m_cur_container->file();
		cb(f, f->size());
	}
}

void StorageManager::addPacket(IoVector &iovec, bool notify)
{
	struct header *hdr = (struct header *) iovec[0].iov_base;
	uint32_t len = validatePacket(iovec);
	off_t size, blocks, resumeLocation;

	if (!m_cur_container)
		throw std::logic_error("No container!");

	switch (hdr->pkt_format) {
	default:
		/* Only pulse data should determine if it is time to take
		 * a new state snapshot.
		 */
		break;
	case ADARA::PacketType::RTDL_V0:
	case ADARA::PacketType::BANKED_EVENT_V0:
	case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
		m_pulseTime = hdr->ts_sec;
		break;
	}

	/* Save off where we are in the stream, as we may need to point
	 * to this location for replay after a snapshot.
	 */
	resumeLocation = m_cur_container->file()->size();
	size = m_cur_container->write(iovec, len, notify);

	/* Is it time to take a state snapshot? If we took one while writing
	 * the current packet out -- ie, we started a new file -- then
	 * that will update m_nextTimeIndex and we'll know to skip it here.
	 */
	if (m_pulseTime >= m_nextIndexTime) {
		StorageFile::SharedPtr state(StorageFile::stateFile(
					     m_cur_container, m_stateDir));
		stateSnapshot(state);
		indexState(state, m_cur_container->file(), resumeLocation);
		state->put_fd();
	}

	/* Is it time to initiate a purge of old data?
	 *
	 * m_blocks_used contains the size of all of our closed files,
	 * and we don't add the current file until we're done with it.
	 */
	blocks = size + m_block_size - 1;
	blocks /= m_block_size;
	if ((m_blocks_used + blocks) > m_max_blocks_allowed) {
		uint64_t goal = m_blocks_used + blocks;
		goal -= m_max_blocks_allowed;
		requestPurge(goal);
	}
}

void StorageManager::addPrologue(IoVector &iovec)
{
	/* We're writing a prologue before putting event data or slow control
	 * updates into a file, so we know we have a current container.
	 */
	if (!m_prologueFile) {
		throw std::logic_error("Invalid use of "
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

	if (iovec[0].iov_len < (4 * sizeof(uint32_t)))
		throw std::logic_error("Initial fragment too small");

	if (len < sizeof(struct header))
		throw std::logic_error("Packet too small");

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

		c = StorageContainer::scan(it->path().string());
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

bool StorageManager::isValidDaily(const std::string &dir)
{
	/* Validate that the directory name is in the proper format
	 * for a daily directory. strptime() allows leading zeros
	 * to be omitted, so we convert back to a string to verify.
	 */
	struct tm tm;
	char *p = strptime(dir.c_str(), "%Y%m%d", &tm);
	if (p && !*p) {
		char tmp[9];
		strftime(tmp, sizeof(tmp), "%Y%m%d", &tm);
		if (strcmp(dir.c_str(), tmp))
			p = NULL;
	}

	return p && !*p;
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

		/* Skip index directories; they are handled by other means. */
		if (file.string().compare(0, m_stateDirPrefix.length(),
						m_stateDirPrefix) == 0)
			continue;

		if (status.type() != fs::directory_file) {
			WARN("Ignoring non-directory '" << it->path() << "'");
			continue;
		}

		if (!isValidDaily(file.string())) {
			WARN("Daily directory '" << it->path()
			      << "' has invalid format");
			continue;
		}

		scanDaily(it->path().string());
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

		STSClientMgr *sts = STSClientMgr::getInstance();
		std::list<StorageContainer::SharedPtr>::iterator it;
		for (it = m_pendingRuns.begin(); it != m_pendingRuns.end();
									++it) {
			INFO("Queuing pending run " << (*it)->runNumber());
			sts->queueRun(*it);
		}

		/* Tell the STS client to start processing the runs we
		 * just queued.
		 */
		if (!m_pendingRuns.empty())
			sts->startConnect();
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

void StorageManager::populateDailyCache(void)
{
	fs::directory_iterator end, it(m_baseDir);

	DEBUG("Building cache of daily directories");
	m_dailyCache.clear();

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		/* Skip over the storage for the next run number */
		if (file == m_run_filename || file == m_run_tempname)
			continue;

		if (status.type() != fs::directory_file)
			continue;

		if (!isValidDaily(file.string()))
			continue;

		m_dailyCache.push_back(file.string());
	}

	/* The daily directories have the format YYYYMMDD, so the default
	 * lexical sort works.
	 */
	m_dailyCache.sort();
}

uint64_t StorageManager::purgeDaily(const std::string &dir, uint64_t goal,
				    bool last)
{
	/* We could cache the list of containers to avoid rescanning
	 * each time we wish to purge, but we expect the list to be
	 * reasonably small, so go for the simple code for now. We
	 * can revist if CPU usage is too high.
	 */
	std::list<fs::path> containers;
	fs::directory_iterator end, it(dir);
	for (; it != end; ++it) {
		fs::file_status status = it->status();

		if (status.type() != fs::directory_file)
			continue;

		containers.push_back(it->path());
	}

	/* The container names have the form YYYYMMDD-HHMMSS.nnnnnnnnn, so
	 * the default lexical sort works.
	 */
	containers.sort();

	uint64_t purged = 0;
	std::list<fs::path>::iterator cit, cend = containers.end();
	for (cit = containers.begin(); purged < goal && cit != cend; ) {
		fs::path &cpath = *cit;

		/* We do the iterator increment in the loop, as we don't
		 * want the container purge to delete the container when
		 * it is the last one in the last daily directory -- ie,
		 * if it could be the current container.
		 */
		++cit;
		purged += StorageContainer::purge(cpath.string(),
						  goal - purged,
						  last && cit == cend);
	}

	/* Try to remove the directory, but expect to fail. */
	try {
		fs::remove(fs::path(dir));
	} catch (fs::filesystem_error e) {
	}

	return purged;
}

uint64_t StorageManager::purgeData(uint64_t purgeRequested)
{
	/* Find oldest container that is purgable, and delete the oldest
	 * file in it. To keep from wasting too much effort, we scan the
	 * base directory once to get a list of daily directories, and
	 * only refresh it when we hit its end without reaching our
	 * purge goal.
	 *
	 * It is worth noting that purgeRequested is in units of the
	 * file system block size.
	 */
	try {
		if (m_dailyExhausted || m_dailyCache.empty())
			populateDailyCache();
	} catch (...) {
		/* If we cannot populate the cache, then we cannot purge. */
		return 0;
	}

	uint64_t purged = 0;
	std::list<std::string>::iterator it, end = m_dailyCache.end();
	for (it = m_dailyCache.begin(); purged < purgeRequested &&
								it != end; ) {
		fs::path dir(m_baseDir);
		dir /= *it;
		if (!fs::exists(dir)) {
			it = m_dailyCache.erase(it);
			continue;
		}

		DEBUG("Purging daily " << *it);

		/* We need to do the increment in the loop, as we may delete
		 * elements from the list as we clean the directories. We
		 * also need to know when we're working on the last known
		 * daily directory so we can tell purgeDir(). It will use
		 * this to avoid erasing the current container.
		 */
		++it;
		purged += purgeDaily(dir.string(), purgeRequested - purged,
				     it == end);
	}

	m_dailyExhausted = (purged < purgeRequested);
	return purged;
}

void StorageManager::indexState(StorageFile::SharedPtr &state,
				StorageFile::SharedPtr &data,
				off_t dataOffset)
{
	IndexEntry entry(m_pulseTime, state, data, dataOffset);
	m_stateIndex.push_front(entry);

	m_nextIndexTime = m_pulseTime + m_indexPeriod;
}

static void removeIndexDir(fs::path *indexDir)
{
	try {
		DEBUG("removeIndexDir " << *indexDir);
		fs::remove_all(*indexDir);
	} catch (fs::filesystem_error err) {
		WARN("Error removing index: " << err.what());
	}

	delete indexDir;
}

static void scheduleIndexRemoval(const fs::path &indexDir)
{
	/* Spawn a background thread to remove this directory, but don't
	 * wait around for it.
	 */
	boost::thread removal(removeIndexDir, new fs::path(indexDir));
	removal.detach();
}

bool StorageManager::retireIndexDir(bool remove)
{
	std::string name;
	uint32_t attempt;

	for (attempt = 1; attempt != 0; attempt++) {
		name = m_stateDirPrefix;
		name.append(".");
		name.append(boost::lexical_cast<std::string>(attempt));

		/* We do this in two steps, as renameat() can overwrite a
		 * directory if it is empty; this could lead to race conditions
		 * with the removal thread, leaving stale directories.
		 */
		if (!faccessat(m_base_fd, name.c_str(), 0,
			      AT_SYMLINK_NOFOLLOW) || errno != ENOENT)
			continue;

		if (renameat(m_base_fd, m_stateDirPrefix.c_str(),
			     m_base_fd, name.c_str()) < 0) {
			int e = errno;
			ERROR("Unable to rename index to " << name << ": "
			      << strerror(e));
			return true;
		}

		break;
	}

	/* We should always succeed before ~4 billion attempts, but make sure
	 * we notice the failure -- we must have a bug.
	 */
	if (!attempt)
		throw std::logic_error("wraparound in retireIndexDir");

	if (remove) {
		fs::path dir(m_baseDir);
		dir /= name;
		scheduleIndexRemoval(dir);
	}

	return false;
}

bool StorageManager::cleanupIndexes(void)
{
	fs::directory_iterator end, it(m_baseDir);

	for (; it != end; ++it) {
		fs::path entry(it->path().filename());

		/* Skip everything except for stale indexes. */
		if (entry.string().compare(0, m_stateDirPrefix.length(),
						m_stateDirPrefix))
			continue;

		fs::file_status status = it->status();
		if (status.type() != fs::directory_file) {
			WARN("Ignoring non-directory '" << it->path()
			     << "' with index prefix");
			continue;
		}

		scheduleIndexRemoval(it->path());
	}

	return false;
}
