#ifndef __ADARA_PARSER_H
#define __ADARA_PARSER_H

#include <string>
#include <stdint.h>
#include <time.h>

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

class Parser {
public:
	Parser(unsigned int buffer_size = 1024 * 1024,
	       unsigned int max_pkt_size = 8 * 1024 * 1024);

	virtual ~Parser();

	/* Returns false if we hit EOF or a callback asked to stop. We return
	 * true if we got we got EAGAIN/EINTR from reading the fd. We throw
	 * exceptions on error, but may hold those until we complete all
	 * packets in the buffer. The max_read parameter, if non-zero,
	 * limits the amount of maximum amount of data read and parsed
	 * from the file descriptor.
	 */
	bool read(int fd, unsigned int max_read = 0);

	/* Flush the internal buffers and get ready to restart parsing.
	 */
	void reset(void);

protected:
	/* This function gets called for every packet that fits in the
	 * internal buffer; oversize packets will be sent to rxOversizePkt().
	 * The default implementation will create an appropriate object
	 * for the packet and call the delivery function with that object.
	 *
	 * This function returns true to interrupt parsing, or false
	 * to continue.
	 *
	 * Derived classes my efficiently ignore packet types by overriding
	 * this handler. They would just return when the packet type is
	 * one they do not care about, and call the base class function
	 * to handle the rest.
	 */
	virtual bool rxPacket(const Packet &pkt);
	virtual bool rxUnknownPkt(const Packet &pkt);
	virtual bool rxOversizePkt(const PacketHeader *hdr,
				   const uint8_t *chunk,
				   unsigned int chunk_offset,
				   unsigned int chunk_len);

	/* These member functions are passed a constant object representing
	 * the packet being processed. They should make copy of the object
	 * if they wish to keep it around, as the passed object will be
	 * destroyed upon return.
	 *
	 * These functions return true to interrupt parsing, or false
	 * to continue.
	 */
	virtual bool rxPacket(const RawDataPkt &pkt);
	virtual bool rxPacket(const RTDLPkt &pkt);
	virtual bool rxPacket(const BankedEventPkt &pkt);
	virtual bool rxPacket(const BeamMonitorPkt &pkt);
	virtual bool rxPacket(const PixelMappingPkt &pkt);
	virtual bool rxPacket(const RunStatusPkt &pkt);
	virtual bool rxPacket(const RunInfoPkt &pkt);
	virtual bool rxPacket(const TransCompletePkt &pkt);
	virtual bool rxPacket(const ClientHelloPkt &pkt);
	virtual bool rxPacket(const StatsResetPkt &pkt);
	virtual bool rxPacket(const SyncPkt &pkt);
	virtual bool rxPacket(const HeartbeatPkt &pkt);
	virtual bool rxPacket(const DeviceDescriptorPkt &pkt);
	virtual bool rxPacket(const VariableU32Pkt &pkt);
	virtual bool rxPacket(const VariableDoublePkt &pkt);
	virtual bool rxPacket(const VariableStringPkt &pkt);

private:
	bool parseBuffer(void);

	uint8_t *	m_buffer;
	unsigned int	m_size;
	unsigned int	m_max_size;
	unsigned int	m_len;

	unsigned int	m_oversize_len;
	unsigned int	m_oversize_offset;
};

} /* namespacce ADARA */

#endif /* __ADARA_PARSER_H */
