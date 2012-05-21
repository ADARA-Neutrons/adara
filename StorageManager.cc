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

boost::shared_ptr<StorageContainer> StorageManager::m_cur_container;

StorageManager::onContChange StorageManager::m_contChange;

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
	m_cur_container = boost::shared_ptr<StorageContainer>(
				new StorageContainer(now, run));

	m_contChange(m_cur_container, true);
}

void StorageManager::stopRecording(void)
{
	endCurrentContainer();
}

void StorageManager::addPacket(IoVector &iovec, bool notify)
{
	struct header *hdr = (struct header *) iovec[0].iov_base;
	off_t size, blocks;
	IoVector::iterator it;
	uint32_t len = 0;

	/* XXX We assume there is no overflow */
	for (it = iovec.begin(); it != iovec.end(); it++)
		len += it->iov_len;

	if (iovec[0].iov_len < (2 * sizeof(uint32_t)))
		throw std::runtime_error("Initial fragment too small");

	if (len < sizeof(*hdr))
		throw std::runtime_error("Packet too small");

	if (!m_cur_container) {
		/* We're not in a run, as we'd already have a container. */
		struct timespec ts = { hdr->ts_sec, hdr->ts_nsec };
		m_cur_container = boost::shared_ptr<StorageContainer>(
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
