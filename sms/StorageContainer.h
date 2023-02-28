#ifndef __STORAGE_CONTAINER_H
#define __STORAGE_CONTAINER_H

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <time.h>
#include <stdint.h>
#include <string>
#include <list>

#include "Storage.h"
#include "StorageFile.h"

class StorageContainer : boost::noncopyable {
public:
	typedef boost::shared_ptr<StorageContainer> SharedPtr;
	typedef boost::weak_ptr<StorageContainer> WeakPtr;
	typedef boost::signals2::signal<void (StorageFile::SharedPtr &)>
				onNewFile;

	struct PauseMode
	{
		StorageFile::SharedPtr m_file;
		std::list<StorageFile::SharedPtr> m_pendingFiles;
		StorageFile::SharedPtr m_lastPrologueFile;
		struct timespec m_minTime; // EPICS Time...!
		struct timespec m_maxTime; // EPICS Time...!
		uint32_t m_numModes;
		uint32_t m_numFiles;
		uint32_t m_numPauseFiles;
		bool m_paused;
	};

	const struct timespec &startTime(void)
		const { return m_startTime; } // Wallclock Time...!
	const struct timespec &minTime(void)
		const { return m_minTime; } // EPICS Time...!
	const struct timespec &maxTime(void)
		const { return m_maxTime; } // EPICS Time...!
	void setMaxTime(struct timespec maxTime)
		{ m_maxTime = maxTime; } // EPICS Time...!

	uint32_t runNumber(void) const { return m_runNumber; }
	std::string propId(void) const { return m_propId; }
	uint32_t totFileCount(void) const { return m_totFileCount; }
	const std::string &name(void) const { return m_name; }
	bool isTranslated(void) const { return m_translated; }
	bool isManual(void) const { return m_manual; }
	uint64_t blocks(void) const;

	uint32_t getRequeueCount(void) const { return m_requeueCount; }
	uint32_t incrRequeueCount(void) { return( ++m_requeueCount ); }

	bool active(void) const { return m_active; }

	bool paused(void)
	{
		// Use Current PauseMode Struct...
		std::list<struct PauseMode>::iterator it;
		it = m_pauseModeStack.begin();
		return( it->m_paused );
	}

	void setPaused( bool paused, uint32_t numFiles )
	{
		// Use Current PauseMode Struct...
		std::list<struct PauseMode>::iterator it;
		it = m_pauseModeStack.begin();

		// Set Container's Paused Mode...
		it->m_paused = paused;

		// Set Container's File Number Counter...
		it->m_numFiles = numFiles;
	}

	boost::signals2::connection connect(const onNewFile::slot_type &slot) {
		return m_newFile.connect(slot);
	}

	static SharedPtr create(
		const struct timespec &start, // Wallclock Time...!
		const struct timespec &minTime, // EPICS Time...!
		uint32_t run, std::string &propId);

	static SharedPtr scan(const std::string &path, bool force = false);
	static uint64_t purge(const std::string &path, uint64_t goal,
				std::string &propId, bool &path_deleted);
	static void purgeBackups(const std::string &path);

	void getCurrentFileIterator(
			std::list<struct PauseMode>::iterator &pm_it )
	{
		pm_it = m_pauseModeStack.begin();
		return;
	}

	uint32_t numPauseModeOnStack(void) {
		return m_pauseModeStack.size();
	}

	void getPauseModeByTime(
			std::list<struct PauseMode>::iterator &pm_it,
			bool ignore_pkt_timestamp,
			struct timespec &ts, // EPICS Time...!
			bool check_old_pausemodes );

	void newFile( std::list<struct PauseMode>::iterator &it,
			bool paused, const struct timespec &minTime ); // EPICS Time...!

	bool write( std::list<struct PauseMode>::iterator &it,
			IoVector &iovec, uint32_t len, bool notify = true,
			uint32_t *written = NULL );

	void terminate(void);

	void notify(void);

	bool save(IoVector &iovec, uint32_t len, uint32_t dataSourceId,
			bool notify, uint32_t *written = NULL);

	void pause( struct timespec &pauseTime ); // EPICS Time...!
	void resume( struct timespec &resumeTime ); // EPICS Time...!

	StorageFile::SharedPtr &file(void)
	{
		// Use Current PauseMode Struct...
		std::list<struct PauseMode>::iterator it;
		it = m_pauseModeStack.begin();
		return it->m_file;
	}

	StorageFile::SharedPtr &lastPrologueFile(void)
	{
		// Use Current PauseMode Struct...
		std::list<struct PauseMode>::iterator it;
		it = m_pauseModeStack.begin();
		return it->m_lastPrologueFile;
	}

	StorageFile::SharedPtr &lastSavePrologueFile(uint32_t dataSourceId)
	{
		if ( dataSourceId < m_lastSavePrologueFiles.size()
				&& m_lastSavePrologueFiles[dataSourceId] ) {
			return m_lastSavePrologueFiles[dataSourceId];
		}
		else
			return m_dummy_file;
	}

	void getLastPrologueFiles( std::list<struct PauseMode>::iterator &it,
			bool do_save_prologues );

	void copyLastPrologueFiles( std::list<struct PauseMode>::iterator &it,
			std::string &name, StorageFile::SharedPtr &lastPrologueFile,
			std::vector<StorageFile::SharedPtr> &lastSavePrologueFiles );

	void getFiles(std::list<StorageFile::SharedPtr> &list);

	void markTranslated(void);
	void markManual(void);

	uint64_t openSize(void);

	size_t numSaveDataSources(void) { return m_ds_input_files.size(); }

	StorageFile::SharedPtr &savefile(uint32_t dataSourceId)
	{
		if ( dataSourceId < m_ds_input_files.size()
				&& m_ds_input_files[dataSourceId] ) {
			return m_ds_input_files[dataSourceId];
		}
		else
			return m_dummy_file;
	}

private:

	WeakPtr m_weakThis;

	std::list<struct PauseMode> m_pauseModeStack;

	struct timespec m_startTime; // Wallclock Time...!
	struct timespec m_minTime; // EPICS Time...!
	struct timespec m_maxTime; // EPICS Time...!

	uint32_t m_runNumber;

	std::string m_propId;

	uint32_t m_totFileCount;

	std::string m_name;

	std::vector<StorageFile::SharedPtr> m_lastSavePrologueFiles;
	onNewFile m_newFile;

	bool m_active;
	bool m_translated;
	bool m_manual;

	uint32_t m_requeueCount;

	uint64_t m_saved_size;

	std::list<StorageFile::SharedPtr> m_files;

	std::vector<StorageFile::SharedPtr> m_ds_input_files;
	std::vector<uint32_t> m_ds_input_num_files;
	StorageFile::SharedPtr m_dummy_file;

	std::list<struct PauseMode>::iterator m_last_found_it;

	struct timespec m_last_ts; // EPICS Time...!

	struct timespec m_last_old_ts; // EPICS Time...!

	bool m_last_ignore_pkt_timestamp;
	bool m_last_check_old_pausemodes;

	StorageContainer(
		const struct timespec &start, // Wallclock Time...!
		const struct timespec &minTime, // EPICS Time...!
		uint32_t run, std::string &propId);
	StorageContainer(const std::string &name);

	static struct timespec m_default_start_time;

	void terminateFile( std::list<struct PauseMode>::iterator &it,
				bool do_terminate,
				const struct timespec &newStart // EPICS Time...!
					= m_default_start_time );

	bool createMarker(const char *);
	bool validate(void);

	static bool validatePath(const std::string &in_path,
				std::string &out_path, struct timespec &ts,
				uint32_t &run);

	static const char *m_completed_marker;
	static const char *m_manual_marker;

	static const char *m_proposal_id_marker_prefix;
};

#endif /* __STORAGE_CONTAINER_H */
