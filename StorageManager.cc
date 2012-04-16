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

	if (stat(baseDir.c_str(), &stats))
		throw ADARA::Exception(errno, "StorageManager::init");

	m_block_size = stats.st_blksize;
	m_base_fd = open(baseDir.c_str(), O_RDONLY | O_DIRECTORY);
	if (m_base_fd < 0)
		throw ADARA::Exception(errno, "StorageManager::init");

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

void StorageManager::addPacket(const void *pkt, uint32_t len, bool notify)
{
	struct header *hdr = (struct header *) pkt;
	off_t size, blocks;

	if (len < sizeof(*hdr))
		throw std::runtime_error("Packet too small");

	if (!m_cur_container) {
		/* We're not in a run, as we'd already have a container. */
		struct timespec ts = { hdr->ts_sec, hdr->ts_nsec };
		m_cur_container = boost::shared_ptr<StorageContainer>(
					new StorageContainer(ts, 0));
		m_contChange(m_cur_container, true);
	}

	size = m_cur_container->write(pkt, len, notify);

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

#if 0
	/* We don't immediately close a file when we exceed the size limit
	 * in order to avoid creating a new file just for the end-of-run
	 * marker. Instead, we wait for the run to start or the next packet
	 * to be written (and clients notified).
	 */
	if (m_cur_file && m_cur_file->oversize()) {
		endCurrentFile(m_cur_container->runNumber() ?
			       ADARA::ADARA_RUN_STATUS_RUN_EOF :
			       ADARA::ADARA_RUN_STATUS_NO_RUN);
	}

	if (!m_cur_file) {
		ADARA::RunStatus status = ADARA::ADARA_RUN_STATUS_NO_RUN;

		if (!m_cur_container) {
			/* We're not in a run, as we'd have a container. */
			struct timespec ts = { hdr->ts_sec, hdr->ts_nsec };
			m_cur_container = boost::shared_ptr<StorageContainer>(
						new StorageContainer(ts, 0));
		} else if (m_cur_container->runNumber()) {
			status = ADARA::ADARA_RUN_STATUS_NEW_RUN;
			if (m_cur_container->runNumber())
				status = ADARA::ADARA_RUN_STATUS_RUN_BOF;
		}

		m_cur_file = boost::shared_ptr<StorageFile>(
				new StorageFile(m_cur_container,
						m_cur_container->numFiles() + 1,
						true, status));
		m_cur_container->m_numFiles++;

		/* TODO add persistant information to beginning of file */

		if (notify)
			notifyFileAdded();
	}

	m_cur_file->write(pkt, len);

	/* Is it time to initiate a purge of old data?
	 *
	 * m_blocks_used contains the size of all of our closed files,
	 * and we don't add the current file until we're done with it.
	 */
	blocks = m_cur_file->size() + m_block_size - 1;
	blocks /= m_block_size;
	if ((m_blocks_used + blocks) > m_max_blocks_allowed) {
		/* TODO start a purge of the storage pool */
	}

	/* We don't check the file size unless we're notifying watchers --
	 * this keeps all of the data for a pulse in the same file. We also
	 * don't want to just end the file here, as we may be instructed
	 * to stop recording before more data comes in, and we don't want
	 * to have a file that's sole contents are the "I'm done" indication.
	 */
	if (notify) {
		/* TODO this should reside in StorageFile */
		if (m_cur_file->size() >= StorageFile::m_max_file_size)
			m_cur_file->m_oversize = true;

		notifyFileUpdated();
	}
#endif
}
