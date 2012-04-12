#ifndef __STORAGE_MANAGER_H
#define __STORAGE_MANAGER_H

#include <boost/smart_ptr.hpp>
#include <boost/function.hpp>
#include <stdint.h>
#include <string>
#include <list>

#include "ADARA.h"

class StorageFile;
class StorageContainer;

class StorageNotifier {
public:
	virtual void fileAdded(boost::shared_ptr<StorageFile> &f) = 0;
	virtual void fileUpdated(boost::shared_ptr<StorageFile> &f) = 0;
};

class StorageManager {
public:
	static void init(const std::string &baseDir);
	static void stop(void);

	static void startRecording(uint32_t run);
	static void stopRecording(void);

	static void addPacket(const void *pkt, uint32_t len,
			      bool notify = true);
	static int base_fd() { return m_base_fd; }

	static boost::shared_ptr<StorageFile> &subscribe(StorageNotifier *);
	static void unsubscribe(StorageNotifier *);

private:
	static void endCurrentFile(ADARA::RunStatus status);
	static void endCurrentContainer(void);

	static void notifyFileAdded(void);
	static void notifyFileUpdated(void);

	static int m_base_fd;
	static boost::shared_ptr<StorageFile> m_cur_file;
	static boost::shared_ptr<StorageContainer> m_cur_container;

	static std::list<StorageNotifier *> m_notifiers;

	static uint32_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;
};

#endif /* __STORAGE_MANAGER_H */
