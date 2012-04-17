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
		m_type = (PacketType) field[1];

		/* Convert EPICS epoch to Unix epoch,
		 * Jan 1, 1990 ==> Jan 1, 1970
		 */
		m_timestamp.tv_sec = field[2] + EPICS_EPOCH_OFFSET;
		m_timestamp.tv_nsec = field[3];
	}

	PacketType type(void) const { return m_type; }
	uint32_t payload_length(void) const { return m_payload_len; }
	const struct timespec &timestamp(void) const { return m_timestamp; }
	uint32_t packet_length(void) const { return m_payload_len + 16; }

	static uint32_t header_length(void) { return 16; }

protected:
	uint32_t	m_payload_len;
	PacketType	m_type;
	struct timespec m_timestamp;

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
	// TODO implement accessors for fields

private:
	RawDataPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RTDLPkt : public Packet {
public:
	RTDLPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	RTDLPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BankedEventPkt : public Packet {
public:
	BankedEventPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	BankedEventPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BeamMonitorPkt : public Packet {
public:
	BeamMonitorPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	BeamMonitorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class PixelMappingPkt : public Packet {
public:
	PixelMappingPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	PixelMappingPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunStatusPkt : public Packet {
public:
	RunStatusPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	RunStatusPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunInfoPkt : public Packet {
public:
	RunInfoPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	RunInfoPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class TransCompletePkt : public Packet {
public:
	TransCompletePkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	TransCompletePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class ClientHelloPkt : public Packet {
public:
	ClientHelloPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	ClientHelloPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class StatsResetPkt : public Packet {
public:
	StatsResetPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	StatsResetPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class SyncPkt : public Packet {
public:
	SyncPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	SyncPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class HeartbeatPkt : public Packet {
public:
	HeartbeatPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	HeartbeatPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class DeviceDescriptorPkt : public Packet {
public:
	DeviceDescriptorPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	DeviceDescriptorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableU32Pkt : public Packet {
public:
	VariableU32Pkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	VariableU32Pkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableDoublePkt : public Packet {
public:
	VariableDoublePkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	VariableDoublePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableStringPkt : public Packet {
public:
	VariableStringPkt(const RawDataPkt &pkt);
	// TODO implement accessors for fields

private:
	VariableStringPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

} /* namespacce ADARA */

#endif /* __ADARA_PACKETS_H */
