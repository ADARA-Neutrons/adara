#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "ADARAUtils.h"

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x3000000  // For Old "Direct" PixelMap Pkt!

#include <boost/program_options.hpp>

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

static const char *markerType(ADARA::MarkerType::Enum type)
{
	switch (type) {
	case ADARA::MarkerType::GENERIC:
		return "Generic";
	case ADARA::MarkerType::SCAN_START:
		return "Scan Start";
	case ADARA::MarkerType::SCAN_STOP:
		return "Scan Stop";
	case ADARA::MarkerType::PAUSE:
		return "Pause";
	case ADARA::MarkerType::RESUME:
		return "Resume";
	case ADARA::MarkerType::OVERALL_RUN_COMMENT:
		return "Overall Comment";
	case ADARA::MarkerType::SYSTEM:
		return "System";
	}

	return "UndefinedType";
}

class Parser : public ADARA::POSIXParser {
public:
	Parser() :
		ADARA::POSIXParser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
		m_hexDump(false), m_wordDump(false), m_showEvents(false),
		m_showVars(true), m_showMults(false), m_showDDP(false),
		m_lowRate(false), m_showRunInfo(false), m_showGeom(false),
		m_showFrame(false), m_showPixMap(false), 
		m_posixRead(false), m_terse(false), m_catch(false)
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

	bool rxPacket(const ADARA::RawDataPkt &pkt);
	bool rxPacket(const ADARA::MappedDataPkt &pkt);

	bool handleDataPkt(const ADARA::RawDataPkt *pkt, bool is_mapped);

	bool rxPacket(const ADARA::RTDLPkt &pkt);
	bool rxPacket(const ADARA::BankedEventPkt &pkt);
	bool rxPacket(const ADARA::BankedEventStatePkt &pkt);
	bool rxPacket(const ADARA::BeamMonitorPkt &pkt);
	bool rxPacket(const ADARA::PixelMappingPkt &pkt);
	bool rxPacket(const ADARA::PixelMappingAltPkt &pkt);
	bool rxPacket(const ADARA::RunStatusPkt &pkt);
	bool rxPacket(const ADARA::RunInfoPkt &pkt);
	bool rxPacket(const ADARA::TransCompletePkt &pkt);
	bool rxPacket(const ADARA::ClientHelloPkt &pkt);
	bool rxPacket(const ADARA::AnnotationPkt &pkt);
	bool rxPacket(const ADARA::SyncPkt &pkt);
	bool rxPacket(const ADARA::HeartbeatPkt &pkt);
	bool rxPacket(const ADARA::GeometryPkt &pkt);
	bool rxPacket(const ADARA::BeamlineInfoPkt &pkt);
	bool rxPacket(const ADARA::BeamMonitorConfigPkt &pkt);
	bool rxPacket(const ADARA::DetectorBankSetsPkt &pkt);
	bool rxPacket(const ADARA::DataDonePkt &pkt);
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

	using ADARA::POSIXParser::rxPacket;

private:
	bool m_hexDump;
	bool m_wordDump;
	bool m_showEvents;
	bool m_showVars;
	bool m_showMults;
	bool m_showDDP;
	bool m_lowRate;
	bool m_showRunInfo;
	bool m_showGeom;
	bool m_showFrame;
	bool m_showPixMap;
	bool m_posixRead;
	bool m_terse;
	bool m_catch;
};

bool Parser::rxPacket(const ADARA::Packet &pkt)
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

	if ( m_hexDump ) {
		const uint8_t *p = pkt.packet();
		uint32_t addr;

		for (addr = 0; addr < pkt.packet_length(); addr++, p++) {
			if ((addr % 16) == 0)
				printf("%s%04x:", addr ? "\n" : "", addr);
			printf(" %02x", *p);
		}
		printf("\n");
	}

	if ( m_wordDump ) {
		const uint32_t *p = (const uint32_t *) pkt.packet();
		uint32_t addr;

		for (addr = 0; addr < pkt.packet_length(); addr += 4, p++)
			printf("%04x: %08x\n", addr, *p);
	}

	return ret;
}

bool Parser::rxUnknownPkt(const ADARA::Packet &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u Unknown Packet\n",
			(uint32_t) (pkt.pulseId() >> 32),
			(uint32_t) pkt.pulseId());
		printf("    type %08x len %u\n", pkt.type(), pkt.packet_length());
	}

	return false;
}

bool Parser::rxOversizePkt(const ADARA::PacketHeader *hdr,
				const uint8_t *UNUSED(chunk),
				unsigned int UNUSED(chunk_offset),
				unsigned int chunk_len)
{
	if ( !m_terse ) {
		// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
		if (hdr) {
			printf("%u.%09u Oversize Packet\n",
				(uint32_t) (hdr->pulseId() >> 32),
				(uint32_t) hdr->pulseId());
			printf("    type %08x len %u\n",
				hdr->type(), hdr->packet_length());
		}
		else {
			printf("[No Header, Continuation...] Oversize Packet\n");
			printf("    chunk_len %u\n", chunk_len);
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RawDataPkt &pkt)
{
	return( handleDataPkt(&pkt, false) );
}

bool Parser::rxPacket(const ADARA::MappedDataPkt &pkt)
{
	return( handleDataPkt(dynamic_cast<const ADARA::RawDataPkt *>(&pkt),
		true) );
}

bool Parser::handleDataPkt(const ADARA::RawDataPkt *pkt, bool is_mapped)
{
	if ( !m_terse || m_showEvents ) {
		printf("%u.%09u %s EVENT DATA (0x%x,v%u)\n"
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
			(uint64_t) pkt->pulseCharge() * 10, pkt->num_events());
	}

	if ( m_showEvents ) {
		uint32_t len = pkt->payload_length();
		uint32_t *p = (uint32_t *) pkt->payload();
		uint32_t tof, i = 0;
		double s;

		/* Skip the header we handled above */
		p += 6;
		len -= 6 * sizeof(uint32_t);

		while (len) {
			if (len < 8) {
				fprintf(stderr, "%s Event packet too short\n",
					is_mapped ? "Mapped" : "Raw");
				return true;
			}

			/* Metadata TOF values have a cycle indicator in
			 * the upper 11 bits (really 10, but there is
			 * currently an unused bit at 31).
			 */
			tof = p[0];
			if ( (p[1] & 0x40000000) || (p[1] & 0x70000000) )
				tof &= ~0xffe00000;
			s = 1e-9 * 100 * tof;
			printf("\t  %u: %08x %08x    (%0.7f seconds)\n",
				i++, p[0], p[1], s);
			p += 2;
			len -= 8;
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RTDLPkt &pkt)
{
	if ( !m_terse || m_showFrame ) {
		printf("%u.%09u RTDL (0x%x,v%u) [%u bytes]\n"
			"    cycle %u%s vetoFlags 0x%x%s timing 0x%x\n"
			"    dataFlags=%s 0x%x (%s)\n"
			"    flavor %d (%s)\n"
			"    intrapulse %luns tofOffset %luns%s\n"
			"    charge %lupC ringPeriod %ups\n",
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

	// display FNA/Frame Data fields...
	if ( m_showFrame )
	{
		for ( uint32_t i=0 ; i < 25 ; i++ )
		{
			printf("    FNA%u %u FrameData %u\n",
				i, pkt.FNA(i), pkt.frameData(i));
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BankedEventPkt &pkt)
{
	if ( !m_terse || m_showEvents ) {
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

	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();
	uint32_t nBanks, nEvents;

	/* Skip the header we handled above */
	p += 4;
	len -= 4 * sizeof(uint32_t);

	while (len) {

		if (len < 16) {
			fprintf(stderr, "Banked Event packet too short "
					"(source section header)\n");
			return true;
		}

		if ( !m_terse || m_showEvents ) {
			printf("    Source %08x nBanks %u intrapulse %luns "
				"tofOffset %luns%s\n", p[0], p[3],
				(uint64_t) p[1] * 100,
				((uint64_t) p[2] & 0x7fffffff) * 100,
				(p[2] & 0x80000000) ? "" : " (raw)");
		}

		nBanks = p[3];
		p += 4;
		len -= 16;

		for (uint32_t i = 0; i < nBanks; i++) {

			if (len < 8) {
				fprintf(stderr, "Banked Event packet "
					"too short (bank section "
					"header)\n");
				return true;
			}

			if ( !m_terse || m_showEvents ) {
				printf("\tBank 0x%x (%u events)\n", p[0], p[1]);
			}

			nEvents = p[1];
			p += 2;
			len -= 8;

			if (len < (nEvents * 2 * sizeof(uint32_t))) {
				fprintf(stderr, "Banked Event packet "
					"too short (events)\n");
				return true;
			}

			if ( m_showEvents ) {
				for (uint32_t j = 0; j < nEvents; j++) {
					printf("\t  %u: %08x %08x"
						"    (%0.7f seconds)\n",
						j, p[0], p[1],
						1e-9 * 100 * p[0]);
					p += 2;
					len -= 8;
				}
			}
			else {
				p += 2 * nEvents;
				len -= 8 * nEvents;
			}
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BankedEventStatePkt &pkt)
{
	if ( !m_terse || m_showEvents ) {
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

	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();
	uint32_t nBanks, nEvents;

	/* Skip the header we handled above */
	p += 4;
	len -= 4 * sizeof(uint32_t);

	while (len) {

		if (len < 16) {
			fprintf(stderr, "Banked Event State packet too short "
					"(source section header)\n");
			return true;
		}

		if ( !m_terse || m_showEvents ) {
			printf("    Source %08x nBanks %u intrapulse %luns "
				"tofOffset %luns%s\n", p[0], p[3],
				(uint64_t) p[1] * 100,
				((uint64_t) p[2] & 0x7fffffff) * 100,
				(p[2] & 0x80000000) ? "" : " (raw)");
		}

		nBanks = p[3];
		p += 4;
		len -= 16;

		for (uint32_t i = 0; i < nBanks; i++) {

			if (len < 12) {
				fprintf(stderr, "Banked event packet "
					"too short (bank section "
					"header)\n");
				return true;
			}

			if ( !m_terse || m_showEvents ) {
				printf("\tBank 0x%x State 0x%x (%u events)\n",
					p[0], p[1], p[2]);
			}

			nEvents = p[2];
			p += 3;
			len -= 12;

			if (len < (nEvents * 2 * sizeof(uint32_t))) {
				fprintf(stderr, "Banked event packet "
					"too short (events)\n");
				return true;
			}

			if ( m_showEvents ) {
				for (uint32_t j = 0; j < nEvents; j++) {
					printf("\t  %u: %08x %08x"
						"    (%0.7f seconds)\n",
						j, p[0], p[1],
						1e-9 * 100 * p[0]);
					p += 2;
					len -= 8;
				}
			}
			else {
				p += 2 * nEvents;
				len -= 8 * nEvents;
			}
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
	if ( !m_terse || m_showEvents ) {
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

	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();
	uint32_t nEvents;

	/* Skip the header we handled above */
	p += 4;
	len -= 4 * sizeof(uint32_t);

	while (len) {
		if (len < 12) {
			fprintf(stderr, "Beam monitor event packet "
					"too short (monitor header)\n");
			return true;
		}

		if ( !m_terse || m_showEvents ) {
			printf("    Monitor %u source %08x "
				"tofOffset %luns%s\n", p[0] >> 22, p[1],
				((uint64_t) p[2] & 0x7fffffff) * 100,
				(p[2] & 0x80000000) ? "" : " (raw)");
		}

		nEvents = p[0] & ((1 << 22) - 1);
		p += 3;
		len -= 12;

		if (len < (nEvents * sizeof(uint32_t))) {
			fprintf(stderr, "Beam monitor event packet "
					"too short (events)\n");
			return true;
		}

		if ( m_showEvents ) {
			for (uint32_t i = 0; i < nEvents; p++, i++) {
				printf("\t  %u: %0.7f seconds cycle %d%s\n", i,
					1e-9 * 100 * (*p & ((1U << 21) - 1)),
					(*p & ~(1U << 31)) >> 21,
					(*p & (1U << 31)) ? "" : " (trailing)");
			}
		}
		else {
			p += nEvents;
		}

		len -= nEvents * sizeof(uint32_t);
	}

	return false;
}

// Pixel Mapping Special Banks
enum SpecialBankIds
{
	UNMAPPED_BANK   = 0xffffffff,
	ERROR_BANK      = 0xfffffffe
};

bool Parser::rxPacket(const ADARA::PixelMappingAltPkt &pkt)
{
	if ( !m_terse || m_showPixMap ) {
		printf("%u.%09u PIXEL MAP TABLE ALT (0x%x,v%u) [%u bytes]\n"
			"    numBanks %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.numBanks());
	}

	if ( m_showPixMap )
	{
		const uint32_t *rpos = (const uint32_t *) pkt.mappingData();
		const uint32_t *epos = (const uint32_t *)
			( pkt.mappingData() + pkt.payload_length()
				- sizeof(uint32_t) );

		int32_t physical_start, physical_stop;
		int32_t physical_step;

		int32_t logical_start, logical_stop;
		int32_t logical_step;

		uint32_t log1, log2;

		uint16_t bank_id;
		uint16_t is_shorthand;
		uint16_t pixel_count;

		uint32_t skip_pixel_count = 0;
		uint32_t tot_pixel_count = 0;
		uint32_t skip_sections = 0;
		uint32_t section_count = 0;

		int16_t cnt;

		while ( rpos < epos )
		{
			// Base/Starting Physical PixelId
			physical_start = *rpos++;

			// BankID, 0/1=Direct/Shorthand Bit, Pixel Count...
			bank_id = (uint16_t)(*rpos >> 16);
			is_shorthand = (uint16_t)((*rpos & 0x8000) >> 15);
			pixel_count = (uint16_t)(*rpos & 0x7FFF);
			printf("    0x%08x:",  *rpos);
			rpos++;

			printf(" %s=%u/0x%x %s=%u %s=%u %s=%u\n",
				"phys_start", physical_start, physical_start,
				"bank", bank_id,
				"shorthand", is_shorthand,
				"pix_count", pixel_count);

			// Process Shorthand PixelId Section...
			if ( is_shorthand )
			{
				// Stopping Physical PixelId
				physical_stop = *rpos++;

				// Base/Starting Logical PixelId
				logical_start = *rpos++;

				// Physical and Logical Step
				physical_step = (int16_t)(*rpos >> 16);
				logical_step = (int16_t)(*rpos & 0xFFFF);
				rpos++;

				// Stopping Logical PixelId
				logical_stop = *rpos++;

				printf("        %s %s=%d/%d/%d %s=%d/%d/%d\n",
					"Shorthand Section",
					"physical",
					physical_start, physical_stop, physical_step,
					"logical",
					logical_start, logical_stop, logical_step);

                // Check for Pixel Start/Stop/Step Sanity...!
                if ( physical_step == 0 || logical_step == 0 )
                {
                    printf("    %s %s=%d/%d/%d %s=%d/%d/%d:\n",
						"WHOA! Erroneous PixelId Sequence!",
                        "physical",
                        physical_start, physical_stop, physical_step,
                        "logical",
                        logical_start, logical_stop, logical_step);
					printf("        %s - %s\n",
                        "Zero Step Size in Shorthand Sequence",
                        "Bail on Packet Parse...!");

                    return false;
                }

				// Verify Physical PixelId Count Versus Section Count...
				cnt = ( physical_stop - physical_start + physical_step )
					/ physical_step;
				if ( cnt != (int32_t) pixel_count )
				{
					printf("            %s: %d != %d - %s\n",
						"Physical PixelId Count Mismatch",
						cnt, pixel_count,
						"Skip Section...");

					// Next Section
					skip_pixel_count += pixel_count;
					skip_sections++;
					continue;
				}

				// Verify Logical PixelId Count Versus Section Count...
				cnt = ( logical_stop - logical_start + logical_step )
					/ logical_step;
				if ( cnt != (int32_t) pixel_count )
				{
					printf("            %s: %d != %d - %s\n",
						"Logical PixelId Count Mismatch",
						cnt, pixel_count,
						"Skip Section...");

					// Next Section
					skip_pixel_count += pixel_count;
					skip_sections++;
					continue;
				}

				// Note: Assume Physical & Logical Counts Match Each Other
				// If They Both Match the Same Internal Section Count ;-D

				// Skip Unmapped Sections of Pixel Map...!
				if ( bank_id == (uint16_t) UNMAPPED_BANK )
				{
					printf("            UNMAPPED Bank\n");

					// Next Section
					skip_pixel_count += pixel_count;
					skip_sections++;
					continue;
				}

				tot_pixel_count += pixel_count;
			}

			// Process Direct PixelId Section...
			else
			{
				// Skip Unmapped Sections of Pixel Map...!
				if ( bank_id == (uint16_t) UNMAPPED_BANK )
				{
					printf("            UNMAPPED Bank\n");

					// Next Section
					skip_pixel_count += pixel_count;
					rpos += pixel_count;
					skip_sections++;
					continue;
				}

				log1 = *rpos++;
				if ( pixel_count > 1 )
				{
					// Advance to "Last" Logical PixelId...
					rpos += pixel_count - 2;
					log2 = *rpos++;
					printf("        logical=%u/0x%x . . . %u/0x%x\n",
						log1, log1, log2, log2);
				}
				else
				{
					printf("        logical=%u/0x%x\n", log1, log1);
				}

				tot_pixel_count += pixel_count;
			}

			section_count++;
		}

		printf("    %s, %s Tot=%u Skip=%u, Sections Used=%u Skip=%u\n",
			"Done with Packet", "PixelIds", 
			tot_pixel_count, skip_pixel_count,
			section_count, skip_sections);
	}

	return false;
}

bool Parser::rxPacket(const ADARA::PixelMappingPkt &pkt)
{
	if ( !m_terse || m_showPixMap ) {
		printf("%u.%09u PIXEL MAP TABLE (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
	}

	if ( m_showPixMap )
	{
		const uint32_t *rpos = (const uint32_t *) pkt.mappingData();
		const uint32_t *epos = (const uint32_t *)
			( pkt.mappingData() + pkt.payload_length() );

		uint32_t        base_logical;
		uint16_t        bank_id;
		uint16_t        pixel_count;
		uint32_t        phys1, phys2;

		while ( rpos < epos )
		{
			base_logical = *rpos++;
			bank_id = (uint16_t)(*rpos >> 16);
			pixel_count = (uint16_t)(*rpos & 0xFFFF);
			rpos++;

			printf("    base_logical=%u/0x%x bank_id=%u pixel_count=%u\n",
				base_logical, base_logical, bank_id, pixel_count);

			if ( bank_id == (uint16_t) UNMAPPED_BANK )
			{
				printf("        UNMAPPED Bank\n");
				// Next Section
				rpos += pixel_count;
				continue;
			}

			else if ( pixel_count > 0 )
			{
				phys1 = *rpos++;
				if ( pixel_count > 1 )
				{
					// Advance to "Last" Physical PixelId...
					rpos += pixel_count - 2;
					phys2 = *rpos++;
					printf("        physical=%u/0x%x . . . %u/0x%x\n",
						phys1, phys1, phys2, phys2);
				}
				else
				{
					printf("        physical=%u/0x%x\n", phys1, phys1);
				}
			}
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RunStatusPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u RUN STATUS (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());

		switch (pkt.status()) {
		case ADARA::RunStatus::NO_RUN:
			printf("    No current run\n");
			break;
		case ADARA::RunStatus::STATE:
			printf("    State snapshot\n");
			break;
		case ADARA::RunStatus::NEW_RUN:
			printf("    New run\n");
			break;
		case ADARA::RunStatus::RUN_EOF:
			printf("    End of file (run continues)\n");
			break;
		case ADARA::RunStatus::RUN_BOF:
			printf("    Beginning of file (continuing run)\n");
			break;
		case ADARA::RunStatus::END_RUN:
			printf("    End of run\n");
			break;
		case ADARA::RunStatus::PROLOGUE:
			printf("    [PROLOGUE]\n");
			break;
		}

		if (pkt.runNumber()) {
			printf("    Run %u started at epoch %u\n",
				pkt.runNumber(), pkt.runStart());
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
					printf( "    %s %u, %s %u\n",
						"Mode Index", modeNum,
						"File Index", fileNum );
					printf( "    %s %u (%s=%u), %s %u (%s=%u)\n",
						"Pause Index", pauseFileNum,
						"paused", paused,
						"Addendum Index", addendumFileNum,
						"addendum", addendum );
				}
				else
				{
					printf( "    %s %u\n",
						"File Index", fileNum );
					printf( "    %s %u (%s=%u), %s %u (%s=%u)\n",
						"Pause Index", pauseFileNum,
						"paused", paused,
						"Addendum Index", addendumFileNum,
						"addendum", addendum );
				}
			}
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RunInfoPkt &pkt)
{
	if ( !m_terse || m_showRunInfo ) {
		// TODO display more fields (check that the contents do not change)
		printf("%u.%09u RUN INFO (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
	}

	if ( m_showRunInfo )
	{
		printf( "%s\n", pkt.info().c_str() );
	}

	return false;
}

bool Parser::rxPacket(const ADARA::TransCompletePkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u TRANSLATION COMPLETE (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		if (!pkt.status())
			printf("    Success");
		else if (pkt.status() < 0x8000)
			printf("    Transient failure");
		else
			printf("    Permament failure");
		if (pkt.reason().length())
			printf(", msg '%s'", pkt.reason().c_str());
		printf("\n");
	}

	return false;
}

bool Parser::rxPacket(const ADARA::ClientHelloPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u CLIENT HELLO (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		if (pkt.requestedStartTime()) {
			if (pkt.requestedStartTime() == 1)
				printf("    Request data from last run transition\n");
			else
				printf("    Request data from timestamp %u\n",
					pkt.requestedStartTime());
		} else
			printf("    Request data from current position\n");
		uint32_t clientFlags = pkt.clientFlags();
		printf("    Client Flags 0x%x [", clientFlags);
		if ( clientFlags & ADARA::ClientHelloPkt::SEND_PAUSE_DATA )
			printf("SEND_PAUSE_DATA");
		else if ( clientFlags & ADARA::ClientHelloPkt::NO_PAUSE_DATA )
			printf("NO_PAUSE_DATA");
		else
			printf("PAUSE_AGNOSTIC");
		printf("]\n");
	}

	return false;
}

bool Parser::rxPacket(const ADARA::AnnotationPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u STREAM ANNOTATION (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		printf("    Type %u (%s%s)\n",
			pkt.marker_type(), markerType(pkt.marker_type()),
			pkt.resetHint() ? ", Reset Hint" : "");
		if (pkt.scanIndex())
			printf("    Scan Index %u\n", pkt.scanIndex());
		const std::string &comment = pkt.comment();
		if (comment.length())
			printf("    Comment '%s'\n", comment.c_str());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::SyncPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u SYNC (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		printf("    Signature [%s], File Offset %lu, Comment [%s]\n",
			pkt.signature().c_str(), pkt.fileOffset(),
			pkt.comment().c_str() );
	}

	return false;
}

bool Parser::rxPacket(const ADARA::HeartbeatPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u HEARTBEAT (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::GeometryPkt &pkt)
{
	if ( !m_terse || m_showGeom ) {
		// TODO display more fields (check that the contents do not change)
		printf("%u.%09u GEOMETRY (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
	}

	if ( m_showGeom )
	{
		printf( "%s\n", pkt.info().c_str() );
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BeamlineInfoPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u BEAMLINE INFO (0x%x,v%u) [%u bytes]\n"
			"    target_station '%u' id '%s' short '%s' long '%s'\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.targetStationNumber(),
			pkt.id().c_str(),
			pkt.shortName().c_str(),
			pkt.longName().c_str());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BeamMonitorConfigPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u BEAM MONITOR CONFIG (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		printf("    num %u\n", pkt.beamMonCount());
		for (uint32_t i = 0; i < pkt.beamMonCount(); i++) {
			printf("    id %u %s %u %s %u %s %u %s %lf\n",
				pkt.bmonId(i),
				"tofOffset", pkt.tofOffset(i),
				"tofMax", pkt.tofMax(i),
				"tofBin", pkt.tofBin(i),
				"distance", pkt.distance(i));
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::DetectorBankSetsPkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u DETECTOR BANK SETS (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
		printf("    num %u\n", pkt.detBankSetCount());
		for (uint32_t i = 0; i < pkt.detBankSetCount(); i++) {
			printf("    name %s bankCount %u flags %u\n",
				pkt.name(i).c_str(), pkt.bankCount(i), pkt.flags(i));
			printf("        banklist [");
			const uint32_t *banklist = pkt.banklist(i);
			bool first = true;
			for (uint32_t b = 0; b < pkt.bankCount(i); b++) {
				if ( first ) first = false;
				else printf(",");
				printf("%u", banklist[b]);
			}
			printf("]\n");
			printf("        %s %u %s %u %s %u %s %lf %s %s\n",
				"tofOffset", pkt.tofOffset(i),
				"tofMax", pkt.tofMax(i),
				"tofBin", pkt.tofBin(i),
				"throttle", pkt.throttle(i),
				"suffix", pkt.suffix(i).c_str());
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::DataDonePkt &pkt)
{
	if ( !m_terse ) {
		printf("%u.%09u DATA DONE (0x%x,v%u) [%u bytes]\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
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

	return false;
}

bool Parser::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u U32 VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u DOUBLE VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value %lf\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u String VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Value '%s'\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.value().c_str());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::VariableU32ArrayPkt &pkt)
{
	uint32_t i;

	if ( !m_terse && m_showVars ) {
		printf("%u.%09u U32 ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount());
		for (i = 0; i < pkt.elemCount(); i++) {
			printf(" %u", pkt.value()[i]);
		}
		printf("\n");
	}

	return false;
}

bool Parser::rxPacket(const ADARA::VariableDoubleArrayPkt &pkt)
{
	uint32_t i;

	if ( !m_terse && m_showVars ) {
		printf("%u.%09u DOUBLE ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    Count %u Values",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.elemCount());
		for (i = 0; i < pkt.elemCount(); i++) {
			printf(" %lf", pkt.value()[i]);
		}
		printf("\n");
	}

	return false;
}

bool Parser::rxPacket(const ADARA::MultVariableU32Pkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u MULT U32 VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues());
		if ( m_showMults ) {
			double base_secs = (double) (pkt.pulseId() >> 32);
			double s;
			uint32_t nsecs = (uint32_t) pkt.pulseId();
			uint32_t i;
			uint32_t tof;
			for (i = 0; i < pkt.numValues(); i++) {
				tof = pkt.tofs()[i];
				s = base_secs + ((double)(nsecs + tof) / 1.0e9);
				printf("\t  %u: tof=%09u %08u    (%0.7f seconds)\n",
					i, tof, pkt.values()[i], s);
			}
			printf("\n");
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::MultVariableDoublePkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u MULT DOUBLE VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues());
		if ( m_showMults ) {
			double base_secs = (double) (pkt.pulseId() >> 32);
			double s;
			uint32_t nsecs = (uint32_t) pkt.pulseId();
			uint32_t i;
			uint32_t tof;
			for (i = 0; i < pkt.numValues(); i++) {
				tof = pkt.tofs()[i];
				s = base_secs + ((double)(nsecs + tof) / 1.0e9);
				printf("\t  %u: tof=%09u %0.7f    (%0.7f seconds)\n",
					i, tof, pkt.values()[i], s);
			}
			printf("\n");
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::MultVariableStringPkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u MULT String VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues());
		if ( m_showMults ) {
			double base_secs = (double) (pkt.pulseId() >> 32);
			double s;
			uint32_t nsecs = (uint32_t) pkt.pulseId();
			uint32_t i;
			uint32_t tof;
			for (i = 0; i < pkt.numValues(); i++) {
				tof = pkt.tofs()[i];
				s = base_secs + ((double)(nsecs + tof) / 1.0e9);
				printf("\t  %u: tof=%09u %s    (%0.7f seconds)\n",
					i, tof, pkt.values()[i].c_str(), s);
			}
			printf("\n");
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::MultVariableU32ArrayPkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u MULT U32 ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues());
		if ( m_showMults ) {
			double base_secs = (double) (pkt.pulseId() >> 32);
			double s;
			uint32_t nsecs = (uint32_t) pkt.pulseId();
			uint32_t i, j;
			uint32_t tof;
			for (i = 0; i < pkt.numValues(); i++) {
				tof = pkt.tofs()[i];
				s = base_secs + ((double)(nsecs + tof) / 1.0e9);
				printf("\t  %u: tof=%09u %08lu    (%0.7f seconds)\n",
					i, tof, pkt.values()[i].size(), s);
				for (j = 0; j < pkt.elemCount(i); j++) {
					printf("\t\t  %u:  %u\n", j, pkt.values()[i][j]);
				}
			}
			printf("\n");
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::MultVariableDoubleArrayPkt &pkt)
{
	if ( !m_terse && m_showVars ) {
		printf("%u.%09u MULT DOUBLE ARRAY VARIABLE (0x%x,v%u) [%u bytes]\n"
			"    Device %u Variable %u\n"
			"    Status %s Severity %s\n"
			"    numValues %u\n",
			(uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
			pkt.base_type(), pkt.version(), pkt.packet_length(),
			pkt.devId(), pkt.varId(), statusString(pkt.status()),
			severityString(pkt.severity()), pkt.numValues());
		if ( m_showMults ) {
			double base_secs = (double) (pkt.pulseId() >> 32);
			double s;
			uint32_t nsecs = (uint32_t) pkt.pulseId();
			uint32_t i, j;
			uint32_t tof;
			for (i = 0; i < pkt.numValues(); i++) {
				tof = pkt.tofs()[i];
				s = base_secs + ((double)(nsecs + tof) / 1.0e9);
				printf("\t  %u: tof=%09u %08lu    (%0.7f seconds)\n",
					i, tof, pkt.values()[i].size(), s);
				for (j = 0; j < pkt.elemCount(i); j++) {
					printf("\t\t  %u:  %0.7lf\n", j, pkt.values()[i][j]);
				}
			}
			printf("\n");
		}
	}

	return false;
}

void Parser::parse_file(FILE *f)
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

void Parser::parse_file(const std::string &name)
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

void Parser::read_file(int fd)
{
	std::string log_info;
	size_t len;

	std::cerr << "Using POSIX read()." << std::endl;

	// NOTE: This is POSIXParser::read()... ;-o
	while ( (len = read( fd, log_info )) );
}

void Parser::parse(int argc, char **argv)
{
	po::options_description opts("Allowed options");
	opts.add_options()
	("help,h", "Show usage information")
	("version,V", "Show version information")
	("hexdump,x", "Dump the contents of each packet in hex (bytes)")
	("worddump,w", "Dump the contents of each packet in hex (words)")
	("hidevars,H", "Hide variable update packets")
	("showmults,M", "Show multiple variable update packet values")
	("showddp,D", "Show payload of device descriptor packets")
	("low,l", "Set low data rate mode (uses very small buffer size)")
	("events,e", "Show events")
	("showrun,R", "Show payload of RunInfo packets")
	("showgeom,G", "Show payload of Geometry packets")
	("showframe,F", "Show FNA/Frame Data of RTDL packets")
	("showpixmap,X", "Show Pixel Mapping packet details")
	("posixread,P", "Use POSIX read() to parse incoming stream")
	("terse,T", "Terse Mode, Produce no output (except as requested)")
	("catch,C", "Catch Exceptions, Try to parse past bad packets");

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
	} catch (po::unknown_option &e) {
		std::cerr << argv[0] << ": " << e.what() << std::endl
			<< std::endl << opts << std::endl;
		exit(2);
	}

	if (vm.count("help")) {
		std::cerr << std::endl << "ADARA Parser Tool"
			<< " - ADARA Common Version " << ADARA::VERSION
			<< ", Tag Name " << ADARA::TAG_NAME
			<< std::endl << std::endl;
		std::cerr << opts << std::endl;
		exit(2);
	}

	if (vm.count("version")) {
		std::cerr << std::endl << "ADARA Parser Tool"
			<< " - ADARA Common Version " << ADARA::VERSION
			<< ", Tag Name " << ADARA::TAG_NAME
			<< std::endl << std::endl;
		exit(0);
	}

	m_hexDump = !!vm.count("hexdump");
	m_wordDump = !!vm.count("worddump");
	m_showEvents = !!vm.count("events");
	m_showVars = !vm.count("hidevars");
	m_showMults = vm.count("showmults");
	m_showDDP = vm.count("showddp");
	m_lowRate = vm.count("low");
	m_showRunInfo = vm.count("showrun");
	m_showGeom = vm.count("showgeom");
	m_showFrame = vm.count("showframe");
	m_showPixMap = vm.count("showpixmap");
	m_posixRead = vm.count("posixread");
	m_terse = vm.count("terse");
	m_catch = vm.count("catch");

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
	Parser p;
	p.parse(argc, argv);
	return 0;
}
