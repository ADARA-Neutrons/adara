
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.StorageManager"));

#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include "ADARA.h"
#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "ADARAUtils.h"
#include "STCClientMgr.h"
#include "EventFd.h"
#include "utils.h"

namespace fs = boost::filesystem;

RateLimitedLogging::History RLLHistory_StorageManager;

// Rate-Limited Logging IDs...
#define RLL_CONTAINER_SAWTOOTH        0

class PoolsizePV : public smsStringPV {
public:
	PoolsizePV(const std::string &name, uint32_t block_size,
			bool auto_save = false) :
		smsStringPV(name, auto_save), m_block_size(block_size),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		std::string poolsize = value();

		DEBUG("PoolsizePV: " << m_pv_name
			<< " PV Value Changed to [" << poolsize << "]"
			<< ", Set Max Blocks Allowed...");

		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			StorageManager::autoSavePV( m_pv_name, poolsize, &ts );
		}

		uint64_t maxSize;

		// Make Sure Poolsize is Actually Set...! ;-D
		if ( poolsize.length() && poolsize.compare("(unset)") ) {
			try {
				maxSize = parse_size(poolsize);
			} catch (std::runtime_error e) {
				std::string msg("Unable to parse storage pool size: ");
				msg += e.what();
				DEBUG("PoolsizePV changed(): " << msg);
				return;
			}
		} else {
			DEBUG("PoolsizePV changed():"
				<< " Ignoring Empty/Unset PV String Value");
			return;
		}

		DEBUG("Poolsize = " << poolsize << " -> MaxSize = " << maxSize
			<< " (BlockSize=" << m_block_size << ")");

		/* Compute Max Blocks Allowed from Max Size... */
		uint64_t max_blocks_allowed = maxSize + m_block_size - 1;
		max_blocks_allowed /= m_block_size;

		/* Set Max Blocks Allowed for StorageManager... */
		StorageManager::set_max_blocks_allowed(max_blocks_allowed);

		/* Update Max Blocks Allowed EPICS PVs... */
		StorageManager::update_max_blocks_allowed_pv();
	}

private:
	uint64_t m_block_size;

	bool m_auto_save;
};

class PercentPV : public smsUint32PV {
public:
	PercentPV(const std::string &name,
			std::string baseDir, uint32_t block_size,
			uint32_t min = 0, uint32_t max = INT32_MAX,
			bool auto_save = false) :
		smsUint32PV(name, min, max, auto_save),
		m_baseDir(baseDir), m_block_size(block_size),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		uint32_t percent = value();

		DEBUG("PercentPV: " << m_pv_name
			<< " PV Value Changed to " << percent
			<< ", Set Max Blocks Allowed...");

		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			std::stringstream ss;
			ss << percent;
			StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
		}

		uint64_t maxSize;

		// Make Sure Percent is Actually Set...! ;-D
		//    -> *Don't* Set Max Blocks to Zero...! ;-o
		if ( !percent ) {
			DEBUG("PercentPV changed(): Ignoring Zero PV Value");
			return;
		}

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
			DEBUG("PercentPV changed(): " << msg);
			return;
		}

		maxSize = fsstats.f_blocks * ((uint64_t) percent) / 100;
		maxSize *= m_block_size;

		DEBUG("Percent = " << percent << " -> MaxSize = " << maxSize
			<< " (" << m_baseDir << ")"
			<< " (BlockSize=" << m_block_size << ")");

		/* Compute Max Blocks Allowed from Max Size... */
		uint64_t max_blocks_allowed = maxSize + m_block_size - 1;
		max_blocks_allowed /= m_block_size;

		/* Set Max Blocks Allowed for StorageManager... */
		StorageManager::set_max_blocks_allowed(max_blocks_allowed);

		/* Update Max Blocks Allowed EPICS PVs... */
		StorageManager::update_max_blocks_allowed_pv();
	}

private:
	std::string m_baseDir;

	uint64_t m_block_size;

	bool m_auto_save;
};

class MaxBlocksPV : public smsUint32PV {
public:
	MaxBlocksPV(const std::string &name, bool isMultiplier,
			uint32_t min = 0, uint32_t max = INT32_MAX,
			bool auto_save = false) :
		smsUint32PV(name, min, max, auto_save),
		m_isMultiplier(isMultiplier),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		uint32_t max_blocks_allowed_value = value();

		DEBUG( "MaxBlocksPV: " << m_pv_name
			<< " PV value changed, Set Max Blocks Allowed"
			<< ( m_isMultiplier ? " Multiplier" : " Base" )
			<< " to "
			<< max_blocks_allowed_value );

		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			std::stringstream ss;
			ss << max_blocks_allowed_value;
			StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
		}

		// Set Max Blocks Allowed Value (Multiplier/Base)
		// for StorageManager...
		if ( StorageManager::set_max_blocks_allowed_value(
				max_blocks_allowed_value, m_isMultiplier ) )
		{
			/* Update Max Blocks Allowed PVs if Requested Value Changed! */
			StorageManager::update_max_blocks_allowed_pv();
		}
	}

private:
	bool m_isMultiplier;

	bool m_auto_save;
};

class BlockSizePV : public smsUint32PV {
public:
	BlockSizePV(const std::string &name) :
		smsUint32PV(name) {}

	// Make "Read-Only" By Design... ;-D
	bool allowUpdate(const gdd &)
	{
		return false;
	}
};

class RescanRunDirPV : public smsStringPV {
public:
	RescanRunDirPV(const std::string &name) :
		smsStringPV(name) {}

	void changed(void)
	{
		std::string rescanRunDir = value();

		if ( !rescanRunDir.length()
				|| !rescanRunDir.compare( "(unset)" ) ) {
			DEBUG("RescanRunDirPV changed():"
				<< " Ignoring Empty Run Directory Path Value");
			return;
		}

		DEBUG("RescanRunDirPV: " << m_pv_name
			<< " Rescanning [" << rescanRunDir << "]");

		StorageContainer::SharedPtr c =
			StorageContainer::scan( rescanRunDir, true );

		if (c) {

			// XXX Deal With Scanned Blocks & Background I/O Thread
			// m_scannedBlocks += c->blocks();

			if (c->runNumber()) {
				ComBusSMSMon *combus = StorageManager::combus();
				if (c->isTranslated()) {
					ERROR("Rescan Run Directory Already Translated?!"
						<< " Please Reset and Try Again...");
				} else if (c->isManual()) {
					ERROR("Rescan Marked for Manual Processing!"
						<< " Please Check Run Directory for Errors...");
				} else {
					/* Queue for Re-Translation */
					STCClientMgr *stc = STCClientMgr::getInstance();
					INFO("Rescan Queuing Run " << c->runNumber());
					stc->queueRun(c);
					/* Tell the STC client to start processing the runs we
					 * just queued. */
					stc->startConnect();
					/* Send Run Queued Message */
					combus->sendOriginal(c->runNumber(), c->propId(),
							std::string("Rescan STC Send Pending"),
							c->startTime()); // Wallclock Time...!
				}
			}
		}
		else {
			DEBUG("RescanRunDirPV: " << m_pv_name
				<< " Error Scanning [" << rescanRunDir << "]");
		}

		// Done, Reset Rescan Run Directory PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		update("", &now);
	}
};

std::string StorageManager::m_baseDir;
int StorageManager::m_base_fd = -1;

struct timespec StorageManager::m_default_time = { 0, 0 };

StorageFile::SharedPtr StorageManager::dummyFile;
std::vector<StorageFile::SharedPtr> StorageManager::dummySaveFiles;

std::list<StorageContainer::SharedPtr> StorageManager::m_containerStack;

StorageFile::SharedPtr StorageManager::m_prologueFile;

StorageManager::ContainerSignal StorageManager::m_contChange;
StorageManager::PrologueSignal StorageManager::m_prologue;

std::map<uint32_t, boost::shared_ptr<StorageManager::PrologueSignal> >
	StorageManager::m_savePrologue;

std::list<StorageContainer::SharedPtr>::iterator
	StorageManager::m_last_found_it = m_containerStack.end();

struct timespec StorageManager::m_last_ts = { -1, -1 }; // EPICS Time...!

bool StorageManager::m_last_ignore_pkt_timestamp = true;
bool StorageManager::m_last_check_old_containers = false;

std::string StorageManager::m_poolsize;
uint32_t StorageManager::m_percent;

uint32_t StorageManager::m_max_blocks_allowed_multiplier = 1;
uint32_t StorageManager::m_max_blocks_allowed_base = 0x40000000;

uint64_t StorageManager::m_block_size;
uint64_t StorageManager::m_blocks_used;
uint64_t StorageManager::m_max_blocks_allowed =
	(uint64_t) m_max_blocks_allowed_base
		* (uint64_t) m_max_blocks_allowed_multiplier;

boost::shared_ptr<PoolsizePV> StorageManager::m_pvPoolsize;

boost::shared_ptr<PercentPV> StorageManager::m_pvPercent;

boost::shared_ptr<MaxBlocksPV>
	StorageManager::m_pvMaxBlocksAllowed;
boost::shared_ptr<MaxBlocksPV>
	StorageManager::m_pvMaxBlocksAllowedMultiplier;

boost::shared_ptr<BlockSizePV> StorageManager::m_pvBlockSize;

boost::shared_ptr<RescanRunDirPV> StorageManager::m_pvRescanRunDir;

boost::shared_ptr<smsBooleanPV> StorageManager::m_pvComBusVerbose;
bool StorageManager::m_combus_verbose;

boost::shared_ptr<smsFloat64PV>
	StorageManager::m_pvContainerCleanupTimeout;
struct timespec StorageManager::m_container_cleanup_timeout;
double StorageManager::m_container_cleanup_timeout_double;

struct timespec StorageManager::m_scanStart; // Wallclock Time...!

std::list<StorageContainer::SharedPtr> StorageManager::m_pendingRuns;

bool StorageManager::m_ioActive = false;

EventFd *StorageManager::m_ioStartEvent;
EventFd *StorageManager::m_ioCompleteEvent;

uint64_t StorageManager::m_scannedBlocks;
uint64_t StorageManager::m_purgedBlocks;

bool StorageManager::m_dailyExhausted;

std::list< std::pair<std::string, std::map<std::string, uint64_t> > >
	StorageManager::m_dailyCache;

ComBusSMSMon *StorageManager::m_combus;

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

std::map<std::string, std::pair<std::string, std::string> >
	StorageManager::m_autoSaveConfig;

std::string StorageManager::m_autosave_basename = "SMS";
std::string StorageManager::m_autosave_filesuffix = ".autosav";
const char *StorageManager::m_autosave_filename;
int StorageManager::m_autoSaveFd;

#define SMS_AUTOSAVE_ROTATE_SIZE	(3)

boost::thread StorageManager::m_ioThread;

void StorageManager::config(const boost::property_tree::ptree &conf)
{
	m_baseDir = conf.get<std::string>("storage.basedir", "");
	if (!m_baseDir.length()) {
		m_baseDir = conf.get<std::string>("sms.basedir");
		m_baseDir += "/data";
	}

	m_stateDir = m_baseDir;
	m_stateDir += "/";
	m_stateDir += m_stateDirPrefix;

	struct stat stats;
	if (stat(m_baseDir.c_str(), &stats)) {
		int err = errno;
		std::string msg("Unable to stat ");
		msg += m_baseDir;
		msg += ": ";
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	m_block_size = (uint64_t) stats.st_blksize;
	DEBUG("Filesystem (" << m_baseDir << ") Block Size = "
		<< m_block_size);

	// Max Blocks Allowed - Option Priorities:
	//    1. if "max_blocks_allowed" is explicitly set go with that, else
	//    2. if "poolsize" is set, then go with that, else
	//    3. if "percent" is set, then go with that (or its default :-)
	uint64_t max_blocks_allowed =
		conf.get<uint64_t>("storage.max_blocks_allowed", 0);
	if ( max_blocks_allowed != 0 ) {
		DEBUG("Explicit Max Blocks Allowed requested in config at: "
			<< max_blocks_allowed);
	}
	else { // i.e. "not set"...
		uint64_t maxSize = 0;
		DEBUG("Explicit Max Blocks Allowed not in config: Try Poolsize.");
		m_poolsize = conf.get<std::string>("storage.poolsize", "");
		if (m_poolsize.length()) {
			try {
				maxSize = parse_size(m_poolsize);
			} catch (std::runtime_error e) {
				std::string msg("Unable to parse storage pool size: ");
				msg += e.what();
				throw std::runtime_error(msg);
			}
			DEBUG("Poolsize = " << m_poolsize
				<< " -> MaxSize = " << maxSize
				<< " (BlockSize=" << m_block_size << ")");
		} else {
			DEBUG("Poolsize not in config: Use Percent (or default 80%).");
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
			DEBUG("Filesystem (" << m_baseDir << ") Total Blocks = "
				<< fsstats.f_blocks);

			m_percent = conf.get<int>("storage.percent", 80);
			maxSize = ((uint64_t) fsstats.f_blocks)
				* ((uint64_t) m_percent) / 100;
			maxSize *= m_block_size;

			DEBUG("Percent = " << m_percent
				<< " -> MaxSize = " << maxSize
				<< " (BlockSize=" << m_block_size << ")");
		}

		/* Compute Max Blocks Allowed from Max Size... */
		max_blocks_allowed = maxSize + m_block_size - 1;
		max_blocks_allowed /= m_block_size;
	}

	set_max_blocks_allowed(max_blocks_allowed);

	m_indexPeriod = conf.get<uint32_t>("storage.index_period", 300);

	m_combus_verbose = conf.get<bool>("storage.combus_verbose", true);
	DEBUG("ComBus Verbose Set to " << m_combus_verbose);

	m_container_cleanup_timeout_double = conf.get<double>(
		"storage.container_cleanup_timeout", 1.0);

	// ALSO Update Actual Internal Container Cleanup Timeout Fields!
	m_container_cleanup_timeout.tv_sec =
		(uint32_t) m_container_cleanup_timeout_double;
	m_container_cleanup_timeout.tv_nsec =
		(uint32_t) ( ( m_container_cleanup_timeout_double
				- ((double) m_container_cleanup_timeout.tv_sec) )
			* NANO_PER_SECOND_D );
	DEBUG("Storage Container Cleanup Timeout Set to "
		<< m_container_cleanup_timeout_double
		<< " -> (" << m_container_cleanup_timeout.tv_sec
			<< ", " << m_container_cleanup_timeout.tv_nsec << ")");

	StorageFile::config(conf);
}

bool StorageManager::set_max_blocks_allowed_value(
		uint32_t max_blocks_allowed_value, bool isMultiplier )
{
	if ( isMultiplier )
		m_max_blocks_allowed_multiplier = max_blocks_allowed_value;
	else
		m_max_blocks_allowed_base = max_blocks_allowed_value;

	uint64_t max_blocks_allowed = (uint64_t) m_max_blocks_allowed_base
		* (uint64_t) m_max_blocks_allowed_multiplier;

	return( set_max_blocks_allowed( max_blocks_allowed ) );
}

bool StorageManager::set_max_blocks_allowed(uint64_t max_blocks_allowed)
{
	m_max_blocks_allowed = max_blocks_allowed;

	DEBUG("Max Blocks Allowed set to " << m_max_blocks_allowed);

	struct statfs fsstats;
	if (statfs(m_baseDir.c_str(), &fsstats)) {
		int err = errno;
		std::string msg("Unable to statfs ");
		msg += m_baseDir;
		msg += ": ";
		msg += strerror(err);
		DEBUG("Warning: Could Not Stat Base Dir to Validate Max Blocks! "
			<< msg);
		return( false ); // requested value unchanged...
	}
	else {
		/* Limit Max Blocks to Total Size of Filesystem at Most... ;-D */
		if ( (m_max_blocks_allowed * m_block_size)
				> (((uint64_t) fsstats.f_blocks) * m_block_size) ) {
			DEBUG("Max Blocks Too Big: requested size="
				<< (m_max_blocks_allowed * m_block_size)
				<< " > filesystem size="
				<< (fsstats.f_blocks * m_block_size)
				<< " (" << m_baseDir << ")"
				<< " (BlockSize=" << m_block_size << ")");
			m_max_blocks_allowed = (uint64_t) fsstats.f_blocks;
			DEBUG("Max Blocks Allowed limited to "
				<< m_max_blocks_allowed);
			return( true ); // requested value was Changed...!
		}
		else {
			DEBUG("Max Blocks Allowed verified less than filesystem size"
				<< " (" << (m_max_blocks_allowed * m_block_size)
				<< " <= " << (fsstats.f_blocks * m_block_size) << ")"
				<< " (" << m_baseDir << ")"
				<< " (BlockSize=" << m_block_size << ")");
			return( false ); // requested value unchanged...
		}
	}
}

void StorageManager::update_max_blocks_allowed_pv(void)
{
	static uint64_t prime_factors[] = { 2, 3, 5, 7, 11, 13, 17, 19 };
	static uint32_t nfactors = sizeof(prime_factors) / sizeof(uint64_t);

	// Magically Factor Uint64 "Max Blocks Allowed" into Base/Multiplier...
	uint64_t base = m_max_blocks_allowed;
	m_max_blocks_allowed_multiplier = 1;

	// Uint32's in EPICS are Really Int32's... ;-b
	DEBUG("update_max_blocks_allowed_pv(): Before Loop"
		<< " m_max_blocks_allowed=" << m_max_blocks_allowed
		<< " INT32_MAX=" << INT32_MAX
		<< " base=" << base
		<< " multiplier=" << m_max_blocks_allowed_multiplier);

	while ( base > INT32_MAX )
	{
		DEBUG("update_max_blocks_allowed_pv(): Loop"
			<< " base=" << base
			<< " > INT32_MAX=" << INT32_MAX
			<< ", multiplier=" << m_max_blocks_allowed_multiplier);

		// Check Divisibility by Prime Factors...
		bool found_match = false;
		for ( uint32_t i = 0 ; !found_match && i < nfactors ; i++ )
		{
			uint64_t q = base / prime_factors[i];
			DEBUG("prime_factors[" << i << "]=" << prime_factors[i]
				<< " q=" << q);
			if ( q * prime_factors[i] == base )
			{
				DEBUG("Divisible!");
				m_max_blocks_allowed_multiplier *= prime_factors[i];
				base = q;
				found_match = true;
			}
		}

		// No Easy Prime Factor Found, Munge by 10's... ;-Q
		if ( !found_match )
		{
			DEBUG("No Prime Factor Found! Divide By 10...!");
			m_max_blocks_allowed_multiplier *= 10;
			base /= 10;
		}
	}

	m_max_blocks_allowed_base = base;

	DEBUG("update_max_blocks_allowed_pv(): Loop Done"
		<< " m_max_blocks_allowed_base=" << m_max_blocks_allowed_base
		<< " m_max_blocks_allowed_multiplier="
			<< m_max_blocks_allowed_multiplier);

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvMaxBlocksAllowed->update(m_max_blocks_allowed_base, &now);
	m_pvMaxBlocksAllowedMultiplier->update(
		m_max_blocks_allowed_multiplier, &now);
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
	try {
		m_ioStartEvent = new EventFd();
		m_ioCompleteEvent = new EventFd(
			boost::bind( &StorageManager::ioCompleted ) );
	}
	catch ( std::exception &e )
	{
		std::string msg("StorageManager::init():");
		msg += " Error Creating EventFds for Background I/O - ";
		msg += e.what();
		throw std::runtime_error(msg);
	}
	catch (...)
	{
		std::string msg("StorageManager::init():");
		msg += " Error Creating EventFds for Background I/O";
		throw std::runtime_error(msg);
	}

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
			throw std::runtime_error("Unable to retire stale index");
		}
	}

	/* Construct the Default SMS AutoSave File Name... */
	/* Make Copy, Don't Count on Compiler to return Permanent Reference! */
	m_autosave_filename =
		strdup( (m_autosave_basename + m_autosave_filesuffix).c_str() );

	/* Parse Any AutoSave File & Capture PV Config... */
	if ( !parseAutoSaveFile() ) {
		ERROR("init(): Failed to Parse SMS AutoSave File...!");
	}

	/* Initialize AutoSave File Name & Descriptor */
	m_autoSaveFd = -1;
}

void StorageManager::lateInit(void)
{
	/* Clean up any lingering index directories in the background. */
	if (cleanupIndexes())
		throw std::runtime_error("Unable to clean stale indexes");

	/* Create Run-Time Configuration PVs for Storage Manager... */

	SMSControl *ctrl = SMSControl::getInstance();
	if (!ctrl) {
		throw std::logic_error(
			"uninitialized SMSControl obj for StorageManager!");
	}

	std::string prefix(ctrl->getPVPrefix());
	prefix += ":StorageManager";

	m_pvPoolsize = boost::shared_ptr<PoolsizePV>(new
		PoolsizePV(prefix + ":Poolsize", m_block_size,
			/* AutoSave */ true));

	m_pvPercent = boost::shared_ptr<PercentPV>(new
		PercentPV(prefix + ":Percent", m_baseDir, m_block_size,
			0, INT32_MAX, /* AutoSave */ true));

	m_pvMaxBlocksAllowed = boost::shared_ptr<MaxBlocksPV>(new
		MaxBlocksPV(prefix + ":MaxBlocksAllowed", false,
			0, INT32_MAX, /* AutoSave */ true));

	m_pvMaxBlocksAllowedMultiplier = boost::shared_ptr<MaxBlocksPV>(new
		MaxBlocksPV(prefix + ":MaxBlocksAllowedMultiplier", true,
			0, INT32_MAX, /* AutoSave */ true));

	m_pvBlockSize = boost::shared_ptr<BlockSizePV>(new
		BlockSizePV(prefix + ":BlockSize"));

	m_pvRescanRunDir = boost::shared_ptr<RescanRunDirPV>(new
		RescanRunDirPV(prefix + ":RescanRunDir"));

	m_pvComBusVerbose = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":ComBusVerbose"));

	m_pvContainerCleanupTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ContainerCleanupTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON, /* AutoSave */ true));

	ctrl->addPV(m_pvPoolsize);
	ctrl->addPV(m_pvPercent);
	ctrl->addPV(m_pvMaxBlocksAllowed);
	ctrl->addPV(m_pvMaxBlocksAllowedMultiplier);
	ctrl->addPV(m_pvBlockSize);
	ctrl->addPV(m_pvRescanRunDir);
	ctrl->addPV(m_pvComBusVerbose);
	ctrl->addPV(m_pvContainerCleanupTimeout);

	/* Set the fencepost for the scan; any containers with a
	 * date after this time have been generated as part of this
	 * invocation of SMS, and will already be accounted for; the
	 * scan process must skip them.
	 */
	clock_gettime(CLOCK_REALTIME, &m_scanStart);

	/* Initialize Storage Manager PVs... */

	m_pvPoolsize->update(
		m_poolsize.length() ? m_poolsize : "(unset)", &m_scanStart);

	m_pvPercent->update(m_percent, &m_scanStart);

	// Update Max Blocks Allowed EPICS PVs...
	// - m_pvMaxBlocksAllowed
	// - m_pvMaxBlocksAllowedMultiplier
	StorageManager::update_max_blocks_allowed_pv();

	m_pvBlockSize->update((uint32_t) m_block_size, &m_scanStart);

	/* Initialize Rescan Run Directory PV... */
	m_pvRescanRunDir->update("", &m_scanStart);

	/* Initialize ComBus Verbosity PV... */
	m_pvComBusVerbose->update(m_combus_verbose, &m_scanStart);

	/* Initialize Storage Container Cleanup Timeout PV... */
	m_pvContainerCleanupTimeout->update(
		m_container_cleanup_timeout_double, &m_scanStart);

	/* Restore Any PVs to AutoSaved Config Values... */

	struct timespec ts;
	std::string value;
	uint32_t uvalue;
	double dvalue;
	bool bvalue;

	if ( StorageManager::getAutoSavePV(
			m_pvPoolsize->getName(), value, ts ) ) {
		m_poolsize = value;
		m_pvPoolsize->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvPercent->getName(), uvalue, ts ) ) {
		m_percent = uvalue;
		m_pvPercent->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvMaxBlocksAllowed->getName(), uvalue, ts ) ) {
		m_max_blocks_allowed_base = uvalue;
		m_pvMaxBlocksAllowed->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvMaxBlocksAllowedMultiplier->getName(), uvalue, ts ) ) {
		m_max_blocks_allowed_multiplier = uvalue;
		m_pvMaxBlocksAllowedMultiplier->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvComBusVerbose->getName(), bvalue, ts ) ) {
		m_combus_verbose = bvalue;
		m_pvComBusVerbose->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvContainerCleanupTimeout->getName(), dvalue, ts ) ) {
		m_container_cleanup_timeout_double = dvalue;
		m_pvContainerCleanupTimeout->update(dvalue, &ts);

		// ALSO Update Actual Internal Container Cleanup Timeout Fields!
		m_container_cleanup_timeout.tv_sec =
			(uint32_t) m_container_cleanup_timeout_double;
		m_container_cleanup_timeout.tv_nsec =
			(uint32_t) ( ( m_container_cleanup_timeout_double
					- ((double) m_container_cleanup_timeout.tv_sec) )
				* NANO_PER_SECOND_D );
		DEBUG("Storage Container Cleanup Timeout Set to "
			<< m_container_cleanup_timeout_double
			<< " -> (" << m_container_cleanup_timeout.tv_sec
				<< ", " << m_container_cleanup_timeout.tv_nsec << ")");
	}

	/* We need a timestamp for the initial index entry; any timestamp
	 * will do, as it will be the catch-all if we are asked to go back
	 * to the beginning of the first container. We just set it here
	 * to note that it has not been overlooked.
	 */
	m_pulseTime = m_nextIndexTime = 1;

	/* start the monitor thread so that it will be available from
	 * backgroundIo thread
	 */
	m_combus = new ComBusSMSMon(
		ctrl->getBeamlineId(), ctrl->getFacility() );
	m_combus->start();

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
	std::list<StorageContainer::SharedPtr>::iterator it;
	StorageContainer::SharedPtr container;
	m_containerStack.push_front( container );
	it = m_containerStack.begin();
	startContainer( it );
}

void StorageManager::stop(
		const struct timespec &stopTime ) // EPICS Time...!
{
	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	// Note: endCurrentContainer() Decrements the TimeStamp
	// by 1 Nanosecond to use as "Max Time" for the Old Container... ;-D
	endCurrentContainer( it, stopTime, true );

	// Close Down All the Stacked Containers...
	for ( ++it ; it != m_containerStack.end() ; ++it )
	{
		(*it)->terminate();
		m_contChange( (*it), false );
	}

	close(m_base_fd);

	uint64_t value = 0;

	if ( m_ioActive ) {
		if ( !m_ioCompleteEvent->block( value ) ) {
			ERROR("stop(): Error Blocking on I/O Complete Event!"
				<< " value=" << value << "/0x"
					<< std::hex << value << std::dec);
		}
	}

	if ( !m_ioStartEvent->signal( IOCMD_SHUTDOWN ) ) {
		ERROR("stop(): Error Signaling I/O Start Event Shutdown"
			<< " with IOCMD_SHUTDOWN Notification = " << IOCMD_SHUTDOWN
				<< "/0x" << std::hex << IOCMD_SHUTDOWN << std::dec);
	}

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

	// NOTE: This is Standard C Library read()... ;-o
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
			ERROR("Run number storage has invalid size " << len);
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
			ERROR("Run number storage has invalid data '"
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
	struct timespec start, after;
	double elapsed;

	clock_gettime(CLOCK_REALTIME, &start);

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
		ERROR("Renaming run number storage failed: " << strerror(e));
		unlinkat(m_base_fd, m_run_tempname, 0);
		return true;
	}

	/* We aren't guaranteed the new file names are safe on disk until
	 * we sync the directory that contains them.
	 */
	if (fsync(m_base_fd)) {
		int e = errno;
		ERROR("fsync on base dir for run number storage failed: "
			<< strerror(e));
		return true;
	}

	clock_gettime(CLOCK_REALTIME, &after);
	elapsed = calcDiffSeconds( after, start );
	DEBUG("updateNextRun() took Total elapsed=" << elapsed);

	return false;
}

bool StorageManager::cleanupRunFiles(void)
{
	uint32_t nextrun = readRunFile(m_run_filename, true);
	uint32_t temprun = readRunFile(m_run_tempname, false);

	/* We should always have a valid next run file */
	if (!nextrun) {
		ERROR("Missing next run number information");
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
			ERROR("Unable to clean up temp run number storage: "
				<< strerror(e));
			return true;
		}
		errno = 0; // reset errno for possible ENOENT...

		return false;
	}

	/* Ok, we want the temporary storage to be the new run number, so
	 * complete the move as for a normal update. Just keep things around
	 * if the rename fails.
	 */
	if (renameat(m_base_fd, m_run_tempname, m_base_fd, m_run_filename)) {
		int e = errno;
		ERROR("Renaming run number storage failed: " << strerror(e));
		return true;
	}

	/* We aren't guaranteed the new file names are safe on disk until
	 * we sync the directory that contains them.
	 */
	if (fsync(m_base_fd)) {
		int e = errno;
		ERROR("fsync on base dir for run number storage failed: "
			<< strerror(e));
		return true;
	}

	return false;
}

void StorageManager::addBaseStorage(uint64_t size)
{
	uint64_t blocks;

	/* Now that the file is no longer being written to, we can add
	 * account for its use of blocks
	 */
	blocks = size + m_block_size - 1;
	blocks /= m_block_size;
	m_blocks_used += blocks;
}

void StorageManager::startContainer(
		std::list<StorageContainer::SharedPtr>::iterator &it,
		const struct timespec &minTime, // EPICS Time...!
		bool paused, uint32_t run, std::string propId,
		std::string lastName, StorageFile::SharedPtr &lastPrologueFile,
		std::vector<StorageFile::SharedPtr> &lastSavePrologueFiles )
{
	struct timespec now;

	DEBUG("startContainer(): Starting New Container"
		<< " at minTime=" << minTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< minTime.tv_nsec << std::setw(0)
		<< " paused=" << paused
		<< " run=" << run
		<< " propId=" << propId);

	if ( (*it) ) {
		ERROR("startContainer(): Already Have A Container "
			<< (*it)->name()
			<< " in [" << (*it)->minTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->minTime().tv_nsec << std::setw(0)
			<< ", " << (*it)->maxTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
			<< " - Can't Start New Container...!");
		throw std::logic_error("Already Have A Container");
	}

	if ( mkdirat( m_base_fd, m_stateDirPrefix.c_str(), 0775 ) < 0 ) {
		int e = errno;
		std::string msg("Unable to create new state dir: ");
		msg += strerror( e );
		throw std::runtime_error( msg );
	}

	clock_gettime( CLOCK_REALTIME, &now );
	(*it) = StorageContainer::create( now, minTime, run, propId );

	DEBUG("startContainer(): New Current Container "
		<< (*it)->name() << " Created"
		<< " in [" << (*it)->minTime().tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< (*it)->minTime().tv_nsec << std::setw(0)
		<< ", " << (*it)->maxTime().tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< (*it)->maxTime().tv_nsec << std::setw(0) << "]");

	// New Container Starts in Paused Mode...?
	if ( paused ) {

		// Set New Container's Initial PauseMode to "Paused" Mode... ;-D
		// Preset File Number Counter to 1...
		// (No Pre-Increment in Paused Mode)
		(*it)->setPaused( paused, 1 );
	}

	if ( run ) {
		m_combus->sendOriginal( run, propId,
			std::string("SMS run started"), now );
	}

	m_contChange( (*it), true );

	// Copy Any Saved "Last" Prologue Header Files from Old Container
	// Into New Container...
	if ( lastPrologueFile || lastSavePrologueFiles.size() ) {
		std::list<struct StorageContainer::PauseMode>::iterator pm_it;
		(*it)->getCurrentFileIterator( pm_it );
		(*it)->copyLastPrologueFiles( pm_it,
			lastName, lastPrologueFile, lastSavePrologueFiles );
	}

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
	std::list<struct StorageContainer::PauseMode>::iterator pm_it;
	(*it)->getCurrentFileIterator( pm_it );
	(*it)->newFile( pm_it, paused, minTime );
}

void StorageManager::endCurrentContainer(
		std::list<StorageContainer::SharedPtr>::iterator &it,
		const struct timespec &newStart, // EPICS Time...!
		bool do_terminate )
{
	if ( !(*it) )
	{
		std::stringstream ss;
		ss << "endCurrentContainer(): No Container to End!"
			<< " newStart=" << newStart.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< newStart.tv_nsec << std::setw(0)
			<< " do_terminate=" << do_terminate;
		throw std::logic_error( ss.str() );
	}

	DEBUG("endCurrentContainer(): Ending Current Container "
		<< (*it)->name()
		<< " with newStart=" << newStart.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< newStart.tv_nsec << std::setw(0)
		<< " do_terminate=" << do_terminate);

	// Set Max Time for Current Container to 1 Nanosecond
	// _Before_ TimeStamp of Next Container Start Time...
	// (So We Maintain Distinct Time Ranges Per Container...)
	struct timespec maxTime = newStart; // EPICS Time...!
	if ( maxTime.tv_nsec > 0 )
		maxTime.tv_nsec--;
	else {
		maxTime.tv_nsec = NANO_PER_SECOND_LL - 1;
		maxTime.tv_sec--;
	}
	(*it)->setMaxTime( maxTime ); // EPICS Time...!

	DEBUG("endCurrentContainer(): Setting Current Container "
		<< (*it)->name()
		<< " maxTime=" << maxTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< maxTime.tv_nsec << std::setw(0));

	// Get Number of Connected DataSources...
	SMSControl *ctrl = SMSControl::getInstance();
	uint32_t numConnected = ctrl->numConnectedDataSources();

	DEBUG("endCurrentContainer(): Number of Connected DataSources = "
		<< numConnected);

	// Do We Terminate This Container Now, or Keep It Alive on the Stack?
	// Note: If There Are *No More DataSources* Connected Right Now,
	// Then We Should Just Go Ahead and Terminate the Current Container,
	// as There's No Immediate Chance of having its Expiration Triggered!
	if ( do_terminate || numConnected == 0 )
	{
		// REMOVEME
		DEBUG("endCurrentContainer(): Terminating Current Container "
			<< (*it)->name()
			<< " in [" << (*it)->minTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->minTime().tv_nsec << std::setw(0)
			<< ", " << (*it)->maxTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
			<< " do_terminate=" << do_terminate
			<< " numConnected=" << numConnected);

		// Close Down This Container...
		(*it)->terminate();
		m_contChange( (*it), false );
		(*it).reset();
	}
	
	// Keep Current Container Alive on the Stack...
	else
	{
		// Get "Last" Prologue and SavePrologue Files
		// Before Pushing New Container Onto Stack...
		std::list<struct StorageContainer::PauseMode>::iterator pm_it;
		(*it)->getCurrentFileIterator( pm_it );
		(*it)->getLastPrologueFiles( pm_it, true );

		// Push New "Empty" Current Container onto Stack...
		StorageContainer::SharedPtr container;
		m_containerStack.push_front( container );
		it = m_containerStack.begin();

		// Reset "Last Time Stamp" for findContainerByTime()...!
		// ("Just in Case" Changing Container Stack Here
		// Invalidates Saved Iterator)
		m_last_ts.tv_sec = -1; // EPICS Time...!
		m_last_ts.tv_nsec = -1;

		DEBUG("endCurrentContainer():"
			<< " Pushed New Empty Current Container onto Stack"
			<< " do_terminate=" << do_terminate
			<< " numConnected=" << numConnected
			<< " - Btw, the Container Stack now has "
			<< m_containerStack.size() << " elements");
	}

	/* Now that we've changed containers, our index of past state
	 * snapshots is invalid; clear it out. We'll start repopulating
	 * the index when we create a new container.
	 */
	m_stateIndex.clear();
	retireIndexDir();
}

void StorageManager::stateSnapshot( StorageFile::SharedPtr &f,
		bool capture_last )
{
	if (m_prologueFile)
		throw std::logic_error("Recursive use of prologue files");

	m_prologueFile = f;
	m_prologue( capture_last );
	m_prologueFile.reset();
}

void StorageManager::fileCreated( StorageFile::SharedPtr &f,
		bool capture_last, bool addStateToIndex )
{
	/* Each new file gives us an opportunity to add a state checkpoint
	 * at low cost; we do not need a separate state file as we'll be
	 * taking a snapshot as part of the file creation.
	 */
	if ( addStateToIndex ) {
		StorageFile::SharedPtr noFile;
		indexState( noFile, f, 0 );
	}

	stateSnapshot( f, capture_last );
}

void StorageManager::saveCreated( uint32_t dataSourceId,
		bool capture_last )
{
	/* Each new Saved Input Stream file needs to get a copy of
	 * all the Device Descriptors and Starting PV Values,
	 * so trigger a Prologue Spew for the given Data Source. :-D
	 */
	(*(m_savePrologue[ dataSourceId ]))( capture_last );
}

void StorageManager::startRecording(
		const struct timespec &runStart, // Wallclock Time...!
		uint32_t run, std::string propId )
{
	if ( !run ) {
		throw std::logic_error(
			"Can't Start Recording - Invalid Run Number (0)!");
	}

	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	if ( !(*it) ) {
		throw std::logic_error(
			"Can't Start Recording - Invalid State, No Run Container!");
	}

	if ( (*it)->runNumber() ) {
		throw std::logic_error(
			"Can't Start Recording - Already Recording!");
	}

	// Convert Wallclock TimeStamp to EPICS Epoch...
	struct timespec runStartEpics = runStart;
	runStartEpics.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	// Note: endCurrentContainer() Decrements the TimeStamp
	// by 1 Nanosecond to use as "Max Time" for the Old Container... ;-D
	endCurrentContainer( it, runStartEpics, false );

	startContainer( it, runStartEpics, false /* paused */, run, propId );
}

void StorageManager::stopRecording(
		const struct timespec &runStop ) // Wallclock Time...!
{
	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	if ( !(*it) ) {
		throw std::logic_error(
			"Can't Stop Recording - Invalid State, No Run Container!");
	}

	m_combus->sendUpdate(
		(*it)->runNumber(), (*it)->propId(),
		std::string("SMS run stopped") );

	// Convert Wallclock TimeStamp to EPICS Epoch...
	struct timespec runStopEpics = runStop;
	runStopEpics.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	// Start Next Container 1 Nanosecond _After_ Run Stop TimeStamp
	// (So We Maintain Distinct Time Ranges Per Container...)
	struct timespec minTime = runStopEpics; // EPICS Time...!
	minTime.tv_nsec++;
	if ( minTime.tv_nsec >= NANO_PER_SECOND_LL ) {
		minTime.tv_nsec -= NANO_PER_SECOND_LL;
		minTime.tv_sec++;
	}

	// Note: endCurrentContainer() Decrements the TimeStamp
	// by 1 Nanosecond to use as "Max Time" for the Old Container... ;-D
	endCurrentContainer( it, minTime, false ); // EPICS Time...!

	startContainer( it, minTime ); // EPICS Time...!
}

void StorageManager::pauseRecording(
		struct timespec *ts ) // Wallclock Time...!
{
	// Convert Wallclock TimeStamp to EPICS Epoch...
	struct timespec pauseTime = *ts;
	pauseTime.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	(*it)->pause( pauseTime ); // EPICS Time...!

	// Update ComBus Verbosity PV...
	m_combus_verbose = m_pvComBusVerbose->value();

	// (Optionally) Notify ComBus of Run Pause...
	if ( (*it)->runNumber() && m_combus_verbose ) {
		m_combus->sendUpdate(
			(*it)->runNumber(), (*it)->propId(),
			std::string("SMS run paused"));
	}
}

void StorageManager::resumeRecording(
		struct timespec *ts ) // Wallclock Time...!
{
	// Convert Wallclock TimeStamp to EPICS Epoch...
	struct timespec resumeTime = *ts;
	resumeTime.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	(*it)->resume( resumeTime ); // EPICS Time...!

	// Update ComBus Verbosity PV...
	m_combus_verbose = m_pvComBusVerbose->value();

	// (Optionally) Notify ComBus of Run Pause...
	if ( (*it)->runNumber() && m_combus_verbose ) {
		m_combus->sendUpdate(
			(*it)->runNumber(), (*it)->propId(),
			std::string("SMS run resumed"));
	}
}

void StorageManager::notify(void)
{
	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	(*it)->notify();
}

void StorageManager::iterateHistory( uint32_t startSeconds,
		FileOffSetFunc cb )
{
	if ( startSeconds )
	{
		if ( m_stateIndex.empty() )
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
		for ( it = m_stateIndex.begin(), v = it++ ; it != end ; v = it++ )
		{
			if ( v->m_key < startSeconds )
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

		if ( rit->m_stateFile )
			cb( rit->m_stateFile, 0 );
		cb( rit->m_dataFile, rit->m_resumeOffset );
		for ( rit++ ; rit != rend ; rit++ )
		{
			if ( rit->isDataOnly() )
				cb( rit->m_dataFile, 0 );
		}
	}
	else
	{
		// Get Current Container...
		std::list<StorageContainer::SharedPtr>::iterator it;
		it = m_containerStack.begin();

		/* Ok, we don't want any historical data, so just create a
		 * transient file to hold the current state information.
		 */
		StorageFile::SharedPtr state( StorageFile::stateFile(
						(*it), "/tmp" ) );
		stateSnapshot( state, false /* capture_last */ );
		cb( state, 0 );
		state->persist( false );
		state->put_fd();

		/* Now that we've snapshotted the state, inform the
		 * callback about the current file we're working on.
		 */
		StorageFile::SharedPtr &f = (*it)->file();
		cb( f, f->size() );
	}
}

void StorageManager::addPacket( IoVector &iovec,
		bool ignore_pkt_timestamp, bool check_old_containers, bool notify )
{
	// DEBUG("addPacket() entry");

	uint32_t len = validatePacket( iovec );

	ADARA::Header *hdr = (ADARA::Header *) iovec[0].iov_base;
	struct timespec ts;
	ts.tv_sec = hdr->ts_sec; // EPICS Time...!
	ts.tv_nsec = hdr->ts_nsec;

	std::list<StorageContainer::SharedPtr>::iterator it;

	it = findContainerByTime( "addPacket()", ignore_pkt_timestamp,
		ts, check_old_containers );

	if ( it == m_containerStack.end() || !(*it) ) {
		DEBUG("addPacket(): No Container Found"
			<< " for ts=" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< ts.tv_nsec << std::setw(0)
			<< " pkt_format=0x" << std::hex << hdr->pkt_format << std::dec
			<< " check_old_containers=" << check_old_containers
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");
		throw std::logic_error("No container!");
	}

	switch ( ADARA_BASE_PKT_TYPE( hdr->pkt_format ) ) {

		default:
			/* Only pulse data should determine if it is time to take
			 * a new state snapshot.
			 */
			break;

		case ADARA::PacketType::RTDL_TYPE:
		case ADARA::PacketType::BANKED_EVENT_TYPE:
		case ADARA::PacketType::BANKED_EVENT_STATE_TYPE:
		case ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE:
			m_pulseTime = hdr->ts_sec;
			break;
	}

	// Get Proper PauseMode Off of Stack for This Packet/Time...
	std::list<struct StorageContainer::PauseMode>::iterator pm_it;
	(*it)->getPauseModeByTime( pm_it, ignore_pkt_timestamp,
		ts, check_old_containers );

	/* Check for Long-Non-Running Containers that Could Make the SMS
	 * Swell Up and Pop by Eating Up Non-Releasable Local Disk Space...!
	 * - If We're About to Create the 101st File Here (Current File
	 *   is "Oversized" and Waiting to Pop), then Split Container Now...!
	 * [Unless It's Already Been "Split", and Has a Container Max Time!]
	 */
	// Check the Given/Selected PauseMode for Overflow...
	if ( !(*it)->runNumber()
			&& (*it)->maxTime().tv_sec == 0
			&& (*it)->maxTime().tv_nsec == 0
			&& pm_it->m_file
			&& pm_it->m_file->oversize()
			&& ( 1 + (*it)->totFileCount() ) > 100 ) {
		WARN("addPacket(): Long-Non-Running Container "
			<< m_baseDir << "/" << (*it)->name()
			<< " Reached 100 Files"
			<< " (" << pm_it->m_file->path() << ")"
			<< " - Split Container for Purging...");
		// Are We "Terminating" This Container or Not...?
		bool do_terminate = ( (*it)->numPauseModeOnStack() == 1 );
		// Preserve Paused State of Container...
		bool paused = (*it)->paused();
		// Get "Newest" Max DataSource Time
		// for Container Time Range Cut-Off...
		SMSControl *ctrl = SMSControl::getInstance();
		struct timespec maxTime =
			ctrl->newestMaxDataSourceTime(); // EPICS Time...!
		// Take *Lesser* of "Newest" Max DataSource Time and Wallclock...
		// (In Case We Have a "Bogus Future TimeStamp" Gumming Things Up!)
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		if ( compareTimeStamps( now, maxTime ) < 0 ) {
			ERROR("addPacket(): Bogus Future Newest Max DataSource Time "
				<< maxTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< maxTime.tv_nsec << std::setw(0)
				<< " > "
				<< now.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< now.tv_nsec << std::setw(0)
				<< " Current Wallclock Time - Use Wallclock!");
			maxTime.tv_sec = now.tv_sec;
			maxTime.tv_nsec = now.tv_nsec;
		}
		// Note: endCurrentContainer() Decrements the TimeStamp
		// by 1 Nanosecond to use as "Max Time" for the Old Container...
		// Let's Keep Everything Up To/Including Max Time in Old Container.
		maxTime.tv_nsec++;
		if ( maxTime.tv_nsec >= NANO_PER_SECOND_LL ) {
			maxTime.tv_nsec -= NANO_PER_SECOND_LL;
			maxTime.tv_sec++;
		}
		// Choose New Container Minimum Time...
		struct timespec minTime;
		if ( do_terminate ) {
			// If the Current Container is Going Away, Then Preserve
			// Original Container Minimum Time for Stack Searching! ;-D
			minTime = (*it)->minTime(); // EPICS Time...!
		}
		else {
			// If We're Keeping the Current Container on the Stack,
			// Split the Time Range, Use Old Max Time as New Min Time...
			minTime = maxTime; // EPICS Time...!
		}
		// Capture Any "Last" Prologue Header Files from Old Container
		std::string lastName = (*it)->name();
		StorageFile::SharedPtr lastPrologueFile =
			(*it)->lastPrologueFile();
		std::vector<StorageFile::SharedPtr> lastSavePrologueFiles;
		for ( uint32_t i=0 ; i < (*it)->numSaveDataSources() ; i++ ) {
			lastSavePrologueFiles.push_back(
				(*it)->lastSavePrologueFile( i ) );
		}
		// *Only* Terminate Old Container If There are
		// No Other Unfinished PauseMode Files...
		// (i.e. the Size of the PauseMode Stack is 1...)
		endCurrentContainer( it, maxTime, // EPICS Time...!
			do_terminate );
		// If We _Just Now_ Created the "Last" Prologue Header Files,
		// And There Weren't Any Before, Then Copy Them Over Now... ;-D
		std::list<StorageContainer::SharedPtr>::iterator former_it;
		if ( !do_terminate ) {
			former_it = m_containerStack.begin();
			if ( former_it != m_containerStack.end() ) {
				++former_it;
				// Make Sure We've Still Got Something... ;-D
				if ( former_it != m_containerStack.end() ) {
					DEBUG("addPacket():"
						<< " Get Newly Created Last Prologue Headers"
						<< " from Former Container "
						<< (*former_it)->name()
						<< " in [" << (*former_it)->minTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*former_it)->minTime().tv_nsec << std::setw(0)
						<< ", " << (*former_it)->maxTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*former_it)->maxTime().tv_nsec
						<< std::setw(0) << "]"
						<< " check_old_containers=" << check_old_containers
						<< " - Btw, the Container Stack has "
						<< m_containerStack.size() << " elements");
					if ( !lastPrologueFile ) {
						lastPrologueFile =
							(*former_it)->lastPrologueFile();
					}
					for ( uint32_t i=0 ;
							i < (*former_it)->numSaveDataSources() ;
								i++ ) {
						if ( !(lastSavePrologueFiles[i]) ) {
							lastSavePrologueFiles[i] =
								(*former_it)->lastSavePrologueFile( i );
						}
					}
				}
				else {
					ERROR("addPacket(): Whoa... No Former Container Left"
						<< " to Get Newly Created Last Prologue Headers!"
						<< " check_old_containers=" << check_old_containers
						<< " - Btw, the Container Stack has "
						<< m_containerStack.size() << " elements");
				}
			}
			else {
				ERROR("addPacket(): Whoa... Empty Container Stack,"
					<< " No Containers Left"
					<< " to Get Newly Created Last Prologue Headers!"
					<< " check_old_containers=" << check_old_containers
					<< " - Btw, the Container Stack has "
					<< m_containerStack.size() << " elements");
			}
		}
		// Create the New Container...
		startContainer( it, minTime, paused, // EPICS Time...!
			0 /* run */, "UNKNOWN" /* propId */,
			lastName, lastPrologueFile, lastSavePrologueFiles );
		// Now, If We *Didn't* Just Actually Terminate the Old Container,
		// Then Reinstate This _Former_ Container for This Write...! ;-D
		// (So the New Overflow File Lands in the Old Container...! :-D)
		if ( !do_terminate ) {
			// Make Sure We've Still Got Something... ;-D
			if ( former_it != m_containerStack.end() )
				it = former_it;
		}
		else {
			// We Just Terminated Our Previous PauseMode,
			// Need to Get Latest/New Current PauseMode...! ;-b
			(*it)->getCurrentFileIterator( pm_it );
		}
	}

	/* Save off where we are in the stream, as we may need to point
	 * to this location for replay after a snapshot.
	 */
	off_t resumeLocation = (*it)->file()->size();

	// Write Data Packet to Disk...
	if ( !(*it)->write( pm_it, iovec, len, notify ) ) {
		// Something Went Wrong Trying to Write This Data to Disk! :-O
		// We will therefore LOSE THIS EXPERIMENT DATA
		// as a result of the error, so LOG IT HERE in the hopes
		// it can be salvaged later...! ;-Q
		std::stringstream ss;
		ss << "LOST EXPERIMENT DATA packetType=0x"
			<< std::hex << hdr->pkt_format << std::dec;
		logIoVector( ss.str(), iovec );
	}

	/* Is it time to take a state snapshot? If we took one while writing
	 * the current packet out -- ie, we started a new file -- then
	 * that will update m_nextTimeIndex and we'll know to skip it here.
	 */
	if ( m_pulseTime >= m_nextIndexTime ) {
		StorageFile::SharedPtr state(
			StorageFile::stateFile( (*it), m_stateDir ) );
		stateSnapshot( state, false /* capture_last */ );
		indexState( state, (*it)->file(), resumeLocation );
		state->put_fd();
	}

	/* Is it time to initiate a purge of old data?
	 *
	 * m_blocks_used contains the size of all of our closed files,
	 * and we don't add the current file until we're done with it.
	 * (query the StorageContainer to get the Total Size of
	 * all open files...! ;-)
	 */
	uint64_t blocks = (*it)->openSize() + m_block_size - 1;
	blocks /= m_block_size;
	// *Don't* Update "Max Blocks Allowed" from PV...!
	// Already Handled in Various PV->changed() Methods...
	if ( ( m_blocks_used + blocks ) > m_max_blocks_allowed ) {
		uint64_t goal = ( m_blocks_used + blocks ) - m_max_blocks_allowed;
		std::stringstream ss;
		ss << "addPacket() Purge Request"
			<< " (m_blocks_used=" << m_blocks_used
			<< " + blocks=" << blocks
			<< " = " << (m_blocks_used + blocks)
			<< " > m_max_blocks_allowed=" << m_max_blocks_allowed
			<< ": goal=" << goal << ")"
			<< " (BlockSize=" << m_block_size << ")";
		requestPurge( goal, ss.str() );
	}

	// DEBUG("addPacket() exit len=" << len);
}

void StorageManager::savePacket( IoVector &iovec, uint32_t dataSourceId,
		ADARA::PacketType::Type pkt_base_type )
{
	uint32_t len = validatePacket( iovec );

	ADARA::Header *hdr = (ADARA::Header *) iovec[0].iov_base;
	struct timespec ts;
	ts.tv_sec = hdr->ts_sec; // EPICS Time...!
	ts.tv_nsec = hdr->ts_nsec;

	// Determine Whether to Ignore Packet TimeStamp
	// Based on Base Packet Type...
	// (Regular Packets Normally Set This Option By Packet Type...)
	bool ignore_pkt_timestamp = false;
	switch ( pkt_base_type )
	{
		// Ignore the Packet TimeStamps for These Packet Types
		// - Just Use the "Current" Container for Whatever Saved Packet
		case ADARA::PacketType::SOURCE_LIST_TYPE:
		case ADARA::PacketType::DEVICE_DESC_TYPE:
		case ADARA::PacketType::VAR_VALUE_U32_TYPE:
		case ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE:
		case ADARA::PacketType::VAR_VALUE_STRING_TYPE:
		case ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE:
		case ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE:
			ignore_pkt_timestamp = true;
			break;

		// *Don't* Ignore the Packet TimeStamps for These Packet Types
		// - We Want to Sort These Packets into Containers By TimeStamp
		case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
		case ADARA::PacketType::HEARTBEAT_TYPE:
		case ADARA::PacketType::DATA_DONE_TYPE:
		case ADARA::PacketType::RAW_EVENT_TYPE:
		case ADARA::PacketType::MAPPED_EVENT_TYPE:
		case ADARA::PacketType::RTDL_TYPE:
		case ADARA::PacketType::SYNC_TYPE:
			ignore_pkt_timestamp = false;
			break;

		// Anything We're Not Expecting to Get,
		// Just Stuff It Into the "Current" Container;
		// Better Safe than Sorry, Who Knows What
		// Weirdness Might Be Within Here... ;-D
		default:
			ignore_pkt_timestamp = true;
			break;
	}

	std::list<StorageContainer::SharedPtr>::iterator it;

	it = findContainerByTime( "savePacket()", ignore_pkt_timestamp,
		ts, false );

	if ( it == m_containerStack.end() || !(*it) ) {
		DEBUG("savePacket(): No Container Found"
			<< " for ts=" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< ts.tv_nsec << std::setw(0)
			<< " pkt_format=0x" << std::hex << hdr->pkt_format << std::dec
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");
		throw std::logic_error("No container!");
	}

	/* Check for Long-Non-Running Containers that Could Make the SMS
	 * Swell Up and Pop by Eating Up Non-Releasable Local Disk Space...!
	 * - If We're About to Create the 101st File Here (Current File
	 *   is "Oversized" and Waiting to Pop), then Split Container Now...!
	 * [Unless It's Already Been "Split", and Has a Container Max Time!]
	 */
	// Check the Given Saved Input File for Overflow...
	if ( !(*it)->runNumber()
			&& (*it)->maxTime().tv_sec == 0
			&& (*it)->maxTime().tv_nsec == 0
			&& (*it)->savefile( dataSourceId )
			&& (*it)->savefile( dataSourceId )->oversize()
			&& ( 1 + (*it)->totFileCount() ) > 100 ) {
		WARN("savePacket(): Long-Non-Running Container "
			<< m_baseDir << "/" << (*it)->name()
			<< " Reached 100 Files"
			<< " (" << (*it)->savefile( dataSourceId )->path() << ")"
			<< " - Split Container for Purging...");
		// Are We "Terminating" This Container or Not...?
		bool do_terminate = ( (*it)->numPauseModeOnStack() == 1 );
		// Preserve Paused State of Container...
		bool paused = (*it)->paused();
		// Get "Newest" Max DataSource Time
		// for Container Time Range Cut-Off...
		SMSControl *ctrl = SMSControl::getInstance();
		struct timespec maxTime =
			ctrl->newestMaxDataSourceTime(); // EPICS Time...!
		// Take *Lesser* of "Newest" Max DataSource Time and Wallclock...
		// (In Case We Have a "Bogus Future TimeStamp" Gumming Things Up!)
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		if ( compareTimeStamps( now, maxTime ) < 0 ) {
			ERROR("savePacket(): Bogus Future Newest Max DataSource Time "
				<< maxTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< maxTime.tv_nsec << std::setw(0)
				<< " > "
				<< now.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< now.tv_nsec << std::setw(0)
				<< " Current Wallclock Time - Use Wallclock!");
			maxTime.tv_sec = now.tv_sec;
			maxTime.tv_nsec = now.tv_nsec;
		}
		// Note: endCurrentContainer() Decrements the TimeStamp
		// by 1 Nanosecond to use as "Max Time" for the Old Container...
		// Let's Keep Everything Up To/Including Max Time in Old Container.
		maxTime.tv_nsec++;
		if ( maxTime.tv_nsec >= NANO_PER_SECOND_LL ) {
			maxTime.tv_nsec -= NANO_PER_SECOND_LL;
			maxTime.tv_sec++;
		}
		// Choose New Container Minimum Time...
		struct timespec minTime;
		if ( do_terminate ) {
			// If the Current Container is Going Away, Then Preserve
			// Original Container Minimum Time for Stack Searching! ;-D
			minTime = (*it)->minTime(); // EPICS Time...!
		}
		else {
			// If We're Keeping the Current Container on the Stack,
			// Split the Time Range, Use Old Max Time as New Min Time...
			minTime = maxTime; // EPICS Time...!
		}
		// Capture Any "Last" Prologue Header Files from Old Container
		std::string lastName = (*it)->name();
		StorageFile::SharedPtr lastPrologueFile =
			(*it)->lastPrologueFile();
		std::vector<StorageFile::SharedPtr> lastSavePrologueFiles;
		for ( uint32_t i=0 ; i < (*it)->numSaveDataSources() ; i++ ) {
			lastSavePrologueFiles.push_back(
				(*it)->lastSavePrologueFile( i ) );
		}
		// *Only* Terminate Old Container If There are
		// No Other Unfinished PauseMode Files...
		// (i.e. the Size of the PauseMode Stack is 1...)
		endCurrentContainer( it, maxTime, // EPICS Time...!
			do_terminate );
		// If We _Just Now_ Created the "Last" Prologue Header Files,
		// And There Weren't Any Before, Then Copy Them Over Now... ;-D
		std::list<StorageContainer::SharedPtr>::iterator former_it;
		if ( !do_terminate ) {
			former_it = m_containerStack.begin();
			if ( former_it != m_containerStack.end() ) {
				++former_it;
				// Make Sure We've Still Got Something... ;-D
				if ( former_it != m_containerStack.end() ) {
					DEBUG("savePacket():"
						<< " Get Newly Created Last Prologue Headers"
						<< " from Former Container "
						<< (*former_it)->name()
						<< " in [" << (*former_it)->minTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*former_it)->minTime().tv_nsec << std::setw(0)
						<< ", " << (*former_it)->maxTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*former_it)->maxTime().tv_nsec
						<< std::setw(0) << "]"
						<< " - Btw, the Container Stack has "
						<< m_containerStack.size() << " elements");
					if ( !lastPrologueFile ) {
						lastPrologueFile =
							(*former_it)->lastPrologueFile();
					}
					for ( uint32_t i=0 ;
							i < (*former_it)->numSaveDataSources() ;
								i++ ) {
						if ( !(lastSavePrologueFiles[i]) ) {
							lastSavePrologueFiles[i] =
								(*former_it)->lastSavePrologueFile( i );
						}
					}
				}
				else {
					ERROR("savePacket(): Whoa... No Former Container Left"
						<< " to Get Newly Created Last Prologue Headers!"
						<< " - Btw, the Container Stack has "
						<< m_containerStack.size() << " elements");
				}
			}
			else {
				ERROR("savePacket(): Whoa... Empty Container Stack,"
					<< " No Containers Left"
					<< " to Get Newly Created Last Prologue Headers!"
					<< " - Btw, the Container Stack has "
					<< m_containerStack.size() << " elements");
			}
		}
		// Create the New Container...
		startContainer( it, minTime, paused, // EPICS Time...!
			0 /* run */, "UNKNOWN" /* propId */,
			lastName, lastPrologueFile, lastSavePrologueFiles );
		// Now, If We *Didn't* Just Actually Terminate the Old Container,
		// Then Reinstate This _Former_ Container for This Write...! ;-D
		// (So the New Overflow File Lands in the Old Container...! :-D)
		if ( !do_terminate ) {
			// Make Sure We've Still Got Something... ;-D
			if ( former_it != m_containerStack.end() )
				it = former_it;
		}
	}

	// Write Saved Stream Packet to Disk...
	if ( !(*it)->save(iovec, len, dataSourceId, true) ) {
		// Something Went Wrong Trying to Write This Data to Disk! :-O
		// We will therefore LOSE This Saved Input Stream Data
		// as a result of the error, so LOG IT HERE in the hopes
		// it can be salvaged later...! ;-Q
		std::stringstream ss;
		ss << "LOST Saved Input Stream Data dataSourceId=" << dataSourceId
			<< " pkt_format=0x" << std::hex << hdr->pkt_format << std::dec;
		logIoVector(ss.str(), iovec);
	}

	/* Is it time to initiate a purge of old data?
	 *
	 * m_blocks_used contains the size of all of our closed files,
	 * and we don't add the current file until we're done with it.
	 * (query the StorageContainer to get the Total Size of
	 * all open files...! ;-)
	 */
	uint64_t blocks = (*it)->openSize() + m_block_size - 1;
	blocks /= m_block_size;
	// *Don't* Update "Max Blocks Allowed" from PV...!
	// Already Handled in Various PV->changed() Methods...
	if ((m_blocks_used + blocks) > m_max_blocks_allowed) {
		uint64_t goal = ( m_blocks_used + blocks ) - m_max_blocks_allowed;
		std::stringstream ss;
		ss << "savePacket() Purge Request"
			<< " (m_blocks_used=" << m_blocks_used
			<< " + blocks=" << blocks
			<< " = " << (m_blocks_used + blocks)
			<< " > m_max_blocks_allowed=" << m_max_blocks_allowed
			<< ": goal=" << goal << ")"
			<< " (BlockSize=" << m_block_size << ")";
		requestPurge( goal, ss.str() );
	}
}

void StorageManager::checkContainerTimeout( std::string label,
		struct timespec &ts ) // EPICS Time...!
{
	DEBUG("checkContainerTimeout():"
		<< " label=" << label
		<< " ts=" << ts.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< ts.tv_nsec << std::setw(0)
		<< " - Btw, the Container Stack has "
		<< m_containerStack.size() << " elements");

	// Obviously, or we wouldn't be here... ;-D
	bool ignore_pkt_timestamp = false;
	bool check_old_containers = true;

	std::list<StorageContainer::SharedPtr>::iterator it;

	// Find Container for This TimeStamp...
	// (and Check Old Containers for Expiration in the Process...! ;-D)
	it = findContainerByTime( "checkContainerTimeout()",
		ignore_pkt_timestamp, ts, check_old_containers );

	if ( it == m_containerStack.end() || !(*it) ) {
		ERROR("checkContainerTimeout(): No Container Found"
			<< " for ts=" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< ts.tv_nsec << std::setw(0)
			<< " label=" << label
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");
		// *Don't* Throw an Exception Here, an Error Log is Enough... ;-D
	}

	// Get Proper PauseMode Off of Stack for This Packet/Time...
	// (and Check Old PauseModes for Expiration in the Process...! ;-D)
	std::list<struct StorageContainer::PauseMode>::iterator pm_it;
	(*it)->getPauseModeByTime( pm_it, ignore_pkt_timestamp,
		ts, check_old_containers );
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

	// Write Prologue Packet to Disk...
	if ( !m_prologueFile->write(iovec, len, false) ) {
		// Something Went Wrong Trying to Write This Prologue Pkt to Disk!
		// We will therefore LOSE THIS DATA as a result of the error,
		// so LOG IT HERE in the hopes it can be salvaged later...! ;-Q
		// Fortunately, We'll get Another Copy of this Prologue Pkt Data
		// with the _Next_ Data File, so we _May_ Be Ok here...?
		ADARA::Header *hdr = (ADARA::Header *) iovec[0].iov_base;
		std::stringstream ss;
		ss << "LOST Prologue Packet Data packetType=0x"
			<< std::hex << hdr->pkt_format << std::dec;
		logIoVector(ss.str(), iovec);
	}
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

	if (len < sizeof(ADARA::Header))
		throw std::logic_error("Packet too small");

	return len;
}

void StorageManager::addSavePrologue(IoVector &iovec,
		uint32_t dataSourceId)
{
	/* We're writing a prologue before putting Saved Input Stream
	 * data into a file, so we know we have a current container.
	 */

	uint32_t len = validatePacket(iovec);

	// Get Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	(*it)->save(iovec, len, dataSourceId, false);
}

void StorageManager::logIoVector(std::string label, IoVector &iovec)
{
	// Go Through IoVector and Convert Everything to String Format... ;-b

	std::stringstream ss;

	ss << std::hex << std::setfill('0');

	ss << "[0x";

	for (IoVector::iterator iovit = iovec.begin() ;
			iovit != iovec.end() ; ++iovit)
	{
		uint8_t *bytes = (uint8_t *) iovit->iov_base;

		for (uint32_t i=0 ; i < iovit->iov_len ; i++)
		{
			ss << std::setw(2) << static_cast<unsigned>(bytes[i]);
		}
	}

	ss << "]";

	ss << std::dec;

	ERROR("logIoVector(): " << label << " - " << ss.str());
}

std::list<StorageContainer::SharedPtr>::iterator
StorageManager::findContainerByTime(
		std::string label,
		bool ignore_pkt_timestamp,
		struct timespec &ts, // EPICS Time...!
		bool check_old_containers )
{
	static uint32_t cnt = 0;

	SMSControl *ctrl = SMSControl::getInstance();

	// *** Quick Cached Time Stamp Lookup Shortcut:
	// If *Everything* is the "Same" for This Time Stamp Lookup,
	// Then We Can Simply Return the Same "Last Iterator"...!
	// (Otherwise, We Need to Proceed Fully Through the Logic Here...)
	if ( ignore_pkt_timestamp == m_last_ignore_pkt_timestamp
			&& check_old_containers == m_last_check_old_containers
			&& ts.tv_sec == m_last_ts.tv_sec // EPICS Time...!
			&& ts.tv_nsec == m_last_ts.tv_nsec )
	{
		if ( ctrl->verbose() > 2 )
		{
			DEBUG("findContainerByTime(): " << label
				<< " *** CACHE HIT! SAME Packet TimeStamp,"
				<< " Re-Use Last Container "
				<< (*m_last_found_it)->name()
				<< " for ts=" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0)
				<< " in [" << (*m_last_found_it)->minTime().tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< (*m_last_found_it)->minTime().tv_nsec << std::setw(0)
				<< ", " << (*m_last_found_it)->maxTime().tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< (*m_last_found_it)->maxTime().tv_nsec
				<< std::setw(0) << "]"
				<< " ignore_pkt_timestamp=" << ignore_pkt_timestamp
				<< " check_old_containers=" << check_old_containers
				<< " - Btw, the Container Stack has "
				<< m_containerStack.size() << " elements");
		}

		return( m_last_found_it );
	}

	// Update Storage Container Cleanup Timeout PV...
	// (Infrequently, maybe once per minute...?)
	if ( !( ++cnt % 9999 ) )
	{
		double container_cleanup_timeout =
			m_pvContainerCleanupTimeout->value();
		if ( !approximatelyEqual( container_cleanup_timeout,
				m_container_cleanup_timeout_double, FLOAT64_EPSILON ) )
		{
			// Update Actual Internal Container Cleanup Timeout Fields!
			m_container_cleanup_timeout_double = container_cleanup_timeout;
			m_container_cleanup_timeout.tv_sec =
				(uint32_t) m_container_cleanup_timeout_double;
			m_container_cleanup_timeout.tv_nsec =
				(uint32_t) ( ( m_container_cleanup_timeout_double
						- ((double) m_container_cleanup_timeout.tv_sec) )
					* NANO_PER_SECOND_D );
			DEBUG("findContainerByTime():"
				<< " Storage Container Cleanup Timeout Set from PV to "
				<< m_container_cleanup_timeout_double
				<< " -> (" << m_container_cleanup_timeout.tv_sec
					<< ", " << m_container_cleanup_timeout.tv_nsec << ")");
		}

		// Update SMSControl SMS Verbose Value from PV...
		ctrl->updateVerbose();
	}

	std::list<StorageContainer::SharedPtr>::iterator found_it;

	std::list<StorageContainer::SharedPtr>::iterator it =
		m_containerStack.begin();

	// If Ignoring Packet TimeStamp, Just Write Into Current Container
	if ( ignore_pkt_timestamp )
	{
		found_it = m_containerStack.begin();

		if ( found_it != m_containerStack.end() )
		{
			if ( ctrl->verbose() > 1 )
			{
				DEBUG("findContainerByTime(): " << label
					<< " Ignore Packet TimeStamp,"
					<< " Use Current Container "
					<< (*found_it)->name()
					<< " for ts=" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0)
					<< " in [" << (*found_it)->minTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*found_it)->minTime().tv_nsec << std::setw(0)
					<< ", " << (*found_it)->maxTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*found_it)->maxTime().tv_nsec
					<< std::setw(0) << "]"
					<< " check_old_containers=" << check_old_containers
					<< " - Btw, the Container Stack has "
					<< m_containerStack.size() << " elements");
			}

			// Step Past Current Container,
			// No Need to Check Its Expiration Yet...
			it = found_it;
			it++;
		}
	}
	else
	{
		found_it = m_containerStack.end();
	}

	for ( ; it != m_containerStack.end(); ++it )
	{
		// Check Back Through Container for Time Range Match...
		// (Unless We've Already Found the Matching Container...)
		if ( found_it == m_containerStack.end() )
		{
			// This Container Encapsulates This EPICS TimeStamp...
			if ( compareTimeStamps( ts, (*it)->minTime() ) >= 0
				&& ( ( (*it)->maxTime().tv_sec == 0
						&& (*it)->maxTime().tv_nsec == 0 )
					|| compareTimeStamps( (*it)->maxTime(), ts ) >= 0 ) )
			{
				if ( ctrl->verbose() > 1 )
				{
					DEBUG("findContainerByTime():"
						<< " Found " << label << " Container "
						<< (*it)->name() << " for ts=" << ts.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< ts.tv_nsec << std::setw(0)
						<< " in [" << (*it)->minTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*it)->minTime().tv_nsec << std::setw(0)
						<< ", " << (*it)->maxTime().tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
						<< " check_old_containers=" << check_old_containers
						<< " - Btw, the Container Stack has "
						<< m_containerStack.size() << " elements");
				}

				// Found It! :-D
				found_it = it;

				// If We're _Not_ Checking Old Containers for Expiration,
				// Then We're Done! :-D
				if ( !check_old_containers )
					break;
			}
		}

		// Now We've Found the Matching Container,
		// So Check for "Old" Storage Containers that have
		// Reached the Cleanup Timeout Threshold and Expired...
		else if ( check_old_containers )
		{
			// Compute the Container's EPICS Expiration Time...
			struct timespec container_expire = (*it)->maxTime();

			// Skip Old Container Expiration Check If No Max Time...
			if ( container_expire.tv_sec == 0
					&& container_expire.tv_nsec == 0 )
			{
				DEBUG("findContainerByTime(): " << label
					<< " No Max Time for Container " << (*it)->name()
					<< " in [" << (*it)->minTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->minTime().tv_nsec << std::setw(0)
					<< ", " << (*it)->maxTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
					<< " - Don't Check for Container Expiration");
				continue;
			}

			// Add Timeout Threshold to Container Max Time...
			container_expire.tv_sec += m_container_cleanup_timeout.tv_sec;
			container_expire.tv_nsec +=
				m_container_cleanup_timeout.tv_nsec;
			if ( container_expire.tv_nsec >= NANO_PER_SECOND_LL )
			{
				container_expire.tv_nsec -= NANO_PER_SECOND_LL;
				container_expire.tv_sec++;
			}

			struct timespec old_ts =
				ctrl->oldestMaxDataSourceTime(); // EPICS Time...!

			if ( ctrl->verbose() > 2 )
			{
				DEBUG("findContainerByTime(): " << label
					<< " Container " << (*it)->name()
					<< " in [" << (*it)->minTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->minTime().tv_nsec << std::setw(0)
					<< ", " << (*it)->maxTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
					<< " has Expiration Time = "
					<< container_expire.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< container_expire.tv_nsec << std::setw(0)
					<< ", old_ts=" << old_ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< old_ts.tv_nsec << std::setw(0)
					<< " - Btw, the Container Stack has "
					<< m_containerStack.size() << " elements");
			}

			// Is It Time to Close Down This Container?
			// Note: old_ts = 0.0 When Uninitialized...
			if ( compareTimeStamps( old_ts, container_expire ) >= 0 )
			{
				DEBUG("findContainerByTime(): " << label
					<< " Container " << (*it)->name()
					<< " in [" << (*it)->minTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->minTime().tv_nsec << std::setw(0)
					<< ", " << (*it)->maxTime().tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
					<< " has EXPIRED: "
					<< " old_ts=" << old_ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< old_ts.tv_nsec << std::setw(0)
					<< " >= Expiration Time "
					<< container_expire.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< container_expire.tv_nsec << std::setw(0)
					<< ", Terminating Expired Container"
					<< " - Btw, the Container Stack has "
					<< m_containerStack.size() << " elements");

				// Finally Close Down This Container...
				(*it)->terminate();
				m_contChange( (*it), false );
				(*it).reset();

				// Remove Container from Stack
				it = m_containerStack.erase( it );
				// Note: erase() Leaves Iterator Pointing at _Next_ Entry,
				// So Pre-Decrement Prior to Impending Loop Increment...
				--it;

				// Note: No Need to Reset "Last Time Stamp" Here,
				// As We're About to Reset the Last Saved Iterator Anyway.
			}
		}

		// If We're _Not_ Checking Old Containers for Expiration,
		// Then We're Done! :-D
		else /* if ( !check_old_containers ) */
		{
			break;
		}
	}

	// Didn't Find Container for That Time...! ;-O
	if ( found_it == m_containerStack.end() )
	{
		// Check TimeStamp Versus Oldest Stacked Container Min Time...
		// (To Capture Case of Bogus SAWTOOTH TimeStamps, That Are
		// So Far Out of Order that We Already Closed Their Container!
		// Or More Likely Simply a Bogus TimeStamp from Somewhere... ;-b)

		// Snag "Oldest" Container...
		it = m_containerStack.end();
		if ( it != m_containerStack.begin() )
		{
			it--;
			if ( (*it) )
			{
				// Does EPICS TimeStamp Occur _Before_ Oldest Container...?
				if ( compareTimeStamps( ts, (*it)->minTime() ) < 0 )
				{
					// Use "Current Container" for All Such
					// Bogus TimeStamps... ;-Q
					it = m_containerStack.begin();

					// Rate-Limited Logging Container SAWTOOTH...
					std::string log_info;
					if ( RateLimitedLogging::checkLog(
							RLLHistory_StorageManager,
							RLL_CONTAINER_SAWTOOTH, "none",
							2, 10, 1000, log_info ) ) {
						ERROR(log_info
							<< "findContainerByTime(): " << label
							<< " Container SAWTOOTH for ts="
							<< ts.tv_sec << "."
							<< std::setfill('0') << std::setw(9)
								<< ts.tv_nsec
							<< " -> Using Current Container "
							<< (*it)->name()
							<< " in [" << (*it)->minTime().tv_sec << "."
							<< std::setfill('0') << std::setw(9)
							<< (*it)->minTime().tv_nsec << std::setw(0)
							<< ", " << (*it)->maxTime().tv_sec << "."
							<< std::setfill('0') << std::setw(9)
								<< (*it)->maxTime().tv_nsec
								<< std::setw(0) << "]"
							<< " check_old_containers="
								<< check_old_containers
							<< " - Btw, the Container Stack has "
							<< m_containerStack.size() << " elements");
					}

					// Cache This Bogus Time Stamp Lookup Result
					// for Next Time...! ;-Q
					m_last_found_it = it;
					m_last_ts.tv_sec = ts.tv_sec; // EPICS Time...!
					m_last_ts.tv_nsec = ts.tv_nsec;
					m_last_ignore_pkt_timestamp = ignore_pkt_timestamp;
					m_last_check_old_containers = check_old_containers;

					return( it );
				}
			}
		}

		// No Containers to Compare Times...?!
		ERROR("findContainerByTime(): No Container Found for " << label
			<< " ts=" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9) << ts.tv_nsec
			<< " check_old_containers=" << check_old_containers
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");
	}

	// Cache This Time Stamp Lookup Result for Next Time...! ;-D
	m_last_found_it = found_it;
	m_last_ts.tv_sec = ts.tv_sec; // EPICS Time...!
	m_last_ts.tv_nsec = ts.tv_nsec;
	m_last_ignore_pkt_timestamp = ignore_pkt_timestamp;
	m_last_check_old_containers = check_old_containers;

	return( found_it );
}

void StorageManager::cleanContainerStack(void)
{
	// Skip Past Current Container...
	std::list<StorageContainer::SharedPtr>::iterator it;
	it = m_containerStack.begin();

	// Make Sure We Actually Have Any Containers... ;-D
	if ( it == m_containerStack.end() || !(*it) )
	{
		DEBUG("cleanContainerStack(): No Containers Present!"
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");
		return;
	}

	DEBUG("cleanContainerStack(): Skip Past Current Container "
		<< (*it)->name()
		<< " in [" << (*it)->minTime().tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< (*it)->minTime().tv_nsec << std::setw(0)
		<< ", " << (*it)->maxTime().tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
		<< " - Btw, the Container Stack has "
		<< m_containerStack.size() << " elements");

	// Close Down All the Stacked Containers...
	for ( ++it ; it != m_containerStack.end() ; ++it )
	{
		DEBUG("cleanContainerStack(): Closing Container "
			<< (*it)->name()
			<< " in [" << (*it)->minTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->minTime().tv_nsec << std::setw(0)
			<< ", " << (*it)->maxTime().tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< (*it)->maxTime().tv_nsec << std::setw(0) << "]"
			<< " - Btw, the Container Stack has "
			<< m_containerStack.size() << " elements");

		// Close Down This Container...
		(*it)->terminate();
		m_contChange( (*it), false );
		(*it).reset();

		// Remove Container from Stack
		it = m_containerStack.erase( it );
		// Note: erase() Leaves Iterator Pointing at _Next_ Entry,
		// So Pre-Decrement Prior to Impending Loop Increment...
		--it;
	}

	// Reset "Last Time Stamp" for findContainerByTime()...!
	// ("Just in Case" Changing Container Stack Here
	// Invalidates Saved Iterator)
	m_last_ts.tv_sec = -1; // EPICS Time...!
	m_last_ts.tv_nsec = -1;
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

			if (c->runNumber()) {
				/* DON'T Send STC Succeeded Message...!
				 * - the original ComBus Message was sent
				 * _Before_ the "Translation Completed" Marker
				 * is written to the local Run Container Directory,
				 * so we _Really_ don't need to Re-Notify the Web Monitor!
				 * (and flood it with 10s of 1000s of old redundant
				 * run messages... ;-b)
				 */
				if (c->isManual()) {
					/* Send STC Failed Message */
					m_combus->sendOriginal(c->runNumber(), c->propId(),
							std::string("Needs Manual Translation"),
							c->startTime()); // Wallclock Time...!
				} else if (!c->isTranslated()) {
					/* Note Pending for Later Translation */
					m_pendingRuns.push_back(c);
					/* Send Run Queued Message */
					m_combus->sendOriginal(c->runNumber(), c->propId(),
							std::string("STC Send Pending"),
							c->startTime()); // Wallclock Time...!
				}
			}
		}
	}
}

bool StorageManager::isValidDaily(const std::string &dir)
{
	/* Validate that the directory name is in the proper format
	 * for a daily directory. strptime() allows leading zeros
	 * to be omitted, so we convert back to a string to verify.
	 */
	struct tm tm = { 0 };
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
		if ( file == m_run_filename || file == m_run_tempname )
			continue;

		/* Skip index directories; they are handled by other means. */
		if ( file.string().compare( 0, m_stateDirPrefix.length(),
						m_stateDirPrefix ) == 0 )
			continue;

		if ( status.type() != fs::directory_file ) {
			WARN("Ignoring non-directory '" << it->path() << "'");
			continue;
		}

		if ( !isValidDaily( file.string() ) ) {
			WARN("Daily directory '" << it->path()
				<< "' has invalid format");
			continue;
		}

		scanDaily( it->path().string() );
	}

	DEBUG("Scanned " << m_scannedBlocks << " blocks, and had "
		<< m_pendingRuns.size() << " runs pending translation.");
}

// Encode/Decode Any Newlines or Other Special Characters in the
// AutoSave Value Strings to Keep from Breaking Lines/Perturbing
// the Format of the AutoSave File...! ;-o

void StorageManager::encodeAutoSaveString( std::string str_in,
		std::string &str_out )
{
	std::stringstream ss_out;

	for ( size_t i=0 ; i < str_in.size() ; ++i )
	{
		if ( str_in[i] == '\n' )
			ss_out << "%0A";
		else
			ss_out << str_in[i];
	}

	str_out = ss_out.str();
}

void StorageManager::decodeAutoSaveString( std::string str_in,
		std::string &str_out )
{
	std::stringstream ss_out;

	for ( size_t i=0 ; i < str_in.size() ; ++i )
	{
		if ( !str_in.compare( i, 3, "%0A" ) ) {
			ss_out << '\n';
			i += 2; // ( pattern_length - 1 )
		}
		else
			ss_out << str_in[i];
	}

	str_out = ss_out.str();
}

void StorageManager::autoSavePV( std::string pv_name, std::string pv_value,
		struct timespec *pv_time )
{
	if ( m_autoSaveFd < 0 && !openAutoSaveFile() )
	{
		ERROR("autoSavePV(): No Valid AutoSave File Descriptor!"
			<< " *** Ignoring PV Write-Thru Value Save for " << pv_name
			<< " = [" << pv_value << "]"
			<< " at [Wallclock] " << pv_time->tv_sec << "."
			<< std::setfill('0') << std::setw(9) << pv_time->tv_nsec);
	}

	// "Clean" the PV Value String for the AutoSave File... ;-D
	std::string pv_value_out;
	encodeAutoSaveString( pv_value, pv_value_out );

	// Assemble the PV AutoSave Entry...
	std::stringstream ss;
	ss << pv_time->tv_sec << "."
		<< std::setfill('0') << std::setw(9) << pv_time->tv_nsec;
	ss << " " << pv_name << " " << pv_value_out << std::endl;
	INFO("autoSavePV(): AutoSaving PV Value - [Wallclock] " << ss.str());

	// Write PV AutoSave Entry to File...
	int rc = write( m_autoSaveFd, ss.str().c_str(), ss.str().length() );
	if ( rc < 0 ) {
		int e = errno;
		ERROR("autoSavePV(): Error Writing PV AutoSave Entry to File"
			<< " [" << ss.str() << "] - "
			<< strerror(e));
		return;
	}

	if ( rc != (int) ss.str().length() ) {
		ERROR("autoSavePV(): Short Write for PV AutoSave Entry"
			<< " [" << ss.str() << "]"
			<< " - Wrote " << rc << " out of "
			<< ss.str().length() << " Bytes Expected!");
	}

	if ( fsync( m_autoSaveFd ) ) {
		int e = errno;
		ERROR("autoSavePV(): Error Syncing PV AutoSave File"
			<< " [" << ss.str() << "] - "
			<< strerror(e));
	}

	/* We aren't guaranteed the new file names are safe on disk until
	 * we sync the directory that contains them.
	 */
	if ( fsync( m_base_fd ) ) {
		int e = errno;
		ERROR("autoSavePV(): Error with Fsync on SMS Base Dir"
			<< " for PV AutoSave File"
			<< " [" << ss.str() << "] - "
			<< strerror(e));
	}
}

bool StorageManager::openAutoSaveFile(void)
{
	m_autoSaveFd = openat(m_base_fd, m_autosave_filename,
			O_CREAT|O_APPEND|O_WRONLY, RUN_STORAGE_MODE);
	if ( m_autoSaveFd < 0 ) {
		int e = errno;
		ERROR("openAutoSaveFile(): Unable to Open SMS AutoSave File"
			<< " for Writing:"
			<< " [" << m_autosave_filename << "] - "
			<< strerror(e));
		return false;
	}
	else {
		ERROR("openAutoSaveFile(): Successfully Opened SMS AutoSave File"
			<< " for Writing:"
			<< " [" << m_autosave_filename << "]");
		return true;
	}
}

bool StorageManager::parseTimeString( std::string pv_time_str,
		struct timespec & pv_time_spec )
{
	size_t dot = pv_time_str.find(".");

	if ( dot != std::string::npos ) {

		try {
			pv_time_spec.tv_sec = boost::lexical_cast<time_t>(
				pv_time_str.substr(0, dot) );
			pv_time_spec.tv_nsec = boost::lexical_cast<long>(
				pv_time_str.substr( dot + 1 ) );
		}
		catch (...) {
			ERROR("parseTimeString(): Error Parsing Time String Format"
				<< " (dddddddddd.ddddddddd)"
				<< " -> [" << pv_time_str << "]"
				<< " dot=" << dot);
			return( false );
		}
	}

	else {
		ERROR("parseTimeString(): Error Parsing Time String Format"
			<< " (dddddddddd.ddddddddd)"
			<< " -> [" << pv_time_str << "]"
			<< " - No Dot!");
		return( false );
	}

	return( true );
}

bool StorageManager::getAutoSavePV( std::string pv_name,
		std::string & pv_value, struct timespec & pv_time )
{
	std::map<std::string,
		std::pair<std::string, std::string> >::iterator it =
			m_autoSaveConfig.find( pv_name );
	
	if ( it != m_autoSaveConfig.end() ) {

		DEBUG("getAutoSavePV(): Found AutoSaved Config for PV " << pv_name
			<< " at " << it->second.first
			<< " = [" << it->second.second << "]");

		pv_value = it->second.second;

		if ( !parseTimeString( it->second.first, pv_time ) ) {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Time for PV"
				<< pv_name << " - (" << it->second.first << ")");
			return( false );
		}

		return( true );
	}

	else {
		ERROR("getAutoSavePV(): No AutoSaved Config Found for PV "
			<< pv_name << "!");
			return( false );
	}
}

bool StorageManager::getAutoSavePV( std::string pv_name,
		double & pv_dvalue, struct timespec & pv_time )
{
	std::map<std::string,
		std::pair<std::string, std::string> >::iterator it =
			m_autoSaveConfig.find( pv_name );
	
	if ( it != m_autoSaveConfig.end() ) {

		DEBUG("getAutoSavePV(): Found AutoSaved Config for PV " << pv_name
			<< " at " << it->second.first
			<< " = [" << it->second.second << "]");

		try {
			pv_dvalue = boost::lexical_cast<double>( it->second.second );
		}
		catch (...) {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Float64 Value for PV"
				<< pv_name << " - (" << it->second.second << ")");
			return( false );
		}

		size_t dot = it->second.first.find(".");
		if ( dot != std::string::npos ) {
			try {
				pv_time.tv_sec = boost::lexical_cast<time_t>(
					it->second.first.substr(0, dot) );
				pv_time.tv_nsec = boost::lexical_cast<long>(
					it->second.first.substr( dot + 1 ) );
			}
			catch (...) {
				ERROR("getAutoSavePV():"
					<< " Error Parsing AutoSaved Config Time for PV"
					<< pv_name << " - (" << it->second.first << ")");
				return( false );
			}
		}
		else {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Time for PV"
				<< pv_name << " - (" << it->second.first << ")");
			return( false );
		}

		return( true );
	}

	else {
		ERROR("getAutoSavePV(): No AutoSaved Config Found for PV "
			<< pv_name << "!");
			return( false );
	}
}

bool StorageManager::getAutoSavePV( std::string pv_name,
		uint32_t & pv_ivalue, struct timespec & pv_time )
{
	std::map<std::string,
		std::pair<std::string, std::string> >::iterator it =
			m_autoSaveConfig.find( pv_name );
	
	if ( it != m_autoSaveConfig.end() ) {

		DEBUG("getAutoSavePV(): Found AutoSaved Config for PV " << pv_name
			<< " at " << it->second.first
			<< " = [" << it->second.second << "]");

		try {
			pv_ivalue = boost::lexical_cast<uint32_t>( it->second.second );
		}
		catch (...) {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Uint32 Value for PV"
				<< pv_name << " - (" << it->second.second << ")");
			return( false );
		}

		size_t dot = it->second.first.find(".");
		if ( dot != std::string::npos ) {
			try {
				pv_time.tv_sec = boost::lexical_cast<time_t>(
					it->second.first.substr(0, dot) );
				pv_time.tv_nsec = boost::lexical_cast<long>(
					it->second.first.substr( dot + 1 ) );
			}
			catch (...) {
				ERROR("getAutoSavePV():"
					<< " Error Parsing AutoSaved Config Time for PV"
					<< pv_name << " - (" << it->second.first << ")");
				return( false );
			}
		}
		else {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Time for PV"
				<< pv_name << " - (" << it->second.first << ")");
			return( false );
		}

		return( true );
	}

	else {
		ERROR("getAutoSavePV(): No AutoSaved Config Found for PV "
			<< pv_name << "!");
			return( false );
	}
}

bool StorageManager::getAutoSavePV( std::string pv_name,
		bool & pv_bvalue, struct timespec & pv_time )
{
	std::map<std::string,
		std::pair<std::string, std::string> >::iterator it =
			m_autoSaveConfig.find( pv_name );
	
	if ( it != m_autoSaveConfig.end() ) {

		DEBUG("getAutoSavePV(): Found AutoSaved Config for PV " << pv_name
			<< " at " << it->second.first
			<< " = [" << it->second.second << "]");

		if ( boost::iequals( it->second.second, "true" )
				|| !it->second.second.compare( "1" ) ) {
			pv_bvalue = true;
		}
		else if ( boost::iequals( it->second.second, "false" )
				|| !it->second.second.compare( "0" ) ) {
			pv_bvalue = false;
		}
		else {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Bool Value for PV"
				<< pv_name << " - (" << it->second.second << ")"
				<< ", Assuming *False*...");
			pv_bvalue = false;
		}

		size_t dot = it->second.first.find(".");
		if ( dot != std::string::npos ) {
			try {
				pv_time.tv_sec = boost::lexical_cast<time_t>(
					it->second.first.substr(0, dot) );
				pv_time.tv_nsec = boost::lexical_cast<long>(
					it->second.first.substr( dot + 1 ) );
			}
			catch (...) {
				ERROR("getAutoSavePV():"
					<< " Error Parsing AutoSaved Config Time for PV"
					<< pv_name << " - (" << it->second.first << ")");
				return( false );
			}
		}
		else {
			ERROR("getAutoSavePV():"
				<< " Error Parsing AutoSaved Config Time for PV"
				<< pv_name << " - (" << it->second.first << ")");
			return( false );
		}

		return( true );
	}

	else {
		ERROR("getAutoSavePV(): No AutoSaved Config Found for PV "
			<< pv_name << "!");
			return( false );
	}
}

bool StorageManager::parseAutoSaveFile(void)
{
	std::ifstream f(m_autosave_filename);

	if ( f.fail() ) {
		int e = errno;
		ERROR("parseAutoSaveFile(): Unable to Open SMS AutoSave File"
			<< " for Reading:"
			<< " [" << m_autosave_filename << "] - "
			<< strerror(e));
		return false;
	}

	ERROR("parseAutoSaveFile(): Successfully Opened SMS AutoSave File"
		<< " for Reading:"
		<< " [" << m_autosave_filename << "]");

	std::string line;
	int lineno = 0;

	for (;;) {

		// Get Next Line from AutoSave File...
		lineno++;
		getline(f, line);
		if ( f.fail() )
			break;

		// Parse the PV AutoSave Line...

		std::istringstream iss(line);

		std::string pv_time;
		std::string pv_name;
		std::string pv_value;

		try {

			iss >> pv_time;

			// Sanity Check Time Stamp for Valid Format...
			// (Handy for Identifying Multi-Line Strings... ;-b)
			struct timespec ts;
			if ( !parseTimeString( pv_time, ts ) ) {
				ERROR("parseAutoSaveFile():"
					<< " Error Parsing Config Time in AutoSave File"
					<< " [" << m_autosave_filename << "]"
					<< " at Line #" << lineno
					<< " -> [" << line << "]"
					<< " pv_time=[" << pv_time << "]");
				continue;
			}

			iss >> pv_name;

			std::getline( iss, pv_value );

			// Strip Off Preceding White Space...
			if ( pv_value.at(0) == ' ' )
				pv_value = pv_value.substr(1);

			// Decode Any Newline or Special Character Encodings...
			std::string pv_value_out;
			decodeAutoSaveString( pv_value, pv_value_out );

			m_autoSaveConfig[ pv_name ] =
				std::pair<std::string, std::string>(
					pv_time, pv_value_out );
		}
		catch (std::runtime_error e) {
			ERROR("parseAutoSaveFile():"
				<< " Exception Parsing AutoSave File"
				<< " [" << m_autosave_filename << "]"
				<< " at Line #" << lineno
				<< ": " << e.what()
				<< " -> [" << line << "]");
		}
		catch (...) {
			ERROR("parseAutoSaveFile():"
				<< " Unknown Exception Parsing AutoSave File"
				<< " [" << m_autosave_filename << "]"
				<< " at Line #" << lineno
				<< " -> [" << line << "]");
		}
	}

	DEBUG("parseAutoSaveFile(): Retrieved AutoSave Config, "
		<< m_autoSaveConfig.size() << " Entries Captured");

	// Dump AutoSave Config
	//std::map<std::string,
		//std::pair<std::string, std::string> >::iterator it;
	//for ( it = m_autoSaveConfig.begin() ;
			//it != m_autoSaveConfig.end(); ++it ) {
		//DEBUG(it->first << "(" << it->second.first << ") = ["
			//<< it->second.second << "]");
	//}

	// Rotate the AutoSave Files, Now That We've Retrieved
	// All the PV Values into the Configuration...

	struct stat stats;

	for ( uint32_t i=SMS_AUTOSAVE_ROTATE_SIZE ; i > 0 ; i-- ) {

		std::stringstream src_asf;
		std::stringstream dst_asf;

		// Move the Current SMS AutoSave File to "Slot 1"...
		if ( i == 1 ) {
			src_asf << m_autosave_filename;
			dst_asf << m_autosave_basename << i << m_autosave_filesuffix;
		}

		// Move "Slot 'I-1'" AutoSave File into "Slot 'I'"...
		else {
			src_asf << m_autosave_basename << (i - 1)
				<< m_autosave_filesuffix;
			dst_asf << m_autosave_basename << i << m_autosave_filesuffix;
		}

		if ( stat( src_asf.str().c_str(), &stats ) ) {
			DEBUG("parseAutoSaveFile(): SMS AutoSave File "
				<< ( m_baseDir + "/" + src_asf.str() )
				<< " Not Found - Skipping AutoSave Rotate...");
			continue;
		}

		try {

			// If It Exists (Not Already Rotated), Remove Old AutoSave File
			if ( !stat( dst_asf.str().c_str(), &stats ) ) {
				boost::filesystem::remove( boost::filesystem::path(
					m_baseDir + "/" + dst_asf.str() ) );
			}

			// Rotate Newer AutoSave File into the Next Slot...
	        boost::filesystem::rename(
				boost::filesystem::path( m_baseDir + "/" + src_asf.str() ),
				boost::filesystem::path( m_baseDir + "/" + dst_asf.str() )
			);

		}
		catch( boost::filesystem::filesystem_error &e ) {
			ERROR("parseAutoSaveFile(): Error Rotating SMS AutoSave File "
				<< ( m_baseDir + "/" + src_asf.str() ) << " to "
				<< ( m_baseDir + "/" + dst_asf.str() ) << "!");
		}

		DEBUG("parseAutoSaveFile(): Rotated SMS AutoSave File "
			<< ( m_baseDir + "/" + src_asf.str() ) << " to "
			<< ( m_baseDir + "/" + dst_asf.str() ) );
	}

	return true;
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

	SMSControl *ctrl = SMSControl::getInstance();

	scanStorage();

	if ( ctrl->verbose() > 2 ) {
		DEBUG("backgroundIo(): Sending Value IOCMD_INITIAL = "
			<< IOCMD_INITIAL
			<< "/0x" << std::hex << IOCMD_INITIAL << std::dec);
	}
	if ( !m_ioCompleteEvent->signal( IOCMD_INITIAL ) ) {
		ERROR("backgroundIo(): Error Signaling I/O Complete Event"
			<< " with IOCMD_INITIAL Completion = " << IOCMD_INITIAL
				<< "/0x" << std::hex << IOCMD_INITIAL << std::dec);
	}

	bool alive = true;
	uint64_t cmd;

	while ( alive ) {

		if ( !m_ioStartEvent->block( cmd ) ) {
			ERROR("backgroundIo(): Error Blocking on I/O Start Event!"
				<< " cmd=" << cmd << "/0x" << std::hex << cmd << std::dec
				<< " - Continuing...");
			continue;
		}
		if ( ctrl->verbose() > 2 ) {
			DEBUG("backgroundIo(): Received cmd = " << cmd
				<< "/0x" << std::hex << cmd << std::dec);
		}

		/* We only accept two commands -- shutdown, and the
		 * minimum number of blocks to purge.
		 */
		if ( cmd == IOCMD_SHUTDOWN )
			alive = false;
		else
			m_purgedBlocks += purgeData( cmd );

		if ( ctrl->verbose() > 2 ) {
			DEBUG("backgroundIo(): Sending Value IOCMD_DONE = "
				<< IOCMD_DONE
				<< "/0x" << std::hex << IOCMD_DONE << std::dec);
		}
		if ( !m_ioCompleteEvent->signal( IOCMD_DONE ) ) {
			ERROR("backgroundIo(): Error Signaling I/O Complete Event"
				<< " with IOCMD_DONE Completion = " << IOCMD_DONE
					<< "/0x" << std::hex << IOCMD_DONE << std::dec);
		}
	}
}

void StorageManager::ioCompleted(void)
{
	SMSControl *ctrl = SMSControl::getInstance();

	if ( ctrl->verbose() > 2 ) {
		DEBUG("ioCompleted() entry");
	}

	uint64_t val = 0;

	if ( !m_ioCompleteEvent->read( val ) ) {
		ERROR("ioCompleted(): Error Reading I/O Complete Event!"
			<< " val=" << val << "/0x" << std::hex << val << std::dec
			<< " - Cancelling I/O Operation and Continuing...");
		m_ioActive = false;
		ERROR("ioCompleted() failure exit");
		return;
	}
	if ( ctrl->verbose() > 2 ) {
		DEBUG("ioCompleted(): Received val = " << val
			<< "/0x" << std::hex << val << std::dec);
	}

	// Initial Data Directory Scan Results...
	if ( val == IOCMD_INITIAL ) {

		/* Initial scan is complete, so update the size of the
		 * data store, and queue any runs needing translation.
		 *
		 * We add, as we've been taking data while the initial
		 * scan progressed.
		 */
		DEBUG("ioCompleted initially scanned " << m_scannedBlocks);
		m_blocks_used += m_scannedBlocks;
		m_scannedBlocks = 0;

		STCClientMgr *stc = STCClientMgr::getInstance();
		std::list<StorageContainer::SharedPtr>::iterator it;
		for (it = m_pendingRuns.begin(); it != m_pendingRuns.end();
									++it) {
			INFO("Queuing pending run " << (*it)->runNumber());
			stc->queueRun(*it);
		}

		/* Tell the STC client to start processing the runs we
		 * just queued.
		 */
		if (!m_pendingRuns.empty())
			stc->startConnect();
	}
	
	// Data Directory Purge Request Completed...
	else {
		DEBUG("ioCompleted(): Purged " << m_purgedBlocks << " Blocks");
		m_blocks_used -= m_purgedBlocks;
		m_purgedBlocks = 0;
	}

	m_ioActive = false;

	if ( ctrl->verbose() > 2 ) {
		DEBUG("ioCompleted() exit");
	}
}

void StorageManager::requestPurge( uint64_t goal, std::string logStr )
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

	SMSControl *ctrl = SMSControl::getInstance();
	DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
		<< "Signaling Purge Request of " << goal << " Blocks - "
		<< logStr );

	m_ioActive = true;
	if ( ctrl->verbose() > 2 ) {
		DEBUG("requestPurge(): Sending goal = " << goal
			<< "/0x" << std::hex << goal << std::dec);
	}
	if ( !m_ioStartEvent->signal( goal ) ) {
		ERROR("requestPurge(): Error Signaling I/O Start Event"
			<< " with Goal = " << goal
				<< "/0x" << std::hex << goal << std::dec);
	}
}

void StorageManager::populateDailyCache(void)
{
	fs::directory_iterator end, it(m_baseDir);

	DEBUG("populateDailyCache():"
		<< " Building Cache of Daily Directories for Purging");
	m_dailyCache.clear();

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		/* Skip over the storage for the next run number */
		if ( file == m_run_filename || file == m_run_tempname )
			continue;

		if ( status.type() != fs::directory_file )
			continue;

		if ( !isValidDaily( file.string() ) )
			continue;

		uint64_t total_size = 0;

		std::map<std::string, uint64_t> daily_map =
			getDirSize( file.string(), total_size );

		DEBUG("populateDailyCache(): Daily " << file.string()
			<< " - " << daily_map.size() << " Sub-Directories,"
			<< " Total Files Size = " << total_size);

		m_dailyCache.push_back(
			std::pair< std::string, std::map<std::string, uint64_t> > (
				file.string(), daily_map ) );
	}

	/* The daily directories have the format YYYYMMDD, so the default
	 * lexical sort works.
	 * Also, there shouldn't be any duplicate daily directories,
	 * so the new std::pair<std::string, uint64_t> types will
	 * _Always_ lexicographically sort on the first string field... ;-D
	 */
	m_dailyCache.sort();
}

std::map<std::string, uint64_t> StorageManager::getDirSize(
		const std::string &dir, uint64_t &total_size )
{
	std::map<std::string, uint64_t> daily_map;

	fs::directory_iterator end, it( m_baseDir + "/" + dir );

	total_size = 0;

	for (; it != end; ++it) {

		std::string sub_dir = dir + "/"
			+ std::string( it->path().filename().c_str() );

		fs::directory_iterator sub_end, sub( m_baseDir + "/" + sub_dir );

		uint64_t sub_size = 0;

		for (; sub != sub_end; ++sub) {

			sub_size += StorageFile::fileSize( sub_dir + "/"
					+ std::string( sub->path().filename().c_str() ) );
		}

		daily_map[ m_baseDir + "/" + sub_dir ] = sub_size;

		total_size += sub_size;
	}

	return( daily_map );
}

uint64_t StorageManager::purgeDaily( const std::string &dir,
		std::map<std::string, uint64_t> &daily_map,
		uint64_t goal, bool last, bool &daily_deleted )
{
	/* We could cache the list of containers to avoid rescanning
	 * each time we wish to purge, but we expect the list to be
	 * reasonably small, so go for the simple code for now. We
	 * can revisit if CPU usage is too high.
	 */

	SMSControl *ctrl = SMSControl::getInstance();

	std::list<fs::path> containers;
	fs::directory_iterator end, it(dir);
	for (; it != end; ++it) {
		fs::file_status status = it->status();

		if ( status.type() != fs::directory_file )
			continue;

		containers.push_back(it->path());
	}

	/* The container names have the form YYYYMMDD-HHMMSS.nnnnnnnnn, so
	 * the default lexical sort works.
	 */
	containers.sort();

	/* Check Daily Cache Map Against Current Container File List
	 * - Any Missing Sub-Directories must have been Manually/Externally
	 *   Deleted, so we can deduct their File Size from the Purge Count
	 *   and then remove them from the Daily Cache Map... ;-D
	 */
	uint64_t total_purged = 0;
	std::map<std::string, uint64_t>::iterator subs,
		subs_end = daily_map.end();
	for ( subs = daily_map.begin() ; subs != subs_end; ++subs ) {
		// Look for Sub-Directory in Current List of Containers...
		std::list<fs::path>::iterator cit =
			std::find( containers.begin(), containers.end(), subs->first );
		if ( cit == containers.end() )
		{
			// Sub-Directory Not Found, Must Be Manually/Externally Deleted
			uint64_t blocks = subs->second + m_block_size - 1;
			blocks /= m_block_size;
			DEBUG("purgeDaily(): Sub-Directory " << subs->first
				<< " Found Deleted - Recovered " << blocks << " Blocks"
				<< " (" << subs->second << " Bytes)");
			total_purged += blocks;
			daily_map.erase( subs );
		}
	}

	/* Now purge files until we reach our goal...
	 */
	uint64_t purged;
	std::list<fs::path>::iterator cit, cend = containers.end();
	for (cit = containers.begin(); total_purged < goal && cit != cend; ) {
		fs::path &cpath = *cit;

		uint32_t date=-1, secs=-1, nanosecs=-1;
		uint32_t run;
		int numParsed = -1;
		if ( (numParsed = sscanf((*cit).filename().c_str(),
				"%8u-%6u.%9u-run-%u",
				&date, &secs, &nanosecs, &run)) == 4 ) {
			if ( ctrl->verbose() > 2 ) {
				INFO("purgeDaily(): Parsed Run Number of"
					<< " [" << (*cit).filename() << "]"
					<< " from [" << cpath.string() << "]"
					<< " in [" << dir << "]"
					<< " as " << run
					<< " (" << date << ", "
						<< secs << "." << nanosecs << ")");
			}
		}
		else if ( numParsed < 3 ) {
			ERROR("purgeDaily():"
				<< " Failed to Parse Run Number (or Date/Time) of"
				<< " [" << (*cit).filename() << "]"
				<< " from [" << cpath.string() << "]"
				<< " in [" << dir << "]"
				<< " (" << date << ", " << secs << "." << nanosecs << ")");
		}
		else if ( ctrl->verbose() > 2 ) {
			INFO("purgeDaily(): Parsed In-Between-Run Date/Time of"
				<< " [" << (*cit).filename() << "]"
				<< " from [" << cpath.string() << "]"
				<< " in [" << dir << "]"
				<< " (" << date << ", " << secs << "." << nanosecs << ")");
		}

		/* We do the iterator increment in the loop, as we don't
		 * want the container purge to delete the container when
		 * it is the last one in the last daily directory -- ie,
		 * if it could be the current container.
		 */
		++cit;

		// Skip the Last Container in the Last Daily Folder...
		// (Because This is the One We're Currently Writing To...! ;-O)
		if ( last && cit == cend ) {
			if ( ctrl->verbose() > 2 ) {
				DEBUG("purgeDaily():"
					<< " Skipping Last Container in Last Daily Folder"
					<< " (Active?) " << cpath.string());
			}
			continue;
		}

		// Try to Purge this Container...
		std::string propId;
		bool path_deleted = false;
		purged = StorageContainer::purge(cpath.string(),
							goal - total_purged,
							propId, path_deleted);

		// If No ProposalId Found, Set to "UNKNOWN"...
		if ( propId.empty() )
			propId = "UNKNOWN";

		// *Don't* Send ComBus Message if Purged and Run Number Known...
		// (This Just Confuses the Web Monitor's "Last Run" Meta-Data...)
		/*
		if ( purged > 0 && numParsed == 4 ) {
			std::string purgeMsg;
			if ( path_deleted )
				purgeMsg = "SMS run backup purged.";
			else
				purgeMsg = "SMS run backup purging...";
			// Better Send Original ComBus Message Here...
			// - who knows whether we've touched this run before...
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			m_combus->sendOriginal(run, propId, purgeMsg, now);
		}
		*/

		// If Container Deleted, Remove from Daily Cache Map...!
		if ( path_deleted ) {
			subs = daily_map.find( cpath.string() );
			if ( subs != subs_end ) {
				if ( ctrl->verbose() > 2 ) {
					DEBUG("purgeDaily(): Removing Container "
						<< cpath.string() << " from Daily Cache Map");
				}
				daily_map.erase( subs );
			}
		}

		// Accumulate Total Blocks Purged
		total_purged += purged;
	}

	/* Try to remove the directory, but expect to fail. */
	daily_deleted = true;
	try {
		fs::remove(fs::path(dir));
	} catch (fs::filesystem_error e) {
		daily_deleted = false;
	}

	return total_purged;
}

uint64_t StorageManager::purgeData(uint64_t purgeRequested)
{
	SMSControl *ctrl = SMSControl::getInstance();

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
		ERROR( ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "purgeData(): Error Populating Daily Cache for Purging!");
		/* If we cannot populate the cache, then we cannot purge. */
		return 0;
	}

	uint64_t purged = 0;

	std::list< std::pair<std::string,
		std::map<std::string, uint64_t> > >::iterator it, next,
			end = m_dailyCache.end();

	for ( it = m_dailyCache.begin();
			purged < purgeRequested && it != end; ) {

		fs::path dir(m_baseDir);
		dir /= it->first;

		// Whole Daily Directory is Gone...! :-D
		// Must Be Manually/Externally Deleted, Subtract All Blocks...
		if ( !fs::exists(dir) ) {
			DEBUG("purgeData(): Whole Daily Directory " << it->first
				<< " has been Externally Deleted!"
				<< " Recover Manually Freed Blocks...");
			std::map<std::string, uint64_t>::iterator subs,
				subs_end = it->second.end();
			for (subs = it->second.begin() ; subs != subs_end; subs++) {
				uint64_t blocks = subs->second + m_block_size - 1;
				blocks /= m_block_size;
				DEBUG("purgeData(): Sub-Directory "
					<< it->first << "/" << subs->first
					<< " Found Deleted - Recovered " << blocks << " Blocks"
					<< " (" << subs->second << " Bytes)");
				purged += blocks;
			}
			it = m_dailyCache.erase(it);
			continue;
		}

		if ( ctrl->verbose() > 2 ) {
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Purging Daily " << it->first);
		}

		/* We need to know when we're working on the last known
		 * daily directory so we can tell purgeDir(). It will use
		 * this to avoid erasing the current container.
		 */
		bool daily_deleted = false;
		next = it;
		purged += purgeDaily( dir.string(), it->second,
			purgeRequested - purged, ++next == end, daily_deleted );

		// Clean Up Daily Cache if Directory Deleted...
		if ( daily_deleted ) {
			DEBUG("Removed Daily " << it->first);
			it = m_dailyCache.erase(it);
		}

		// We need to do the increment within the loop, as we may delete
		// elements from the list as we clean the directories.
		else {
			++it;
		}
	}

	m_dailyExhausted = (purged < purgeRequested);

	DEBUG("purgeData(): Purged " << purged << " Total Blocks"
		<< " (dailyExhausted=" << m_dailyExhausted << ")");

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
		errno = 0; // reset errno for possible ENOENT...

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

void StorageManager::sendComBus(
		uint32_t a_run_num, std::string a_proposal_id,
		std::string a_run_state,
		const struct timespec & a_start_time)
{
	if ( a_start_time.tv_sec == 0 && a_start_time.tv_nsec == 0 )
	{
		m_combus->sendUpdate(a_run_num, a_proposal_id,
			a_run_state);
	}
	else
	{
		m_combus->sendOriginal(a_run_num, a_proposal_id,
			a_run_state, a_start_time);
	}
}

