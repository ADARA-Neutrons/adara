#ifndef __STORAGE_CONTAINER_H
#define __STORAGE_CONTAINER_H

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>
#include <time.h>
#include <stdint.h>
#include <string>

#include "Storage.h"
#include "StorageFile.h"

class StorageContainer : boost::noncopyable {
public:
	typedef boost::shared_ptr<StorageContainer> SharedPtr;
	typedef boost::weak_ptr<StorageContainer> WeakPtr;
	typedef boost::signal<void (StorageFile::SharedPtr &)> onNewFile;

	const struct timespec &startTime(void) const { return m_startTime; }
	uint32_t runNumber (void) const { return m_runNumber; }
	uint32_t numFiles (void) const { return m_numFiles; }
	const std::string &name(void) const { return m_name; }
	bool isTranslated(void) const { return m_translated; }
	bool isManual(void) const { return m_manual; }
	uint64_t blocks(void) const;

	bool active(void) const { return m_active; }

	boost::signals::connection connect(const onNewFile::slot_type &slot) {
		return m_newFile.connect(slot);
	}

	static SharedPtr create(const struct timespec &start, uint32_t run);
	static SharedPtr scan(const std::string &path);
	static uint64_t purge(const std::string &path, uint64_t goal,
			      bool rmdir);

	void newFile(void);
	off_t write(IoVector &iovec, uint32_t len, bool notify = true);
	void terminate(void);
	void notify(void);

	StorageFile::SharedPtr &file(void) { return m_cur_file; }

	void getFiles(std::list<StorageFile::SharedPtr> &list);

	void markTranslated(void);
	void markManual(void);

private:
	WeakPtr m_weakThis;
	struct timespec m_startTime;
	uint32_t m_runNumber;
	uint32_t m_numFiles;
	std::string m_name;
	StorageFile::SharedPtr m_cur_file;
	onNewFile m_newFile;
	bool m_active;
	bool m_translated;
	bool m_manual;

	std::list<StorageFile::SharedPtr> m_files;

	StorageContainer(const struct timespec &start, uint32_t run);
	StorageContainer(const std::string &name);

	void terminateFile(void);

	bool createMarker(const char *);
	bool validate(void);

	static bool validatePath(const std::string &in_path,
				 std::string &out_path, struct timespec &ts,
				 uint32_t &run);

	static const char *m_completed_marker;
	static const char *m_manual_marker;
};

#endif /* __STORAGE_CONTAINER_H */
