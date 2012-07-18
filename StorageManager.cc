#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string>
#include <stdexcept>

#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "ADARA.h"

/* TODO better, common place for this */
struct header {
	uint32_t payload_len;
	uint32_t pkt_format;
	uint32_t ts_sec;
	uint32_t ts_nsec;
};

int StorageManager::m_base_fd = -1;

StorageContainer::SharedPtr StorageManager::m_cur_container;
StorageFile::SharedPtr StorageManager::m_prologueFile;

StorageManager::ContainerSignal StorageManager::m_contChange;
StorageManager::PrologueSignal StorageManager::m_prologue;

uint32_t StorageManager::m_block_size;
uint64_t StorageManager::m_blocks_used;
uint64_t StorageManager::m_max_blocks_allowed = 0x40000000;

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

	/* TODO kick off background scan */
}

void StorageManager::stop(void)
{
	endCurrentContainer();
	close(m_base_fd);
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

void StorageManager::endCurrentContainer(void)
{
	if (!m_cur_container)
		return;

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
	struct timespec now;

	if (!run)
		throw std::runtime_error("Invalid run number");

	if (m_cur_container) {
		if (m_cur_container->runNumber())
			throw std::runtime_error("Already recording");

		endCurrentContainer();
	}

	clock_gettime(CLOCK_REALTIME, &now);
	m_cur_container = StorageContainer::SharedPtr(
					new StorageContainer(now, run));

	m_contChange(m_cur_container, true);
}

void StorageManager::stopRecording(void)
{
	endCurrentContainer();
}

void StorageManager::iterateHistory(uint32_t startSeconds, FileOffSetFunc cb)
{
	/* TODO allow requests of actual historical data */

	/* Ok, we don't want any historical data, so just create a
	 * transient file to hold the current state information.
	 */

	uint32_t run = 0;
	if (m_cur_container)
		run = m_cur_container->runNumber();

	/* Create a file for the current state, and call the prologue
	 * handlers to populate it.
	 */
	StorageFile::SharedPtr state(new StorageFile(run));
	fileCreated(state);
	cb(state, 0);

	/* Now, tell the callback about the current file we're working on.
	 */
	if (m_cur_container) {
		StorageFile::SharedPtr &f = m_cur_container->file();
		if (f)
			cb(f, f->size());
	}
}

void StorageManager::addPacket(IoVector &iovec, bool notify)
{
	uint32_t len = validatePacket(iovec);
	off_t size, blocks;

	if (!m_cur_container) {
		/* We're not in a run, as we'd already have a container. */
		struct header *hdr = (struct header *) iovec[0].iov_base;
		struct timespec ts = { hdr->ts_sec, hdr->ts_nsec };
		ts.tv_sec += ADARA::EPICS_EPOCH_OFFSET;
		m_cur_container = StorageContainer::SharedPtr(
					new StorageContainer(ts, 0));
		m_contChange(m_cur_container, true);
	}

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
