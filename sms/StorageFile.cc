
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.StorageFile"));

#include <string>

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

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include "ADARA.h"
#include "StorageFile.h"
#include "StorageContainer.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "utils.h"

namespace fs = boost::filesystem;

struct sync_packet {
	ADARA::Header	hdr;
	uint8_t		signature[16];
	uint64_t	offset;
	uint32_t	comment_len;
	//char		comment[];
} __attribute__((packed));

struct ancient_run_status_packet {
	ADARA::Header	hdr;
	uint32_t	run_number;
	uint32_t	run_start; // EPICS Time...!
	uint32_t	status_number;
} __attribute__((packed));

struct run_status_packet {
	ADARA::Header	hdr;
	uint32_t	run_number;
	uint32_t	run_start; // EPICS Time...!
	uint32_t	status_number;
	uint32_t	paused_number;
	uint32_t	addendum_number;
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
			std::string msg("config(): Unable to parse max file size: ");
			msg += e.what();
			ERROR(msg);
			throw std::runtime_error("StorageFile::" + msg);
		}
	}

	val = conf.get<std::string>("storage.syncdist", "");
	if (val.length()) {
		try {
			m_max_sync_distance = parse_size(val);
		} catch (std::runtime_error e) {
			std::string msg("config(): Unable to parse sync distance: ");
			msg += e.what();
			ERROR(msg);
			throw std::runtime_error("StorageFile::" + msg);
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
	SMSControl *ctrl = SMSControl::getInstance();

	//TODO C++ way for this?
	//assert(m_fd_refs);

	m_fdRefs--;
	if (!m_fdRefs) {
		if (m_fd >= 0) {
			if (ctrl->verbose() > 0) {
				DEBUG("Close m_fd=" << m_fd);
			}
			::close(m_fd);
			m_fd = -1;
		}
	}
}

void StorageFile::makePath(bool is_prologue)
{
	StorageContainer::SharedPtr c = m_owner.lock();
	char name[32];

	if (!c) {
		std::string msg(
			"makePath(): StorageFile Owner StorageContainer is Empty!");
		ERROR(msg);
		throw std::logic_error("StorageFile::" + msg);
	}

	m_path = c->name();

	if (is_prologue) {
		if (m_fileNumber) {
			snprintf(name, sizeof(name), "/prologue-ds%08u",
				m_pauseFileNumber);
		}
		else {
			snprintf(name, sizeof(name), "/prologue-m%08u", m_modeNumber );
		}
	}

	else {
		if ( m_paused ) {
			snprintf(name, sizeof(name), "/m%08u-f%08u-p%08u",
				m_modeNumber, m_fileNumber, m_pauseFileNumber);
		}
		else {
			snprintf(name, sizeof(name), "/m%08u-f%08u",
				m_modeNumber, m_fileNumber);
		}
	}

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
	SMSControl *ctrl = SMSControl::getInstance();

	//assert(m_fd < 0 && !m_fd_refs);
	m_fd = openat(StorageManager::base_fd(), m_path.c_str(), flags, 0660);
	if (m_fd < 0) {
		int err = errno;
		std::string msg("open(): openat(");
		msg += m_path;
		msg += ") error: ";
		msg += strerror(err);
		ERROR(msg);
		m_fd = -1;   // just to be sure... ;-b
		throw std::runtime_error("StorageFile::" + msg);
	}
	if (ctrl->verbose() > 0) {
		DEBUG("New File Descriptor m_fd=" << m_fd);
	}

	m_fdRefs = 1;
}

void StorageFile::addSync(void)
{
	struct sync_packet sync = {
		hdr : {
			payload_len : 28,
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::SYNC_TYPE,
				ADARA::PacketType::SYNC_VERSION ),
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

		// Check File Descriptor...
		if (m_fd < 0) {
			ERROR("addSync(): Invalid File Descriptor!"
				<< " m_fd=" << m_fd);
			// This Will Require Cleanup of Raw Data File... ;-b
			if (len != sizeof(sync)) {
				ERROR("addSync(): BUMMER!"
					<< " Partial Write Before Failure -"
					<< " wrote " << (sizeof(sync) - len) << " bytes"
					<< " of " << sizeof(sync) << " total");
			}
			break;   // need to update Sync Distance anyway...
		}

		rc = ::write(m_fd, p, len);
		if (rc <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;

			// *Don't* Throw Exception Here...!
			// We can live without Sync point in Raw Data files...
			// Whine Loudly tho. ;-D
			int err = errno;
			ERROR("addSync() Write Error: "
				<< "m_fd=" << m_fd << " - "
				<< strerror(err));
			// This Will Require Cleanup of Raw Data File... ;-b
			if (len != sizeof(sync)) {
				ERROR("addSync(): BUMMER!"
					<< " Partial Write Before Failure -"
					<< " wrote " << (sizeof(sync) - len) << " bytes"
					<< " of " << sizeof(sync) << " total");
			}
			break;   // need to update Sync Distance anyway...
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
	SMSControl *ctrl = SMSControl::getInstance();

	bool useAncientRunStatusPkt = ctrl->getUseAncientRunStatusPkt();

	struct ancient_run_status_packet ancient_spkt = {
		hdr : {
			payload_len : 12,
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::RUN_STATUS_TYPE, 0x00 ),
		},
	};

	struct run_status_packet spkt = {
		hdr : {
			payload_len : 20,
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::RUN_STATUS_TYPE,
				ADARA::PacketType::RUN_STATUS_VERSION ),
		},
	};

	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);

	if ( useAncientRunStatusPkt )
	{
		ancient_spkt.hdr.ts_sec = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
		ancient_spkt.hdr.ts_nsec = now.tv_nsec;

		ancient_spkt.run_number = m_runNumber;

		if (m_runNumber) {
			// Convert Wallclock Time to EPICS Time...
			ancient_spkt.run_start =
				m_startTime - ADARA::EPICS_EPOCH_OFFSET;
		}
	}

	else
	{
		spkt.hdr.ts_sec = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
		spkt.hdr.ts_nsec = now.tv_nsec;

		spkt.run_number = m_runNumber;

		if (m_runNumber) {
			// Convert Wallclock Time to EPICS Time...
			spkt.run_start = m_startTime - ADARA::EPICS_EPOCH_OFFSET;
		}
	}

	// Ignore Paused File Number in RunStatus Packet...
	// (TODO Figure out how to munge this field if we ever need
	// to _Recover_ any Paused Files into a given run...! ;-)
	// [Solved in V1 Packet Type, See Below... Activated as of 1.8.1]
	if ( useAncientRunStatusPkt )
	{
		ancient_spkt.status_number = (m_fileNumber & 0xfff)
			| ((m_modeNumber & 0xfff) << 12)
			| ((uint32_t) status << 24);
	}

	else
	{
		spkt.status_number = (m_fileNumber & 0xfff)
			| ((m_modeNumber & 0xfff) << 12)
			| ((uint32_t) status << 24);

		spkt.paused_number = m_pauseFileNumber
			| ((uint32_t) m_paused << 24);
		spkt.addendum_number = m_addendumFileNumber
			| ((uint32_t) m_addendum << 24);
	}

	// Stuff Run Status Packet into IoVector for Write to Disk...

	IoVector iovec(1);

	if ( useAncientRunStatusPkt )
	{
		iovec[0].iov_base = &ancient_spkt;
		iovec[0].iov_len = sizeof(ancient_spkt);

		// Write Run Status Packet to Disk...
		if ( !write(iovec, sizeof(ancient_spkt), false) ) {
			// Something Went Wrong Writing This Run Status Pkt to Disk!
			// We will therefore LOSE THIS DATA as a result of the error,
			// so LOG IT HERE in the hopes it can be salvaged later...! ;-Q
			// Fortunately, We'll get Another Copy of this Run Status Pkt
			// with the _Next_ Data File, so we _May_ Be Ok here...?
			std::stringstream ss;
			ss << "LOST Ancient Run Status Packet"
				<< " status=" << status
				<< " modeNumber=" << m_modeNumber
				<< " fileNumber=" << m_fileNumber;
			StorageManager::logIoVector(ss.str(), iovec);
		}
	}

	else
	{
		iovec[0].iov_base = &spkt;
		iovec[0].iov_len = sizeof(spkt);

		// Write Run Status Packet to Disk...
		if ( !write(iovec, sizeof(spkt), false) ) {
			// Something Went Wrong Writing This Run Status Pkt to Disk!
			// We will therefore LOSE THIS DATA as a result of the error,
			// so LOG IT HERE in the hopes it can be salvaged later...! ;-Q
			// Fortunately, We'll get Another Copy of this Run Status Pkt
			// with the _Next_ Data File, so we _May_ Be Ok here...?
			std::stringstream ss;
			ss << "LOST Run Status Packet"
				<< " status=" << status
				<< " modeNumber=" << m_modeNumber
				<< " fileNumber=" << m_fileNumber
				<< " pauseFileNumber=" << m_pauseFileNumber
				<< " paused=" << m_paused
				<< " addendumFileNumber=" << m_addendumFileNumber
				<< " addendum=" << m_addendum;
			StorageManager::logIoVector(ss.str(), iovec);
		}
	}
}

bool StorageFile::write(IoVector &iovec, uint32_t len, bool do_notify,
		uint32_t *written)
{
	// DEBUG("StorageFile::write() entry len=" << len
		// << " nvecs=" << iovec.size());

	struct iovec *vec;
	int nvecs;

	uint32_t retry_count = 0;
	bool partial = false;
	bool ret;

	if ( written )
		*written = 0;

	// On Non-Partial Write Errors, We _Retry_ the Write Twice to Be Sure!
	do
	{
		// Reset to Start of the IoVector...
		vec = &iovec.front();
		nvecs = iovec.size();

		uint32_t remaining = len;

		ret = true;

		ssize_t rc;
		int iovcnt;

		while ( remaining ) {
			iovcnt = nvecs;
			if ( iovcnt > IOV_MAX )
				iovcnt = IOV_MAX;

			// Check File Descriptor...
			if ( m_fd < 0 ) {
				ERROR("write(): Invalid File Descriptor!"
					<< " m_fd=" << m_fd
					<< " retry_count=" << retry_count);
				// This Will Require Cleanup of Data File... ;-b
				if ( partial ) {
					ERROR("write(): BUMMER!"
						<< " Partial IoVector Write Before Failure -"
						<< " wrote " << (iovec.size() - nvecs) << " iovecs"
						<< " of " << iovec.size() << " total"
						<< " (" << remaining << " bytes remaining");
				}
				retry_count = 3; // kick out of retry loop, not recoverable
				ret = false;
				break;	// still need to check file oversize...
			}

			rc = writev( m_fd, vec, iovcnt );
			if ( rc <= 0 ) {
				if ( errno == EAGAIN || errno == EINTR )
					continue;

				// *Don't* Throw Exception Here...!
				// Always Try to Forge On...!
				int err = errno;
				ERROR("write(): writev() Error:"
					<< " m_fd=" << m_fd
					<< " retry_count=" << retry_count << " - "
					<< strerror(err));
				// This Will Require Cleanup of the Data File... ;-b
				if ( partial ) {
					ERROR("write(): BUMMER!"
						<< " Partial IoVector Write Before Failure -"
						<< " wrote " << (iovec.size() - nvecs) << " iovecs"
						<< " of " << iovec.size() << " total"
						<< " (" << remaining << " bytes remaining");
				}
				ret = false;
				break;	// still need to check file oversize...
			}

			// We at least wrote _Part_ of this packet to disk!
			// (Maybe all of it... :-)
			partial = true;

			if ( written )
				*written += rc;

			m_syncDistance += rc;
			m_size += rc;

			if ( rc == remaining )
				break;

			remaining -= rc;

			while ( rc ) {
				if ( vec->iov_len <= (size_t) rc ) {
					rc -= vec->iov_len;
					vec++;
					nvecs--;
				} else {
					uint8_t *p = (uint8_t *) vec->iov_base;
					p += rc;
					vec->iov_base = p;
					vec->iov_len -= rc;
					rc = 0;
				}
			}
		}
	}
	while ( ret == false && !partial && ++retry_count < 3 );

	/* We want the final run status to be the last packet in the file,
	 * so don't add a sync packet if we're no longer active.
	 */
	if ( m_syncDistance >= m_max_sync_distance && m_active )
		addSync();

	/* We don't check the file size unless we're notifying subscribers --
	 * this keeps all of the data for a pulse in the same file. We also
	 * don't want to just end the file here, as we may be instructed
	 * to stop recording before more data comes in, and we don't want
	 * to have a file that's only contents are the "I'm done" indication.
	 */
	if ( do_notify )
		notify();

	// DEBUG("StorageFile::write() exit");

	return( ret );
}

void StorageFile::notify(void)
{
	// DEBUG("StorageFile::notify entry m_size=" << m_size
		// << " m_sizeLastUpdate=" << m_sizeLastUpdate
		// << " m_max_file_size=" << m_max_file_size);

	if (m_size >= m_max_file_size)
		m_oversize = true;

	if (m_size > m_sizeLastUpdate) {
		m_sizeLastUpdate = m_size;
		m_update(*this);
	}

	// DEBUG("StorageFile::notify exit");
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

bool StorageFile::save(IoVector &iovec, uint32_t len, uint32_t *written)
{
	// DEBUG("StorageFile::save() entry len=" << len
		// << " nvecs=" << iovec.size());

	struct iovec *vec;
	int nvecs;

	uint32_t retry_count = 0;
	bool partial = false;
	bool ret;

	if ( written )
		*written = 0;

	// On Non-Partial Write Errors, We _Retry_ the Write Twice to Be Sure!
	do
	{
		// Reset to Start of the IoVector...
		vec = &iovec.front();
		nvecs = iovec.size();

		uint32_t remaining = len;

		ret = true;

		ssize_t rc;
		int iovcnt;

		while ( remaining ) {
			iovcnt = nvecs;
			if ( iovcnt > IOV_MAX )
				iovcnt = IOV_MAX;

			// Check File Descriptor...
			if ( m_fd < 0 ) {
				ERROR("save(): Invalid File Descriptor!"
					<< " m_fd=" << m_fd
					<< " retry_count=" << retry_count);
				// This Will Require Cleanup of Saved Stream File... ;-b
				if ( partial ) {
					ERROR("save(): BUMMER!"
						<< " Partial IoVector Write Before Failure -"
						<< " wrote " << (iovec.size() - nvecs) << " iovecs"
						<< " of " << iovec.size() << " total"
						<< " (" << remaining << " bytes remaining");
				}
				retry_count = 3; // kick out of retry loop, not recoverable
				ret = false;
				break;	// still need to check file oversize...
			}

			rc = writev( m_fd, vec, iovcnt );
			if ( rc <= 0 ) {
				if ( errno == EAGAIN || errno == EINTR )
					continue;

				// *Don't* Throw Exception Here...!
				// We can live without the Saved Input Stream files...
				// Whine Loudly tho. ;-D
				int err = errno;
				ERROR("save(): writev() Error:"
					<< " m_fd=" << m_fd
					<< " retry_count=" << retry_count << " - "
					<< strerror(err));
				// This Will Require Cleanup of Saved Stream File... ;-b
				if ( partial ) {
					ERROR("save(): BUMMER!"
						<< " Partial IoVector Write Before Failure -"
						<< " wrote " << (iovec.size() - nvecs) << " iovecs"
						<< " of " << iovec.size() << " total"
						<< " (" << remaining << " bytes remaining");
				}
				ret = false;
				break;	// still need to check file oversize...
			}

			// We at least wrote _Part_ of this packet to disk!
			// (Maybe all of it... :-)
			partial = true;

			if ( written )
				*written += rc;

			m_size += rc;

			if ( rc == remaining )
				break;

			remaining -= rc;

			while ( rc ) {
				if ( vec->iov_len <= (size_t) rc ) {
					rc -= vec->iov_len;
					vec++;
					nvecs--;
				} else {
					uint8_t *p = (uint8_t *) vec->iov_base;
					p += rc;
					vec->iov_base = p;
					vec->iov_len -= rc;
					rc = 0;
				}
			}
		}
	}
	while ( ret == false && !partial && ++retry_count < 3 );

	if ( m_size >= m_max_file_size )
		m_oversize = true;

	// DEBUG("StorageFile::save() exit");

	return( ret );
}

void StorageFile::terminateSave(void)
{
	m_active = false;

	put_fd();
}

StorageFile::StorageFile(OwnerPtr &owner, bool paused,
		uint32_t modeNumber, uint32_t fileNumber,
		uint32_t pauseFileNumber) :
	m_owner(owner), m_runNumber(0),
	m_modeNumber(modeNumber), m_fileNumber(fileNumber),
	m_pauseFileNumber(pauseFileNumber),
	m_addendumFileNumber(0),
	m_startTime(0), // Wallclock Time...!
	m_persist(true), m_oversize(false),
	m_active(false), m_paused(paused), m_addendum(false),
	m_size(0), m_sizeLastUpdate(0), m_syncDistance(0),
	m_fd(-1), m_fdRefs(0)
{
	StorageContainer::SharedPtr c = m_owner.lock();
	if (c) {
		m_runNumber = c->runNumber();
		m_startTime = c->startTime().tv_sec; // Wallclock Time...!
	}
	// Even if Container isn't Paused, this Run file could be (a la import)
	if (pauseFileNumber)
		m_paused = true;
}

StorageFile::SharedPtr StorageFile::newFile(OwnerPtr owner,
		bool paused, uint32_t modeNumber, uint32_t fileNumber,
		uint32_t pauseFileNumber, ADARA::RunStatus::Enum status)
{
	// (Fyi, Non-Zero Paused File Number Forces File to Paused State...!)
	StorageFile::SharedPtr f(
		new StorageFile(owner, paused, modeNumber, fileNumber,
			pauseFileNumber) );
	f->m_active = true;
	f->makePath( status == ADARA::RunStatus::PROLOGUE );
	f->open(O_CREAT|O_EXCL|O_RDWR);
	if ( status != ADARA::RunStatus::PROLOGUE ) {
		f->addSync();
		f->addRunStatus(status);
	}
	return f;
}

StorageFile::SharedPtr StorageFile::stateFile(OwnerPtr runInfo,
					      const std::string &basePath)
{
	StorageFile::SharedPtr f(
		new StorageFile(runInfo, /* paused */ false, 0, 0, 0) );

	SMSControl *ctrl = SMSControl::getInstance();

	f->m_path = basePath + "/SMS-State-XXXXXX";

	/* We don't assume C++11 compliance, so we cannot rely on
	 * string::data() giving us a NULL-terminated string for mkstemp()
	 * to modify in-place, so we have to make a copy, make our file,
	 * then copy back in.
	 */
	char *path = strdup(f->m_path.c_str());
	if (!path) {
		ERROR("stateFile(): strdup(): bad alloc for " << f->m_path);
		throw std::bad_alloc();
	}

	f->m_fd = mkstemp(path);
	if (f->m_fd < 0) {
		int err = errno;
		free(path);
		std::string msg("stateFile(");
		msg += f->m_path;
		msg += ") mkstemp error: ";
		msg += strerror(err);
		ERROR(msg);
		f->m_fd = -1;   // just to be sure... ;-b
		throw std::runtime_error("StorageFile::" + msg);
	}
	if (ctrl->verbose() > 0) {
		DEBUG("New State File Descriptor m_fd=" << f->m_fd);
	}

	try {
		/* We did not increase the string length, but there is no
		 * guarantee the library won't reallocate and fail here.
		 * Clean up any resulting mess.
		 */
		f->m_path = path;
		free(path);
	} catch (...) {
		ERROR("stateFile() reallocation error for " << f->m_path);
		free(path);
		throw;
	}

	/* mkstemp() did the open for us, so we have a reference to the fd */
	f->m_fdRefs++;
	f->addSync();
	f->addRunStatus(ADARA::RunStatus::STATE);
	return f;
}

StorageFile::SharedPtr StorageFile::saveFile(OwnerPtr owner,
		uint32_t dataSourceId, uint32_t saveFileNumber)
{
	StorageFile::SharedPtr f(
		new StorageFile(owner, /* paused */ false, 0, saveFileNumber, 0) );

	f->m_active = true;

	StorageContainer::SharedPtr c = f->m_owner.lock();
	char name[32];

	if (!c) {
		std::string msg(
			"saveFile(): StorageFile Owner StorageContainer is Empty!");
		ERROR(msg);
		throw std::logic_error("StorageFile::" + msg);
	}

	f->m_path = c->name();

	snprintf(name, sizeof(name), "/ds%08u-s%08u",
		dataSourceId, saveFileNumber);

	f->m_path += name;

	if (f->m_runNumber) {
		char postfix[32];

		/* 25 chars max */
		snprintf(postfix, sizeof(postfix), "-run-%u", f->m_runNumber);
		f->m_path += postfix;
	}

	/* +6 chars, maximum size is 38 incl NULL */
	f->m_path += ".adara";

	f->open(O_CREAT|O_EXCL|O_RDWR);

	return f;
}

StorageFile::SharedPtr StorageFile::importFile(OwnerPtr owner,
		const std::string &path, bool &saved_file, uint64_t &saved_size)
{
	fs::path p(path);
	uint32_t modeIndex = 0, fileIndex = 0, saveFileNumber = 0;
	uint32_t pauseFileNumber = 0, addendumFileNumber = 0;
	uint32_t runNumber = 0;
	uint32_t sourceId = 0;
	std::string save_type;

	// Explicitly Parse All Known File Name Types...
	bool paused_file = false;
	bool addendum_file = false;
	if ( sscanf(p.filename().c_str(), "m%u-f%u-p%u-run-%u.adara",
			&modeIndex, &fileIndex, &pauseFileNumber, &runNumber)
				== 4 ) {
		// DEBUG("Ignoring ADARA Paused Run file: " << p);
		paused_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "f%u-p%u-run-%u.adara",
			&fileIndex, &pauseFileNumber, &runNumber) == 3 ) {
		// DEBUG("Ignoring ADARA Paused Run file: " << p);
		paused_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "m%u-f%u-p%u.adara",
			&modeIndex, &fileIndex, &pauseFileNumber) == 3 ) {
		// DEBUG("Ignoring ADARA Paused Non-Run file: " << p);
		paused_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "f%u-p%u.adara",
			&fileIndex, &pauseFileNumber) == 2 ) {
		// DEBUG("Ignoring ADARA Paused Non-Run file: " << p);
		paused_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "ds%u-s%u-run-%u.adara",
			&sourceId, &saveFileNumber, &runNumber) == 3 ) {
		save_type = "Run";
		saved_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "ds%u-s%u.adara",
			&sourceId, &saveFileNumber) == 2 ) {
		save_type = "Non-Run";
		saved_file = true;
	}
	// *Only* Support Addendums to Run Containers, Not Between Runs...
	else if ( sscanf(p.filename().c_str(), "m%u-f%u-add%u-run-%u.adara",
			&modeIndex, &fileIndex, &addendumFileNumber,
			&runNumber) == 4 ) {
		// Verify Valid Addendum File Number...
		if ( !addendumFileNumber ) {
			WARN("Improperly named ADARA file"
				<< " (Zero Addendum File Number): " << p);
			return StorageFile::SharedPtr();
		}
		// Log in StorageContainer::validate(), for Sorted Ordering... :-D
		// DEBUG("Including ADARA Run Addendum file: " << p);
		addendum_file = true;
	}
	// *Only* Support Addendums to Run Containers, Not Between Runs...
	else if ( sscanf(p.filename().c_str(), "f%u-add%u-run-%u.adara",
			&fileIndex, &addendumFileNumber, &runNumber) == 3 ) {
		// Verify Valid Addendum File Number...
		if ( !addendumFileNumber ) {
			WARN("Improperly named ADARA file"
				<< " (Zero Addendum File Number): " << p);
			return StorageFile::SharedPtr();
		}
		// Log in StorageContainer::validate(), for Sorted Ordering... :-D
		// DEBUG("Including ADARA Run Addendum file: " << p);
		addendum_file = true;
	}
	else if ( sscanf(p.filename().c_str(), "prologue-ds%u-run-%u.adara",
			&sourceId, &runNumber) == 2 ) {
		save_type = "Run Save Prologue";
		saved_file = true; // Categorize All Prologue Files as "Saved"
	}
	else if ( sscanf(p.filename().c_str(), "prologue-ds%u.adara",
			&sourceId ) == 1 ) {
		save_type = "Non-Run Save Prologue";
		saved_file = true; // Categorize All Prologue Files as "Saved"
	}
	else if ( sscanf(p.filename().c_str(), "prologue-m%u-run-%u.adara",
			&modeIndex, &runNumber) == 2 ) {
		save_type = "Run Prologue";
		saved_file = true; // Categorize All Prologue Files as "Saved"
	}
	else if ( sscanf(p.filename().c_str(), "prologue-m%u.adara",
			&modeIndex ) == 1 ) {
		save_type = "Non-Run Prologue";
		saved_file = true; // Categorize All Prologue Files as "Saved"
	}
	else if ( sscanf(p.filename().c_str(), "m%u-f%u-run-%u.adara",
				&modeIndex, &fileIndex, &runNumber) != 3
			&& sscanf(p.filename().c_str(), "f%u-run-%u.adara",
				&fileIndex, &runNumber) != 2
			&& sscanf(p.filename().c_str(), "m%u-f%u.adara",
				&modeIndex, &fileIndex) != 2
			&& sscanf(p.filename().c_str(), "f%u.adara",
				&fileIndex) != 1 ) {
		WARN("Improperly named ADARA file: " << p);
		return StorageFile::SharedPtr();
	}

	// Handle Saved Data Source Stream Files/Return Block Count...
	if ( saved_file )
	{
		saved_size = fileSize( path );
		
		// DEBUG("Ignoring ADARA Data Source " << sourceId
			// << " PauseMode Mode Index " << modeIndex
			// << " Saved Input Stream " << save_type << " file: " << p
			// << " (" << saved_size << " bytes)");

		return StorageFile::SharedPtr();
	}

	// Ok to Have Omitted PauseMode Number == 0... (For Backwards Compat)

	// Verify Valid File Number...
	if ( !fileIndex ) {
		WARN("Improperly named ADARA file (Zero File Number): " << p);
		return StorageFile::SharedPtr();
	}

	// Verify Valid Paused File Number... (used to set Paused State!)
	if ( paused_file && !pauseFileNumber ) {
		WARN("Improperly named ADARA file"
			<< " (Zero Pause File Number): " << p);
		return StorageFile::SharedPtr();
	}

	// Validate Any Run Number in File Name vs. Enclosing Container...
	StorageContainer::SharedPtr o = owner.lock();
	if (runNumber != o->runNumber()) {
		WARN("ADARA run doesn't match container for file " << p);
		return StorageFile::SharedPtr();
	}

	// (Fyi, Non-Zero Paused File Number Forces File to Paused State...!)
	StorageFile::SharedPtr f(
		new StorageFile(owner, paused_file, modeIndex, fileIndex,
			pauseFileNumber) );
	f->m_path = path;

	// Don't Throw An Exception Just Trying to Import Some Old Data File...
	// (Whine Loudly Tho... ;-D)
	try {
		f->open(O_RDONLY);
	} catch (std::runtime_error e) {
		std::string msg("importFile(");
		msg += path;
		msg += ") Open Error: ";
		msg += e.what();
		msg += " - Ignoring...";
		ERROR(msg);
		return StorageFile::SharedPtr();
	} catch (...) {
		std::string msg("importFile(");
		msg += path;
		msg += ") Unknown Open Error";
		msg += " - Ignoring...";
		ERROR(msg);
		return StorageFile::SharedPtr();
	}

	f->m_addendum = addendum_file;
	f->m_addendumFileNumber = addendumFileNumber;

	struct stat statbuf;
	int err = fstat(f->m_fd, &statbuf);
	if (err) {
		err = errno;
		// Whine Loudly... ;-D
		std::string msg("importFile(");
		msg += path;
		msg += ") Stat Error: ";
		msg += "f->m_fd=";
		msg += boost::lexical_cast<std::string>(f->m_fd);
		msg += " - ";
		msg += strerror(err);
		ERROR(msg);
	}

	f->put_fd();

	// Don't Throw An Exception Just Trying to Stat Some Old Data File...!
	if (err) {
		return StorageFile::SharedPtr();
	}

	f->m_size = statbuf.st_size;

	return f;
}

uint64_t StorageFile::fileSize(const std::string &path)
{
	std::string full_path = StorageManager::base_dir() + "/" + path;

	struct stat statbuf;
	int err = stat(full_path.c_str(), &statbuf);
	if (err)
		err = errno;

	// Don't Throw An Exception Just Trying to Stat Some Old Data File...!
	// (Whine Loudly Tho... ;-D)
	if (err) {
		std::string msg("fileSize(");
		msg += path;
		msg += ") Stat Error: ";
		msg += strerror(err);
		ERROR(msg);
		return( (uint64_t) 0 );
	}

	uint64_t file_size = statbuf.st_size;

	return( file_size );
}

bool StorageFile::catFile(StorageFile::SharedPtr src)
{
	SMSControl *ctrl = SMSControl::getInstance();

	char buf[1024];
	uint8_t *p;

	int total, nbytes, len, rc;
	int src_fd;

	bool ret = true;

	// Make Sure We Got A Real Source File... ;-D
	if ( !src ) {
		ERROR("catFile():"
			<< " [" << m_path << "]"
			<< " Error Empty Source File!");
		return( false );
	}

	// Open Source File & Retrieve File Descriptor...
	try {
		src_fd = src->get_fd();
	} catch (std::runtime_error re) {
		ERROR("catFile():"
			<< " [" << m_path << "]"
			<< " Unable to Open Source File "
			<< src->m_path << ": " << re.what());
		return( false );
	}

	// Repeatedly Read A Buffer from Source File
	// And Concatenate the Buffer to This File...

	total = 0;

	while ( ret == true ) {

		// Check Source File Descriptor...
		if ( src_fd < 0 ) {
			ERROR("catFile():"
				<< " [" << m_path << "]"
				<< " Invalid File Descriptor"
				<< " for Source File " << src->m_path);
			return( false );
		}

		// Read Buffer from Source File...
		// NOTE: This is Standard C Library read()... ;-o
		rc = ::read( src_fd, buf, sizeof(buf) );

		if ( rc == (ssize_t) -1 ) {
			if ( errno != EAGAIN && errno != EINTR ) {
				int e = errno;
				ERROR("catFile():"
					<< " [" << m_path << "]"
					<< " Unable to Read Source File "
					<< src->m_path << " src_fd=" << src_fd << " - "
					<< strerror(e));
				// *Don't* Throw Exception Here, Just Limp Along
				// and Wait for Help/Restart... ;-D
				if ( src_fd >= 0 ) {
					if ( ctrl->verbose() > 0 ) {
						DEBUG("Close Dead src_fd=" << src_fd);
					}
					src->put_fd();
					src_fd = -1;
				}
				return( false );
			}
			nbytes = 0;
		}
		else {
			// DEBUG("catFile():"
				// << " [" << m_path << "]"
				// << " Read " << rc << " Bytes"
				// << " from Source File " << src->m_path);
			nbytes = rc;
			if ( nbytes == 0 ) {
				// DEBUG("catFile():"
					// << " [" << m_path << "]"
					// << " End of File for Source File "
					// << src->m_path);
				break;
			}
		}

		// Concatenate Buffer to This File...

		p = (uint8_t *) buf;

		for ( len=nbytes ; len ; len -= rc ) {

			// Check File Descriptor...
			if (m_fd < 0) {
				ERROR("catFile():"
					<< " [" << m_path << "]"
					<< " Invalid File Descriptor!"
					<< " m_fd=" << m_fd);
				// This Will Require Cleanup of Raw Data File... ;-b
				if (len != nbytes) {
					ERROR("catFile():"
						<< " [" << m_path << "]"
						<< " BUMMER!"
						<< " Partial Write Before Failure -"
						<< " wrote " << (nbytes - len) << " bytes"
						<< " of " << nbytes << " bytes from This Buffer"
						<< " of the Source File " << src->m_path);
				}
				ret = false;
				break;
			}

			rc = ::write(m_fd, p, len);
			if (rc <= 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;

				// *Don't* Throw Exception Here...!
				// We can live without Sync point in Raw Data files...
				// Whine Loudly tho. ;-D
				int err = errno;
				ERROR("catFile():"
					<< " [" << m_path << "]"
					<< " Write Error: " << "m_fd=" << m_fd << " - "
					<< strerror(err));
				// This Will Require Cleanup of Raw Data File... ;-b
				if (len != nbytes) {
					ERROR("catFile():"
						<< " [" << m_path << "]"
						<< " BUMMER!"
						<< " Partial Write Before Failure -"
						<< " wrote " << (nbytes - len) << " bytes"
						<< " of " << nbytes << " bytes from This Buffer"
						<< " of the Source File " << src->m_path);
				}
				ret = false;
				break;
			}

			m_syncDistance += rc;
			m_size += rc;
			p += rc;
		}

		total += nbytes;
	}

	DEBUG("catFile():"
		<< " [" << m_path << "]"
		<< " Copied " << total << " Bytes"
		<< " from Source File " << src->m_path);

	// Close Source File...
	if ( src_fd >= 0 ) {
		src->put_fd();
		src_fd = -1;
	}

	return( ret );
}

