#ifndef __STORAGE_MANAGER_H
#define __STORAGE_MANAGER_H

#include <boost/smart_ptr.hpp>
#include <boost/function.hpp>
#include <boost/signal.hpp>
#include <stdint.h>
#include <sys/uio.h>
#include <string>
#include <vector>

#include "ADARA.h"
#include "Storage.h"

class StorageFile;
class StorageContainer;

class StorageManager {
public:
	typedef boost::shared_ptr<StorageContainer> ContainerSharedPtr;
	typedef boost::signal<void (ContainerSharedPtr &, bool)> onContChange;

	static void init(const std::string &baseDir);
	static void stop(void);

	static void startRecording(uint32_t run);
	static void stopRecording(void);

	static void addPacket(IoVector &iovec, bool notify = true);
	static void addPacket(const void *pkt, uint32_t len,
			      bool notify = true) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPacket(iovec, notify);
	}
	static int base_fd() { return m_base_fd; }

	static boost::signals::connection onContainerChange(
					const onContChange::slot_type &s) {
		return m_contChange.connect(s);
	}

	static ContainerSharedPtr &container (void) { return m_cur_container; }

private:
	static int m_base_fd;

	static uint32_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;

	static boost::shared_ptr<StorageContainer> m_cur_container;

	static onContChange m_contChange;

	static void addBaseStorage(off_t size);

	static void endCurrentContainer(void);

	friend class StorageContainer;
};

#endif /* __STORAGE_MANAGER_H */
