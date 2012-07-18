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
#include "StorageContainer.h"
#include "StorageFile.h"

class StorageManager {
public:
	typedef boost::signal<void (StorageContainer::SharedPtr &, bool)>
								ContainerSignal;
	typedef boost::signal<void (void)> PrologueSignal;
	typedef boost::function<void (StorageFile::SharedPtr &, off_t)>
								FileOffSetFunc;

	static void init(const std::string &baseDir);
	static void stop(void);

	static void startRecording(uint32_t run);
	static void stopRecording(void);

	static void iterateHistory(uint32_t startSeconds, FileOffSetFunc cb);

	static void addPacket(IoVector &iovec, bool notify = true);
	static void addPacket(const void *pkt, uint32_t len,
			      bool notify = true) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPacket(iovec, notify);
	}

	static void addPrologue(IoVector &iovec);
	static void addPrologue(const void *pkt, uint32_t len) {
		IoVector iovec(1);
		iovec[0].iov_base = (void *) pkt;
		iovec[0].iov_len = len;
		addPrologue(iovec);
	}

	static int base_fd() { return m_base_fd; }

	static boost::signals::connection onContainerChange(
					const ContainerSignal::slot_type &s) {
		return m_contChange.connect(s);
	}

	static boost::signals::connection onPrologue(
					const PrologueSignal::slot_type &s) {
		return m_prologue.connect(s);
	}

	static StorageContainer::SharedPtr &container (void) {
		return m_cur_container;
	}

private:
	typedef boost::signals::connection connection;

	static int m_base_fd;

	static uint32_t m_block_size;
	static uint64_t m_blocks_used;
	static uint64_t m_max_blocks_allowed;

	static StorageContainer::SharedPtr m_cur_container;
	static StorageFile::SharedPtr m_prologueFile;

	static ContainerSignal m_contChange;
	static PrologueSignal m_prologue;

	static void addBaseStorage(off_t size);
	static void endCurrentContainer(void);
	static void fileCreated(StorageFile::SharedPtr &f);
	static uint32_t validatePacket(const IoVector &iovec);

	friend class StorageContainer;
};

#endif /* __STORAGE_MANAGER_H */
