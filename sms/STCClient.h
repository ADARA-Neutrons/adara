#ifndef __STC_CLIENT_H
#define __STC_CLIENT_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <memory>

#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "STCClientMgr.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "TimerAdapter.h"

class ReadyAdapter;

class STCClient : public ADARA::POSIXParser {
public:
	STCClient(int fd, StorageContainer::SharedPtr &run, STCClientMgr &mgr);
	~STCClient();

	static void config(const boost::property_tree::ptree &conf);

private:
	typedef boost::signals2::connection connection;

	STCClientMgr &m_mgr;

	int m_stc_fd;
	int m_file_fd;

	off_t m_cur_offset;

	StorageContainer::SharedPtr m_run;

	bool m_send_paused_data;

	ReadyAdapter *m_read;
	ReadyAdapter *m_write;

	TimerAdapter<STCClient> *m_timer;

	connection m_contConnection;
	connection m_fileConnection;

	STCClientMgr::Disposition m_disp;
	std::string m_reason;

	std::list<StorageFile::SharedPtr> m_files;

	void readable(void);
	void writable(void);

	bool sendHeartbeat(void);

	void sendDataDone(void);

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

#endif /* __STC_CLIENT_H */
