#ifndef __STS_CLIENT_H
#define __STS_CLIENT_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <memory>

#include "POSIXParser.h"
#include "STSClientMgr.h"
#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "TimerAdapter.h"

class ReadyAdapter;

class STSClient : public ADARA::POSIXParser {
public:
	STSClient(int fd, StorageContainer::SharedPtr &run,
		  STSClientMgr &mgr);
	~STSClient();

	static void config(const boost::property_tree::ptree &conf);

private:
	typedef boost::signals::connection connection;

	STSClientMgr &m_mgr;
	int m_sts_fd;
	int m_file_fd;
	off_t m_cur_offset;
	StorageContainer::SharedPtr m_run;
	std::auto_ptr<ReadyAdapter> m_read;
	std::auto_ptr<ReadyAdapter> m_write;
	std::auto_ptr<TimerAdapter<STSClient> > m_timer;
	connection m_contConnection;
	connection m_fileConnection;
	STSClientMgr::Disposition m_disp;

	std::list<StorageFile::SharedPtr> m_files;

	void readable(void);
	void writable(void);
	bool sendHeartbeat(void);

	void fileAdded(StorageFile::SharedPtr &f);
	void fileUpdated(const StorageFile &f);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr, const uint8_t *chunk,
			   unsigned int chunk_offset, unsigned int chunk_len);
	bool rxPacket(const ADARA::TransCompletePkt &pkt);

	static double m_heartbeat_interval;
	static unsigned int m_max_send_chunk;

	using ADARA::POSIXParser::rxPacket;
};

#endif /* __STS_CLIENT_H */
