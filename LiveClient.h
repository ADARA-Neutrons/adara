#ifndef __LIVE_CLIENT_H
#define __LIVE_CLIENT_H

#include <boost/smart_ptr.hpp>
#include <list>

#include "ADARAParser.h"
#include "StorageManager.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

class LiveClient : public StorageNotifier, ADARA::Parser {
public:
	LiveClient(int fd);
	~LiveClient();

	void fileAdded(boost::shared_ptr<StorageFile> &f);
	void fileUpdated(boost::shared_ptr<StorageFile> &f);

private:
	ReadyAdapter *m_read;
	ReadyAdapter *m_write;
	bool m_hello_received;
	off_t m_cur_offset;
	int m_client_fd;
	int m_file_fd;
	TimerAdapter<LiveClient> *m_timer;

	void writable(void);
	void readable(void);

	bool timerExpired(void);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr, const uint8_t *chunk,
			   unsigned int chunk_offset, unsigned int chunk_len);

	bool rxPacket(const ADARA::ClientHelloPkt &pkt);

	std::list<boost::shared_ptr<StorageFile> > m_files;

	static unsigned int m_max_send_chunk;
	static double m_hello_timeout;

	friend class ReadyAdapter;
	friend class TimerAdapter<LiveClient>;
};

#endif /* __LIVE_CLIENT_H */
