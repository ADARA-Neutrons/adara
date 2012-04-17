#include "ADARAParser.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>

using namespace ADARA;

Exception::Exception(int err) : m_error(err)
{
	char *msg, buf[256];

	/* We use the GNU version of strerror_r(). If _GNU_SOURCE isn't
	 * defined, this returns an int and the compiler will complain.
	 */
	msg = strerror_r(err, buf, sizeof(buf));
	m_msg = msg;
}

/* ------------------------------------------------------------------------ */

Parser::Parser(unsigned int buffer_size, unsigned int max_pkt_size) :
		m_size(buffer_size), m_max_size(max_pkt_size), m_len(0),
		m_oversize_len(0)
{
	m_buffer = new uint8_t[buffer_size];
}

Parser::~Parser()
{
	delete [] m_buffer;
}

void Parser::reset(void)
{
	m_len = 0;
	m_oversize_len = 0;
}

bool Parser::read(int fd, unsigned int max_read)
{
	unsigned int bytes_read = 0;
	ssize_t rc;

	while (!max_read || bytes_read < max_read) {
		rc = ::read(fd, m_buffer + m_len, m_size - m_len);
		if (rc < 0) {
			if (errno == EINTR || errno == EAGAIN ||
						errno == EWOULDBLOCK)
				return true;

			throw Exception(errno);
		}

		if (rc == 0)
			return false;

		bytes_read += rc;
		m_len += rc;

		if (parseBuffer())
			return false;
	}

	return true;
}

bool Parser::parseBuffer(void)
{
	uint8_t *p = m_buffer;
	bool stopped = false;

	/* If we're processing an oversize packet, then we will find its
	 * data at the front of the buffer. We'll either consume our
	 * entire buffer, or find the end of the oversize packet and
	 * process the rest of the buffer as normal.
	 */
	if (m_oversize_len) {
		unsigned int chunk_len;

		chunk_len = m_len < m_oversize_len ? m_len : m_oversize_len;
		stopped = rxOversizePkt(NULL, p, m_oversize_offset, chunk_len);
		m_oversize_offset += chunk_len;
		m_oversize_len -= chunk_len;
		m_len -= chunk_len;
		p += chunk_len;
	}

	while (!stopped && m_len > PacketHeader::header_length()) {
		PacketHeader hdr(p);

		if (hdr.payload_length() % 4) {
			throw Exception(EINVAL,
					"Payload length not multiple of 4");
		}

		if (m_max_size < hdr.packet_length()) {
			/* This packet is over the maximum limit; we'll
			 * call the oversize handler with this first
			 * chunk, consuming our entire buffer.
			 */
			stopped = rxOversizePkt(&hdr, p, 0, m_len);
			m_oversize_len = hdr.payload_length() - m_len;
			m_oversize_offset = m_len;
			m_len = 0;

			return stopped;
		}

		if (m_size < hdr.packet_length()) {
			/* This packet is too big to possibly fit in our
			 * current buffer, so we need to grow. Once we've
			 * resized, return to our caller as we obviously
			 * don't have the full packet yet.
			 */
			unsigned int new_size = m_size * 2;
			uint8_t *new_buffer;

			do {
				new_size = m_size * 2;
			} while (new_size < hdr.packet_length());

			if (new_size > m_max_size)
				new_size = m_max_size;

			new_buffer = new uint8_t[new_size];
			memcpy(new_buffer, p, m_len);

			delete m_buffer;
			m_buffer = new_buffer;
			m_size = new_size;

			return false;
		}

		if (m_len < hdr.packet_length())
			break;

		Packet pkt(p, hdr.packet_length());

		p += hdr.packet_length();
		m_len -= hdr.packet_length();

		if (rxPacket(pkt)) {
			stopped = true;
			break;
		}
	}

	/* If we have anything left over, shove it to the front.
	 */
	if (m_len && p != m_buffer)
		memmove(m_buffer, p, m_len);

	return stopped;
}

bool Parser::rxPacket(const Packet &pkt)
{
#define MAP_TYPE(pkt_type, obj_type)					\
	case pkt_type: {						\
		obj_type raw(pkt.packet(), pkt.packet_length());	\
		return rxPacket(raw);					\
	}

	switch (pkt.type()) {
		MAP_TYPE(ADARA_PKT_RAW_EVENT_V0, RawDataPkt);
		MAP_TYPE(ADARA_PKT_RTDL_V0, RTDLPkt);
		MAP_TYPE(ADARA_PKT_BANKED_EVENT_V0, BankedEventPkt);
		MAP_TYPE(ADARA_PKT_BEAM_MONITOR_EVENT_V0, BeamMonitorPkt);
		MAP_TYPE(ADARA_PKT_PIXEL_MAPPING_V0, PixelMappingPkt);
		MAP_TYPE(ADARA_PKT_RUN_STATUS_V0, RunStatusPkt);
		MAP_TYPE(ADARA_PKT_RUN_INFO_V0, RunInfoPkt);
		MAP_TYPE(ADARA_PKT_TRANSLATION_COMPLETE_V0, TransCompletePkt);
		MAP_TYPE(ADARA_PKT_CLIENT_HELLO_V0, ClientHelloPkt);
		MAP_TYPE(ADARA_PKT_STATS_RESET_V0, StatsResetPkt);
		MAP_TYPE(ADARA_PKT_SYNC_V0, SyncPkt);
		MAP_TYPE(ADARA_PKT_HEARTBEAT_V0, HeartbeatPkt);
		MAP_TYPE(ADARA_PKT_DEVICE_DESC_V0, DeviceDescriptorPkt);
		MAP_TYPE(ADARA_PKT_VAR_VALUE_U32_V0, VariableU32Pkt);
		MAP_TYPE(ADARA_PKT_VAR_VALUE_DOUBLE_V0, VariableDoublePkt);
		MAP_TYPE(ADARA_PKT_VAR_VALUE_STRING_V0, VariableStringPkt);

		/* No default handler; we want the compiler to warn about
		 * the unhandled PacketType values when we add new packets.
		 */
	}

	return rxUnknownPkt(pkt);
#undef MAP_TYPE
}

bool Parser::rxUnknownPkt(const Packet &pkt)
{
	/* Default is to discard the data */
	return false;
}

bool Parser::rxOversizePkt(const PacketHeader *hdr, const uint8_t *chunk,
			   unsigned int chunk_offset, unsigned int chunk_len)
{
	/* Default is to discard the data */
	return false;
}

#define EXPAND_HANDLER(type) \
bool Parser::rxPacket(const type &) { return false; }

EXPAND_HANDLER(RawDataPkt)
EXPAND_HANDLER(RTDLPkt)
EXPAND_HANDLER(BankedEventPkt)
EXPAND_HANDLER(BeamMonitorPkt)
EXPAND_HANDLER(PixelMappingPkt)
EXPAND_HANDLER(RunStatusPkt)
EXPAND_HANDLER(RunInfoPkt)
EXPAND_HANDLER(TransCompletePkt)
EXPAND_HANDLER(ClientHelloPkt)
EXPAND_HANDLER(StatsResetPkt)
EXPAND_HANDLER(SyncPkt)
EXPAND_HANDLER(HeartbeatPkt)
EXPAND_HANDLER(DeviceDescriptorPkt)
EXPAND_HANDLER(VariableU32Pkt)
EXPAND_HANDLER(VariableDoublePkt)
EXPAND_HANDLER(VariableStringPkt)
