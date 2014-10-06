#ifndef __DATA_SOURCE_H
#define __DATA_SOURCE_H

#include <boost/smart_ptr.hpp>
#include <map>

#include "POSIXParser.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

extern "C" {
struct addrinfo;
}

class HWSource;
class smsStringPV;
class smsConnectedPV;
class smsBooleanPV;

class DataSource : public ADARA::POSIXParser {
public:
	DataSource(const std::string &name, const std::string &uri,
		uint32_t id, const std::string beamlineId,
		double connect_retry, double connect_timeout, double data_timeout,
		bool ignore_eop, unsigned int read_chunk);
	~DataSource();

	bool m_readDelay;

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
	bool m_ignore_eop;
	unsigned int m_max_read_chunk;

	boost::shared_ptr<smsStringPV> m_pvName;
	boost::shared_ptr<smsConnectedPV> m_pvConnected;
	boost::shared_ptr<smsBooleanPV> m_pvIgnoreEoP;

	uint64_t m_lastRTDLPulseId;
	uint16_t m_lastRTDLCycle;
	uint32_t m_dupRTDL;

	void fdReady(void);

	void startConnect(void);
	void connectComplete(void);
	void dataReady(void);

	void dumpLastReadStats(std::string who);

	void unregisterHWSources(bool isSourceDown, std::string why);
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
	bool rxPacket(const ADARA::HeartbeatPkt &pkt);

	// Last Packet Debug
	int m_last_pkt_type; // PacketType::Enum
	uint32_t m_last_pkt_len;
	time_t m_last_pkt_sec;
	long m_last_pkt_nsec;

	HWSource &getHWSource(uint32_t hwId);

	friend class TimerAdapter<DataSource>;

	using ADARA::POSIXParser::rxPacket;
};

#endif /* __DATA_SOURCE_H */
