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
	DataSource(const std::string &uri, uint32_t id);
	~DataSource();

private:
	enum State { IDLE, CONNECTING, ACTIVE };

	std::string m_uri;
	ReadyAdapter *m_fdreg;
	TimerAdapter<DataSource> *m_timer;
	struct addrinfo *m_addrinfo;
	State m_state;
	uint32_t m_sourceId;
	int m_fd;

	bool m_newPulse;
	uint64_t m_lastPulseId;
	uint32_t m_dupCount;
	uint16_t m_expectedPktSeq;
	bool m_pulseEOP;
	ADARA::PulseFlavor::Enum m_pulseFlavor;
	uint32_t m_pulseCharge;
	uint16_t m_pulseVeto;
	uint16_t m_pulseCycle;
	uint8_t m_pulseTimingStatus;
	uint32_t m_pulseIntraTime;
	uint32_t m_pulseTofOffset;

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

	void endPulse(bool dup);
	bool checkPulseInvariants(const ADARA::RawDataPkt &pkt);

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

	friend class TimerAdapter<DataSource>;

	using ADARA::Parser::rxPacket;
};

#endif /* __DATA_SOURCE_H */
