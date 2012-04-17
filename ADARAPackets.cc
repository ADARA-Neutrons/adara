#include "ADARAPackets.h"

#include <string.h>

using namespace ADARA;

Packet::Packet(const uint8_t *data, uint32_t len) :
	PacketHeader(data), m_data(data), m_len(len), m_allocated(false)
{
}

Packet::Packet(const Packet &pkt) :
	PacketHeader(pkt.packet()), m_allocated(true)
{
	m_data = new uint8_t[pkt.packet_length()];
	m_len = pkt.packet_length();
	memcpy(const_cast<uint8_t *> (m_data), pkt.packet(), m_len);
}

Packet::~Packet()
{
	if (m_allocated)
		delete [] m_data;
}

#define EXPAND_CONSTRUCTOR(type) \
type::type(const uint8_t *data, uint32_t len) : Packet(data, len) { }
EXPAND_CONSTRUCTOR(RawDataPkt)
EXPAND_CONSTRUCTOR(RTDLPkt)
EXPAND_CONSTRUCTOR(BankedEventPkt)
EXPAND_CONSTRUCTOR(BeamMonitorPkt)
EXPAND_CONSTRUCTOR(PixelMappingPkt)
EXPAND_CONSTRUCTOR(RunStatusPkt)
EXPAND_CONSTRUCTOR(RunInfoPkt)
EXPAND_CONSTRUCTOR(TransCompletePkt)
EXPAND_CONSTRUCTOR(ClientHelloPkt)
EXPAND_CONSTRUCTOR(StatsResetPkt)
EXPAND_CONSTRUCTOR(SyncPkt)
EXPAND_CONSTRUCTOR(HeartbeatPkt)
EXPAND_CONSTRUCTOR(DeviceDescriptorPkt)
EXPAND_CONSTRUCTOR(VariableU32Pkt)
EXPAND_CONSTRUCTOR(VariableDoublePkt)
EXPAND_CONSTRUCTOR(VariableStringPkt)
