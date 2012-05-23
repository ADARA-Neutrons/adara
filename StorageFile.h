#ifndef __STORAGE_FILE
#define __STORAGE_FILE

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>

#include "ADARA.h"
#include "Storage.h"

class StorageContainer;

class StorageFile : boost::noncopyable {
public:
	typedef boost::signal<void (const StorageFile &)> onUpdate;

	/* TODO can we convert this to a shared_ptr/weak_ptr setup
	 * for some fd class?
	 */
	int get_fd(void);
	void put_fd(void);
	bool active(void) const { return m_active; }
	bool oversize(void) const { return m_oversize; }
	off_t size(void) const { return m_size; }
	uint32_t fileNumber(void) const { return m_fileNumber; }

	boost::signals::connection connect(const onUpdate::slot_type &slot) {
		return m_update.connect(slot);
	}

	~StorageFile();

private:
	std::string m_path;
	uint32_t m_runNumber;
	uint32_t m_fileNumber;
	uint32_t m_startTime;
	bool m_oversize;
	bool m_active;
	off_t m_size;
	off_t m_syncDistance;
	int m_fd;
	unsigned int m_fdRefs;
	onUpdate m_update;

	static off_t m_max_file_size;
	static off_t m_max_sync_distance;

	void makePath(const StorageContainer &c);
	void open(int flags);
	off_t write(IoVector &iovec, uint32_t len, bool notify = true);
	void addSync(void);
	void addRunStatus(ADARA::RunStatus::Enum status);
	void terminate(ADARA::RunStatus::Enum status);

	StorageFile(const StorageContainer &container,
		    uint32_t number, bool create = false,
		    ADARA::RunStatus::Enum = ADARA::RunStatus::NO_RUN);

	friend class StorageManager;
	friend class StorageContainer;
};

#endif /* __STORAGE_FILE */
