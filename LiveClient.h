#ifndef __LIVE_CLIENT_H
#define __LIVE_CLIENT_H

#include <boost/smart_ptr.hpp>
#include <list>

#include "ADARAParser.h"
#include "StorageManager.h"
#include "StorageContainer.h"
#include "StorageFile.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

class LiveClient : public ADARA::Parser {
public:
	LiveClient(int fd);
	~LiveClient();

private:
	typedef boost::signals::connection connection;
	typedef std::pair<StorageFile::SharedPtr, off_t> FileEntry;
	typedef std::list<FileEntry> FileList;

	FileList m_files;
	ReadyAdapter *m_read;
	ReadyAdapter *m_write;
	bool m_hello_received;
	int m_client_fd;
	int m_file_fd;
	TimerAdapter<LiveClient> *m_timer;
	connection m_mgrConnection;
	connection m_contConnection;
	connection m_fileConnection;

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

	using ADARA::Parser::rxPacket;
};

#endif /* __LIVE_CLIENT_H */
