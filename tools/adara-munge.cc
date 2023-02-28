#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <fstream>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "ADARAUtils.h"

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x3000000  // For Old "Direct" PixelMap Pkt!

#define MAX_DEVICE_ID   100

#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>

namespace po = boost::program_options;

static const char *statusString(ADARA::VariableStatus::Enum status)
{
	switch (status) {
	case ADARA::VariableStatus::OK:
		return "OK";
	case ADARA::VariableStatus::READ_ERROR:
		return "ReadErr";
	case ADARA::VariableStatus::WRITE_ERROR:
		return "WriteErr";
	case ADARA::VariableStatus::HIHI_LIMIT:
		return "HiHi";
	case ADARA::VariableStatus::HIGH_LIMIT:
		return "High";
	case ADARA::VariableStatus::LOLO_LIMIT:
		return "LoLo";
	case ADARA::VariableStatus::LOW_LIMIT:
		return "Low";
	case ADARA::VariableStatus::BAD_STATE:
		return "BadState";
	case ADARA::VariableStatus::CHANGED_STATE:
		return "ChangedState";
	case ADARA::VariableStatus::NO_COMMUNICATION:
		return "NoComm";
	case ADARA::VariableStatus::COMMUNICATION_TIMEOUT:
		return "CommTimeout";
	case ADARA::VariableStatus::HARDWARE_LIMIT:
		return "HwLimit";
	case ADARA::VariableStatus::BAD_CALCULATION:
		return "BadCalc";
	case ADARA::VariableStatus::INVALID_SCAN:
		return "InvalidScan";
	case ADARA::VariableStatus::LINK_FAILED:
		return "LinkFail";
	case ADARA::VariableStatus::INVALID_STATE:
		return "InvalidState";
	case ADARA::VariableStatus::BAD_SUBROUTINE:
		return "BadSub";
	case ADARA::VariableStatus::UNDEFINED_ALARM:
		return "Undef";
	case ADARA::VariableStatus::DISABLED:
		return "Disabled";
	case ADARA::VariableStatus::SIMULATED:
		return "Simulated";
	case ADARA::VariableStatus::READ_PERMISSION:
		return "ReadPerm";
	case ADARA::VariableStatus::WRITE_PERMISSION:
		return "WritePerm";
	case ADARA::VariableStatus::UPSTREAM_DISCONNECTED:
		return "UpDisc";
	case ADARA::VariableStatus::NOT_REPORTED:
		return "NotReported";
	}

	return "UndefinedStatus";
}

static const char *severityString(ADARA::VariableSeverity::Enum severity)
{
	switch (severity) {
	case ADARA::VariableSeverity::OK:
		return "OK";
	case ADARA::VariableSeverity::MINOR_ALARM:
		return "Minor";
	case ADARA::VariableSeverity::MAJOR_ALARM:
		return "Major";
	case ADARA::VariableSeverity::INVALID:
		return "Invalid";
	case ADARA::VariableSeverity::NOT_REPORTED:
		return "NotReported";
	}

	return "UndefinedSeverity";
}

static const char *pulseFlavor(ADARA::PulseFlavor::Enum flavor)
{
	switch (flavor) {
	case ADARA::PulseFlavor::NO_BEAM:
		return "No Beam";
	case ADARA::PulseFlavor::NORMAL_TGT_1:
		return "Target Station 1 Normal";
	case ADARA::PulseFlavor::NORMAL_TGT_2:
		return "Target Station 2 Normal";
	case ADARA::PulseFlavor::DIAG_10us:
		return "10us Diagnostic";
	case ADARA::PulseFlavor::DIAG_50us:
		return "50us Diagnostic";
	case ADARA::PulseFlavor::DIAG_100us:
		return "100us Diagnostic";
	case ADARA::PulseFlavor::SPECIAL_PHYSICS_1:
		return "Special Physics 1";
	case ADARA::PulseFlavor::SPECIAL_PHYSICS_2:
		return "Special Physics 2";
	}

	return "UndefinedFlavor";
}

static const char *dataFlags(uint32_t flags)
{
	static std::string dataFlagsStr;
	bool first = true;
	dataFlagsStr = "";
	if (flags & ADARA::DataFlags::GOT_NEUTRONS) {
		if (!first) dataFlagsStr += " ";
		else first = false;
		dataFlagsStr += "GOT_NEUTRONS";
	}
	if (flags & ADARA::DataFlags::GOT_METADATA) {
		if (!first) dataFlagsStr += " ";
		else first = false;
		dataFlagsStr += "GOT_METADATA";
	}
	if (first)
		dataFlagsStr += "[DataFlags Not Set]";
	return dataFlagsStr.c_str();
}

class MungeParser : public ADARA::POSIXParser {
public:
	MungeParser() :
		ADARA::POSIXParser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
		m_run_start_epoch(0),
		m_addendum_file_number(0), m_addendum(false),
		m_pause_file_number(0), m_paused(false),
		m_run_file_number(0), m_run_number(0),
		m_case(0),
		m_skip_pkt(false),
		m_addRunEnd(false),
		m_genStart(false), m_genStop(false),
		m_saveStart(false), m_saveStop(false),
		m_skipStart(false), m_skipStop(false),
		m_isStart(false), m_isStop(false),
		m_hysterical(false),
		m_filterafter(false), m_filterbefore(false),
		m_clearemptydata(false),
		m_posixRead(false), m_showDDP(false), m_lowRate(false),
		m_terse(false), m_catch(false),
		m_out(std::cout),
		m_save_count(0),
		m_skip_count(0)
	{
		m_lastpkttime.tv_sec = 0;
		m_lastpkttime.tv_nsec = 0;
		m_threshtime.tv_sec = 0;
		m_threshtime.tv_nsec = 0;
		m_runstart.tv_sec = 0;
		m_runstart.tv_nsec = 0;
		m_runstop.tv_sec = 0;
		m_runstop.tv_nsec = 0;
		m_starttime.tv_sec = 0;
		m_starttime.tv_nsec = 0;
		m_endtime.tv_sec = 0;
		m_endtime.tv_nsec = 0;
	}

	void parse(int argc, char **argv);

	void parse_file(FILE *);
	void parse_file(const std::string &);

	void read_file(int fd);

	bool rxPacket(const ADARA::Packet &pkt);

	bool rxUnknownPkt(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr,
				const uint8_t *chunk,
				unsigned int chunk_offset,
				unsigned int chunk_len);

	// Packet Types to Munge...! ;-D

	bool rxPacket(const ADARA::RawDataPkt &pkt);
	bool rxPacket(const ADARA::MappedDataPkt &pkt);

	bool handleDataPkt(const ADARA::RawDataPkt *pkt, bool is_mapped);

	bool rxPacket(const ADARA::RTDLPkt &pkt);
	bool rxPacket(const ADARA::BankedEventPkt &pkt);
	bool rxPacket(const ADARA::BankedEventStatePkt &pkt);
	bool rxPacket(const ADARA::BeamMonitorPkt &pkt);
	bool rxPacket(const ADARA::RunStatusPkt &pkt);
	bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);
	bool rxPacket(const ADARA::VariableU32ArrayPkt &pkt);
	bool rxPacket(const ADARA::VariableDoubleArrayPkt &pkt);
	bool rxPacket(const ADARA::MultVariableU32Pkt &pkt);
	bool rxPacket(const ADARA::MultVariableDoublePkt &pkt);
	bool rxPacket(const ADARA::MultVariableStringPkt &pkt);
	bool rxPacket(const ADARA::MultVariableU32ArrayPkt &pkt);
	bool rxPacket(const ADARA::MultVariableDoubleArrayPkt &pkt);

	void addRunStatus( uint32_t m_run_number, uint32_t m_run_start_epoch,
		uint32_t m_run_file_number,
		uint32_t m_pause_file_number, bool m_paused,
		uint32_t m_addendum_file_number, bool m_addendum,
		ADARA::RunStatus::Enum status );

	void addRunStart( struct timespec *ts );

	void addRunStop( struct timespec *ts );

	using ADARA::POSIXParser::rxPacket;

private:

	uint32_t m_descriptor_count[MAX_DEVICE_ID];
	struct timespec m_lastpkttime;
	struct timespec m_threshtime;
	struct timespec m_runstart;
	struct timespec m_runstop;
	struct timespec m_starttime;
	struct timespec m_endtime;
	uint32_t m_run_start_epoch;
	uint32_t m_addendum_file_number;
	bool m_addendum;
	uint32_t m_pause_file_number;
	bool m_paused;
	uint32_t m_run_file_number;
	uint32_t m_run_number;
	uint32_t m_case;


	bool m_skip_pkt;
	bool m_addRunEnd;
	bool m_genStart;
	bool m_genStop;
	bool m_saveStart;
	bool m_saveStop;
	bool m_skipStart;
	bool m_skipStop;
	bool m_isStart;
	bool m_isStop;
	bool m_hysterical;
	bool m_filterafter;
	bool m_filterbefore;
	bool m_clearemptydata;
	bool m_posixRead;
	bool m_showDDP;
	bool m_lowRate;
	bool m_terse;
	bool m_catch;

	std::ostream &m_out;

	std::string m_save_file;
	std::vector<uint32_t> m_save_pkts;
	std::vector<uint32_t> m_device_ids;
	uint32_t m_last_device_id;
	uint32_t m_save_count;
	std::ofstream m_save_out;

	std::vector<uint32_t> m_skip_pkts;
	uint32_t m_skip_count;
};

bool MungeParser::rxPacket(const ADARA::Packet &pkt)
{
	bool ret = false;

	m_last_device_id = 0;

	m_isStart = false;
	m_isStop = false;

	try {
		ret = ADARA::POSIXParser::rxPacket(pkt);
	}
	catch ( std::exception &e ) {
		std::cerr << "ADARA-Parser: Caught Exception"
			<< " in ADARA::POSIXParser::rxPacket():"
			<< " [" << e.what() << "]" << std::endl;
		if ( !m_catch ) {
			std::cerr << "Exiting..." << std::endl;
			exit(99);
		}
		else {
			std::cerr << "Continuing..." << std::endl;
		}
	}
	catch ( ... )
	{
		std::cerr << "ADARA-Parser: Caught Unknown Exception"
			<< " in ADARA::POSIXParser::rxPacket()." << std::endl;
		if ( !m_catch ) {
			std::cerr << "Exiting..." << std::endl;
			exit(99);
		}
		else {
			std::cerr << "Continuing..." << std::endl;
		}
	}

	// Capture "Last Packet's" Pulse Id (Timestamp)...
	// - for Adding Run Status End Packet...
	m_lastpkttime.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_lastpkttime.tv_nsec = (uint32_t) pkt.pulseId();

	// Check for Packets to "Skip" By Type, I.e. "Throw Away"... ;-D
	for ( uint32_t i=0 ; i < m_skip_pkts.size() ; i++ )
	{
		if ( pkt.type() == m_skip_pkts[i] )
		{
			std::cerr << "[Skipping Packet Type " << pkt.type()
				<< " (0x" << std::hex << pkt.type()
					<< std::dec << ")"
				<< "]" << std::endl;
			m_skip_pkt = true;
		}
	}

	// Skip Run Start...
	if ( m_skipStart && m_isStart )
	{
		std::cerr << "[Skipping Run Start Packet,"
			<< " Run Status Type " << pkt.type()
			<< " (0x" << std::hex << pkt.type()
				<< std::dec << ")"
			<< "]" << std::endl;

		m_skip_pkt = true;
	}

	// Skip Run Stop...
	if ( m_skipStop && m_isStop )
	{
		std::cerr << "[Skipping Run Stop Packet,"
			<< " Run Status Type " << pkt.type()
			<< " (0x" << std::hex << pkt.type()
				<< std::dec << ")"
			<< "]" << std::endl;

		m_skip_pkt = true;
	}

	// Pass Packet Through to Output Stream...
	if ( !m_skip_pkt )
	{
		if ( ( m_filterafter
				&& compareTimeStamps( m_lastpkttime, m_threshtime ) > 0  )
			|| ( m_filterbefore
				&& compareTimeStamps( m_lastpkttime, m_threshtime ) < 0 )
			|| ( !m_filterafter && !m_filterbefore ) )
		{
			m_out.write( (const char *)pkt.packet(), pkt.packet_length() );
		}
		else
			m_skip_count++;
	}
	else
	{
		m_skip_pkt = false;
		m_skip_count++;
	}

	// Check for Packet Types to Save...
	if ( m_save_out.is_open() )
	{
		for ( uint32_t i=0 ; i < m_save_pkts.size() ; i++ )
		{
			if ( pkt.type() == m_save_pkts[i] )
			{
				std::cerr << "[Matching Packet Type " << pkt.type()
					<< " (0x" << std::hex << pkt.type()
						<< std::dec << ")"
					<< " Found for Saving Packets."
					<< "]" << std::endl;

				// Check for Specific Device IDs on Variable Value Pkts...
				// Skip Saving Packet if the Device ID is _Not_ on List.
				if ( m_device_ids.size() > 0 )
				{
					bool skip_this = true;
					for ( uint32_t d=0 ; d < m_device_ids.size() ; d++ )
					{
						if ( m_last_device_id == m_device_ids[d] )
						{
							std::cerr << "[Matching Device ID "
								<< m_last_device_id
								<< " (0x" << std::hex << m_last_device_id
									<< std::dec << ")"
								<< " - Save Packet."
								<< "]" << std::endl;
							skip_this = false;
						}
					}

					if ( skip_this )
					{
						std::cerr << "[No Matching Device ID Found for "
							<< m_last_device_id
							<< " (0x" << std::hex << m_last_device_id
								<< std::dec << ")"
							<< " - Skipping Save of Packet...!"
							<< "]" << std::endl;
						continue;
					}
				}

				m_save_out.write( (const char *)pkt.packet(),
					pkt.packet_length() );
				m_save_count++;
			}
		}

		// Save Run Start...
		if ( m_saveStart && m_isStart )
		{
			std::cerr << "[Saving Run Start Packet,"
				<< " Run Status Type " << pkt.type()
				<< " (0x" << std::hex << pkt.type()
					<< std::dec << ")"
				<< "]" << std::endl;

			m_save_out.write( (const char *)pkt.packet(),
				pkt.packet_length() );
			m_save_count++;
		}

		// Save Run Stop...
		if ( m_saveStop && m_isStop )
		{
			std::cerr << "[Saving Run Stop Packet,"
				<< " Run Status Type " << pkt.type()
				<< " (0x" << std::hex << pkt.type()
					<< std::dec << ")"
				<< "]" << std::endl;

			m_save_out.write( (const char *)pkt.packet(),
				pkt.packet_length() );
			m_save_count++;
		}
	}

	return ret;
}

bool MungeParser::rxUnknownPkt(const ADARA::Packet &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u Unknown Packet\n",
			(uint32_t) (pkt.pulseId() >> 32),
			(uint32_t) pkt.pulseId() );
		fprintf( stderr, "    type %08x len %u\n",
			pkt.type(), pkt.packet_length() );
	}

	return false;
}

bool MungeParser::rxOversizePkt(const ADARA::PacketHeader *hdr,
				const uint8_t *UNUSED(chunk),
				unsigned int UNUSED(chunk_offset),
				unsigned int chunk_len)
{
	if ( !m_terse ) {
		// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
		if (hdr) {
			fprintf( stderr, "%u.%09u Oversize Packet\n",
				(uint32_t) (hdr->pulseId() >> 32),
				(uint32_t) hdr->pulseId() );
			fprintf( stderr, "    type %08x len %u\n",
				hdr->type(), hdr->packet_length() );
		}
		else {
			fprintf( stderr,
				"[No Header, Continuation...] Oversize Packet\n" );
			fprintf( stderr,"    chunk_len %u\n", chunk_len );
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::RawDataPkt &pkt)
{
	return( handleDataPkt(&pkt, false) );
}

bool MungeParser::rxPacket(const ADARA::MappedDataPkt &pkt)
{
	return( handleDataPkt(dynamic_cast<const ADARA::RawDataPkt *>(&pkt),
		true) );
}

bool MungeParser::handleDataPkt(const ADARA::RawDataPkt *pkt,
		bool is_mapped)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u %s EVENT DATA (0x%x,v%u)\n"
			"    srcId 0x%08x pulseSeq 0x%x sourceSeq 0x%x%s\n"
			"    cycle %u%s vetoFlags 0x%x%s timing 0x%x\n"
			"    dataFlags=%s 0x%x (%s)\n"
			"    flavor %d (%s)\n"
			"    intrapulse %luns tofOffset %luns%s\n"
			"    charge %lupC, %u events\n",
			(uint32_t) (pkt->pulseId() >> 32), (uint32_t) pkt->pulseId(),
			is_mapped ? "MAPPED" : "RAW",
			pkt->base_type(), pkt->version(),
			pkt->sourceID(), pkt->pulseSeq(), pkt->sourceSeq(),
			pkt->endOfPulse() ? " EOP" : "",
			pkt->cycle(), pkt->badCycle() ? " (BAD)" : "",
			pkt->vetoFlags(), pkt->badVeto() ? " (BAD)" : "",
			pkt->timingStatus(),
			pkt->gotDataFlags() ? "true" : "false",
			(int) pkt->dataFlags(), dataFlags(pkt->dataFlags()),
			(int) pkt->flavor(), pulseFlavor(pkt->flavor()),
			(uint64_t) pkt->intraPulseTime() * 100,
			(uint64_t) pkt->tofOffset() * 100,
			pkt->tofCorrected() ? "" : " (raw)",
			(uint64_t) pkt->pulseCharge() * 10, pkt->num_events() );
	}

	// Munge Any "Empty, Version 0" Data Packets to "Version 1" Type,
	// to _Force_ No "GOT_NEUTRONS" or "GOT_METADATA" Data Flags,
	// Thereby Zeroing Out the Corresponding RTDL Proton Charge! ;-D
	// (as per EQSANS Erroneous 30 Hz vs. 60 Hz 2nd DSP Issues, 9/8/21)
	if ( m_clearemptydata
			&& pkt->num_events() == 0 && pkt->version() == 0 )
	{
		fprintf( stderr,
			"*** ClearEmptyData pkt->num_events()=%d pkt->version()=%d\n",
			pkt->num_events(), pkt->version() );

		ADARA::RawDataPkt *PKT =
			const_cast<ADARA::RawDataPkt*>(pkt);
		fprintf( stderr, "Before PKT->version()=%d\n", PKT->version() );
		PKT->remapVersion( (ADARA::PacketType::Version) 0x1 );
		fprintf( stderr, "After PKT->version()=%d (pkt->version()=%d)\n",
			PKT->version(), pkt->version() );
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime.tv_sec = (uint32_t) (pkt->pulseId() >> 32);
	m_endtime.tv_nsec = (uint32_t) pkt->pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::RTDLPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u RTDL (0x%x,v%u) [%u bytes]\n"
			"    cycle %u%s vetoFlags 0x%x%s timing 0x%x\n"
			"    dataFlags=%s 0x%x (%s)\n"
			"    flavor %d (%s)\n"
			"    intrapulse %luns tofOffset %luns%s\n"
			"    charge %lupC period %ups\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), pkt.badCycle() ? " (BAD)" : "",
			pkt.vetoFlags(), pkt.badVeto() ? " (BAD)" : "",
			pkt.timingStatus(),
			pkt.gotDataFlags() ? "true" : "false",
			(int) pkt.dataFlags(), dataFlags(pkt.dataFlags()),
			(int) pkt.flavor(), pulseFlavor(pkt.flavor()),
			(uint64_t) pkt.intraPulseTime() * 100,
			(uint64_t) pkt.tofOffset() * 100,
			pkt.tofCorrected() ? "" : " (raw)",
			(uint64_t) pkt.pulseCharge() * 10, pkt.ringPeriod() );
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime.tv_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BankedEventPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u BANKED EVENT DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags() );
		if (pkt.flags()) {
			fprintf( stderr, "    flags" );
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				fprintf( stderr, " ERROR" );
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				fprintf( stderr, " PARTIAL" );
			if (pkt.flags() & ADARA::PULSE_VETO)
				fprintf( stderr, " VETO" );
			if (pkt.flags() & ADARA::MISSING_RTDL)
				fprintf( stderr, " NO_RTDL" );
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				fprintf( stderr, " MAPPING" );
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				fprintf( stderr, " DUP_PULSE" );
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				fprintf( stderr, " PCHG_UNCOR" );
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				fprintf( stderr, " VETO_UNCOR" );
			if (pkt.flags() & ADARA::GOT_METADATA)
				fprintf( stderr, " GOT_METADATA" );
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				fprintf( stderr, " GOT_NEUTRONS" );
			fprintf( stderr, "\n" );
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime.tv_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BankedEventStatePkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u BANKED EVENT STATE DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags() );
		if (pkt.flags()) {
			fprintf( stderr, "    flags" );
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				fprintf( stderr, " ERROR" );
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				fprintf( stderr, " PARTIAL" );
			if (pkt.flags() & ADARA::PULSE_VETO)
				fprintf( stderr, " VETO" );
			if (pkt.flags() & ADARA::MISSING_RTDL)
				fprintf( stderr, " NO_RTDL" );
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				fprintf( stderr, " MAPPING" );
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				fprintf( stderr, " DUP_PULSE" );
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				fprintf( stderr, " PCHG_UNCOR" );
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				fprintf( stderr, " VETO_UNCOR" );
			if (pkt.flags() & ADARA::GOT_METADATA)
				fprintf( stderr, " GOT_METADATA" );
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				fprintf( stderr, " GOT_NEUTRONS" );
			if (pkt.flags() & ADARA::HAS_STATES)
				fprintf( stderr, " HAS_STATES" );
			fprintf( stderr, "\n" );
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime.tv_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u BEAM MONITOR DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags() );
		if (pkt.flags()) {
			fprintf( stderr, "    flags" );
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				fprintf( stderr, " ERROR" );
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				fprintf( stderr, " PARTIAL" );
			if (pkt.flags() & ADARA::PULSE_VETO)
				fprintf( stderr, " VETO" );
			if (pkt.flags() & ADARA::MISSING_RTDL)
				fprintf( stderr, " NO_RTDL" );
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				fprintf( stderr, " MAPPING" );
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				fprintf( stderr, " DUP_PULSE" );
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				fprintf( stderr, " PCHG_UNCOR" );
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				fprintf( stderr, " VETO_UNCOR" );
			if (pkt.flags() & ADARA::GOT_METADATA)
				fprintf( stderr, " GOT_METADATA" );
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				fprintf( stderr, " GOT_NEUTRONS" );
			fprintf( stderr, "\n" );
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime.tv_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::RunStatusPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u RUN STATUS (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length() );

		switch (pkt.status()) {
		case ADARA::RunStatus::NO_RUN:
			fprintf( stderr, "    No current run\n" );
			break;
		case ADARA::RunStatus::STATE:
			fprintf( stderr, "    State snapshot\n" );
			break;
		case ADARA::RunStatus::NEW_RUN:
			fprintf( stderr, "    New run\n" );
			m_isStart = true;
			break;
		case ADARA::RunStatus::RUN_EOF:
			fprintf( stderr, "    End of file (run continues)\n" );
			break;
		case ADARA::RunStatus::RUN_BOF:
			fprintf( stderr, "    Beginning of file (continuing run)\n" );
			break;
		case ADARA::RunStatus::END_RUN:
			fprintf( stderr, "    End of run\n" );
			m_isStop = true;
			break;
		case ADARA::RunStatus::PROLOGUE:
			fprintf( stderr, "    [PROLOGUE]\n" );
			break;
		}

		if (pkt.runNumber()) {
			fprintf( stderr, "    Run %u started at epoch %u\n",
				pkt.runNumber(), pkt.runStart() );
			if (pkt.status() != ADARA::RunStatus::STATE
					&& pkt.status() != ADARA::RunStatus::PROLOGUE)
			{
				uint32_t fileNum = pkt.fileNumber();
				uint32_t modeNum = 0;
				uint32_t pauseFileNum = pkt.pauseFileNumber();
				uint32_t paused = pkt.paused();
				uint32_t addendumFileNum = pkt.addendumFileNumber();
				uint32_t addendum = pkt.addendum();

				// Embedded Mode Number...?
				if ( fileNum > 0xfff )
				{
					modeNum = ( fileNum >> 12 ) & 0xfff;
					fileNum &= 0xfff;
					fprintf( stderr, "    %s %u, %s %u\n",
						"Mode Index", modeNum,
						"File Index", fileNum );
					fprintf( stderr, "    %s %u (%s=%u), %s %u (%s=%u)\n",
						"Pause Index", pauseFileNum,
						"paused", paused,
						"Addendum Index", addendumFileNum,
						"addendum", addendum );
				}
				else
				{
					fprintf( stderr, "    %s %u\n",
						"File Index", fileNum );
					fprintf( stderr, "    %s %u (%s=%u), %s %u (%s=%u)\n",
						"Pause Index", pauseFileNum,
						"paused", paused,
						"Addendum Index", addendumFileNum,
						"addendum", addendum );
				}
			}
		}
	}

	// Capture Run Start Epoch/File Information for
	// Adding Omitted RunStatusPkt for End of Run...!
	if ( m_addRunEnd || m_genStart || m_genStop )
	{
		if ( m_addRunEnd )
			std::cerr << "[Add Run End Mode]" << std::endl;

		// RunStatusPkt from Start of Run/File...
		if ( pkt.status() == ADARA::RunStatus::NEW_RUN
				|| pkt.status() == ADARA::RunStatus::RUN_BOF )
		{
			// For Add Run End:
			// - Capture Run Number, Run Start Epoch Time & File Number...
			if ( m_addRunEnd )
			{
				m_run_number = pkt.runNumber();
				m_run_start_epoch = pkt.runStart();
				m_run_file_number = pkt.fileNumber();
				m_pause_file_number = pkt.pauseFileNumber();
				m_paused = pkt.paused();
				m_addendum_file_number = pkt.addendumFileNumber();
				m_addendum = pkt.addendum();

				std::cerr << "Captured Run Status Meta-Data:"
					<< std::endl
					<< "- Run Number = " << m_run_number << std::endl
					<< "- Run Start Epoch = " << m_run_start_epoch
						<< std::endl
					<< "- Run File Number = " << m_run_file_number
						<< " (0x" << std::hex << m_run_file_number
							<< std::dec << ")"
					<< "- Paused File Number = " << m_pause_file_number
						<< " (0x" << std::hex << m_pause_file_number
							<< std::dec << ")"
					<< "- Paused = " << m_paused
						<< " (0x" << std::hex << m_paused
							<< std::dec << ")"
					<< "- Addendum File Number = "
							<< m_addendum_file_number
						<< " (0x" << std::hex << m_addendum_file_number
							<< std::dec << ")"
					<< "- Addendum = " << m_addendum
						<< " (0x" << std::hex << m_addendum
							<< std::dec << ")"
						<< std::endl;
			}

			// For Generate Run Start:
			// - Capture Run Start Time
			if ( m_genStart && pkt.status() == ADARA::RunStatus::NEW_RUN )
			{
				std::cerr << "[Generate Run Start Mode]" << std::endl;

				// Capture "Run Start" Timestamp (Pulse Id) from RunStatus
				// - for Generating "Start Run" System Annot Packet...
				m_runstart.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
				m_runstart.tv_nsec = (uint32_t) pkt.pulseId();
			}
		}

		// Hmmm... This Run File _Already Has_ a RunStatusPkt
		// for End of File...
		else if ( pkt.status() == ADARA::RunStatus::RUN_EOF )
		{
			if ( m_addRunEnd )
			{
				std::cerr
					<< "*** Warning:"
					<< " Run File Already Has Run Status Packet"
					<< " - End of File (Run Continues)..."
					<< " Skipping This Run Status Packet...!"
					<< std::endl;

				m_skip_pkt = true;
			}
		}

		// Hmmm... This Run Already _Has_ a RunStatusPkt for End of Run...!
		else if ( pkt.status() == ADARA::RunStatus::END_RUN )
		{
			if ( m_addRunEnd )
			{
				std::cerr
					<< "*** Error: Run File Already Has Run Status"
					<< " - End of Run...!"
					<< " Deactivating \"Add End of Run\" Option..."
					<< std::endl;

				m_addRunEnd = false;
			}

			// For Generate Run Stop:
			// - Capture Run Stop Time
			if ( m_genStop )
			{
				std::cerr << "[Generate Run Stop Mode]" << std::endl;

				// Capture "Run Stop" Timestamp (Pulse Id) from RunStatus
				// - for Generating "Stop Run" System Annot Packet...
				m_runstop.tv_sec = (uint32_t) (pkt.pulseId() >> 32);
				m_runstop.tv_nsec = (uint32_t) pkt.pulseId();
			}
		}
	}

	// "Hysterical" Replay Tweaks to Run Status (for REF_M originally)
	// - Run Start Time and Run End Time Must Match Historical Data...!
	else if ( m_hysterical )
	{
		if ( m_starttime.tv_sec > 0 && m_starttime.tv_nsec > 0 )
		{
			if ( pkt.status() == ADARA::RunStatus::NEW_RUN )
			{
				std::cerr << "*** Found Run Status - New Run...!"
					<< std::endl;
				uint64_t newPulseId;
				newPulseId = ((uint64_t) m_starttime.tv_sec) << 32;
				newPulseId |= m_starttime.tv_nsec;
				ADARA::RunStatusPkt *PKT =
					const_cast<ADARA::RunStatusPkt*>(&pkt);
				PKT->setPulseId( newPulseId );
				PKT->setRunStart( m_starttime.tv_sec );
			}
			else if ( pkt.status() == ADARA::RunStatus::END_RUN )
			{
				std::cerr << "*** Found Run Status - End of Run...!"
					<< std::endl;
				uint64_t newPulseId;
				newPulseId = ((uint64_t) m_endtime.tv_sec) << 32;
				newPulseId |= m_endtime.tv_nsec;
				ADARA::RunStatusPkt *PKT =
					const_cast<ADARA::RunStatusPkt*>(&pkt);
				PKT->setPulseId( newPulseId );
				// *Always* Set to Run Start Time! ;-D
				PKT->setRunStart( m_starttime.tv_sec );
			}
		}
		else
		{
			std::cerr
				<< "Hysterical Run Status Mode _Without_ Start Time Set!"
				<< " Ignoring..." << std::endl;
		}
	}

	return false;
}

struct run_status_packet {
	ADARA::Header   hdr;
	uint32_t    run_number;
	uint32_t    run_start;
	uint32_t    status_number;
	uint32_t    pause_number;
	uint32_t    addendum_number;
} __attribute__((packed));

void MungeParser::addRunStatus( uint32_t run_number,
		uint32_t run_start_epoch, uint32_t run_file_number,
		uint32_t pause_file_number, bool paused,
		uint32_t addendum_file_number, bool addendum,
		ADARA::RunStatus::Enum status )
{
	struct run_status_packet spkt = {
		hdr : {
			payload_len : 20,
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::RUN_STATUS_TYPE,
				ADARA::PacketType::RUN_STATUS_VERSION ),
		},
	};

	spkt.hdr.ts_sec = m_lastpkttime.tv_sec;
	spkt.hdr.ts_nsec = m_lastpkttime.tv_nsec;

	spkt.run_number = run_number;

	spkt.run_start = run_start_epoch;

	// Ignore Paused File Number in RunStatus Packet...
	// (TODO Figure out how to munge this field if we ever need
	// to _Recover_ any Paused Files into a given run...! ;-)
	// [Solved in V1 Packet Type... Activated as of 1.8.1]
	spkt.status_number = run_file_number | ((uint32_t) status << 24);

	spkt.pause_number = pause_file_number | ((uint32_t) paused << 24);
	spkt.addendum_number = addendum_file_number
		| ((uint32_t) addendum << 24);

	m_out.write( (const char *)&spkt, sizeof(spkt) );
}

struct annotation_packet {
	ADARA::Header   hdr;
	uint32_t    marker_type;
	uint32_t    scan_index;
	char	    comment[1024];
} __attribute__((packed));

void MungeParser::addRunStart( struct timespec *ts )
{
	struct annotation_packet apkt = {
		hdr : {
			payload_len : 2 * sizeof(uint32_t),
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::STREAM_ANNOTATION_TYPE,
				ADARA::PacketType::STREAM_ANNOTATION_VERSION ),
		},
	};

	apkt.hdr.ts_sec = ts->tv_sec;
	apkt.hdr.ts_nsec = ts->tv_nsec;

	apkt.marker_type = (uint32_t) ADARA::MarkerType::SYSTEM << 16;

	apkt.scan_index = 0;

	char *command = (char *) "Start Run";

	// Zero Pad End of String
	memset( apkt.comment, 0, sizeof(apkt.comment) );

	sprintf( apkt.comment, "%s", command );

	size_t cmd_size = ( ( strlen( command ) + 3 ) & ~3 );

	apkt.hdr.payload_len += cmd_size;

	apkt.marker_type |= strlen( command );

	size_t apkt_size = sizeof(ADARA::Header) + apkt.hdr.payload_len;

	std::cerr << "   apkt.comment=[" << apkt.comment << "]"
		<< " apkt_size=" << apkt_size << std::endl;

	// Save System Annot Packet to Output File (if present)
	if ( m_save_out.is_open() )
	{
		m_save_out.write( (const char *)&apkt, apkt_size );
		m_save_count++;
	}

	// Else Just Munge the Regular Output Stream...
	else
	{
		m_out.write( (const char *)&apkt, apkt_size );
	}
}

void MungeParser::addRunStop( struct timespec *ts )
{
	struct annotation_packet apkt = {
		hdr : {
			payload_len : 2 * sizeof(uint32_t),
			pkt_format : ADARA_PKT_TYPE(
				ADARA::PacketType::STREAM_ANNOTATION_TYPE,
				ADARA::PacketType::STREAM_ANNOTATION_VERSION ),
		},
	};

	apkt.hdr.ts_sec = ts->tv_sec;
	apkt.hdr.ts_nsec = ts->tv_nsec;

	apkt.marker_type = (uint32_t) ADARA::MarkerType::SYSTEM << 16;

	apkt.scan_index = 0;

	char *command = (char *) "Stop Run";

	// Zero Pad End of String
	memset( apkt.comment, 0, sizeof(apkt.comment) );

	sprintf( apkt.comment, "%s", command );

	size_t cmd_size = ( ( strlen( command ) + 3 ) & ~3 );

	apkt.hdr.payload_len += cmd_size;

	apkt.marker_type |= strlen( command );

	size_t apkt_size = sizeof(ADARA::Header) + apkt.hdr.payload_len;

	std::cerr << "   apkt.comment=[" << apkt.comment << "]"
		<< " apkt_size=" << apkt_size << std::endl;

	// Save System Annot Packet to Output File (if present)
	if ( m_save_out.is_open() )
	{
		m_save_out.write( (const char *)&apkt, apkt_size );
		m_save_count++;
	}

	// Else Just Munge the Regular Output Stream...
	else
	{
		m_out.write( (const char *)&apkt, apkt_size );
	}
}

bool MungeParser::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	if ( !m_terse || m_showDDP ) {
		// TODO display more fields (check that the contents don't change)
		fprintf( stderr,
			"%u.%09u DEVICE DESCRIPTOR (0x%x,v%u) [%u bytes]\n"
			"    Device %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId() );
	}

	if ( m_showDDP )
	{
		fprintf( stderr, "%s\n", pkt.description().c_str() );
	}

	// Note the Device ID for this Device Descriptor Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	//
	// Evil Device Id Re-Numbering Issue (beamline.xml Changed Mid-Run!)
	//

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in DeviceDescriptorPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// Another Descriptor for the Given Device Id...
	(m_descriptor_count[ pkt.devId() ])++;

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::DeviceDescriptorPkt *PKT =
				const_cast<ADARA::DeviceDescriptorPkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::DeviceDescriptorPkt *PKT =
			const_cast<ADARA::DeviceDescriptorPkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u U32 VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in VariableU32Pkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::VariableU32Pkt *PKT =
				const_cast<ADARA::VariableU32Pkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::VariableU32Pkt *PKT =
			const_cast<ADARA::VariableU32Pkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u DOUBLE VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %lf\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	// CNCS FitSam "Off-By-11-Minutes" Bug, November 2017...
	// - correct timing by 667.908933229 seconds...
	/*
	if ( pkt.devId() == 2 )
	{
		std::cerr << "*** Found FitSam Device (2) Double Variable Update!"
			<< std::endl;
		uint32_t sec = (uint32_t) (pkt.pulseId() >> 32);
		uint32_t nsec = (uint32_t) pkt.pulseId();
		sec += 667;
		nsec += 908933229;
		if ( nsec >= NANO_PER_SECOND_LL )
		{
			sec++;
			nsec -= NANO_PER_SECOND_LL;
		}
		uint64_t newPulseId;
		newPulseId = ((uint64_t) sec) << 32;
		newPulseId |= nsec;
		ADARA::VariableDoublePkt *PKT =
			const_cast<ADARA::VariableDoublePkt*>(&pkt);
		PKT->setPulseId( newPulseId );
	}
	*/

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in VariableDoublePkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::VariableDoublePkt *PKT =
				const_cast<ADARA::VariableDoublePkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::VariableDoublePkt *PKT =
			const_cast<ADARA::VariableDoublePkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	// CASE #3: CNCS "Multiply Logs By Factor of 5.0" Fix 5/16/2019
	// - for IPTS-21648 Runs 288127 to 288305...
	// -> BL5:Mot:Sample:Axis1 (Sample:Axis1) = bl5-Parker2 Dev 7 Var 1
	// -> BL5:Mot:omega (omega) = bl5-Parker2 Dev 7 Var 7
	else if ( m_case == 3 )
	{
		ADARA::VariableDoublePkt *PKT =
			const_cast<ADARA::VariableDoublePkt*>(&pkt);
		if ( PKT->devId() == 7 )
		{
			if ( PKT->varId() == 1 || PKT->varId() == 7 )
			{
				// Munge Log Values by Factor of 5.0...! ;-D
				double value = PKT->value();
				value *= 5.0;
				PKT->updateValue( value );
			}
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr, "%u.%09u String VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value '%s'\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value().c_str() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in VariableStringPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::VariableStringPkt *PKT =
				const_cast<ADARA::VariableStringPkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::VariableStringPkt *PKT =
			const_cast<ADARA::VariableStringPkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableU32ArrayPkt &pkt)
{
	uint32_t i;

	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u U32 ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount() );
		for (i = 0; i < pkt.elemCount(); i++) {
			fprintf( stderr, " %u", pkt.value()[i] );
		}
		fprintf( stderr, "\n" );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in VariableU32ArrayPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::VariableU32ArrayPkt *PKT =
				const_cast<ADARA::VariableU32ArrayPkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::VariableU32ArrayPkt *PKT =
			const_cast<ADARA::VariableU32ArrayPkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableDoubleArrayPkt &pkt)
{
	uint32_t i;

	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u DOUBLE ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount() );
		for (i = 0; i < pkt.elemCount(); i++) {
			fprintf( stderr, " %lf", pkt.value()[i] );
		}
		fprintf( stderr, "\n" );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in VariableDoubleArrayPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	// CASE #1: Stream Files _Before_ and Up To Change
	// -> *ALL* OLD Device Ids Need to Be Edited into New Device Ids...!
	// -> *Only* Replace Device Ids Until *2nd* Device Descriptor is Found!
	if ( m_case == 1 )
	{
		if ( m_descriptor_count[ pkt.devId() ] < 2 )
		{
			ADARA::VariableDoubleArrayPkt *PKT =
				const_cast<ADARA::VariableDoubleArrayPkt*>(&pkt);
			switch ( PKT->devId() )
			{
				case 1: break;
				case 2: PKT->remapDeviceId( 34 ); break;
				case 3: PKT->remapDeviceId( 33 ); break;
				case 4: PKT->remapDeviceId( 21 ); break;
				case 5: PKT->remapDeviceId( 31 ); break;
				case 6: PKT->remapDeviceId( 30 ); break;
				case 7: PKT->remapDeviceId( 8 ); break;
				case 8: PKT->remapDeviceId( 9 ); break;
				case 9: PKT->remapDeviceId( 10 ); break;
				case 10: PKT->remapDeviceId( 11 ); break;
				case 11: PKT->remapDeviceId( 19 ); break;
				case 12: PKT->remapDeviceId( 29 ); break;
				case 13: PKT->remapDeviceId( 28 ); break;
				case 14: PKT->remapDeviceId( 18 ); break;
				case 15: PKT->remapDeviceId( 26 ); break;
				case 16: PKT->remapDeviceId( 24 ); break;
				case 18: PKT->remapDeviceId( 15 ); break;
				case 19: PKT->remapDeviceId( 12 ); break;
				case 20: PKT->remapDeviceId( 27 ); break;
				case 21: PKT->remapDeviceId( 32 ); break;
				case 22: PKT->remapDeviceId( 17 ); break;
				case 24: PKT->remapDeviceId( 25 ); break;
				default: break;
			}
		}
	}

	// CASE #2: Stream Files _After_ Change
	// -> *Only* Edit Device Ids for Devices with Multiple Descriptors...
	
	else if ( m_case == 2 )
	{
		ADARA::VariableDoubleArrayPkt *PKT =
			const_cast<ADARA::VariableDoubleArrayPkt*>(&pkt);
		switch ( PKT->devId() )
		{
			case 2: PKT->remapDeviceId( 34 ); break;
			case 3: PKT->remapDeviceId( 33 ); break;
			case 4: PKT->remapDeviceId( 21 ); break;
			case 5: PKT->remapDeviceId( 31 ); break;
			case 6: PKT->remapDeviceId( 30 ); break;
			case 7: PKT->remapDeviceId( 8 ); break;
			case 13: PKT->remapDeviceId( 28 ); break;
			case 14: PKT->remapDeviceId( 18 ); break;
			case 20: PKT->remapDeviceId( 27 ); break;
			case 22: PKT->remapDeviceId( 17 ); break;
			default: break;
		}
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::MultVariableU32Pkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u MULT U32 VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in MultVariableU32Pkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::MultVariableDoublePkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u MULT DOUBLE VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in MultVariableDoublePkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::MultVariableStringPkt &pkt)
{
	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u MULT String VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in MultVariableStringPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::MultVariableU32ArrayPkt &pkt)
{
	//uint32_t i;

	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u MULT U32 ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in MultVariableU32ArrayPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	return false;
}

bool MungeParser::rxPacket(const ADARA::MultVariableDoubleArrayPkt &pkt)
{
	//uint32_t i;

	if ( !m_terse ) {
		fprintf( stderr,
			"%u.%09u MULT DOUBLE ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues() );
	}

	// Note the Device ID for this Variable Value Packet...
	// (For Device ID Filtering of Saved Packets...)
	m_last_device_id = pkt.devId();

	if ( pkt.devId() >= MAX_DEVICE_ID )
	{
		if ( !m_terse ) {
			std::cerr << "Warning in MultVariableDoubleArrayPkt:"
				<< " Device Id " << pkt.devId()
				<< " > Max Device Id " << MAX_DEVICE_ID
				<< " - Ignoring Munge Cases for this Device...!"
				<< std::endl;
		}

		return false;
	}

	return false;
}

void MungeParser::parse_file(FILE *f)
{
	size_t len;

	while (!feof(f)) {
		if ( m_lowRate )
			len = fread(bufferFillAddress(), 1, 16, f);
		else
			len = fread(bufferFillAddress(), 1, bufferFillLength(), f);

		if (!len) {
			if (feof(f))
				return;
			throw std::string("read error");
		}

		bufferBytesAppended((unsigned int) len);

		std::string log_info;
		if (bufferParse(log_info, 0) < 0) {
			log_info.append("parse error");
			throw log_info;
		}
	}
}

void MungeParser::parse_file(const std::string &name)
{
	FILE *f = fopen(name.c_str(), "rb");
	if (!f) {
		int e = errno;
		std::string msg("unable to open: ");
		msg += name;
		msg += ": ";
		msg += strerror(e);
		throw msg;
	}

	try {
		if ( m_posixRead ) {
			read_file( fileno( stdin ) );
		}
		else {
			parse_file(f);
		}
		fclose(f);
	} catch (std::string m) {
		fclose(f);

		std::string msg(name);
		msg += ": ";
		msg += m;
		throw msg;
	}
}

void MungeParser::read_file(int fd)
{
	std::string log_info;
	size_t len;

	std::cerr << "Using POSIX read()." << std::endl;

	// NOTE: This is POSIXParser::read()... ;-o
	while ( (len = read( fd, log_info )) );
}

void MungeParser::parse(int argc, char **argv)
{
	std::string m_threshtime_str;
	std::string m_starttime_str;
	std::string m_runstart_str;
	std::string m_runstop_str;

		po::options_description opts("Allowed options");
		opts.add_options()
		("help,h", "Show usage information")
		("posixread,P", "Use POSIX read() to parse incoming stream")
		("showddp,D", "Show payload of device descriptor packets")
		("terse,T", "Terse Mode, Produce no output (except as requested)")
		("catch,C", "Catch Exceptions, Try to parse past bad packets")
		("addrunend,E", "Add Omitted Run Status Packet to End of Stream")
		("genstart,S", "Generate Run Start System Annot Matching Stream")
		("runstart", po::value<std::string>(&m_runstart_str),
			"Manually Entered Run Start Time")
		("genstop,T", "Generate Run Stop System Annot Matching Stream")
		("runstop", po::value<std::string>(&m_runstop_str),
			"Manually Entered Run Stop Time")
		("savepkts,p",
			po::value<std::vector<uint32_t> >(&m_save_pkts)->multitoken(),
			"List of Packet Types (UINT32) to Save WITH VERSION")
		("deviceid,d",
			po::value<std::vector<uint32_t> >(&m_device_ids)->multitoken(),
			"List of Device IDs (UINT32) to Filter Saved Packets")
		("savestart", "Save Run Start Run Status Packet")
		("savestop", "Save Run Stop Run Status Packet")
		("savefile,F", po::value<std::string>(&m_save_file),
			"Save Certain Packet Types to Given File")
		("skippkts",
			po::value<std::vector<uint32_t> >(&m_skip_pkts)->multitoken(),
			"List of Packet Types (UINT32) to SKIP (i.e. Throw Away)")
		("skipstart", "Skip Run Start Run Status Packet")
		("skipstop", "Skip Run Stop Run Status Packet")
		("hysterical,H", "Set Hysterical Run Start/End Times")
		("starttime", po::value<std::string>(&m_starttime_str),
			"Hysterical Start Time for Experiment Data")
		("filterafter,A", "Filter Data Stream to After Threshold Time")
		("filterbefore,B", "Filter Data Stream to Before Threshold Time")
		("threshtime", po::value<std::string>(&m_threshtime_str),
			"Filter Threshold Time for Data Stream")
		("clearemptydata",
			"Clear Empty Data Packets to Zero Proton Charge")
		("case", po::value<uint32_t>(&m_case),
			"Which Munge Case We Are Executing... (1,2,3...)");

	po::options_description hidden("Hidden options");
	hidden.add_options()
		("file", po::value<std::vector<std::string> >(), "input files");

	po::options_description cmdline_options;
	cmdline_options.add(opts).add(hidden);

	po::positional_options_description p;
	p.add("file", -1);

		po::variables_map vm;
		try {
			po::store(po::command_line_parser(argc, argv).
				options(cmdline_options).positional(p).run(), vm);
			po::notify(vm);
		} catch (po::unknown_option e) {
			std::cerr << argv[0] << ": " << e.what() << std::endl
				<< std::endl << opts << std::endl;
			exit(2);
		}

		if (vm.count("help")) {
			std::cerr << opts << std::endl;
			exit(2);
		}

	m_addRunEnd = vm.count("addrunend");

	m_genStart = vm.count("genstart");

	if ( m_runstart_str.size() ) {
		std::cerr << "Manually Entered Run Start Time Set as: "
			<< m_runstart_str << std::endl;
		size_t dot = m_runstart_str.find(".");
		if ( dot != std::string::npos ) {
			m_runstart.tv_sec = boost::lexical_cast<uint32_t>(
				m_runstart_str.substr(0, dot) );
			m_runstart.tv_nsec = boost::lexical_cast<uint32_t>(
				m_runstart_str.substr(dot + 1) );
			std::cerr << "Manually Entered Run Start Time"
				<< " -> sec=" << m_runstart.tv_sec
				<< " nsec=" << m_runstart.tv_nsec << std::endl;
		}
	}

	m_genStop = vm.count("genstop");

	if ( m_runstop_str.size() ) {
		std::cerr << "Manually Entered Run Stop Time Set as: "
			<< m_runstop_str << std::endl;
		size_t dot = m_runstop_str.find(".");
		if ( dot != std::string::npos ) {
			m_runstop.tv_sec = boost::lexical_cast<uint32_t>(
				m_runstop_str.substr(0, dot) );
			m_runstop.tv_nsec = boost::lexical_cast<uint32_t>(
				m_runstop_str.substr(dot + 1) );
			std::cerr << "Manually Entered Run Stop Time"
				<< " -> sec=" << m_runstop.tv_sec
				<< " nsec=" << m_runstop.tv_nsec << std::endl;
		}
	}

	m_saveStart = vm.count("savestart");

	m_saveStop = vm.count("savestop");

	// Save Packets Options...
	if ( m_save_pkts.size() || m_device_ids.size()
			|| m_genStart || m_genStop || m_saveStart || m_saveStop ) {
		for ( uint32_t i=0 ; i < m_save_pkts.size() ; i++ ) {
			std::cerr << "Packet Type " << m_save_pkts[i]
				<< " (0x" << std::hex << m_save_pkts[i] << std::dec << ")"
			<< " Selected for Saving." << std::endl;
		}
		for ( uint32_t i=0 ; i < m_device_ids.size() ; i++ ) {
			std::cerr << "Device IDs " << m_device_ids[i]
				<< " (0x" << std::hex << m_device_ids[i] << std::dec << ")"
			<< " Selected for Saving." << std::endl;
		}
		if ( m_save_file.size() ) {
			std::cerr << "Saving Selected Packet Types to File: "
				<< m_save_file << std::endl;
		}
		else {
			m_save_file = "save_pkts.adara";
			std::cerr << "Saving Selected Packet Types to Default File: "
				<< m_save_file << std::endl;
		}
		// Try to Open Save Packet File...
		m_save_out.open( m_save_file.c_str(),
			std::ofstream::out | std::ofstream::binary );
		if ( !m_save_out.good() ) {
			std::cerr << "Error Opening Save Packets File \""
				<< m_save_file << "\"...! Disabling Packet Saving...!"
				<< std::endl;
			m_save_out.close();
			m_save_pkts.clear();
			m_device_ids.clear();
		}
	}
	else if ( m_save_file.size()
			&& !m_genStart && !m_genStop && !m_saveStart && !m_saveStop ) {
		std::cerr << "Warning: Save Packets File Specified "
			<< "with NO Selected Packet Types! Ignoring..."
			<< std::endl;
	}

	m_skipStart = vm.count("skipstart");

	m_skipStop = vm.count("skipstop");

	// Skip Packets Options...
	if ( m_skip_pkts.size() ) {
		for ( uint32_t i=0 ; i < m_skip_pkts.size() ; i++ ) {
			std::cerr << "Packet Type " << m_skip_pkts[i]
				<< " (0x" << std::hex << m_skip_pkts[i] << std::dec << ")"
			<< " Selected for SKIPPING." << std::endl;
		}
	}

	m_hysterical = vm.count("hysterical");

	if ( m_starttime_str.size() ) {
		std::cerr << "Hysterical Run Start Time Requested as: "
			<< m_starttime_str << std::endl;
		size_t dot = m_starttime_str.find(".");
		if ( dot != std::string::npos ) {
			m_starttime.tv_sec = boost::lexical_cast<uint32_t>(
				m_starttime_str.substr(0, dot) );
			m_starttime.tv_nsec = boost::lexical_cast<uint32_t>(
				m_starttime_str.substr(dot + 1) );
			std::cerr << "Hysterical Run Start Time"
				<< " -> sec=" << m_starttime.tv_sec
				<< " nsec=" << m_starttime.tv_nsec << std::endl;
		}
	}

	m_filterafter = vm.count("filterafter");
	m_filterbefore = vm.count("filterbefore");

	if ( m_threshtime_str.size() ) {
		std::cerr << "Filter Threshold Time Requested as: "
			<< m_threshtime_str << std::endl;
		size_t dot = m_threshtime_str.find(".");
		if ( dot != std::string::npos ) {
			m_threshtime.tv_sec = boost::lexical_cast<uint32_t>(
				m_threshtime_str.substr(0, dot) );
			m_threshtime.tv_nsec = boost::lexical_cast<uint32_t>(
				m_threshtime_str.substr(dot + 1) );
			std::cerr << "Filter Threshold Time"
				<< " -> sec=" << m_threshtime.tv_sec
				<< " nsec=" << m_threshtime.tv_nsec << std::endl;
		}
	}

	m_clearemptydata = vm.count("clearemptydata");

	m_posixRead = vm.count("posixread");
	m_showDDP = vm.count("showddp");
	m_lowRate = vm.count("low");
	m_terse = vm.count("terse");
	m_catch = vm.count("catch");

	// For Evil Device Id Re-Numbering Issue (beamline.xml Changed Mid-Run)
	for ( uint32_t i=0 ;
			i < ( sizeof(m_descriptor_count) / sizeof(uint32_t) ) ; i++ ) {
		m_descriptor_count[i] = 0;
	}

	if (!vm.count("file")) {
		try {
			if ( m_posixRead ) {
				read_file( fileno( stdin ) );
			}
			else {
				parse_file(stdin);
			}
		} catch (std::string m) {
			std::cerr << argv[0] << ": stdin: " << m << std::endl;
			exit(1);
		}
	} else {
		std::vector<std::string> files;
		std::vector<std::string>::iterator it;
		files = vm["file"].as<std::vector<std::string> >();
		try {
			for (it = files.begin(); it != files.end(); it++)
				parse_file(*it);
		} catch (std::string m) {
			std::cerr << argv[0] << ": " << m << std::endl;
			exit(1);
		}
	}

	// Add Final "RunStatusPkt" to End of Run...
	if ( m_addRunEnd )
	{
		if ( m_run_number > 0
				&& m_run_start_epoch > 0
				&& m_run_file_number > 0 )
		{
			std::cerr
				<< "[Adding Final Run Status Packet for End of Run!]"
				<< std::endl
				<< "- Run Number = " << m_run_number << std::endl
				<< "- Run Start Epoch = " << m_run_start_epoch << std::endl
				<< "- Run File Number = " << m_run_file_number
					<< " (0x" << std::hex << m_run_file_number
						<< std::dec << ")"
				<< "- Paused File Number = " << m_pause_file_number
					<< " (0x" << std::hex << m_pause_file_number
						<< std::dec << ")"
				<< "- Paused = " << m_paused
					<< " (0x" << std::hex << m_paused
						<< std::dec << ")"
				<< "- Addendum File Number = "
						<< m_addendum_file_number
					<< " (0x" << std::hex << m_addendum_file_number
						<< std::dec << ")"
				<< "- Addendum = " << m_addendum
					<< " (0x" << std::hex << m_addendum
						<< std::dec << ")"
				<< std::endl;

			addRunStatus( m_run_number, m_run_start_epoch,
				m_run_file_number,
				m_pause_file_number, m_paused,
				m_addendum_file_number, m_addendum,
				ADARA::RunStatus::END_RUN );
		}

		else
		{
			std::cerr
				<< "Error: Adding Final Run Status Packet for End of Run!"
				<< " Missing Required Meta-Data:"
				<< std::endl
				<< "- Run Number = " << m_run_number << std::endl
				<< "- Run Start Epoch = " << m_run_start_epoch << std::endl
				<< "- Run File Number = " << m_run_file_number << std::endl
					<< " (0x" << std::hex << m_run_file_number
						<< std::dec << ")"
				<< "- Paused File Number = " << m_pause_file_number
					<< " (0x" << std::hex << m_pause_file_number
						<< std::dec << ")"
				<< "- Paused = " << m_paused
					<< " (0x" << std::hex << m_paused
						<< std::dec << ")"
				<< "- Addendum File Number = "
						<< m_addendum_file_number
					<< " (0x" << std::hex << m_addendum_file_number
						<< std::dec << ")"
				<< "- Addendum = " << m_addendum
					<< " (0x" << std::hex << m_addendum
						<< std::dec << ")"
				<< "Can't Add Final Run Status Packet..."
				<< std::endl;
		}
	}

	// Generate "Run Start" as Found...
	if ( m_genStart )
	{
		// Got Run Start Time, Generate System Annot Packet...
		if ( m_runstart.tv_sec > 0 && m_runstart.tv_nsec > 0 )
		{
			std::cerr
				<< "Generating Run Start System Annot Packet"
				<< " -> sec=" << m_runstart.tv_sec
				<< " nsec=" << m_runstart.tv_nsec << std::endl;

			addRunStart( &m_runstart );
		}

		// Didn't Find a Run Start Time...
		else
		{
			std::cerr
				<< "No Run Start Timestamp Found!"
				<< " Can't Generate System Annot Packet..." << std::endl;
		}
	}

	// Generate "Run Stop" as Found...
	if ( m_genStop )
	{
		// Got Run Stop Time, Generate System Annot Packet...
		if ( m_runstop.tv_sec > 0 && m_runstop.tv_nsec > 0 )
		{
			std::cerr
				<< "Generating Run Stop System Annot Packet"
				<< " -> sec=" << m_runstop.tv_sec
				<< " nsec=" << m_runstop.tv_nsec << std::endl;

			addRunStop( &m_runstop );
		}

		// Didn't Find a Run Stop Time...
		else
		{
			std::cerr
				<< "No Run Stop Timestamp Found!"
				<< " Can't Generate System Annot Packet..." << std::endl;
		}
	}

	// Close Any Saved Packets File...
	if ( m_save_out.is_open() ) {
		std::cerr << "Closing Saved Packets File \""
			<< m_save_file << "\""
			<< " - Saved " << m_save_count << " Packets."
			<< std::endl;
		m_save_out.close();
	}

	// Dump Any Skipped Packet Count...
	if ( m_skip_pkts.size() ) {
		std::cerr << "SKIPPED " << m_skip_count << " Packets."
			<< std::endl;
	}
}

int main(int argc, char **argv)
{
	MungeParser mp;
	mp.parse(argc, argv);
	return 0;
}

