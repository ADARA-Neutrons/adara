#include <boost/lexical_cast.hpp>

#include <string.h>

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

/* ------------------------------------------------------------------------ */

RawDataPkt::RawDataPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len < (6 * sizeof(uint32_t)))
		throw invalid_packet("RawDataPacket is too short");
}

RawDataPkt::RawDataPkt(const RawDataPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

RTDLPkt::RTDLPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len != 120)
		throw invalid_packet("RTDL Packet is incorrect length");

	if ((m_fields[4] >> 24) != 4)
		throw invalid_packet("Missing ring period");
}

RTDLPkt::RTDLPkt(const RTDLPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

SourceListPkt::SourceListPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{}

SourceListPkt::SourceListPkt(const SourceListPkt &pkt) :
	Packet(pkt)
{}

/* ------------------------------------------------------------------------ */

BankedEventPkt::BankedEventPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len < (4 * sizeof(uint32_t)))
		throw invalid_packet("BankedEvent packet is too short");
}

BankedEventPkt::BankedEventPkt(const BankedEventPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

BeamMonitorPkt::BeamMonitorPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len < (4 * sizeof(uint32_t)))
		throw invalid_packet("BeamMonitor packet is too short");
}

BeamMonitorPkt::BeamMonitorPkt(const BeamMonitorPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

PixelMappingPkt::PixelMappingPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{}

PixelMappingPkt::PixelMappingPkt(const PixelMappingPkt &pkt) :
	Packet(pkt)
{}

/* ------------------------------------------------------------------------ */

RunStatusPkt::RunStatusPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len != (3 * sizeof(uint32_t)))
		throw invalid_packet("RunStatus packet is incorrect size");
}

RunStatusPkt::RunStatusPkt(const RunStatusPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

RunInfoPkt::RunInfoPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *) payload();
	const char *xml = (const char *) payload() + sizeof(uint32_t);

	if (m_payload_len < sizeof(uint32_t))
		throw invalid_packet("RunInfo packet is too short");
	if (m_payload_len < (size + sizeof(uint32_t)))
		throw invalid_packet("RunInfo packet has oversize string");

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_xml.assign(xml, size);
}

RunInfoPkt::RunInfoPkt(const RunInfoPkt &pkt) :
	Packet(pkt), m_xml(pkt.m_xml)
{}

/* ------------------------------------------------------------------------ */

TransCompletePkt::TransCompletePkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *) payload();
	const char *reason = (const char *) payload() + sizeof(uint32_t);

	m_status = (uint16_t) (size >> 16);
	size &= 0xffff;
	if (m_payload_len < sizeof(uint32_t))
		throw invalid_packet("TransComplete packet is too short");
	if (m_payload_len < (size + sizeof(uint32_t)))
		throw invalid_packet("TransComplete packet has oversize "
				     "string");

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_reason.assign(reason, size);
}

TransCompletePkt::TransCompletePkt(const TransCompletePkt &pkt) :
	Packet(pkt), m_reason(pkt.m_reason)
{}

/* ------------------------------------------------------------------------ */

ClientHelloPkt::ClientHelloPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_payload_len != sizeof(uint32_t))
		throw invalid_packet("ClientHello packet is incorrect size");

	m_reqStart = *(const uint32_t *) payload();
}

ClientHelloPkt::ClientHelloPkt(const ClientHelloPkt &pkt) :
	Packet(pkt), m_reqStart(pkt.m_reqStart)
{}

/* ------------------------------------------------------------------------ */

AnnotationPkt::AnnotationPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len < (2 * sizeof(uint32_t)))
		throw invalid_packet("AnnotationPkt packet is incorrect size");

	uint16_t size = m_fields[0] & 0xffff;
	if (m_payload_len < (size + (2 * sizeof(uint32_t))))
		throw invalid_packet("AnnotationPkt packet has oversize "
				     "string");
}

AnnotationPkt::AnnotationPkt(const AnnotationPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

SyncPkt::SyncPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *)(payload() + 24);

	if (m_payload_len < 28)
		throw invalid_packet("Sync packet is too small");
	if (m_payload_len < (size + 28))
		throw invalid_packet("Sync packet has oversize string");
}

SyncPkt::SyncPkt(const SyncPkt &pkt) :
	Packet(pkt)
{}

/* ------------------------------------------------------------------------ */

HeartbeatPkt::HeartbeatPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	if (m_payload_len)
		throw invalid_packet("Heartbeat packet is incorrect size");
}

HeartbeatPkt::HeartbeatPkt(const HeartbeatPkt &pkt) :
	Packet(pkt)
{}

/* ------------------------------------------------------------------------ */

GeometryPkt::GeometryPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	uint32_t size = *(const uint32_t *) payload();
	const char *xml = (const char *) payload() + sizeof(uint32_t);

	if (m_payload_len < sizeof(uint32_t))
		throw invalid_packet("Geometry packet is too short");
	if (m_payload_len < (size + sizeof(uint32_t)))
		throw invalid_packet("Geometry packet has oversize string");

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_xml.assign(xml, size);
}

GeometryPkt::GeometryPkt(const GeometryPkt &pkt) :
	Packet(pkt), m_xml(pkt.m_xml)
{}

/* ------------------------------------------------------------------------ */

BeamlineInfoPkt::BeamlineInfoPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	const char *info = (const char *) payload() + sizeof(uint32_t);
	uint32_t sizes = *(const uint32_t *) payload();
	uint32_t id_len, shortName_len, longName_len, info_len;

	if (m_payload_len < sizeof(uint32_t))
		throw invalid_packet("Beamline info packet is too short");

	longName_len = sizes & 0xff;
	shortName_len = (sizes >> 8) & 0xff;
	id_len = (sizes >> 16) & 0xff;

	info_len = id_len + shortName_len + longName_len;

	if (m_payload_len < (info_len + sizeof(uint32_t)))
		throw invalid_packet("Beamline info packet has undersize data");

	m_id.assign(info, id_len);
	info += id_len;
	m_shortName.assign(info, shortName_len);
	info += shortName_len;
	m_longName.assign(info, longName_len);
}

BeamlineInfoPkt::BeamlineInfoPkt(const BeamlineInfoPkt &pkt) :
	Packet(pkt), m_id(pkt.m_id), m_shortName(pkt.m_shortName),
	m_longName(pkt.m_longName)
{}

/* ------------------------------------------------------------------------ */

DeviceDescriptorPkt::DeviceDescriptorPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len)
{
	const uint32_t *fields = (const uint32_t *) payload();
	uint32_t size;

	if (m_payload_len < (2 * sizeof(uint32_t)))
		throw invalid_packet("DeviceDescriptor packet is too short");
	size = fields[1];
	if (m_payload_len < (size + (2 * sizeof(uint32_t))))
		throw invalid_packet("DeviceDescriptor packet has oversize "
				     "string");

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_devId = fields[0];
	m_desc.assign((const char *) &fields[2], size);
}

DeviceDescriptorPkt::DeviceDescriptorPkt(const DeviceDescriptorPkt &pkt) :
	Packet(pkt), m_devId(pkt.m_devId), m_desc(pkt.m_desc)
{}

/* ------------------------------------------------------------------------ */

VariableU32Pkt::VariableU32Pkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len != (4 * sizeof(uint32_t))) {
		std::string msg("VariableValue (U32) packet is incorrect "
				"length: ");
		msg += boost::lexical_cast<std::string>(m_payload_len);
		throw invalid_packet(msg);
	}
	if (validate_status(status())) {
		std::string msg("VariableValue (U32) packet has invalid "
				"status: ");
		msg += boost::lexical_cast<std::string>(status());
		throw invalid_packet(msg);
	}
	if (validate_severity(severity())) {
		std::string msg("VariableValue (U32) packet has invalid "
				"severity: ");
		msg += boost::lexical_cast<std::string>(severity());
		throw invalid_packet(msg);
	}
}

VariableU32Pkt::VariableU32Pkt(const VariableU32Pkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

VariableDoublePkt::VariableDoublePkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	if (m_payload_len != (sizeof(double) + (3 * sizeof(uint32_t)))) {
		std::string msg("VariableValue (double) packet is incorrect "
				"length: ");
		msg += boost::lexical_cast<std::string>(m_payload_len);
		throw invalid_packet(msg);
	}
	if (validate_status(status())) {
		std::string msg("VariableValue (double) packet has invalid "
				"status: ");
		msg += boost::lexical_cast<std::string>(status());
		throw invalid_packet(msg);
	}
	if (validate_severity(severity())) {
		std::string msg("VariableValue (double) packet has invalid "
				"severity: ");
		msg += boost::lexical_cast<std::string>(severity());
		throw invalid_packet(msg);
	}
}

VariableDoublePkt::VariableDoublePkt(const VariableDoublePkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload())
{}

/* ------------------------------------------------------------------------ */

VariableStringPkt::VariableStringPkt(const uint8_t *data, uint32_t len) :
	Packet(data, len), m_fields((const uint32_t *)payload())
{
	uint32_t size;

	if (m_payload_len < (4 * sizeof(uint32_t))) {
		std::string msg("VariableValue (string) packet is too short ");
		msg += boost::lexical_cast<std::string>(m_payload_len);
		throw invalid_packet(msg);
	}
	size = m_fields[3];
	if (m_payload_len < (size + (2 * sizeof(uint32_t)))) {
		std::string msg("VariableValue (string) packet has oversize "
				"string: ");
		msg += boost::lexical_cast<std::string>(size);
		msg += " vs payload ";
		msg += boost::lexical_cast<std::string>(m_payload_len);
		throw invalid_packet(msg);
	}

	if (validate_status(status())) {
		std::string msg("VariableValue (string) packet has invalid "
				"status: ");
		msg += boost::lexical_cast<std::string>(status());
		throw invalid_packet(msg);
	}
	if (validate_severity(severity())) {
		std::string msg("VariableValue (string) packet has invalid "
				"severity: ");
		msg += boost::lexical_cast<std::string>(severity());
		throw invalid_packet(msg);
	}

	/* TODO it would be better to create the string on access
	 * rather than object construction; the user may not care.
	 */
	m_val.assign((const char *) &m_fields[4], size);
}

VariableStringPkt::VariableStringPkt(const VariableStringPkt &pkt) :
	Packet(pkt), m_fields((const uint32_t *)payload()), m_val(pkt.m_val)
{}
