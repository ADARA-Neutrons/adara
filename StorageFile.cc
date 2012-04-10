
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "StorageFile.h"
#include "StorageContainer.h"
#include "StorageManager.h"

/* TODO find a better place for these packet structures */
struct header {
	uint32_t payload_len;
	uint32_t pkt_format;
	uint32_t ts_sec;
	uint32_t ts_nsec;
};

struct sync_packet {
	struct header	hdr;
	uint8_t		signature[16];
	uint64_t	offset;
	uint32_t	comment_len;
	//char		comment[];
} __attribute__((packed));

struct run_status_packet {
	struct header	hdr;
	uint32_t	run_number;
	uint32_t	run_start;
	uint32_t	status_number;
} __attribute__((packed));

off_t StorageFile::m_max_sync_distance = 16 * 1024 * 1024;
off_t StorageFile::m_max_file_size = 100 * 1024 * 1024;

StorageFile::~StorageFile()
{
	//assert(!m_fd_refs);
	//assert(m_fd == -1);
}

int StorageFile::get_fd(void)
{
	if (m_fd_refs) {
		m_fd_refs++;
		return m_fd;
	}

	open(O_RDONLY);
	return m_fd;
}

void StorageFile::put_fd(void)
{
	//TODO C++ way for this?
	//assert(m_fd_refs);

	m_fd_refs--;
	if (!m_fd_refs) {
		::close(m_fd);
		m_fd = -1;
	}
}

void StorageFile::open(int flags)
{
	std::string path;
	char name[8];
	uint64_t run;

	//assert(m_fd < 0 && !m_fd_refs);

	path = m_container->name();

	snprintf(name, sizeof(name), "/f%05u", m_file_number);
	path += name;

	run = m_container->runNumber();
	if (run) {
		char postfix[32];

		/* 25 chars max */
		snprintf(postfix, sizeof(postfix), "-run-%lu", run);
		path += postfix;
	}

	/* +6 chars, maximum size is 38 incl NULL */
	path += ".adara";

	m_fd = openat(StorageManager::base_fd(), path.c_str(), flags, 0660);
	if (m_fd < 0)
		throw ADARA::Exception(errno, "StorageFile::open");

	m_fd_refs = 1;
}

void StorageFile::add_sync(void)
{
	struct sync_packet sync = {
		hdr : {
			payload_len : 28,
			pkt_format : ADARA::ADARA_PKT_SYNC_V0,
		},
		signature : { 0x53, 0x4e, 0x53, 0x41, 0x44, 0x41, 0x52, 0x41,
			      0x4f, 0x52, 0x4e, 0x4c, 0x00, 0x00, 0xf0, 0x7f },
	};
	struct timespec now;
	uint8_t *p = (uint8_t *) &sync;
	int len, rc;

	clock_gettime(CLOCK_REALTIME, &now);
	sync.hdr.ts_sec = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	sync.hdr.ts_nsec = now.tv_nsec;
	sync.offset = m_size;

	for (len = sizeof(sync); len; len -= rc) {
		rc = ::write(m_fd, p, len);
		if (rc < 0) {
			if (errno == EINTR)
				continue;

			throw ADARA::Exception(errno, "StorageFile add_sync");
		}

		/* XXX This should not be possible */
		if (!rc)
			throw ADARA::Exception(0, "StorageFile add_sync");

		m_size += rc;
		p += rc;
	}

	/* We want to try to keep sync packets as close to a multiple of
	 * the desired distance as possible.
	 */
	m_sync_distance %= m_max_sync_distance;
}

void StorageFile::add_run_status(ADARA::RunStatus status)
{
	struct run_status_packet spkt = {
		hdr : {
			payload_len : 28,
			pkt_format : ADARA::ADARA_PKT_RUN_STATUS_V0,
		},
	};
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	spkt.hdr.ts_sec = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	spkt.hdr.ts_nsec = now.tv_nsec;

	spkt.run_number = m_container->runNumber();
	if (m_container->runNumber())
		spkt.run_start = m_container->startTime().tv_sec;
	spkt.status_number = m_file_number | ((uint32_t) status << 24);

	write(&spkt, sizeof(spkt));
}

void StorageFile::write(void *data, uint32_t count)
{
	uint8_t *p = (uint8_t *) data;
	int rc;

	while (count) {
		rc = ::write(m_fd, p, count);
		if (rc < 0) {
			if (errno == EINTR)
				continue;

			throw ADARA::Exception(errno, "StorageFile write");
		}

		/* XXX This should not be possible */
		if (!rc)
			throw ADARA::Exception(0, "StorageFile write");

		m_sync_distance += rc;
		m_size += rc;
		p += rc;
		count -= rc;
	}

	if (m_sync_distance >= m_max_sync_distance)
		add_sync();
}

void StorageFile::terminate(ADARA::RunStatus status)
{
	/* Disable the generation of a sync packet as we're closing out
	 * the file and want the run status to be the last packet.
	 */
	m_sync_distance = 0;
	m_active = false;

	add_run_status(status);
	put_fd();
}

StorageFile::StorageFile(boost::shared_ptr<StorageContainer> &container,
			 uint32_t number, bool create,
			 ADARA::RunStatus status) :
	m_container(container), m_oversize(false), m_active(create),
	m_file_number(number), m_fd(-1)
{
	if (!create) {
		struct stat statbuf;
		int rc;

		open(O_RDONLY);
		rc = fstat(m_fd, &statbuf);
		if (rc)
			rc = errno;
		put_fd();

		if (rc)
			throw ADARA::Exception(rc, "StorageFile::StorageFile stat");

		m_size = statbuf.st_size;
		return;
	}

	open(O_CREAT|O_EXCL|O_RDWR);
	add_sync();
	add_run_status(status);
}
