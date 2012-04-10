#ifndef __STORAGE_FILE
#define __STORAGE_FILE

#include <boost/smart_ptr.hpp>
#include "ADARA.h"

class StorageContainer;

class StorageFile {
public:
	/* TODO can we convert this to a shared_ptr/weak_ptr setup
	 * for some fd class?
	 */
	int get_fd(void);
	void put_fd(void);
	bool active(void) const { return m_active; }
	bool oversize(void) const { return m_oversize; }
	off_t size(void) const { return m_size; }

	~StorageFile();

private:
	boost::shared_ptr<StorageContainer> m_container;
	bool m_oversize;
	bool m_active;
	off_t m_size;
	off_t m_sync_distance;
	uint32_t m_file_number;
	int m_fd;
	unsigned int m_fd_refs;

	static off_t m_max_file_size;
	static off_t m_max_sync_distance;

	void open(int flags);
	void write(void *data, uint32_t count);
	void add_sync(void);
	void add_run_status(ADARA::RunStatus status);
	void terminate(ADARA::RunStatus status);

	StorageFile(boost::shared_ptr<StorageContainer> &container,
		    uint32_t number, bool create = false,
		    ADARA::RunStatus = ADARA::ADARA_RUN_STATUS_NO_RUN);

	friend class StorageManager;
};

#endif /* __STORAGE_FILE */
