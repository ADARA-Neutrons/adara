#ifndef __STORAGE_MANAGER_H
#define __STORAGE_MANAGER_H

#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <string>

#include "ADARA.h"

class StorageFile;
class StorageContainer;

class StorageManager {
public:
	static void init(const std::string &baseDir);
	static void stop(void);

	static void startRecording(uint32_t run);
	static void stopRecording(void);

	static void addPacket(const void *pkt, uint32_t len,
			      bool notify = true);
	static int base_fd() { return m_base_fd; }

private:
	static void endCurrentFile(ADARA::RunStatus status);
	static void endCurrentContainer(void);

	static int m_base_fd;
	static boost::shared_ptr<StorageFile> m_cur_file;
	static boost::shared_ptr<StorageContainer> m_cur_container;

	static uint32_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;
};

#endif /* __STORAGE_MANAGER_H */
