#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "ADARAUtils.h"

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x3000000  // For PixelMap!

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
		m_starttime_sec(0), m_starttime_nsec(0),
		m_endtime_sec(0), m_endtime_nsec(0),
		m_case(0), m_posixRead(false), m_showDDP(false), m_lowRate(false),
		m_terse(false), m_catch(false),
		m_out(std::cout)
	{ }

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

	using ADARA::POSIXParser::rxPacket;

private:
	uint32_t m_descriptor_count[100];
	uint32_t m_starttime_sec;
	uint32_t m_starttime_nsec;
	uint32_t m_endtime_sec;
	uint32_t m_endtime_nsec;
	uint32_t m_case;
	bool m_posixRead;
	bool m_showDDP;
	bool m_lowRate;
	bool m_terse;
	bool m_catch;

	std::ostream &m_out;
};

bool MungeParser::rxPacket(const ADARA::Packet &pkt)
{
	bool ret = false;

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

	// Pass Packet Through to Output Stream...
	m_out.write( (const char *)pkt.packet(), pkt.packet_length() );

	return ret;
}

bool MungeParser::rxUnknownPkt(const ADARA::Packet &pkt)
{
	if ( !m_terse ) {
		fprintf(stderr,"%u.%09u Unknown Packet\n",
			(uint32_t) (pkt.pulseId() >> 32),
			(uint32_t) pkt.pulseId());
		fprintf(stderr,"    type %08x len %u\n",
			pkt.type(), pkt.packet_length());
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
			fprintf(stderr,"%u.%09u Oversize Packet\n",
				(uint32_t) (hdr->pulseId() >> 32),
				(uint32_t) hdr->pulseId());
			fprintf(stderr,"    type %08x len %u\n",
				hdr->type(), hdr->packet_length());
		}
		else {
			fprintf(stderr,
				"[No Header, Continuation...] Oversize Packet\n");
			fprintf(stderr,"    chunk_len %u\n", chunk_len);
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
		printf("%u.%09u %s EVENT DATA (0x%x,v%u)\n"
			"    srcId 0x%08x pktSeq 0x%x dspSeq 0x%x%s\n"
			"    cycle %u%s vetoFlags 0x%x%s timing 0x%x\n"
			"    dataFlags=%s 0x%x (%s)\n"
			"    flavor %d (%s)\n"
			"    intrapulse %luns tofOffset %luns%s\n"
			"    charge %lupC, %u events\n",
			(uint32_t) (pkt->pulseId() >> 32), (uint32_t) pkt->pulseId(),
			is_mapped ? "MAPPED" : "RAW",
			pkt->base_type(), pkt->version(),
			pkt->sourceID(), pkt->pktSeq(), pkt->dspSeq(),
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
			(uint64_t) pkt->pulseCharge() * 10, pkt->num_events());
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime_sec = (uint32_t) (pkt->pulseId() >> 32);
	m_endtime_nsec = (uint32_t) pkt->pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::RTDLPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u RTDL (0x%x,v%u) [%u bytes]\n"
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
			(uint64_t) pkt.pulseCharge() * 10, pkt.ringPeriod());
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BankedEventPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u BANKED EVENT DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags());
		if (pkt.flags()) {
			printf("    flags");
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				printf(" ERROR");
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				printf(" PARTIAL");
			if (pkt.flags() & ADARA::PULSE_VETO)
				printf(" VETO");
			if (pkt.flags() & ADARA::MISSING_RTDL)
				printf(" NO_RTDL");
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				printf(" MAPPING");
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				printf(" DUP_PULSE");
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				printf(" PCHG_UNCOR");
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				printf(" VETO_UNCOR");
			if (pkt.flags() & ADARA::GOT_METADATA)
				printf(" GOT_METADATA");
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				printf(" GOT_NEUTRONS");
			printf("\n");
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BankedEventStatePkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u BANKED EVENT STATE DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags());
		if (pkt.flags()) {
			printf("    flags");
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				printf(" ERROR");
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				printf(" PARTIAL");
			if (pkt.flags() & ADARA::PULSE_VETO)
				printf(" VETO");
			if (pkt.flags() & ADARA::MISSING_RTDL)
				printf(" NO_RTDL");
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				printf(" MAPPING");
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				printf(" DUP_PULSE");
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				printf(" PCHG_UNCOR");
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				printf(" VETO_UNCOR");
			if (pkt.flags() & ADARA::GOT_METADATA)
				printf(" GOT_METADATA");
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				printf(" GOT_NEUTRONS");
			if (pkt.flags() & ADARA::HAS_STATES)
				printf(" HAS_STATES");
			printf("\n");
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u BEAM MONITOR DATA (0x%x,v%u) [%u bytes]\n"
			"    cycle %u charge %lupC energy %ueV vetoFlags 0x%x\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
			pkt.pulseEnergy(), pkt.vetoFlags());
		if (pkt.flags()) {
			printf("    flags");
			if (pkt.flags() & ADARA::ERROR_PIXELS)
				printf(" ERROR");
			if (pkt.flags() & ADARA::PARTIAL_DATA)
				printf(" PARTIAL");
			if (pkt.flags() & ADARA::PULSE_VETO)
				printf(" VETO");
			if (pkt.flags() & ADARA::MISSING_RTDL)
				printf(" NO_RTDL");
			if (pkt.flags() & ADARA::MAPPING_ERROR)
				printf(" MAPPING");
			if (pkt.flags() & ADARA::DUPLICATE_PULSE)
				printf(" DUP_PULSE");
			if (pkt.flags() & ADARA::PCHARGE_UNCORRECTED)
				printf(" PCHG_UNCOR");
			if (pkt.flags() & ADARA::VETO_UNCORRECTED)
				printf(" VETO_UNCOR");
			if (pkt.flags() & ADARA::GOT_METADATA)
				printf(" GOT_METADATA");
			if (pkt.flags() & ADARA::GOT_NEUTRONS)
				printf(" GOT_NEUTRONS");
			printf("\n");
		}
	}

	// Save Last Packet Time as Potential "End of Run" Timestamp...
	m_endtime_sec = (uint32_t) (pkt.pulseId() >> 32);
	m_endtime_nsec = (uint32_t) pkt.pulseId();

	return false;
}

bool MungeParser::rxPacket(const ADARA::RunStatusPkt &pkt)
{
	if ( !m_terse ) {
		fprintf(stderr,"%u.%09u RUN STATUS (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());

		switch (pkt.status()) {
		case ADARA::RunStatus::NO_RUN:
			fprintf(stderr,"    No current run\n");
			break;
		case ADARA::RunStatus::STATE:
			fprintf(stderr,"    State snapshot\n");
			break;
		case ADARA::RunStatus::NEW_RUN:
			fprintf(stderr,"    New run\n");
			break;
		case ADARA::RunStatus::RUN_EOF:
			fprintf(stderr,"    End of file (run continues)\n");
			break;
		case ADARA::RunStatus::RUN_BOF:
			fprintf(stderr,"    Beginning of file (continuing run)\n");
			break;
		case ADARA::RunStatus::END_RUN:
			fprintf(stderr,"    End of run\n");
			break;
		}

		if (pkt.runNumber()) {
			fprintf(stderr,"    Run %u started at epoch %u\n",
				pkt.runNumber(), pkt.runStart());
			if (pkt.status() != ADARA::RunStatus::STATE)
				fprintf(stderr,"    File index %u\n", pkt.fileNumber());
#if 0
			if (pkt.version() == 0x01) {
				printf("    Paused 0x%x Pause File index %u\n",
					pkt.paused(), pkt.pauseFileNumber());
				printf("    Addendum 0x%x Addendum File index %u\n",
					pkt.addendum(), pkt.addendumFileNumber());
			}
#endif
		}
	}

#ifdef REF_M_HYSTERICAL
	// REF_M "Hysterical" Replay Tweaks to Run Status
	// - Run Start Time and Run End Time Must Match Historical Data...!
	if ( m_starttime_sec > 0 && m_starttime_nsec > 0 )
	{
		if ( pkt.status() == ADARA::RunStatus::NEW_RUN )
		{
			std::cerr << "*** Found Run Status - New Run...!"
				<< std::endl;
			uint64_t newPulseId;
			newPulseId = ((uint64_t) m_starttime_sec) << 32;
			newPulseId |= m_starttime_nsec;
			ADARA::RunStatusPkt *PKT =
				const_cast<ADARA::RunStatusPkt*>(&pkt);
			PKT->setPulseId( newPulseId );
			PKT->setRunStart( m_starttime_sec );
		}
		else if ( pkt.status() == ADARA::RunStatus::END_RUN )
		{
			std::cerr << "*** Found Run Status - End of Run...!"
				<< std::endl;
			uint64_t newPulseId;
			newPulseId = ((uint64_t) m_endtime_sec) << 32;
			newPulseId |= m_endtime_nsec;
			ADARA::RunStatusPkt *PKT =
				const_cast<ADARA::RunStatusPkt*>(&pkt);
			PKT->setPulseId( newPulseId );
			// *Always* Set to Run Start Time! ;-D
			PKT->setRunStart( m_starttime_sec );
		}
	}
#endif

	return false;
}

bool MungeParser::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	if ( !m_terse || m_showDDP ) {
		// TODO display more fields (check that the contents don't change)
		printf("%u.%09u DEVICE DESCRIPTOR (0x%x,v%u) [%u bytes]\n"
			"    Device %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId());
	}

	if ( m_showDDP )
	{
		printf( "%s\n", pkt.description().c_str() );
	}

	//
	// Evil Device Id Re-Numbering Issue (beamline.xml Changed Mid-Run!)
	//

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
		fprintf(stderr,"%u.%09u U32 VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value());
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
		fprintf(stderr,"%u.%09u DOUBLE VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %lf\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value());
	}

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
		if ( nsec > NANO_PER_SECOND_LL )
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

	return false;
}

bool MungeParser::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	if ( !m_terse ) {
		fprintf(stderr,"%u.%09u String VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value '%s'\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value().c_str());
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
		fprintf(stderr,"%u.%09u U32 ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount());
		for (i = 0; i < pkt.elemCount(); i++) {
			fprintf(stderr," %u", pkt.value()[i]);
		}
		fprintf(stderr,"\n");
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
		fprintf(stderr,
			"%u.%09u DOUBLE ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount());
		for (i = 0; i < pkt.elemCount(); i++) {
			fprintf(stderr," %lf", pkt.value()[i]);
		}
		fprintf(stderr,"\n");
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
	std::string m_starttime;

		po::options_description opts("Allowed options");
		opts.add_options()
		("help,h", "Show usage information")
		("posixread,P", "Use POSIX read() to parse incoming stream")
		("terse,T", "Terse Mode, Produce no output (except as requested)")
		("catch,C", "Catch Exceptions, Try to parse past bad packets")
		("starttime", po::value<std::string>(&m_starttime),
			"Hysterical Start Time for Experiment Data")
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

	if ( m_starttime.size() ) {
		std::cerr << "Run Start Time Requested as: "
			<< m_starttime << std::endl;
		size_t dot = m_starttime.find(".");
		if ( dot != std::string::npos ) {
			m_starttime_sec = boost::lexical_cast<uint32_t>(
				m_starttime.substr(0, dot) );
			m_starttime_nsec = boost::lexical_cast<uint32_t>(
				m_starttime.substr(dot + 1) );
			std::cerr << "Run Start Time"
				<< " -> sec=" << m_starttime_sec
				<< " nsec=" << m_starttime_nsec << std::endl;
		}
	}

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
}

int main(int argc, char **argv)
{
	MungeParser mp;
	mp.parse(argc, argv);
	return 0;
}

