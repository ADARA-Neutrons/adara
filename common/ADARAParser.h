#ifndef __ADARA_PARSER_H
#define __ADARA_PARSER_H

#include <string>
#include <stdint.h>
#include <stdexcept>
#include <map>
#include <unistd.h> // for ssize_t

#include "ADARA.h"
#include "ADARAPackets.h"

namespace ADARA {

class Parser {
public:
	Parser(unsigned int inital_buffer_size = 1024 * 1024,
	       unsigned int max_pkt_size = 48 * 1024 * 1024); // For PixelMap!

	virtual ~Parser();

	struct timespec last_start_read_time;
	struct timespec last_last_start_read_time;

	struct timespec last_end_read_time;
	struct timespec last_last_end_read_time;

	ssize_t last_bytes_read;
	ssize_t last_last_bytes_read;
	ssize_t last_read_errno;
	ssize_t last_last_read_errno;
	ssize_t last_pkts_parsed;
	ssize_t last_last_pkts_parsed;
	unsigned long last_total_bytes;
	unsigned long last_last_total_bytes;
	unsigned int last_total_packets;
	unsigned int last_last_total_packets;
	unsigned int last_read_count;
	unsigned int last_last_read_count;
	unsigned int last_loop_count;
	unsigned int last_last_loop_count;
	double last_parse_elapsed_total;
	double last_last_parse_elapsed_total;
	double last_read_elapsed_total;
	double last_last_read_elapsed_total;
	double last_parse_elapsed;
	double last_last_parse_elapsed;
	double last_read_elapsed;
	double last_last_read_elapsed;
	double last_elapsed;
	double last_last_elapsed;

protected:
	/* The ADARA::Parser class maintains an internal buffer that
	 * subclasses and direct users must fill with stream data for
	 * parsing.
	 *
	 * bufferFillAddress() returns the address at which to begin
	 * placing additional data. bufferFillLength() returns the
	 * maximum amount of data that can be appended at that address.
	 * The address is guaranteed to be non-NULL if the length is
	 * non-zero, but will be NULL if length is zero.
	 * Users must not cache the return values from these functions
	 * over calls to bufferBytesAppended() or parse().
	 *
	 * Once data has been placed in the specified buffer, the user
	 * must call bufferBytesAppended() to inform the class how much
	 * new data has been placed in the buffer.
	 */
	uint8_t *bufferFillAddress(void) const {
		if (bufferFillLength())
			return m_buffer + m_len;
		return NULL;
	}

	unsigned int bufferFillLength(void) const {
		return m_size - m_len;
	}

	void bufferBytesAppended(unsigned int count) {
		if (bufferFillLength() < count) {
			const char *msg = "attempting to append too much data";
			throw std::logic_error(msg);
		}

		m_len += count;
	}

	/* ADARA::Parser::bufferParse() parses the packets in the internal
	 * buffer, and calls the appropriate virtual functions for each one.
	 * The caller may specify the maximum number of packets to parse in
	 * a batch, with zero indicating parse until the buffer is exhausted.
	 *
	 * bufferParse() returns a positive integer indicating the number of
	 * packets parsed if none of the callbacks returned true (requesting
	 * stop), a negative number indicating packets parsed before a
	 * callback requested a stop, or zero if no packets were completed.
	 *
	 * Partial packet chunks will be counted as completed when the last
	 * fragment is processed.
	 */
	int bufferParse(std::string & log_info, unsigned int max_packets = 0);

	/* Flush the internal buffers and get ready to restart parsing.
	 */
	virtual void reset(void);

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
	virtual bool rxPacket(const MappedDataPkt &pkt);
	virtual bool rxPacket(const RTDLPkt &pkt);
	virtual bool rxPacket(const SourceListPkt &pkt);
	virtual bool rxPacket(const BankedEventPkt &pkt);
	virtual bool rxPacket(const BankedEventStatePkt &pkt);
	virtual bool rxPacket(const BeamMonitorPkt &pkt);
	virtual bool rxPacket(const PixelMappingPkt &pkt);
	virtual bool rxPacket(const PixelMappingAltPkt &pkt);
	virtual bool rxPacket(const RunStatusPkt &pkt);
	virtual bool rxPacket(const RunInfoPkt &pkt);
	virtual bool rxPacket(const TransCompletePkt &pkt);
	virtual bool rxPacket(const ClientHelloPkt &pkt);
	virtual bool rxPacket(const AnnotationPkt &pkt);
	virtual bool rxPacket(const SyncPkt &pkt);
	virtual bool rxPacket(const HeartbeatPkt &pkt);
	virtual bool rxPacket(const GeometryPkt &pkt);
	virtual bool rxPacket(const BeamlineInfoPkt &pkt);
	virtual bool rxPacket(const BeamMonitorConfigPkt &pkt);
	virtual bool rxPacket(const DetectorBankSetsPkt &pkt);
	virtual bool rxPacket(const DataDonePkt &pkt);
	virtual bool rxPacket(const DeviceDescriptorPkt &pkt);
	virtual bool rxPacket(const VariableU32Pkt &pkt);
	virtual bool rxPacket(const VariableDoublePkt &pkt);
	virtual bool rxPacket(const VariableStringPkt &pkt);
	virtual bool rxPacket(const VariableU32ArrayPkt &pkt);
	virtual bool rxPacket(const VariableDoubleArrayPkt &pkt);
	virtual bool rxPacket(const MultVariableU32Pkt &pkt);
	virtual bool rxPacket(const MultVariableDoublePkt &pkt);
	virtual bool rxPacket(const MultVariableStringPkt &pkt);
	virtual bool rxPacket(const MultVariableU32ArrayPkt &pkt);
	virtual bool rxPacket(const MultVariableDoubleArrayPkt &pkt);

	/* Collect a log string with statistics on "discarded" packet types,
	 * i.e. packets that for one reason or another were _Not_ parsed
	 * or processed.
	 *
	 * Since we don't have a common logging approach in this software suite
	 * (<sigh/>), we just fill up a happy logging string with info
	 * and return it for the caller's logger du jour.
	 */
	void getDiscardedPacketsLogString(std::string & log_info);

	/* Reset the collected "discarded packet" statistics.
	 */
	void resetDiscardedPacketsStats(void);

private:
	uint8_t *	m_buffer;
	unsigned int	m_size;
	unsigned int	m_max_size;
	unsigned int	m_len;

	unsigned int	m_restart_offset;
	unsigned int	m_oversize_len;
	unsigned int	m_oversize_offset;

	std::map<PacketType::Type, uint64_t>	m_discarded_packets;
};

} /* namespacce ADARA */

#endif /* __ADARA_PARSER_H */
