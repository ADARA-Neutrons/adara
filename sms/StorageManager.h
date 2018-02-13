#ifndef __STORAGE_MANAGER_H
#define __STORAGE_MANAGER_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/function.hpp>
#include <boost/signals2.hpp>
#include <boost/thread.hpp>
#include <stdint.h>
#include <string>
#include <vector>

#include "ADARA.h"
#include "Storage.h"
#include "StorageContainer.h"
#include "StorageFile.h"

#include "ComBusSMSMon.h"

static struct timespec combuszerotime = {0,0};

class EventFd;

class PoolsizePV;
class PercentPV;
class MaxBlocksPV;
class BlockSizePV;
class RescanRunDirPV;

class StorageManager {
public:
	typedef boost::signals2::signal<void (StorageContainer::SharedPtr &,
					bool)> ContainerSignal;
	typedef boost::signals2::signal<void (void)> PrologueSignal;
	typedef boost::function<void (StorageFile::SharedPtr &, off_t)>
								FileOffSetFunc;

	static void init(void);
	static void lateInit(void);
	static void stop(void);

	static void startRecording(uint32_t run, std::string propId);
	static void stopRecording(void);

	static void pauseRecording(void);
	static void resumeRecording(void);

	static void iterateHistory(uint32_t startSeconds, FileOffSetFunc cb);

	static void addPacket(IoVector &iovec, bool notify = true);
	static void addPacket(const void *pkt, uint32_t len, bool notify = true)
	{
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPacket(iovec, notify);
	}

	static void savePacket(IoVector &iovec, uint32_t dataSourceId);
	static void savePacket(const void *pkt, uint32_t len,
			uint32_t dataSourceId)
	{
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		savePacket(iovec, dataSourceId);
	}

	static void notify(void);

	static void addPrologue(IoVector &iovec);
	static void addPrologue(const void *pkt, uint32_t len) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPrologue(iovec);
	}

	static void addSavePrologue(IoVector &iovec, uint32_t dataSourceId);
	static void addSavePrologue(const void *pkt, uint32_t len,
			uint32_t dataSourceId)
	{
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addSavePrologue(iovec, dataSourceId);
	}

	static int base_fd(void) { return m_base_fd; }

	static std::string base_dir(void) { return m_baseDir; }

	static boost::signals2::connection onContainerChange(
					const ContainerSignal::slot_type &s) {
		return m_contChange.connect(s);
	}

	static boost::signals2::connection onPrologue(
					const PrologueSignal::slot_type &s) {
		return m_prologue.connect(s);
	}

	static boost::signals2::connection onSavePrologue(
					const PrologueSignal::slot_type &s,
					uint32_t dataSourceId ) {
		m_savePrologue[ dataSourceId ] = boost::shared_ptr<PrologueSignal>(
			new PrologueSignal());
		return m_savePrologue[ dataSourceId ]->connect(s);
	}

	static StorageContainer::SharedPtr &container(void) {
		return m_cur_container;
	}

	static bool streaming(void) {
		return !!m_cur_container;
	}

	static uint32_t getNextRun(void);
	static bool updateNextRun(uint32_t run);

	static const struct timespec &scanStart(void) {
		return m_scanStart;
	}

	static void autoSavePV(std::string name, std::string value,
		struct timespec *ts);

	static void config(const boost::property_tree::ptree &conf);

	static bool set_max_blocks_allowed_value(
		uint32_t max_blocks_allowed_value, bool isMultiplier );

	static bool set_max_blocks_allowed(uint64_t maxSize);

	static void update_max_blocks_allowed_pv(void);

	static ComBusSMSMon *combus(void) { return m_combus; }

	static void sendComBus(uint32_t a_run_num, std::string a_proposal_id,
		std::string a_run_state,
		const struct timespec & a_start_time = combuszerotime);

private:
	typedef boost::signals2::connection connection;

	struct IndexEntry {
		StorageFile::SharedPtr	m_stateFile;
		StorageFile::SharedPtr	m_dataFile;
		uint32_t		m_key;
		off_t			m_resumeOffset;

		IndexEntry(uint32_t s, StorageFile::SharedPtr &f,
				StorageFile::SharedPtr &d, off_t r) :
			m_stateFile(f), m_dataFile(d), m_key(s),
			m_resumeOffset(r) {}

		bool isDataOnly(void) const { return !m_resumeOffset; }
	};

	static std::string m_baseDir;
	static int m_base_fd;

	static std::string m_poolsize;
	static uint32_t m_percent;

	static uint32_t m_max_blocks_allowed_multiplier;
	static uint32_t m_max_blocks_allowed_base;

	static uint64_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;

	static boost::shared_ptr<PoolsizePV> m_pvPoolsize;
	static boost::shared_ptr<PercentPV> m_pvPercent;
	static boost::shared_ptr<MaxBlocksPV> m_pvMaxBlocksAllowed;
	static boost::shared_ptr<MaxBlocksPV> m_pvMaxBlocksAllowedMultiplier;
	static boost::shared_ptr<BlockSizePV> m_pvBlockSize;
	static boost::shared_ptr<RescanRunDirPV> m_pvRescanRunDir;

	static struct timespec m_scanStart;
	static uint64_t m_scannedBlocks;
	static std::list<StorageContainer::SharedPtr> m_pendingRuns;

	static StorageContainer::SharedPtr m_cur_container;
	static StorageFile::SharedPtr m_prologueFile;

	static ContainerSignal m_contChange;
	static PrologueSignal m_prologue;

	static std::map<uint32_t, boost::shared_ptr<PrologueSignal> >
		m_savePrologue;

	static const char *m_run_filename;
	static const char *m_run_tempname;
	static std::string m_stateDirPrefix;
	static std::string m_stateDir;

	static std::list<IndexEntry> m_stateIndex;
	static uint32_t m_pulseTime;
	static uint32_t m_nextIndexTime;
	static uint32_t m_indexPeriod;

	static const char *m_autosave_filename;
	static int m_autoSaveFd;

	static boost::thread m_ioThread;
	static bool m_ioActive;
	static EventFd *m_ioStartEvent;
	static EventFd *m_ioCompleteEvent;
	static uint64_t m_purgedBlocks;

	static bool m_dailyExhausted;
	static std::list<
		std::pair<std::string,
			std::map<std::string, uint64_t> > > m_dailyCache;

	static ComBusSMSMon *m_combus;

	static uint32_t readRunFile(const char *path, bool notify);
	static bool cleanupRunFiles(void);

	static void stateSnapshot(StorageFile::SharedPtr &f);
	static bool retireIndexDir(bool remove = true);
	static bool cleanupIndexes(void);
	static void indexState(StorageFile::SharedPtr &state,
				StorageFile::SharedPtr &data, off_t dataOffset);

	static void scanStorage(void);
	static void scanDaily(const std::string &dir);
	static bool isValidDaily(const std::string &dir);

	static bool openAutoSaveFile(void);

	static void backgroundIo(void);
	static void ioCompleted(void);
	static void requestPurge(uint64_t goal, std::string logStr);
	static uint64_t purgeData(uint64_t goal);
	static uint64_t purgeDaily(const std::string &dir,
				std::map<std::string, uint64_t> &daily_map,
				uint64_t goal, bool last, bool &daily_deleted);
	static void populateDailyCache(void);
	static std::map<std::string, uint64_t> getDirSize(
				const std::string &dir, uint64_t &total_size);

	static void addBaseStorage(uint64_t size);
	static void startContainer(uint32_t run = 0,
				std::string propId = std::string("UNKNOWN"));
	static void endCurrentContainer(void);
	static void fileCreated(StorageFile::SharedPtr &f);
	static uint32_t validatePacket(const IoVector &iovec);

	static void saveCreated(uint32_t dataSourceId);

	friend class StorageContainer;
};

#endif /* __STORAGE_MANAGER_H */
