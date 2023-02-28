
#include <string.h>
#include <string>
#include <sstream>

#include "ADARAPackets.h"

using namespace ADARA;

static bool validate_status(uint16_t val)
{
	VariableStatus::Enum e = static_cast<VariableStatus::Enum>(val);

	/* No default case so that we get warned when new status values
	 * get added.
	 */
	switch(e) {
	case VariableStatus::OK:
	case VariableStatus::READ_ERROR:
	case VariableStatus::WRITE_ERROR:
	case VariableStatus::HIHI_LIMIT:
	case VariableStatus::HIGH_LIMIT:
	case VariableStatus::LOLO_LIMIT:
	case VariableStatus::LOW_LIMIT:
	case VariableStatus::BAD_STATE:
	case VariableStatus::CHANGED_STATE:
	case VariableStatus::NO_COMMUNICATION:
	case VariableStatus::COMMUNICATION_TIMEOUT:
	case VariableStatus::HARDWARE_LIMIT:
	case VariableStatus::BAD_CALCULATION:
	case VariableStatus::INVALID_SCAN:
	case VariableStatus::LINK_FAILED:
	case VariableStatus::INVALID_STATE:
	case VariableStatus::BAD_SUBROUTINE:
	case VariableStatus::UNDEFINED_ALARM:
	case VariableStatus::DISABLED:
	case VariableStatus::SIMULATED:
	case VariableStatus::READ_PERMISSION:
	case VariableStatus::WRITE_PERMISSION:
	case VariableStatus::UPSTREAM_DISCONNECTED:
	case VariableStatus::NOT_REPORTED:
		return false;
	}

	return true;
}

static bool validate_severity(uint16_t val)
{
	VariableSeverity::Enum e = static_cast<VariableSeverity::Enum>(val);

	/* No default case so that we get warned when new severities get added.
	 */
	switch (e) {
	case VariableSeverity::OK:
	case VariableSeverity::MINOR_ALARM:
	case VariableSeverity::MAJOR_ALARM:
	case VariableSeverity::INVALID:
	case VariableSeverity::NOT_REPORTED:
		return false;
	}

	return true;
}

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

/* -------------------------------------------------------------------- */

RawDataPkt::RawDataPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RawDataPacket V0 is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RawDataPacket V1 is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::RAW_EVENT_VERSION
			&& m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer RawDataPacket is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

RawDataPkt::RawDataPkt(const RawDataPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

MappedDataPkt::MappedDataPkt(const uint8_t *data, uint32_t len) :
	RawDataPkt(data, len)
{
	if (m_version == 0x00 && m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MappedDataPacket V0 is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MappedDataPacket V1 is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::MAPPED_EVENT_VERSION
			&& m_payload_len < (6 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MappedDataPacket is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

MappedDataPkt::MappedDataPkt(const MappedDataPkt &pkt) :
	RawDataPkt(pkt)
{}

/* -------------------------------------------------------------------- */

RTDLPkt::RTDLPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len),
	m_fields(const_cast<uint32_t *>(
		reinterpret_cast<const uint32_t *>(payload())))
	// Note: RTDLPkt m_fields can't be "const", as we Modify Pulse Charge!
{
	if (m_version == 0x00 && m_payload_len != 120) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RTDL V0 Packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len != 120) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RTDL V1 Packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::RTDL_VERSION
			&& m_payload_len < 120) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer RTDL Packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// "Missing Ring Period" - General RTDL Error...
	//   - Log RTDL Packet Contents for Diagnostics
	if ((m_fields[4] >> 24) != 4) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RTDL Packet";
		ss << " (0x" << std::hex << m_base_type << std::dec;
		ss << ",v" << m_version << ")";
		ss << " [" << ( m_payload_len + 16 ) << " bytes]";
		ss << " - Missing ring period:";
		// m_fields[0]
		ss << " [0]=[0x" << std::hex << m_fields[0] << std::dec << "]";
		ss << " dataFlags="
			<< ( ( m_version >= 0x01 ) ? "true" : "false" );
		ss << " 0x"
			<< std::hex << ( (m_fields[0] >> 27) & 0x1f ) << std::dec;
		ss << " flavor="
			<< static_cast<PulseFlavor::Enum>( (m_fields[0] >> 24) & 0x7 );
		ss << " charge="
			<< ( (uint64_t) ( m_fields[0] & 0x00ffffff ) ) << "pC";
		// m_fields[1]
		ss << " [1]=[0x" << std::hex << m_fields[1] << std::dec << "]";
		ss << " cycle=" << ( m_fields[1] & 0x3ff );
		ss << " badCycle=" << ( !!(m_fields[1] & 0x40000000) );
		ss << " vetoFlags=0x"
			<< std::hex << ( (m_fields[1] >> 10) & 0xfff ) << std::dec;
		ss << " badVeto=" << ( !!(m_fields[1] & 0x80000000) );
		ss << " timing=0x"
			<< std::hex << ( (m_fields[1] >> 22) & 0xff ) << std::dec;
		// m_fields[2]
		ss << " [2]=[0x" << std::hex << m_fields[2] << std::dec << "]";
		ss << " intrapulse="
			<< ( (uint64_t) m_fields[2] * 100 ) << "ns";
		// m_fields[3]
		ss << " [3]=[0x" << std::hex << m_fields[3] << std::dec << "]";
		ss << " tofOffset="
			<< ( (uint64_t) (m_fields[3] & 0x7fffffff) * 100 ) << "ns";
		ss << " tofCorrected="
			<< ( ( !!(m_fields[3] & 0x80000000) ) ? "corrected" : "raw" );
		// m_fields[4]
		ss << " [4]=[0x" << std::hex << m_fields[4] << std::dec << "]";
		ss << " ringPeriod=" << ( m_fields[4] & 0xffffff ) << "ps";
		// m_fields[5...]
		for ( uint32_t i=0 ; i < 25 ; i++ ) {
			ss << " FNA" << i << "="
				<< ( (m_fields[ 5 + i ] >> 24) & 0xff )
				<< "/" << ( m_fields[ 5 + i ] & 0xffffff );
		}
		ss << ".";
		throw invalid_packet(ss.str());
	}
}

RTDLPkt::RTDLPkt(const RTDLPkt &pkt) :
	Packet(pkt),
	m_fields(const_cast<uint32_t *>(
		reinterpret_cast<const uint32_t *>(payload())))
	// Note: RTDLPkt m_fields can't be "const", as we Modify Pulse Charge!
{}

/* -------------------------------------------------------------------- */

SourceListPkt::SourceListPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{}

SourceListPkt::SourceListPkt(const SourceListPkt &pkt) :
	Packet(pkt)
{}

/* -------------------------------------------------------------------- */

BankedEventPkt::BankedEventPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BankedEvent V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BankedEvent V1 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::BANKED_EVENT_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer BankedEvent packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

BankedEventPkt::BankedEventPkt(const BankedEventPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

BankedEventStatePkt::BankedEventStatePkt(
		const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BankedEventState V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::BANKED_EVENT_STATE_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer BankedEventState packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

BankedEventStatePkt::BankedEventStatePkt(const BankedEventStatePkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

BeamMonitorPkt::BeamMonitorPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BeamMonitor V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BeamMonitor V1 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::BEAM_MONITOR_EVENT_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer BeamMonitor packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

BeamMonitorPkt::BeamMonitorPkt(const BeamMonitorPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

PixelMappingPkt::PixelMappingPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{}

PixelMappingPkt::PixelMappingPkt(const PixelMappingPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

PixelMappingAltPkt::PixelMappingAltPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{}

PixelMappingAltPkt::PixelMappingAltPkt(const PixelMappingAltPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

RunStatusPkt::RunStatusPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len != (3 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RunStatus V0 packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01
			&& m_payload_len != (5 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RunStatus V1 packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::RUN_STATUS_VERSION
			&& m_payload_len < (5 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer RunStatus packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
}

RunStatusPkt::RunStatusPkt(const RunStatusPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

RunInfoPkt::RunInfoPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *) payload();
	const char *xml = (const char *) payload() + sizeof(uint32_t);

	if (m_version == 0x00 && m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RunInfo V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::RUN_INFO_VERSION
			&& m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer RunInfo packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	if (m_version == 0x00 && m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " RunInfo V0 packet has Undersize XML string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(xml + i)) )
				ss << "#000";
			else
				ss << *(xml + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::RUN_INFO_VERSION
			&& m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer RunInfo packet has Undersize XML string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(xml + i)) )
				ss << "#000";
			else
				ss << *(xml + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_xml.assign(xml, size);
}

RunInfoPkt::RunInfoPkt(const RunInfoPkt &pkt) :
	Packet(pkt), m_xml(pkt.m_xml)
{}

/* -------------------------------------------------------------------- */

TransCompletePkt::TransCompletePkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *) payload();
	const char *reason = (const char *) payload() + sizeof(uint32_t);

	if (m_version == 0x00 && m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " TransComplete V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::TRANS_COMPLETE_VERSION
			&& m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer TransComplete packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	m_status = (uint16_t) (size >> 16);
	size &= 0xffff;
	if (m_version == 0x00 && m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " TransComplete V0 packet has Undersize Reason string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(reason + i)) )
				ss << "#000";
			else
				ss << *(reason + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::TRANS_COMPLETE_VERSION
			&& m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer TransComplete packet has Undersize Reason string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(reason + i)) )
				ss << "#000";
			else
				ss << *(reason + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_reason.assign(reason, size);
}

TransCompletePkt::TransCompletePkt(const TransCompletePkt &pkt) :
	Packet(pkt), m_reason(pkt.m_reason)
{}

/* -------------------------------------------------------------------- */

ClientHelloPkt::ClientHelloPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_version == 0x00 && m_payload_len != sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " ClientHello V0 packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01
			&& m_payload_len != (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " ClientHello V1 packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::CLIENT_HELLO_VERSION
			&& m_payload_len < (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer ClientHello packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	const uint32_t *fields = (const uint32_t *) payload();

	m_reqStart = fields[0];

	m_clientFlags = ( m_version > 0 ) ? fields[1] : 0;
}

ClientHelloPkt::ClientHelloPkt(const ClientHelloPkt &pkt) :
	Packet(pkt), m_reqStart(pkt.m_reqStart),
	m_clientFlags(pkt.m_clientFlags)
{}

/* -------------------------------------------------------------------- */

AnnotationPkt::AnnotationPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " AnnotationPkt V0 packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::STREAM_ANNOTATION_VERSION
			&& m_payload_len < (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer AnnotationPkt packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	uint16_t size = m_fields[0] & 0xffff;
	const char *comment = (const char *) &m_fields[2];
	if (m_version == 0x00
			&& m_payload_len < (size + (2 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " AnnotationPkt V0 packet has Undersize Annotation string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - (2 * sizeof(uint32_t)));
		ss << " [";
		for ( uint32_t i=0 ;
				i < m_payload_len - (2 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(comment + i)) )
				ss << "#000";
			else
				ss << *(comment + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::STREAM_ANNOTATION_VERSION
			&& m_payload_len < (size + (2 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer AnnotationPkt packet"
			<< " has Undersize Annotation string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - (2 * sizeof(uint32_t)));
		ss << " [";
		for ( uint32_t i=0 ; 
				i < m_payload_len - (2 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(comment + i)) )
				ss << "#000";
			else
				ss << *(comment + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
}

AnnotationPkt::AnnotationPkt(const AnnotationPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

SyncPkt::SyncPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len < 28) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Sync V0 packet is too small: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::SYNC_VERSION
			&& m_payload_len < 28) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Sync packet is too small: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	const char *signature = (const char *) &m_fields[0];
	m_signature.assign(signature, 16);

	m_offset = *((uint64_t *) &m_fields[4]);

	uint32_t size = m_fields[6];
	const char *comment = (const char *) &m_fields[7];
	if (m_version == 0x00 && m_payload_len < (size + 28)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Sync V0 packet has Undersize Sync Comment string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - 28);
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - 28 ; i++ ) {
			if ( ! ((uint8_t) *(comment + i)) )
				ss << "#000";
			else
				ss << *(comment + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::SYNC_VERSION
			&& m_payload_len < (size + 28)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Sync packet has Undersize Sync Comment string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - 28);
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - 28 ; i++ ) {
			if ( ! ((uint8_t) *(comment + i)) )
				ss << "#000";
			else
				ss << *(comment + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	m_comment.assign(comment, size);
}

SyncPkt::SyncPkt(const SyncPkt &pkt) :
	Packet(pkt)
{}

/* -------------------------------------------------------------------- */

HeartbeatPkt::HeartbeatPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_version == 0x00 && m_payload_len != 0) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Heartbeat V0 packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::HEARTBEAT_VERSION
			&& m_payload_len == 0) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer ClientHello packet is too short: "
			<< m_payload_len;
		// ...any newer version would have to grow, lol...? ;-D
		throw invalid_packet(ss.str());
	}
}

HeartbeatPkt::HeartbeatPkt(const HeartbeatPkt &pkt) :
	Packet(pkt)
{}

/* -------------------------------------------------------------------- */

GeometryPkt::GeometryPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_version == 0x00 && m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Geometry V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::GEOMETRY_VERSION
			&& m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Geometry packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	uint32_t size = *(const uint32_t *) payload();
	const char *xml = (const char *) payload() + sizeof(uint32_t);
	if (m_version == 0x00 && m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Geometry V0 packet has Undersize Geometry string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(xml + i)) )
				ss << "#000";
			else
				ss << *(xml + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::GEOMETRY_VERSION
			&& m_payload_len < (size + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Geometry packet has Undersize Geometry string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(xml + i)) )
				ss << "#000";
			else
				ss << *(xml + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_xml.assign(xml, size);
}

GeometryPkt::GeometryPkt(const GeometryPkt &pkt) :
	Packet(pkt), m_xml(pkt.m_xml)
{}

/* -------------------------------------------------------------------- */

BeamlineInfoPkt::BeamlineInfoPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	const char *info = (const char *) payload() + sizeof(uint32_t);
	uint32_t sizes = *(const uint32_t *) payload();
	uint32_t id_len, shortName_len, longName_len, info_len;

	if (m_version == 0x00 && m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Beamline Info V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01 && m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Beamline Info V1 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::BEAMLINE_INFO_VERSION
			&& m_payload_len < sizeof(uint32_t)) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Beamline Info packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	longName_len = sizes & 0xff;
	shortName_len = (sizes >> 8) & 0xff;
	id_len = (sizes >> 16) & 0xff;
	m_targetStationNumber = (sizes >> 24) & 0xff; // Formerly Unused in V0

	// Unspecified (Version 0 Packet) Target Station Number Defaults to 1.
	if ( m_targetStationNumber == 0 )
		m_targetStationNumber = 1;

	info_len = id_len + shortName_len + longName_len;

	if (m_version == 0x00 && m_payload_len < (info_len + sizeof(uint32_t)))
	{
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Beamline Info V0 packet has Undersize Beamline Info data";
		ss << " info_len=" << info_len << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(info + i)) )
				ss << "#000";
			else
				ss << *(info + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version == 0x01
			&& m_payload_len < (info_len + sizeof(uint32_t)))
	{
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Beamline Info V1 packet has Undersize Beamline Info data";
		ss << " info_len=" << info_len << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(info + i)) )
				ss << "#000";
			else
				ss << *(info + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::BEAMLINE_INFO_VERSION
			&& m_payload_len < (info_len + sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer Beamline Info packet"
			<< " has Undersize Beamline Info data";
		ss << " info_len=" << info_len << " vs. available payload="
			<< (m_payload_len - sizeof(uint32_t));
		ss << " [";
		for ( uint32_t i=0 ; i < m_payload_len - sizeof(uint32_t) ; i++ ) {
			if ( ! ((uint8_t) *(info + i)) )
				ss << "#000";
			else
				ss << *(info + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}

	m_id.assign(info, id_len);
	info += id_len;
	m_shortName.assign(info, shortName_len);
	info += shortName_len;
	m_longName.assign(info, longName_len);
}

BeamlineInfoPkt::BeamlineInfoPkt(const BeamlineInfoPkt &pkt) :
	Packet(pkt), m_targetStationNumber(pkt.m_targetStationNumber),
	m_id(pkt.m_id), m_shortName(pkt.m_shortName),
	m_longName(pkt.m_longName)
{}

/* -------------------------------------------------------------------- */

BeamMonitorConfigPkt::BeamMonitorConfigPkt(const uint8_t *data,
		uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	// Size of Beam Monitor Config Section, Including:
	// U32( ID, TOF Offset, Max TOF, Histo Bin Size ) + Double( Distance )
	m_sectionSize = ( 4 * sizeof(uint32_t) ) + sizeof(double);

	// Expected Payload Size
	uint32_t expected_payload_len = sizeof(uint32_t); // Beam Monitor Count
	expected_payload_len += beamMonCount() * m_sectionSize; // Config Sects
	if ( m_version >= 0x01 ) {
		// Disembodied Beam Monitor Formats Appended at End of Packet ;-b
		expected_payload_len += beamMonCount() * sizeof(uint32_t);
	}

	if ( m_version == 0x00 && m_payload_len != expected_payload_len ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BeamMonitorConfig V0 packet is incorrect length: "
			<< m_payload_len << " != " << expected_payload_len;
		throw invalid_packet(ss.str());
	}
	else if ( m_version == 0x01
			&& m_payload_len != expected_payload_len ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " BeamMonitorConfig V1 packet is incorrect length: "
			<< m_payload_len << " != " << expected_payload_len;
		throw invalid_packet(ss.str());
	}
	else if ( m_version > ADARA::PacketType::BEAM_MONITOR_CONFIG_VERSION
			&& m_payload_len < expected_payload_len ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer BeamMonitorConfig packet is too short: "
			<< m_payload_len << " < " << expected_payload_len;
		throw invalid_packet(ss.str());
	}
}

BeamMonitorConfigPkt::BeamMonitorConfigPkt(
		const BeamMonitorConfigPkt &pkt ) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
		m_sectionSize(pkt.m_sectionSize)
{}

/* -------------------------------------------------------------------- */

DetectorBankSetsPkt::DetectorBankSetsPkt(const uint8_t *data,
		uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload()),
	m_sectionOffsets(NULL), m_after_banks_offset(NULL)
{
	// Get Number of Detector Bank Sets...
	//    - Basic Packet Size Sanity Check

	if ( m_version == 0x00 && m_payload_len < sizeof(uint32_t) ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " DetectorBankSets V0 packet is too short for Count! "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if ( m_version > ADARA::PacketType::DETECTOR_BANK_SETS_VERSION
			&& m_payload_len < sizeof(uint32_t) ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer DetectorBankSets packet is too short for Count! "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	uint32_t numSets = detBankSetCount();

	// Don't Allocate Anything if there are No Detector Bank Sets...
	if ( numSets < 1 )
		return;

	m_sectionOffsets = new uint32_t[numSets];

	m_after_banks_offset = new uint32_t[numSets];

	// Traverse Detector Bank Sets...
	//    - Set Section Offsets
	//    - Set "After Banks" Offsets

	// Base Section Sizes (w/o Bank Ids)
	uint32_t baseSectionOffsetPart1 = 0
		+ m_name_offset   // name
		+ 2;   // flags & bank id count
	uint32_t baseSectionOffsetPart2 = 0
		+ 3   // histo params
		+ 2   // throttle rate (double)
		+ m_suffix_offset;
	uint32_t baseSectionOffsetNoBanks =
		baseSectionOffsetPart1 + baseSectionOffsetPart2;

	// Running Section Offset (in number of uint32_t elements)
	uint32_t sectionOffset = 1;   // for Detector Bank Set Count...

	for ( uint32_t i=0 ; i < numSets ; i++ )
	{
		// Section Offset
		m_sectionOffsets[i] = sectionOffset;

		if ( m_version == 0x00 && m_payload_len <
				( ( sectionOffset + baseSectionOffsetNoBanks )
					* sizeof(uint32_t) ) ) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " DetectorBankSets V0 packet: too short for Set "
				<< ( i + 1 ) << " of " << numSets;
			ss << " sectionOffset=" << sectionOffset;
			ss << " baseSectionOffsetNoBanks=" << baseSectionOffsetNoBanks;
			ss << " payload_len=" << m_payload_len;
			delete[] m_sectionOffsets;
			m_sectionOffsets = (uint32_t *) NULL;
			delete[] m_after_banks_offset;
			m_after_banks_offset = (uint32_t *) NULL;
			throw invalid_packet(ss.str());
		}
		else if ( m_version > ADARA::PacketType::DETECTOR_BANK_SETS_VERSION
				&& m_payload_len <
					( ( sectionOffset + baseSectionOffsetNoBanks )
						* sizeof(uint32_t) ) ) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " Newer DetectorBankSets packet: too short for Set "
				<< ( i + 1 ) << " of " << numSets;
			ss << " sectionOffset=" << sectionOffset;
			ss << " baseSectionOffsetNoBanks=" << baseSectionOffsetNoBanks;
			ss << " payload_len=" << m_payload_len;
			delete[] m_sectionOffsets;
			m_sectionOffsets = (uint32_t *) NULL;
			delete[] m_after_banks_offset;
			m_after_banks_offset = (uint32_t *) NULL;
			throw invalid_packet(ss.str());
		}

		// Offset thru end of Bank Ids list...
		sectionOffset += baseSectionOffsetPart1
			+ bankCount(i);   // just in time m_sectionOffset delivery...!

		// Save as "After Banks" Offset...
		m_after_banks_offset[i] = sectionOffset;

		// Rest of Set Offset...
		sectionOffset += baseSectionOffsetPart2;
	}

	// Final Payload Size Check... ;-D
	if ( m_version == 0x00
			&& m_payload_len < ( sectionOffset * sizeof(uint32_t) ) ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " DetectorBankSets V0 packet: overall too short "
			<< " numSets=" << numSets;
		ss << " baseSectionOffsetNoBanks=" << baseSectionOffsetNoBanks;
		ss << " final sectionOffset=" << sectionOffset;
		ss << " payload_len=" << m_payload_len;
		delete[] m_sectionOffsets;
		m_sectionOffsets = (uint32_t *) NULL;
		delete[] m_after_banks_offset;
		m_after_banks_offset = (uint32_t *) NULL;
		throw invalid_packet(ss.str());
	}
	else if ( m_version > ADARA::PacketType::DETECTOR_BANK_SETS_VERSION
			&& m_payload_len < ( sectionOffset * sizeof(uint32_t) ) ) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer DetectorBankSets packet: overall too short "
			<< " numSets=" << numSets;
		ss << " baseSectionOffsetNoBanks=" << baseSectionOffsetNoBanks;
		ss << " final sectionOffset=" << sectionOffset;
		ss << " payload_len=" << m_payload_len;
		delete[] m_sectionOffsets;
		m_sectionOffsets = (uint32_t *) NULL;
		delete[] m_after_banks_offset;
		m_after_banks_offset = (uint32_t *) NULL;
		throw invalid_packet(ss.str());
	}
}

DetectorBankSetsPkt::DetectorBankSetsPkt(
		const DetectorBankSetsPkt &pkt ) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_sectionOffsets(NULL), m_after_banks_offset(NULL)
{
	uint32_t numSets = detBankSetCount();

	// Don't Allocate Anything if there are No Detector Bank Sets...
	if ( numSets < 1 )
		return;

	m_sectionOffsets = new uint32_t[numSets];
	memcpy(const_cast<uint32_t *> (m_sectionOffsets),
		pkt.m_sectionOffsets, numSets * sizeof(uint32_t));

	m_after_banks_offset = new uint32_t[numSets];
	memcpy(const_cast<uint32_t *> (m_after_banks_offset),
		pkt.m_after_banks_offset, numSets * sizeof(uint32_t));
}

DetectorBankSetsPkt::~DetectorBankSetsPkt()
{
	delete[] m_sectionOffsets;
	delete[] m_after_banks_offset;
}

/* -------------------------------------------------------------------- */

DataDonePkt::DataDonePkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_version == 0x00 && m_payload_len != 0) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " DataDone V0 packet is incorrect size: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::DATA_DONE_VERSION
			&& m_payload_len == 0) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer DataDone packet is too short: "
			<< m_payload_len;
		// ...any newer version would have to grow, lol...? ;-D
		throw invalid_packet(ss.str());
	}
}

DataDonePkt::DataDonePkt(const DataDonePkt &pkt) :
	Packet(pkt)
{}

/* -------------------------------------------------------------------- */

DeviceDescriptorPkt::DeviceDescriptorPkt(
	const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	const uint32_t *fields = (const uint32_t *) payload();
	uint32_t size;

	if (m_version == 0x00 && m_payload_len < (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " DeviceDescriptor V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::DEVICE_DESC_VERSION
			&& m_payload_len < (2 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer DeviceDescriptor packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	size = fields[1];
	const char *desc = (const char *) &fields[2];
	if (m_version == 0x00
			&& m_payload_len < (size + (2 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " DeviceDescriptor V0 packet"
			<< " has Undersize Descriptor string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - (2 * sizeof(uint32_t)));
		ss << " [";
		for ( uint32_t i=0 ;
				i < m_payload_len - (2 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(desc + i)) )
				ss << "#000";
			else
				ss << *(desc + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::DEVICE_DESC_VERSION
			&& m_payload_len < (size + (2 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer DeviceDescriptor packet"
			<< " has Undersize Descriptor string";
		ss << " size=" << size << " vs. available payload="
			<< (m_payload_len - (2 * sizeof(uint32_t)));
		ss << " [";
		for ( uint32_t i=0 ;
				i < m_payload_len - (2 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(desc + i)) )
				ss << "#000";
			else
				ss << *(desc + i);
		}
		ss << "]";
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_devId = fields[0];
	m_desc.assign((const char *) &fields[2], size);
}

DeviceDescriptorPkt::DeviceDescriptorPkt(const DeviceDescriptorPkt &pkt) :
	Packet(pkt), m_devId(pkt.m_devId), m_desc(pkt.m_desc)
{}

/* -------------------------------------------------------------------- */

VariableU32Pkt::VariableU32Pkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00 && m_payload_len != (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32) V0 packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_U32_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (U32) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}
}

VariableU32Pkt::VariableU32Pkt(const VariableU32Pkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

VariableDoublePkt::VariableDoublePkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_version == 0x00
			&& m_payload_len
				!= (sizeof(double) + (3 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double) V0 packet is incorrect length: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_DOUBLE_VERSION
			&& m_payload_len < (sizeof(double) + (3 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (Double) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}
}

VariableDoublePkt::VariableDoublePkt(const VariableDoublePkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* -------------------------------------------------------------------- */

VariableStringPkt::VariableStringPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t size;

	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (String) V0 packet is too short "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_STRING_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (String) packet is too short "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	size = m_fields[3];
	const char *str = (const char *) &m_fields[4];
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (String) V0 packet"
			<< " has Undersize Value string: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		ss << " [";
		for ( uint32_t i=0 ;
				i < m_payload_len - (4 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(str + i)) )
				ss << "#000";
			else
				ss << *(str + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_STRING_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (String) packet"
			<< " has Undersize Value string: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		ss << " [";
		for ( uint32_t i=0 ;
				i < m_payload_len - (4 * sizeof(uint32_t)) ; i++ ) {
			if ( ! ((uint8_t) *(str + i)) )
				ss << "#000";
			else
				ss << *(str + i);
		}
		ss << "]";
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (String) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (String) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_val.assign((const char *) &m_fields[4], size);
}

VariableStringPkt::VariableStringPkt(const VariableStringPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()), m_val(pkt.m_val)
{}

/* -------------------------------------------------------------------- */

VariableU32ArrayPkt::VariableU32ArrayPkt(
		const uint8_t *data, uint32_t len ) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t count;
	size_t size;

	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32 Array) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_U32_ARRAY_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (U32 Array) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	count = elemCount();
	size = count * sizeof(uint32_t);
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32 Array) V0 packet"
			<< " has Undersize U32 array: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_U32_ARRAY_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (U32 Array) packet"
			<< " has Undersize U32 array: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32 Array) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (U32 Array) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the array on access
	 * rather than object construction; the user may not care.
	 */
	m_val = std::vector<uint32_t>( count );
	memcpy(const_cast<uint32_t *> (m_val.data()),
		(const uint32_t *) &m_fields[4], count * sizeof(uint32_t));
}

VariableU32ArrayPkt::VariableU32ArrayPkt(const VariableU32ArrayPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()), m_val(pkt.m_val)
{}

/* -------------------------------------------------------------------- */

VariableDoubleArrayPkt::VariableDoubleArrayPkt(
		const uint8_t *data, uint32_t len ) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t count;
	size_t size;

	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double Array) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (Double Array) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	count = elemCount();
	size = count * sizeof(double);
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double Array) V0 packet has Undersize "
			<< "Double array: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer VariableValue (Double Array) packet has Undersize "
			<< "Double array: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double Array) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " VariableValue (Double Array) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the array on access
	 * rather than object construction; the user may not care.
	 */
	m_val = std::vector<double>( count );
	memcpy(const_cast<double *> (m_val.data()),
		(const double *) &m_fields[4], count * sizeof(double));
}

VariableDoubleArrayPkt::VariableDoubleArrayPkt(
		const VariableDoubleArrayPkt &pkt ) :
	Packet(pkt), m_fields((const uint32_t *)payload()), m_val(pkt.m_val)
{}

/* -------------------------------------------------------------------- */

MultVariableU32Pkt::MultVariableU32Pkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t numVals;
	size_t size;

	// Check Versus Base Header Size...
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::MULT_VAR_VALUE_U32_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (U32) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// Check Versus Base Header Size Plus NumValues TOF + U32 Values...
	numVals = numValues();
	size = numVals * 2 * sizeof(uint32_t); // TOF + Value
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32) V0 packet"
			<< " has Undersize U32 TOF/Value arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_U32_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (U32) packet"
			<< " has Undersize U32 TOF/Value arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	// Copy Over NumValues TOF and U32 Values from Payload...

	const uint32_t *ptr = (const uint32_t *) &m_fields[4];

	/* TODO it would be better to create the array on access
	 * rather than object construction; the user may not care.
	 */

	for ( uint32_t i=0 ; i < numVals ; i++ ) {

		// Next TOF...
		m_tofs.push_back( *ptr++ );

		// Next U32 Value...
		m_vals.push_back( *ptr++ );
	}
}

MultVariableU32Pkt::MultVariableU32Pkt(const MultVariableU32Pkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_vals(pkt.m_vals), m_tofs(pkt.m_tofs)
{}

/* -------------------------------------------------------------------- */

MultVariableDoublePkt::MultVariableDoublePkt(
		const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t numVals;
	size_t size;

	// Check Versus Base Header Size...
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (Double) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// Check Versus Base Header Size Plus NumValues TOF + Double Values...
	numVals = numValues();
	size = numVals * (sizeof(uint32_t) + sizeof(double)); // TOF + Value
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double) V0 packet"
			<< " has Undersize Double TOF/Value arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (Double) packet"
			<< " has Undersize Double TOF/Value arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	/* TODO it would be better to create the array on access
	 * rather than object construction; the user may not care.
	 */
	m_vals = std::vector<double>( numVals );
	m_tofs = std::vector<uint32_t>( numVals );

	// Copy Over NumValues TOF and Double Values from Payload...

	const uint32_t *ptr = (const uint32_t *) &m_fields[4];

	/* TODO it would be better to create the array on access
	 * rather than object construction; the user may not care.
	 */

	for ( uint32_t i=0 ; i < numVals ; i++ ) {

		// Next TOF...
		m_tofs.push_back( *ptr++ );

		// Next Double Value...
		m_vals.push_back( *((double *)ptr++) );
	}
}

MultVariableDoublePkt::MultVariableDoublePkt(
		const MultVariableDoublePkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_vals(pkt.m_vals), m_tofs(pkt.m_tofs)
{}

/* -------------------------------------------------------------------- */

MultVariableStringPkt::MultVariableStringPkt(
		const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t numVals;
	uint32_t size;

	// Check Versus Base Header Size...
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (String) V0 packet is too short "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version > ADARA::PacketType::MULT_VAR_VALUE_STRING_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (String) packet is too short "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// Check Versus Base Header Size Plus NumValues TOF + String Lengths...
	numVals = numValues();
	size = numVals * 2 * sizeof(uint32_t); // TOF + Length...
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (String) V0 packet"
			<< " has Undersize String TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_STRING_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (String) packet"
			<< " has Undersize String TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (String) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (String) packet has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	// Check & Copy Over NumValues Strings from Payload...

	const uint32_t *ptr = (const uint32_t *) &m_fields[4];
	uint32_t payload_remaining = m_payload_len - (4 * sizeof(uint32_t));

	for ( uint32_t i=0 ; i < numVals ; i++ ) {

		// Next TOF...
		m_tofs.push_back( *ptr++ );

		// Next String Size...
		size = *ptr++;

		// Next String...
		const char *str = (const char *) ptr;

		if (m_version == 0x00
				&& payload_remaining < (size + (2 * sizeof(uint32_t)))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " MultVariableValue (String) V0 packet"
				<< " has Undersize String Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			ss << " [";
			for ( uint32_t i=0 ;
					i < payload_remaining - sizeof(uint32_t) ; i++ ) {
				if ( ! ((uint8_t) *(str + i)) )
					ss << "#000";
				else
					ss << *(str + i);
			}
			ss << "]";
			throw invalid_packet(ss.str());
		}
		else if (m_version
					> ADARA::PacketType::MULT_VAR_VALUE_STRING_VERSION
				&& payload_remaining < (size + (2 * sizeof(uint32_t)))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " Newer MultVariableValue (String) packet"
				<< " has Undersize String Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			ss << " [";
			for ( uint32_t i=0 ;
					i < payload_remaining - sizeof(uint32_t) ; i++ ) {
				if ( ! ((uint8_t) *(str + i)) )
					ss << "#000";
				else
					ss << *(str + i);
			}
			ss << "]";
			throw invalid_packet(ss.str());
		}

		/* TODO it would be better to create the string on access
		 * rather than object construction; the user may not care.
		 */
		m_vals[i].assign(str, size);

		// "roundup(N,4)" bytes of null padded chars...
		uint32_t roundup = ( size + sizeof(uint32_t) - 1 )
			/ sizeof(uint32_t);

		ptr += roundup;
		payload_remaining -= ( roundup + 2 ) * sizeof(uint32_t);
	}
}

MultVariableStringPkt::MultVariableStringPkt(
		const MultVariableStringPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_vals(pkt.m_vals), m_tofs(pkt.m_tofs)
{}

/* -------------------------------------------------------------------- */

MultVariableU32ArrayPkt::MultVariableU32ArrayPkt(
		const uint8_t *data, uint32_t len ) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t numVals;
	size_t size;

	// Check Versus Base Header Size...
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32 Array) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (U32 Array) packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// Check Versus Base Header Size
	// Plus NumValues TOF + U32 Array Lengths
	numVals = numValues();
	size = numVals * 2 * sizeof(uint32_t); // TOF + Length...
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32 Array) V0 packet"
			<< " has Undersize U32 Array TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (U32 Array) packet"
			<< " has Undersize U32 Array TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32 Array) packet has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (U32 Array) packet"
			<< " has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	// Check & Copy Over NumValues U32 Arrays from Payload...

	const uint32_t *ptr = (const uint32_t *) &m_fields[4];
	uint32_t payload_remaining = m_payload_len - (4 * sizeof(uint32_t));

	for ( uint32_t i=0 ; i < numVals ; i++ ) {

		// Next TOF...
		m_tofs.push_back( *ptr++ );

		// Next U32 Array Size...
		size = *ptr++;

		// Next U32 Array...
		const uint32_t *arr = (const uint32_t *) ptr;

		if (m_version == 0x00
				&& payload_remaining < (( size + 2 ) * sizeof(uint32_t))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " MultVariableValue (U32 Array) V0 packet"
				<< " has Undersize U32 Array Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			throw invalid_packet(ss.str());
		}
		else if (m_version
					> ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_VERSION
				&& payload_remaining < (( size + 2 ) * sizeof(uint32_t))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " Newer MultVariableValue (U32 Array) packet"
				<< " has Undersize U32 Array Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			throw invalid_packet(ss.str());
		}

		/* TODO it would be better to create the array on access
		 * rather than object construction; the user may not care.
		 */
		m_vals[i] = std::vector<uint32_t>( size );
		memcpy(const_cast<uint32_t *> (m_vals[i].data()),
			arr, size * sizeof(uint32_t));

		ptr += size;
		payload_remaining -= ( size + 2 ) * sizeof(uint32_t);
	}
}

MultVariableU32ArrayPkt::MultVariableU32ArrayPkt(
		const MultVariableU32ArrayPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_vals(pkt.m_vals), m_tofs(pkt.m_tofs)
{}

/* -------------------------------------------------------------------- */

MultVariableDoubleArrayPkt::MultVariableDoubleArrayPkt(
		const uint8_t *data, uint32_t len ) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t numVals;
	size_t size;

	// Check Versus Base Header Size...
	if (m_version == 0x00 && m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double Array) V0 packet is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_VERSION
			&& m_payload_len < (4 * sizeof(uint32_t))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (Double Array) packet"
			<< " is too short: "
			<< m_payload_len;
		throw invalid_packet(ss.str());
	}

	// Check Versus Base Header Size
	// Plus NumValues TOF + Double Array Lengths...
	numVals = numValues();
	size = numVals * 2 * sizeof(uint32_t); // TOF + Length...
	if (m_version == 0x00
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double Array) V0 packet has Undersize "
			<< "Double Array TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}
	else if (m_version
				> ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_VERSION
			&& m_payload_len < (size + (4 * sizeof(uint32_t)))) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " Newer MultVariableValue (Double Array) packet"
			<< " has Undersize Double Array TOF/Length arrays: " << size;
		ss << " vs payload "
			<< ( m_payload_len - (4 * sizeof(uint32_t)) );
		throw invalid_packet(ss.str());
	}

	if (validate_status(status())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double Array) packet"
			<< " has invalid status: "
			<< status();
		throw invalid_packet(ss.str());
	}

	if (validate_severity(severity())) {
		std::stringstream ss;
		ss << ( (uint32_t) (m_pulseId >> 32) )
			<< "." << ( (uint32_t) m_pulseId );
		ss << " MultVariableValue (Double Array) packet"
			<< " has invalid severity: "
			<< severity();
		throw invalid_packet(ss.str());
	}

	// Check & Copy Over NumValues Double Arrays from Payload...

	const uint32_t *ptr = (const uint32_t *) &m_fields[4];
	uint32_t payload_remaining = m_payload_len - (4 * sizeof(uint32_t));

	for ( uint32_t i=0 ; i < numVals ; i++ ) {

		// Next TOF...
		m_tofs.push_back( *ptr++ );

		// Next Double Array Size...
		size = *ptr++;

		// Next Double Array...
		const double *arr = (const double *) ptr;

		if (m_version == 0x00
				&& payload_remaining
					< (( size * sizeof(double) )
						+ ( 2 * sizeof(uint32_t) ))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " MultVariableValue (Double Array) V0 packet"
				<< " has Undersize Double Array Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			throw invalid_packet(ss.str());
		}
		else if (m_version >
					ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_VERSION
				&& payload_remaining
					< (( size * sizeof(double) )
						+ ( 2 * sizeof(uint32_t) ))) {
			std::stringstream ss;
			ss << ( (uint32_t) (m_pulseId >> 32) )
				<< "." << ( (uint32_t) m_pulseId );
			ss << " Newer MultVariableValue (Double Array) packet"
				<< " has Undersize Double Array Values array: " << size;
			ss << " vs remaining payload " << payload_remaining;
			throw invalid_packet(ss.str());
		}

		/* TODO it would be better to create the array on access
		 * rather than object construction; the user may not care.
		 */
		m_vals[i] = std::vector<double>( size );
		memcpy(const_cast<double *> (m_vals[i].data()),
			arr, size * sizeof(double));

		// Size of Double Array in Uint32's... ;-D
		uint32_t dblsize = size * sizeof(double) / sizeof(uint32_t);

		ptr += dblsize;
		payload_remaining -= ( dblsize + 2 ) * sizeof(uint32_t);
	}
}

MultVariableDoubleArrayPkt::MultVariableDoubleArrayPkt(
		const MultVariableDoubleArrayPkt &pkt ) :
	Packet(pkt), m_fields((const uint32_t *)payload()),
	m_vals(pkt.m_vals), m_tofs(pkt.m_tofs)
{}

