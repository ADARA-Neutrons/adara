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
	typedef boost::signal<void (StorageFile::SharedPtr &)> onNewFile;

	const struct timespec &startTime(void) const { return m_startTime; }
	uint32_t runNumber (void) const { return m_runNumber; }
	uint32_t numFiles (void) const { return m_numFiles; }
	const std::string &name(void) const { return m_name; }

	bool active(void) const { return m_active; }

	boost::signals::connection connect(const onNewFile::slot_type &slot) {
		return m_newFile.connect(slot);
	}

	StorageFile::SharedPtr &file(void) { return m_cur_file; }

	void getFiles(std::list<StorageFile::SharedPtr> &list);

private:
	struct timespec m_startTime;
	uint32_t m_runNumber;
	uint32_t m_numFiles;
	std::string m_name;
	StorageFile::SharedPtr m_cur_file;
	onNewFile m_newFile;
	bool m_active;

	std::list<StorageFile::SharedPtr> m_files;

	StorageContainer(const struct timespec &start, uint32_t run);
	StorageContainer(const std::string &name);

	off_t write(IoVector &iovec, uint32_t len, bool notify = true);
	void terminateFile(void);
	void terminate(void);

	friend class StorageManager;
};

#endif /* __STORAGE_CONTAINER_H */
