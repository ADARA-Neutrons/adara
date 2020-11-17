#ifndef __LIVE_CLIENT_H
#define __LIVE_CLIENT_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <string>
#include <list>

#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "LiveServer.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

class smsStringPV;
class smsUint32PV;
class smsConnectedPV;

class LiveClient : public ADARA::POSIXParser {
public:
	LiveClient(LiveServer *server, int fd);
	~LiveClient();

	static void config(const boost::property_tree::ptree &conf);

private:
	typedef boost::signals2::connection connection;
	typedef std::pair<StorageFile::SharedPtr, off_t> FileEntry;
	typedef std::list<FileEntry> FileList;

	typedef std::pair<StorageContainer::SharedPtr, bool> ContEntry;
	typedef std::list<ContEntry> ContList;

	LiveServer *m_server;

	ContList m_conts;

	FileList m_files;

	bool m_starting_new_file;

	off_t m_bytes_written;

	ReadyAdapter *m_read;
	ReadyAdapter *m_write;

	bool m_hello_received;

	int m_client_fd;
	int m_file_fd;

	bool m_send_paused_data;

	uint32_t m_client_flags;

	TimerAdapter<LiveClient> *m_timer;

	connection m_mgrConnection;
	connection m_contConnection;
	connection m_fileConnection;

	std::string m_clientName;
	int32_t m_clientId;

	boost::shared_ptr<smsStringPV> m_pvName;
	boost::shared_ptr<smsUint32PV> m_pvRequestedStartTime;
	boost::shared_ptr<smsStringPV> m_pvCurrentFilePath;
	boost::shared_ptr<smsConnectedPV> m_pvStatus;

	void containerChange(StorageContainer::SharedPtr &, bool);

	void historicalFile(StorageFile::SharedPtr &f, off_t start);
	void fileAdded(StorageFile::SharedPtr &f);

	void fileUpdated(const StorageFile &f);

	void writable(void);
	void readable(void);

	bool timerExpired(void);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr, const uint8_t *chunk,
			   unsigned int chunk_offset, unsigned int chunk_len);

	bool rxPacket(const ADARA::ClientHelloPkt &pkt);

	static unsigned int m_max_send_chunk;
	static double m_hello_timeout;

	friend class TimerAdapter<LiveClient>;

	using ADARA::POSIXParser::rxPacket;
};

#endif /* __LIVE_CLIENT_H */
