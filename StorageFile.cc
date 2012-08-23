#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

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
off_t StorageFile::m_max_file_size = 200 * 1024 * 1024;

StorageFile::~StorageFile()
{
	if (m_tempFile)
		close(m_fd);

	//assert(!m_fd_refs);
	//assert(m_fd == -1);
}

int StorageFile::get_fd(void)
{
	/* We don't do reference counting for temporary files; if we close
	 * the file descriptor, we'll loose the data.
	 */
	if (m_tempFile)
		return m_fd;

	if (m_fdRefs) {
		m_fdRefs++;
		return m_fd;
	}

	open(O_RDONLY);
	return m_fd;
}

void StorageFile::put_fd(void)
{
	/* We don't do reference counting for temporary files; if we close
	 * the file descriptor, we'll loose the data.
	 */
	if (m_tempFile)
		return;

	//TODO C++ way for this?
	//assert(m_fd_refs);

	m_fdRefs--;
	if (!m_fdRefs) {
		::close(m_fd);
		m_fd = -1;
	}
}

void StorageFile::makePath(const StorageContainer &c)
{
	char name[16];

	m_path = c.name();

	snprintf(name, sizeof(name), "/f%05u", m_fileNumber);
	m_path += name;

	if (m_runNumber) {
		char postfix[32];

		/* 25 chars max */
		snprintf(postfix, sizeof(postfix), "-run-%u", m_runNumber);
		m_path += postfix;
	}

	/* +6 chars, maximum size is 38 incl NULL */
	m_path += ".adara";
}

void StorageFile::open(int flags)
{
	//assert(m_fd < 0 && !m_fd_refs);
	m_fd = openat(StorageManager::base_fd(), m_path.c_str(), flags, 0660);
	if (m_fd < 0) {
		int err = errno;
		std::string msg("StorageFile::open() openat error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	m_fdRefs = 1;
}

void StorageFile::addSync(void)
{
	struct sync_packet sync = {
		hdr : {
			payload_len : 28,
			pkt_format : ADARA::PacketType::SYNC_V0,
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
		if (rc <= 0) {
			if (errno == EINTR)
				continue;

			int err = errno;
			std::string msg("StorageFile::addSync() write error: ");
			msg += strerror(err);
			throw std::runtime_error(msg);
		}

		m_size += rc;
		p += rc;
	}

	/* We want to try to keep sync packets as close to a multiple of
	 * the desired distance as possible.
	 */
	m_syncDistance %= m_max_sync_distance;
}

void StorageFile::addRunStatus(ADARA::RunStatus::Enum status)
{
	struct run_status_packet spkt = {
		hdr : {
			payload_len : 12,
			pkt_format : ADARA::PacketType::RUN_STATUS_V0,
		},
	};
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	spkt.hdr.ts_sec = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	spkt.hdr.ts_nsec = now.tv_nsec;

	spkt.run_number = m_runNumber;
	if (m_runNumber)
		spkt.run_start = m_startTime - ADARA::EPICS_EPOCH_OFFSET;
	spkt.status_number = m_fileNumber | ((uint32_t) status << 24);

	IoVector iovec(1);
	iovec[0].iov_base = &spkt;
	iovec[0].iov_len = sizeof(spkt);
	write(iovec, sizeof(spkt), false);
}

off_t StorageFile::write(IoVector &iovec, uint32_t len, bool notify)
{
	struct iovec *vec = &iovec.front();
	int nvecs = iovec.size();
	int iovcnt;
	ssize_t rc;

	while (len) {
		iovcnt = nvecs;
		if (iovcnt > IOV_MAX)
			iovcnt = IOV_MAX;

		rc = writev(m_fd, vec, iovcnt);
		if (rc <= 0) {
			if (errno == EINTR)
				continue;

			int err = errno;
			std::string msg("StorageFile::writev() error: ");
			msg += strerror(err);
			throw std::runtime_error(msg);
		}

		m_syncDistance += rc;
		m_size += rc;

		if (rc == len)
			break;

		len -= rc;
		while (rc) {
			if (vec->iov_len <= (size_t) rc) {
				rc -= vec->iov_len;
				vec++;
				nvecs--;
			} else {
				uint8_t *p = (uint8_t *) vec->iov_len;
				p += rc;
				vec->iov_base = p;
				vec->iov_len -= rc;
				break;
			}
		}
	}

	/* We want the final run status to be the last packet in the file,
	 * so don't add a sync packet if we're no longer active.
	 */
	if (m_syncDistance >= m_max_sync_distance && m_active)
		addSync();

	/* We don't check the file size unless we're notifying subscribers --
	 * this keeps all of the data for a pulse in the same file. We also
	 * don't want to just end the file here, as we may be instructed
	 * to stop recording before more data comes in, and we don't want
	 * to have a file that's only contents are the "I'm done" indication.
	 */
	if (notify) {
		if (m_size >= m_max_file_size)
			m_oversize = true;

		m_update(*this);
	}

	return m_size;
}

void StorageFile::terminate(ADARA::RunStatus::Enum status)
{
	/* Disable the generation of a sync packet as we're closing out
	 * the file and want the run status to be the last packet.
	 */
	m_syncDistance = 0;
	m_active = false;

	addRunStatus(status);
	m_update(*this);
	put_fd();
}

StorageFile::StorageFile(const StorageContainer &container,
			 uint32_t fileNumber, bool create,
			 ADARA::RunStatus::Enum status) :
	m_runNumber(container.runNumber()), m_fileNumber(fileNumber),
	m_startTime(container.startTime().tv_sec), m_oversize(false),
	m_active(create), m_size(0), m_syncDistance(0), m_fd(-1),
	m_tempFile(false)
{
	makePath(container);
	if (!create) {
		struct stat statbuf;
		int rc;

		open(O_RDONLY);
		rc = fstat(m_fd, &statbuf);
		if (rc)
			rc = errno;
		put_fd();

		if (rc) {
			int err = errno;
			std::string msg("StorageFile::StorageFile()"
					" stat error: ");
			msg += strerror(err);
			throw std::runtime_error(msg);
		}

		m_size = statbuf.st_size;
		return;
	}

	open(O_CREAT|O_EXCL|O_RDWR);
	addSync();
	addRunStatus(status);
}

StorageFile::StorageFile(const std::string &path, uint32_t runNumber,
			 uint32_t fileNumber, uint32_t startTime) :
	m_runNumber(runNumber), m_fileNumber(fileNumber),
	m_startTime(startTime), m_oversize(false),
	m_active(true), m_size(0), m_syncDistance(0), m_fd(-1),
	m_tempFile(false)
{
	open(O_CREAT|O_EXCL|O_RDWR);
	addSync();
	addRunStatus(ADARA::RunStatus::STATE);
}

StorageFile::StorageFile(uint32_t runNumber) :
	m_runNumber(runNumber), m_fileNumber(0),
	m_startTime(0), m_oversize(false),
	m_active(false), m_size(0), m_syncDistance(0), m_fd(-1), m_fdRefs(1),
	m_tempFile(true)
{
	/* This constructor is used to generate a temporary state file
	 * for live clients that don't care about historical data.
	 */
	char path_template[] = "/tmp/SMS-Storage-State-XXXXXX";

	m_fd = mkstemp(path_template);
	if (m_fd < 0) {
		int err = errno;
		std::string msg("StorageFile::open() mkstemp error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	/* We don't want this to persist, so delete it so the data is
	 * free'd when we close the file descriptor.
	 */
	unlink(path_template);

	addSync();
	addRunStatus(ADARA::RunStatus::STATE);
}
