#ifndef __STORAGE_MANAGER_H
#define __STORAGE_MANAGER_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/function.hpp>
#include <boost/signal.hpp>
#include <boost/thread.hpp>
#include <stdint.h>
#include <sys/uio.h>
#include <string>
#include <vector>

#include "ADARA.h"
#include "Storage.h"
#include "StorageContainer.h"
#include "StorageFile.h"

class EventFd;

class StorageManager {
public:
	typedef boost::signal<void (StorageContainer::SharedPtr &, bool)>
								ContainerSignal;
	typedef boost::signal<void (void)> PrologueSignal;
	typedef boost::function<void (StorageFile::SharedPtr &, off_t)>
								FileOffSetFunc;

	static void init(void);
	static void lateInit(void);
	static void stop(void);

	static void startRecording(uint32_t run);
	static void stopRecording(void);

	static void iterateHistory(uint32_t startSeconds, FileOffSetFunc cb);

	static void addPacket(IoVector &iovec, bool notify = true);
	static void addPacket(const void *pkt, uint32_t len,
			      bool notify = true) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPacket(iovec, notify);
	}

	static void notify(void);

	static void addPrologue(IoVector &iovec);
	static void addPrologue(const void *pkt, uint32_t len) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPrologue(iovec);
	}

	static int base_fd() { return m_base_fd; }

	static boost::signals::connection onContainerChange(
					const ContainerSignal::slot_type &s) {
		return m_contChange.connect(s);
	}

	static boost::signals::connection onPrologue(
					const PrologueSignal::slot_type &s) {
		return m_prologue.connect(s);
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

	static void config(const boost::property_tree::ptree &conf);

private:
	typedef boost::signals::connection connection;

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

	static uint32_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;

	static struct timespec m_scanStart;
	static uint64_t m_scannedBlocks;
	static std::list<StorageContainer::SharedPtr> m_pendingRuns;

	static StorageContainer::SharedPtr m_cur_container;
	static StorageFile::SharedPtr m_prologueFile;

	static ContainerSignal m_contChange;
	static PrologueSignal m_prologue;

	static const char *m_run_filename;
	static const char *m_run_tempname;
	static std::string m_stateDirPrefix;
	static std::string m_stateDir;

	static std::list<IndexEntry> m_stateIndex;
	static uint32_t m_pulseTime;
	static uint32_t m_nextIndexTime;
	static uint32_t m_indexPeriod;

	static boost::thread m_ioThread;
	static bool m_ioActive;
	static EventFd *m_ioStartEvent;
	static EventFd *m_ioCompleteEvent;
	static uint64_t m_purgedBlocks;

	static bool m_dailyExhausted;
	static std::list<std::string> m_dailyCache;

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

	static void backgroundIo(void);
	static void ioCompleted(void);
	static void requestPurge(uint64_t goal);
	static uint64_t purgeData(uint64_t goal);
	static uint64_t purgeDaily(const std::string &dir, uint64_t goal,
				   bool last);
	static void populateDailyCache(void);

	static void addBaseStorage(off_t size);
	static void startContainer(uint32_t run = 0);
	static void endCurrentContainer(void);
	static void fileCreated(StorageFile::SharedPtr &f);
	static uint32_t validatePacket(const IoVector &iovec);

	friend class StorageContainer;
};

#endif /* __STORAGE_MANAGER_H */
