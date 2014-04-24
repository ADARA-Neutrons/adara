#ifndef __DATA_SOURCE_H
#define __DATA_SOURCE_H

#include <map>

#include "POSIXParser.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

extern "C" {
struct addrinfo;
}

class HWSource;

class DataSource : public ADARA::POSIXParser {
public:
	DataSource(const std::string &name, const std::string &uri, uint32_t id,
		   double connect_retry, double connect_timeout,
		   double data_timeout, unsigned int read_chunk);
	~DataSource();

private:
	typedef boost::shared_ptr<HWSource> HWSrcPtr;
	typedef std::map<uint32_t, HWSrcPtr> HWSrcMap;

	enum State { IDLE, CONNECTING, ACTIVE };

	std::string m_name;
	ReadyAdapter *m_fdreg;
	TimerAdapter<DataSource> *m_timer;
	struct addrinfo *m_addrinfo;
	State m_state;
	uint32_t m_smsSourceId;
	int m_fd;
	HWSrcMap m_hwSources;
	double m_connect_retry;
	double m_connect_timeout;
	double m_data_timeout;
	unsigned int m_max_read_chunk;

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
	bool rxPacket(const ADARA::SourceListPkt &pkt);
	bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);

	HWSource &getHWSource(uint32_t hwId);

	friend class TimerAdapter<DataSource>;

	using ADARA::POSIXParser::rxPacket;
};

#endif /* __DATA_SOURCE_H */
