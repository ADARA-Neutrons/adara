#ifndef __STORAGE_FILE
#define __STORAGE_FILE

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <boost/property_tree/ptree.hpp>

#include <stdint.h>

#include "ADARA.h"
#include "Storage.h"

class StorageContainer;

class StorageFile : boost::noncopyable {
public:
	typedef boost::shared_ptr<StorageFile> SharedPtr;
	typedef boost::signals2::signal<void (const StorageFile &)> onUpdate;

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
	std::string path(void) const { return m_path; }
	bool active(void) const { return m_active; }
	void setInactive(void) { m_active = false; }
	bool paused(void) const { return m_paused; }
	bool addendum(void) const { return m_addendum; }
	bool oversize(void) const { return m_oversize; }
	off_t size(void) const { return m_size; }
	uint32_t modeNumber(void) const { return m_modeNumber; }
	uint32_t fileNumber(void) const { return m_fileNumber; }
	uint32_t pauseFileNumber(void) const { return m_pauseFileNumber; }
	uint32_t addendumFileNumber(void) const { return m_addendumFileNumber; }
	bool willPersist(void) const { return m_persist; }
	void persist(bool p = true) { m_persist = p; }
	OwnerPtr owner(void) const { return m_owner; }

	boost::signals2::connection connect(const onUpdate::slot_type &slot) {
		return m_update.connect(slot);
	}

	/* Create a new file within a container to store data */
	static SharedPtr newFile(OwnerPtr owner, bool paused,
			uint32_t modeNumber, uint32_t fileNumber,
			uint32_t pauseFileNumber, ADARA::RunStatus::Enum status);

	/* Create a file to persist experiment state information */
	static SharedPtr stateFile(OwnerPtr runInfo,
			const std::string &basePath);

	/* Create a file to save the input stream for a given data source */
	static SharedPtr saveFile(OwnerPtr runInfo, uint32_t dataSourceId,
		uint32_t saveFileNumber);

	/* Create an object to manage an existing file */
	static SharedPtr importFile(OwnerPtr owner, const std::string &path,
			bool &saved_file, uint64_t &saved_size);

	static uint64_t fileSize(const std::string &path);

	bool write(IoVector &iovec, uint32_t len, bool do_notify = true,
		uint32_t *written = NULL);
	void terminate(ADARA::RunStatus::Enum status);
	void notify(void);

	bool save(IoVector &iovec, uint32_t len, uint32_t *written = NULL);
	void terminateSave(void);

	bool catFile(SharedPtr src);

	~StorageFile();

	static void config(const boost::property_tree::ptree &conf);

private:
	OwnerPtr m_owner;
	std::string m_path;
	uint32_t m_runNumber;
	uint32_t m_modeNumber;
	uint32_t m_fileNumber;
	uint32_t m_pauseFileNumber;
	uint32_t m_addendumFileNumber;
	uint32_t m_startTime; // Wallclock Time...!
	bool m_persist;
	bool m_oversize;
	bool m_active;
	bool m_paused;
	bool m_addendum;
	off_t m_size;
	off_t m_sizeLastUpdate;
	off_t m_syncDistance;
	int m_fd;
	unsigned int m_fdRefs;
	onUpdate m_update;

	static off_t m_max_file_size;
	static off_t m_max_sync_distance;

	void makePath(bool is_prologue);
	void open(int flags);
	void addSync(void);
	void addRunStatus(ADARA::RunStatus::Enum status);

	StorageFile(OwnerPtr &owner, bool paused,
		uint32_t modeNumber, uint32_t fileNumber, uint32_t pauseFileNumber);
};

#endif /* __STORAGE_FILE */
