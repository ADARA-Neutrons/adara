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

#include <boost/filesystem.hpp>

#include "StorageFile.h"
#include "StorageContainer.h"
#include "StorageManager.h"
#include "Logging.h"
#include "utils.h"

namespace fs = boost::filesystem;

static LoggerPtr logger(Logger::getLogger("SMS.StorageFile"));

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

void StorageFile::config(const boost::property_tree::ptree &conf)
{
	std::string val = conf.get<std::string>("storage.filesize", "");
	if (val.length()) {
		try {
			m_max_file_size = parse_size(val);
		} catch (std::runtime_error e) {
			std::string msg("Unable to parse file size: ");
			msg += e.what();
			throw std::runtime_error(msg);
		}
	}

	val = conf.get<std::string>("storage.syncdist", "");
	if (val.length()) {
		try {
			m_max_sync_distance = parse_size(val);
		} catch (std::runtime_error e) {
			std::string msg("Unable to parse sync distance: ");
			msg += e.what();
			throw std::runtime_error(msg);
		}
	}
}

StorageFile::~StorageFile()
{
	//assert(!m_fd_refs);
	//assert(m_fd == -1);

	if (!m_persist)
		unlink(m_path.c_str());
}

int StorageFile::get_fd(void)
{
	if (m_fdRefs) {
		m_fdRefs++;
		return m_fd;
	}

	open(O_RDONLY);
	return m_fd;
}

void StorageFile::put_fd(void)
{
	//TODO C++ way for this?
	//assert(m_fd_refs);

	m_fdRefs--;
	if (!m_fdRefs) {
		::close(m_fd);
		m_fd = -1;
	}
}

void StorageFile::makePath(void)
{
	StorageContainer::SharedPtr c = m_owner.lock();
	char name[16];

	if (!c)
		throw std::logic_error("StorageFile owner is empty!");

	m_path = c->name();

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

off_t StorageFile::write(IoVector &iovec, uint32_t len, bool do_notify)
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
	if (do_notify)
		notify();

	return m_size;
}

void StorageFile::notify(void)
{
	if (m_size >= m_max_file_size)
		m_oversize = true;

	if (m_size > m_sizeLastUpdate) {
		m_sizeLastUpdate = m_size;
		m_update(*this);
	}
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

StorageFile::StorageFile(OwnerPtr &owner, uint32_t fileNumber) :
	m_owner(owner), m_runNumber(0), m_fileNumber(fileNumber),
	m_startTime(0), m_persist(true), m_oversize(false), m_active(false),
	m_size(0), m_sizeLastUpdate(0), m_syncDistance(0), m_fd(-1), m_fdRefs(0)
{
	StorageContainer::SharedPtr c = m_owner.lock();
	if (c) {
		m_runNumber = c->runNumber();
		m_startTime = c->startTime().tv_sec;
	}
}

StorageFile::SharedPtr StorageFile::newFile(OwnerPtr owner,
					    uint32_t fileNumber,
					    ADARA::RunStatus::Enum status)
{
	StorageFile::SharedPtr f(new StorageFile(owner, fileNumber));
	f->m_active = true;
	f->makePath();
	f->open(O_CREAT|O_EXCL|O_RDWR);
	f->addSync();
	f->addRunStatus(status);
	return f;
}

StorageFile::SharedPtr StorageFile::stateFile(OwnerPtr runInfo,
					      const std::string &basePath)
{
	StorageFile::SharedPtr f(new StorageFile(runInfo, 0));
	f->m_path = basePath + "/SMS-State-XXXXXX";

	/* We don't assume C++11 compliance, so we cannot rely on
	 * string::data() giving us a NULL-terminated string for mkstemp()
	 * to modify in-place, so we have to make a copy, make our file,
	 * then copy back in.
	 */
	char *path = strdup(f->m_path.c_str());
	if (!path)
		throw std::bad_alloc();

	f->m_fd = mkstemp(path);
	if (f->m_fd < 0) {
		int err = errno;
		free(path);
		std::string msg("StorageFile::stateFile(");
		msg += f->m_path;
		msg += ") mkstemp error: ";
		msg += strerror(err);
		throw std::runtime_error(msg);
        }

	try {
		/* We did not increase the string length, but there is no
		 * guarantee the library won't reallocate and fail here.
		 * Clean up any resulting mess.
		 */
		f->m_path = path;
		free(path);
	} catch (...) {
		free(path);
		throw;
	}

	/* mkstemp() did the open for us, so we have a reference to the fd */
	f->m_fdRefs++;
	f->addSync();
	f->addRunStatus(ADARA::RunStatus::STATE);
	return f;
}

StorageFile::SharedPtr StorageFile::importFile(OwnerPtr owner,
					       const std::string &path)
{
	fs::path p(path);
	uint32_t fileNumber, runNumber = 0;

	if (sscanf(p.filename().c_str(), "f%05u-run-%u.adara",
				&fileNumber, &runNumber) == 0 || !fileNumber) {
		WARN("Improperly named ADARA file: " << p);
		return StorageFile::SharedPtr();
	}

	StorageContainer::SharedPtr o = owner.lock();
	if (runNumber != o->runNumber()) {
		WARN("ADARA run doesn't match container for file " << p);
		return StorageFile::SharedPtr();
	}

	StorageFile::SharedPtr f(new StorageFile(owner, fileNumber));
	f->m_path = path;
	f->open(O_RDONLY);

	struct stat statbuf;
	int err = fstat(f->m_fd, &statbuf);
	if (err)
		err = errno;
	f->put_fd();

	if (err) {
		std::string msg("StorageFile::addFile() stat error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	f->m_size = statbuf.st_size;
	return f;
}
