#ifndef __STORAGE_FILE
#define __STORAGE_FILE

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>
#include <boost/property_tree/ptree.hpp>

#include "ADARA.h"
#include "Storage.h"

class StorageContainer;

class StorageFile : boost::noncopyable {
public:
	typedef boost::shared_ptr<StorageFile> SharedPtr;
	typedef boost::signal<void (const StorageFile &)> onUpdate;

	/* We cannot use StorageContainer::WeakPtr here due to a circular
	 * dependency, so open-code a matching type to hold the weak pointer
	 * to the owning container.
	 */
	typedef boost::weak_ptr<StorageContainer> OwnerPtr;

	/* TODO can we convert this to a shared_ptr/weak_ptr setup
	 * for some fd class?
	 */
	int get_fd(void);
	void put_fd(void);
	bool active(void) const { return m_active; }
	bool oversize(void) const { return m_oversize; }
	off_t size(void) const { return m_size; }
	uint32_t fileNumber(void) const { return m_fileNumber; }
	bool willPersist(void) const { return m_persist; }
	void persist(bool p = true) { m_persist = p; }
	OwnerPtr owner(void) const { return m_owner; }

	boost::signals::connection connect(const onUpdate::slot_type &slot) {
		return m_update.connect(slot);
	}

	/* Create a new file within a container to store data */
	static SharedPtr newFile(OwnerPtr owner, uint32_t fileNumber,
				 ADARA::RunStatus::Enum status);

	/* Create a file to persist experiment state information */
	static SharedPtr stateFile(OwnerPtr runInfo,
				   const std::string &basePath);

	/* Create an object to manage an existing file */
	static SharedPtr importFile(OwnerPtr owner,
				    const std::string &path);

	off_t write(IoVector &iovec, uint32_t len, bool do_notify = true);
	void terminate(ADARA::RunStatus::Enum status);
	void notify(void);

	~StorageFile();

	static void config(const boost::property_tree::ptree &conf);

private:
	OwnerPtr m_owner;
	std::string m_path;
	uint32_t m_runNumber;
	uint32_t m_fileNumber;
	uint32_t m_startTime;
	bool m_persist;
	bool m_oversize;
	bool m_active;
	off_t m_size;
	off_t m_sizeLastUpdate;
	off_t m_syncDistance;
	int m_fd;
	unsigned int m_fdRefs;
	onUpdate m_update;

	static off_t m_max_file_size;
	static off_t m_max_sync_distance;

	void makePath(void);
	void open(int flags);
	void addSync(void);
	void addRunStatus(ADARA::RunStatus::Enum status);

	StorageFile(OwnerPtr &owner, uint32_t fileNumber);
};

#endif /* __STORAGE_FILE */
