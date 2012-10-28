#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <string>
#include <stdexcept>

#include <boost/lexical_cast.hpp>

#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "ADARA.h"

#include "Logging.h"

/* TODO better, common place for this */
struct header {
	uint32_t payload_len;
	uint32_t pkt_format;
	uint32_t ts_sec;
	uint32_t ts_nsec;
};

static LoggerPtr logger(Logger::getLogger("SMS.StorageManager"));

int StorageManager::m_base_fd = -1;

StorageContainer::SharedPtr StorageManager::m_cur_container;
StorageFile::SharedPtr StorageManager::m_prologueFile;

StorageManager::ContainerSignal StorageManager::m_contChange;
StorageManager::PrologueSignal StorageManager::m_prologue;

uint32_t StorageManager::m_block_size;
uint64_t StorageManager::m_blocks_used;
uint64_t StorageManager::m_max_blocks_allowed = 0x40000000;

#define RUN_STORAGE_MODE 0660

const char *StorageManager::m_run_filename = "next_run";
const char *StorageManager::m_run_tempname = "next_run.temp";

void StorageManager::init(const std::string &baseDir)
{
	struct stat stats;

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

	if (cleanupRunFiles())
		throw std::runtime_error("Unable to obtain initial run number");

	/* TODO kick off background scan */

	/* Start the initial container */
	startContainer();
}

void StorageManager::stop(void)
{
	endCurrentContainer();
	close(m_base_fd);
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
