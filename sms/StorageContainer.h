#ifndef __STORAGE_CONTAINER_H
#define __STORAGE_CONTAINER_H

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <time.h>
#include <stdint.h>
#include <string>

#include "Storage.h"
#include "StorageFile.h"

class StorageContainer : boost::noncopyable {
public:
	typedef boost::shared_ptr<StorageContainer> SharedPtr;
	typedef boost::weak_ptr<StorageContainer> WeakPtr;
	typedef boost::signals2::signal<void (StorageFile::SharedPtr &)>
				onNewFile;

	const struct timespec &startTime(void) const { return m_startTime; }
	uint32_t runNumber(void) const { return m_runNumber; }
	std::string propId(void) const { return m_propId; }
	uint32_t numFiles(void) const { return m_numFiles; }
	uint32_t numPauseFiles(void) const { return m_numPauseFiles; }
	uint32_t totFileCount(void) const { return m_totFileCount; }
	const std::string &name(void) const { return m_name; }
	bool isTranslated(void) const { return m_translated; }
	bool isManual(void) const { return m_manual; }
	uint64_t blocks(void) const;

	uint32_t getRequeueCount(void) const { return m_requeueCount; }
	uint32_t incrRequeueCount(void) { return( ++m_requeueCount ); }

	bool active(void) const { return m_active; }
	bool paused(void) const { return m_paused; }
	void setPaused(bool paused) { m_paused = paused; }

	boost::signals2::connection connect(const onNewFile::slot_type &slot) {
		return m_newFile.connect(slot);
	}

	static SharedPtr create(const struct timespec &start,
		uint32_t run, std::string &propId);
	static SharedPtr scan(const std::string &path, bool force = false);
	static uint64_t purge(const std::string &path, uint64_t goal,
				std::string &propId, bool &path_deleted);
	static void purgeBackups(const std::string &path);

	void newFile(void);
	bool write(IoVector &iovec, uint32_t len, bool notify = true,
			uint32_t *written = NULL);
	void terminate(void);
	void notify(void);

	bool save(IoVector &iovec, uint32_t len, uint32_t dataSourceId,
			bool notify, uint32_t *written = NULL);

	void pause(void);
	void resume(void);

	StorageFile::SharedPtr &file(void) { return m_cur_file; }

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
	struct timespec m_startTime;
	uint32_t m_runNumber;
	std::string m_propId;
	uint32_t m_numFiles;
	uint32_t m_numPauseFiles;
	uint32_t m_totFileCount;
	std::string m_name;
	StorageFile::SharedPtr m_cur_file;
	onNewFile m_newFile;
	bool m_active;
	bool m_paused;
	bool m_translated;
	bool m_manual;
	uint32_t m_requeueCount;
	uint64_t m_saved_size;

	std::list<StorageFile::SharedPtr> m_files;

	std::vector<StorageFile::SharedPtr> m_ds_input_files;
	std::vector<uint32_t> m_ds_input_num_files;
	StorageFile::SharedPtr m_dummy_file;

	StorageContainer(const struct timespec &start,
		uint32_t run, std::string &propId);
	StorageContainer(const std::string &name);

	void terminateFile(void);

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
