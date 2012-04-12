#ifndef __DATA_SOURCE_H
#define __DATA_SOURCE_H

#include "ADARAParser.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

extern "C" {
struct addrinfo;
}

class DataSource : public ADARA::Parser {
public:
	DataSource(const std::string &uri);
	~DataSource();

private:
	enum State { IDLE, CONNECTING, ACTIVE };

	ReadyAdapter *m_fdreg;
	TimerAdapter<DataSource> *m_timer;
	struct addrinfo *m_addrinfo;
	State m_state;
	int m_fd;

	static unsigned int m_max_read_chunk;
	static double m_connect_retry;
	static double m_connect_timeout;
	static double m_data_timeout;

	void fdReady(void);

	void startConnect(void);
	void connectComplete(void);
	void dataReady(void);

	void connectionFailed(void);

	bool timerExpired(void);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxUnknownPkt(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr,
			   const uint8_t *chunk, unsigned int chunk_offset,
			   unsigned int chunk_len);

	bool rxPacket(const ADARA::RawDataPkt &pkt);
	bool rxPacket(const ADARA::RTDLPkt &pkt);
	bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);

	friend class ReadyAdapter;
	friend class TimerAdapter<DataSource>;
};

#endif /* __DATA_SOURCE_H */
