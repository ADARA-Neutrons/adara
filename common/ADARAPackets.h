#ifndef __ADARA_PACKETS_H
#define __ADARA_PACKETS_H

#include <stdint.h>

#include "ADARA.h"

namespace ADARA {

class PacketHeader {
public:
	PacketHeader(const uint8_t *data) {
		const uint32_t *field = (const uint32_t *) data;

		m_payload_len = field[0];
		m_type = (PacketType::Enum) field[1];

		/* Convert EPICS epoch to Unix epoch,
		 * Jan 1, 1990 ==> Jan 1, 1970
		 */
		m_timestamp.tv_sec = field[2] + EPICS_EPOCH_OFFSET;
		m_timestamp.tv_nsec = field[3];

		m_pulseId = ((uint64_t) field[2]) << 32;
		m_pulseId |= field[3];
	}

	PacketType::Enum type(void) const { return m_type; }
	uint32_t payload_length(void) const { return m_payload_len; }
	const struct timespec &timestamp(void) const { return m_timestamp; }
	uint64_t pulseId(void) const { return m_pulseId; }
	uint32_t packet_length(void) const { return m_payload_len + 16; }

	static uint32_t header_length(void) { return 16; }

protected:
	uint32_t m_payload_len;
	PacketType::Enum m_type;
	struct timespec m_timestamp;
	uint64_t m_pulseId;

	/* Don't allow the default constructor */
	PacketHeader();
};

class Packet : public PacketHeader {
public:
	Packet(const uint8_t *data, uint32_t len);
	Packet(const Packet &pkt);

	virtual ~Packet();

	const uint8_t *packet(void) const { return m_data; }
	const uint8_t *payload(void) const {
		return m_data + header_length();
	}

protected:
	const uint8_t *	m_data;
	uint32_t	m_len;
	bool		m_allocated;

private:
	/* Don't allow the default constructor or assignment operator */
	Packet();
	Packet &operator=(const Packet &pkt);
};

class RawDataPkt : public Packet {
public:
	RawDataPkt(const RawDataPkt &pkt);

	uint32_t sourceID(void) const { return m_fields[0]; }
	bool endOfPulse(void) const { return !!(m_fields[1] & 0x80000000); }
	uint16_t pktSeq(void) const { return (m_fields[1] >> 16) & 0x7fff; }
	uint16_t dspSeq(void) const { return m_fields[1] & 0x7fff; }
	PulseFlavor::Enum flavor(void) const {
		return static_cast<PulseFlavor::Enum>
						((m_fields[2] >> 24) & 0x7);
	}
	uint32_t pulseCharge(void) const { return m_fields[2] & 0x00ffffff; }
	bool badVeto(void) const { return !!(m_fields[3] & 0x8000000); }
	bool badCycle(void) const { return !!(m_fields[3] & 0x40000000); }
	uint8_t timingStatus(void) const {
		return (uint8_t) (m_fields[3] >> 22);
	}
	uint16_t veto(void) const { return (m_fields[3] >> 10) & 0xfff; }
	uint16_t cycle(void) const { return m_fields[3] &0x3ff; }
	uint32_t intraPulseTime(void) const { return m_fields[4]; }
	bool tofCorrected(void) const { return !!(m_fields[5] & 0x80000000); }
	uint32_t tofOffset(void) const { return m_fields[5] & 0x7fffffff; }
	uint32_t tofField(void) const { return m_fields[5]; }

	const Event *events(void) const { return (const Event *) &m_fields[6]; }
	uint32_t num_events(void) const {
		return (m_payload_len - 24) / (uint32_t) (2 * sizeof(uint32_t));
	}

private:
	const uint32_t *m_fields;

	RawDataPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RTDLPkt : public Packet {
public:
	RTDLPkt(const RTDLPkt &pkt);

	PulseFlavor::Enum flavor(void) const {
		return static_cast<PulseFlavor::Enum>
						((m_fields[0] >> 24) & 0x7);
	}
	uint32_t pulseCharge(void) const { return m_fields[0] & 0x00ffffff; }
	bool badVeto(void) const { return !!(m_fields[1] & 0x8000000); }
	bool badCycle(void) const { return !!(m_fields[1] & 0x40000000); }
	uint8_t timingStatus(void) const {
		return (uint8_t) (m_fields[1] >> 22);
	}
	uint16_t veto(void) const { return (m_fields[1] >> 10) & 0xfff; }
	uint16_t cycle(void) const { return m_fields[1] &0x3ff; }
	uint32_t intraPulseTime(void) const { return m_fields[2]; }
	bool tofCorrected(void) const { return !!(m_fields[3] & 0x80000000); }
	uint32_t tofOffset(void) const { return m_fields[3] & 0x7fffffff; }
	uint32_t ringPeriod(void) const { return m_fields[4] & 0xffffff; }

	// TODO implement accessor for optional fields

private:
	const uint32_t *m_fields;

	RTDLPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class SourceListPkt : public Packet {
public:
	SourceListPkt(const SourceListPkt &pkt);

	const uint32_t *ids(void) const { return (const uint32_t *) payload(); }
	uint32_t num_ids(void) const {
		return (uint32_t) payload_length() / (uint32_t) sizeof(uint32_t);
	}

private:
	SourceListPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BankedEventPkt : public Packet {
public:
	BankedEventPkt(const BankedEventPkt &pkt);

	enum Flags {
		ERROR_PIXELS    = 0x0001,
		PARTIAL_DATA    = 0x0002,
		PULSE_VETO      = 0x0004,
		MISSING_RTDL    = 0x0008,
		MAPPING_ERROR   = 0x0010,
		DUPLICATE_PULSE = 0x0020,
	};

	uint32_t pulseCharge(void) const { return m_fields[0]; }
	uint32_t pulseEnergy(void) const { return m_fields[1]; }
	uint32_t cycle(void) const { return m_fields[2]; }
	uint32_t flags(void) const { return m_fields[3]; }

	// TODO implment bank/event accessors

private:
	const uint32_t *m_fields;

	BankedEventPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BeamMonitorPkt : public Packet {
public:
	BeamMonitorPkt(const BeamMonitorPkt &pkt);

	uint32_t pulseCharge(void) const { return m_fields[0]; }
	uint32_t pulseEnergy(void) const { return m_fields[1]; }
	uint32_t cycle(void) const { return m_fields[2]; }
	uint32_t flags(void) const { return m_fields[3]; }

	// TODO implment monitor/event accessors

private:
	const uint32_t *m_fields;

	BeamMonitorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class PixelMappingPkt : public Packet {
public:
	PixelMappingPkt(const PixelMappingPkt &pkt);
	// TODO implement accessors for fields

private:
	PixelMappingPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunStatusPkt : public Packet {
public:
	RunStatusPkt(const RunStatusPkt &pkt);

	uint32_t runNumber(void) const { return m_fields[0]; }
	uint32_t runStart(void) const { return m_fields[1]; }
	uint32_t fileNumber(void) const { return m_fields[2] & 0xffffff; }
	RunStatus::Enum status(void) const {
		return static_cast<RunStatus::Enum>(m_fields[2] >> 24);
	}

private:
	const uint32_t *m_fields;

	RunStatusPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunInfoPkt : public Packet {
public:
	RunInfoPkt(const RunInfoPkt &pkt);

	const std::string &info(void) const { return m_xml; }

private:
	std::string m_xml;

	RunInfoPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class TransCompletePkt : public Packet {
public:
	TransCompletePkt(const TransCompletePkt &pkt);

	uint16_t status(void) const { return m_status; }
	const std::string &reason(void) const { return m_reason; }

private:
	uint16_t m_status;
	std::string m_reason;

	TransCompletePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class ClientHelloPkt : public Packet {
public:
	ClientHelloPkt(const ClientHelloPkt &pkt);

	uint32_t requestedStartTime(void) const { return m_reqStart; }

private:
	uint32_t m_reqStart;

	ClientHelloPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class AnnotationPkt : public Packet {
public:
	AnnotationPkt(const AnnotationPkt &pkt);

	bool resetHint(void) const { return !!(m_fields[0] & 0x80000000); }
	MarkerType::Enum type(void) const {
		uint16_t type = (m_fields[0] >> 16) & 0x7fff;
		return static_cast<MarkerType::Enum>(type);
	}
	uint32_t scanIndex(void) const { return m_fields[1]; }
	const std::string &comment(void) const {
		if (!m_comment.length() && (m_fields[0] & 0xffff)) {
			m_comment.assign((const char *) &m_fields[2],
					 m_fields[0] & 0xffff);
		}

		return m_comment;
	}

private:
	const uint32_t *m_fields;
	mutable std::string m_comment;

	AnnotationPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class SyncPkt : public Packet {
public:
	SyncPkt(const SyncPkt &pkt);
	// TODO implement accessors for fields

private:
	SyncPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class HeartbeatPkt : public Packet {
public:
	HeartbeatPkt(const HeartbeatPkt &pkt);

private:
	HeartbeatPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class GeometryPkt : public Packet {
public:
	GeometryPkt(const GeometryPkt &pkt);

	const std::string &info(void) const { return m_xml; }

private:
	std::string m_xml;

	GeometryPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BeamlineInfoPkt : public Packet {
public:
	BeamlineInfoPkt(const BeamlineInfoPkt &pkt);

	const std::string &id(void) const { return m_id; }
	const std::string &shortName(void) const { return m_shortName; }
	const std::string &longName(void) const { return m_longName; }

private:
	std::string m_id;
	std::string m_shortName;
	std::string m_longName;

	BeamlineInfoPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class DeviceDescriptorPkt : public Packet {
public:
	DeviceDescriptorPkt(const DeviceDescriptorPkt &pkt);

	uint32_t devId(void) const { return m_devId; }
	const std::string &description(void) const { return m_desc; }

	void remapDevice(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
	        fields[0] = dev;
		m_devId = dev;
	};

private:
	uint32_t m_devId;
	std::string m_desc;

	DeviceDescriptorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableU32Pkt : public Packet {
public:
	VariableU32Pkt(const VariableU32Pkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t value(void) const { return m_fields[3]; }

	void remapDevice(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

private:
	const uint32_t *m_fields;

	VariableU32Pkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableDoublePkt : public Packet {
public:
	VariableDoublePkt(const VariableDoublePkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	double value(void) const { return *(const double *) &m_fields[3]; }

	void remapDevice(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

private:
	const uint32_t *m_fields;

	VariableDoublePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableStringPkt : public Packet {
public:
	VariableStringPkt(const VariableStringPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
						(m_fields[2] & 0xffff);
	}
	const std::string &value(void) const { return m_val; }

	void remapDevice(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

private:
	const uint32_t *m_fields;
	std::string m_val;

	VariableStringPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

} /* namespacce ADARA */

#endif /* __ADARA_PACKETS_H */
