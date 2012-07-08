#ifndef __STS_CLIENT_H
#define __STS_CLIENT_H

#include <boost/smart_ptr.hpp>
#include <memory>

#include "ADARAParser.h"
#include "STSClientMgr.h"
#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "TimerAdapter.h"

class ReadyAdapter;

class STSClient : public ADARA::Parser {
public:
	STSClient(int fd, StorageManager::ContainerSharedPtr &run,
		  STSClientMgr &mgr);
	~STSClient();

private:
	typedef boost::signals::connection connection;

	STSClientMgr &m_mgr;
	int m_sts_fd;
	int m_file_fd;
	off_t m_cur_offset;
	StorageManager::ContainerSharedPtr m_run;
	std::auto_ptr<ReadyAdapter> m_read;
	std::auto_ptr<ReadyAdapter> m_write;
	std::auto_ptr<TimerAdapter<STSClient> > m_timer;
	connection m_contConnection;
	connection m_fileConnection;
	STSClientMgr::Disposition m_disp;

	std::list<StorageContainer::FileSharedPtr> m_files;

	void readable(void);
	void writable(void);
	bool sendHeartbeat(void);

	void fileAdded(StorageContainer::FileSharedPtr &f);
	void fileUpdated(const StorageFile &f);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr, const uint8_t *chunk,
			   unsigned int chunk_offset, unsigned int chunk_len);
	bool rxPacket(const ADARA::TransCompletePkt &pkt);

	static double m_heartbeat_interval;
	static unsigned int m_max_send_chunk;

	using ADARA::Parser::rxPacket;
};

#endif /* __STS_CLIENT_H */
