#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "ADARAParser.h"

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
		return "Target 1 Normal";
	case ADARA::PulseFlavor::NORMAL_TGT_2:
		return "Target 2 Normal";
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
	}

	return "UndefinedType";
}

class Parser : public ADARA::Parser {
public:
	Parser() :
		m_hexDump(false), m_wordDump(false), m_showEvents(false),
        m_showVars(true), m_showDDP(false)
	{ }

	void parse(int argc, char **argv);
	void parse_file(FILE *);
	void parse_file(const std::string &);

	bool rxUnknownPkt(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr,
			   const uint8_t *chunk,
			   unsigned int chunk_offset,
			   unsigned int chunk_len);

	bool rxPacket(const ADARA::Packet &pkt);
	bool rxPacket(const ADARA::RawDataPkt &pkt);
	bool rxPacket(const ADARA::RTDLPkt &pkt);
	bool rxPacket(const ADARA::BankedEventPkt &pkt);
	bool rxPacket(const ADARA::BeamMonitorPkt &pkt);
	bool rxPacket(const ADARA::PixelMappingPkt &pkt);
	bool rxPacket(const ADARA::RunStatusPkt &pkt);
	bool rxPacket(const ADARA::RunInfoPkt &pkt);
	bool rxPacket(const ADARA::TransCompletePkt &pkt);
	bool rxPacket(const ADARA::ClientHelloPkt &pkt);
	bool rxPacket(const ADARA::AnnotationPkt &pkt);
	bool rxPacket(const ADARA::SyncPkt &pkt);
	bool rxPacket(const ADARA::HeartbeatPkt &pkt);
	bool rxPacket(const ADARA::GeometryPkt &pkt);
	bool rxPacket(const ADARA::BeamlineInfoPkt &pkt);
	bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);

	using ADARA::Parser::rxPacket;

private:
	bool m_hexDump;
	bool m_wordDump;
	bool m_showEvents;
	bool m_showVars;
    bool m_showDDP;
};

bool Parser::rxPacket(const ADARA::Packet &pkt)
{
	bool ret = ADARA::Parser::rxPacket(pkt);

	if (m_hexDump) {
		const uint8_t *p = pkt.packet();
		uint32_t addr;

		for (addr = 0; addr < pkt.packet_length(); addr++, p++) {
			if ((addr % 16) == 0)
				printf("%s%04x:", addr ? "\n" : "", addr);
			printf(" %02x", *p);
		}
		printf("\n");
	}

	if (m_wordDump) {
		const uint32_t *p = (const uint32_t *) pkt.packet();
		uint32_t addr;

		for (addr = 0; addr < pkt.packet_length(); addr += 4, p++)
			printf("%04x: %08x\n", addr, *p);
	}

	return ret;
}

bool Parser::rxUnknownPkt(const ADARA::Packet &pkt)
{
	printf("%u.%09u Unknown Packet\n", (uint32_t) (pkt.pulseId() >> 32),
		(uint32_t) pkt.pulseId());
	printf("    type %08x len %u\n", pkt.type(), pkt.packet_length());

	return false;
}

bool Parser::rxOversizePkt(const ADARA::PacketHeader *hdr,
			   const uint8_t *chunk,
			   unsigned int chunk_offset,
			   unsigned int chunk_len)
{
	if (hdr) {
		printf("%u.%09u Oversize Packet\n",
		       (uint32_t) (hdr->pulseId() >> 32),
		       (uint32_t) hdr->pulseId());
		printf("    type %08x len %u\n", hdr->type(),
		       hdr->packet_length());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RawDataPkt &pkt)
{
	printf("%u.%09u RAW EVENT DATA\n"
	       "    srcId 0x%08x pktSeq 0x%x dspSeq 0x%x%s\n"
	       "    cycle %u%s veto 0x%x%s timing 0x%x flavor %d (%s)\n"
	       "    intrapulse %luns tofOffset %luns%s\n"
	       "    charge %lupC, %u events\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.sourceID(), pkt.pktSeq(), pkt.dspSeq(),
	       pkt.endOfPulse() ? " EOP" : "",
	       pkt.cycle(), pkt.badCycle() ? " (BAD)" : "",
	       pkt.veto(), pkt.badVeto() ? " (BAD)" : "",
	       pkt.timingStatus(), (int) pkt.flavor(),
	       pulseFlavor(pkt.flavor()), (uint64_t) pkt.intraPulseTime() * 100,
	       (uint64_t) pkt.tofOffset() * 100,
	       pkt.tofCorrected() ? "" : " (raw)",
	       (uint64_t) pkt.pulseCharge() * 10, pkt.num_events());

	if (m_showEvents) {
		uint32_t len = pkt.payload_length();
		uint32_t *p = (uint32_t *) pkt.payload();
		uint32_t tof, i = 0;
		double s;

		/* Skip the header we handled above */
		p += 6;
		len -= 6 * sizeof(uint32_t);

		while (len) {
			if (len < 8) {
				fprintf(stderr, "Raw event packet too short\n");
				return true;
			}

			/* Metadata TOF values have a cycle indicator in
			 * the upper 11 bits (really 10, but there is
			 * currently an unused bit at 31).
			 */
			tof = p[0];
			if (p[1] & 0x70000000)
				tof &= ~0xffc00000;
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
	// TODO display FNA X fields
	printf("%u.%09u RTDL\n"
	       "    cycle %u%s veto 0x%x%s timing 0x%x flavor %d (%s)\n"
	       "    intrapulse %luns tofOffset %luns%s\n"
	       "    charge %lupC period %ups\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.cycle(), pkt.badCycle() ? " (BAD)" : "",
	       pkt.veto(), pkt.badVeto() ? " (BAD)" : "",
	       pkt.timingStatus(), (int) pkt.flavor(),
	       pulseFlavor(pkt.flavor()), (uint64_t) pkt.intraPulseTime() * 100,
	       (uint64_t) pkt.tofOffset() * 100,
	       pkt.tofCorrected() ? "" : " (raw)",
	       (uint64_t) pkt.pulseCharge() * 10, pkt.ringPeriod());

	return false;
}

bool Parser::rxPacket(const ADARA::BankedEventPkt &pkt)
{
	printf("%u.%09u BANKED EVENT DATA\n"
	       "    cycle %u charge %lupC energy %ueV\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
	       pkt.pulseEnergy());
	if (pkt.flags()) {
		printf("    flags");
		if (pkt.flags() & ADARA::BankedEventPkt::ERROR_PIXELS)
			printf(" ERROR");
		if (pkt.flags() & ADARA::BankedEventPkt::PARTIAL_DATA)
			printf(" PARTIAL");
		if (pkt.flags() & ADARA::BankedEventPkt::PULSE_VETO)
			printf(" VETO");
		if (pkt.flags() & ADARA::BankedEventPkt::MISSING_RTDL)
			printf(" NO_RTDL");
		if (pkt.flags() & ADARA::BankedEventPkt::MAPPING_ERROR)
			printf(" MAPPING");
		if (pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE)
			printf(" DUP_PULSE");
		printf("\n");
	}

	if (m_showEvents) {
		uint32_t len = pkt.payload_length();
		uint32_t *p = (uint32_t *) pkt.payload();
		uint32_t nBanks, nEvents;

		/* Skip the header we handled above */
		p += 4;
		len -= 4 * sizeof(uint32_t);

		while (len) {
			if (len < 16) {
				fprintf(stderr, "Banked event packet too short "
						"(source section header)\n");
				return true;
			}

			printf("    Source %08x intrapulse %luns "
			       "tofOffset %luns%s\n", p[0],
			       (uint64_t) p[1] * 100,
			       ((uint64_t) p[2] & 0x7fffffff) * 100,
			       (p[2] & 0x80000000) ? "" : " (raw)");
			nBanks = p[3];
			p += 4;
			len -= 16;

			for (uint32_t i = 0; i < nBanks; i++) {
				if (len < 8) {
					fprintf(stderr, "Banked event packet "
						"too short (bank section "
						"header)\n");
					return true;
				}

				printf("\tBank 0x%x (%u events)\n", p[0], p[1]);
				nEvents = p[1];
				p += 2;
				len -= 8;

				if (len < (nEvents * 2 * sizeof(uint32_t))) {
					fprintf(stderr, "Banked event packet "
						"too short (events)\n");
					return true;
				}

				for (uint32_t j = 0; j < nEvents; j++) {
					printf("\t  %u: %08x %08x"
					       "    (%0.7f seconds)\n",
					       j, p[0], p[1],
					       1e-9 * 100 * p[0]);
					p += 2;
					len -= 8;
				}
			}
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
	printf("%u.%09u BEAM MONITOR DATA\n"
	       "    cycle %u charge %lupC energy %ueV\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
	       pkt.pulseEnergy());
	if (pkt.flags()) {
		printf("    flags");
		if (pkt.flags() & ADARA::BankedEventPkt::ERROR_PIXELS)
			printf(" ERROR");
		if (pkt.flags() & ADARA::BankedEventPkt::PARTIAL_DATA)
			printf(" PARTIAL");
		if (pkt.flags() & ADARA::BankedEventPkt::PULSE_VETO)
			printf(" VETO");
		if (pkt.flags() & ADARA::BankedEventPkt::MISSING_RTDL)
			printf(" NO_RTDL");
		if (pkt.flags() & ADARA::BankedEventPkt::MAPPING_ERROR)
			printf(" MAPPING");
		if (pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE)
			printf(" DUP_PULSE");
		printf("\n");
	}

	if (m_showEvents) {
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

			printf("    Monitor %u source %08x "
			       "tofOffset %luns%s\n", p[0] >> 22, p[1],
			       ((uint64_t) p[2] & 0x7fffffff) * 100,
			       (p[2] & 0x80000000) ? "" : " (raw)");
			nEvents = p[0] & ((1 << 22) - 1);
			p += 3;
			len -= 12;

			if (len < (nEvents * sizeof(uint32_t))) {
				fprintf(stderr, "Beam monitor event packet "
						"too short (events)\n");
				return true;
			}

			for (uint32_t i = 0; i < nEvents; p++, i++) {
				printf("\t  %u: %0.7f seconds cycle %d%s\n", i,
				       1e-9 * 100 * (*p & ((1U << 21) - 1)),
				       (*p & ~(1U << 31)) >> 21,
				       (*p & (1U << 31)) ? "" : " (trailing)");
			}

			len -= nEvents * sizeof(uint32_t);
		}
	}

	return false;
}

bool Parser::rxPacket(const ADARA::PixelMappingPkt &pkt)
{
	// TODO display more fields (check that the table doesn't change)
	printf("%u.%09u PIXEL MAP TABLE\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	return false;
}

bool Parser::rxPacket(const ADARA::RunStatusPkt &pkt)
{
	printf("%u.%09u RUN STATUS\n",(uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
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
	}

	if (pkt.runNumber()) {
		printf("    Run %u started at epoch %u\n",
		       pkt.runNumber(), pkt.runStart());
		if (pkt.status() != ADARA::RunStatus::STATE)
			printf("    File index %u\n", pkt.fileNumber());
	}

	return false;
}

bool Parser::rxPacket(const ADARA::RunInfoPkt &pkt)
{
	// TODO display more fields (check that the contents do not change)
	printf("%u.%09u RUN INFO\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	return false;
}

bool Parser::rxPacket(const ADARA::TransCompletePkt &pkt)
{
	printf("%u.%09u TRANSLATION COMPLETE\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId());
	if (!pkt.status())
		printf("    Success");
	else if (pkt.status() < 0x8000)
		printf("    Transient failure");
	else
		printf("    Permament failure");
	if (pkt.reason().length())
		printf(", msg '%s'", pkt.reason().c_str());
	printf("\n");

	return false;
}

bool Parser::rxPacket(const ADARA::ClientHelloPkt &pkt)
{
	printf("%u.%09u CLIENT HELLO\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	if (pkt.requestedStartTime()) {
		if (pkt.requestedStartTime() == 1)
			printf("    Request data from last run transition\n");
		else
			printf("    Request data from timestamp %u\n",
			       pkt.requestedStartTime());
	} else
		printf("     Request data from current position\n");

	return false;
}

bool Parser::rxPacket(const ADARA::AnnotationPkt &pkt)
{
	printf("%u.%09u STREAM ANNOTATION\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	printf("    Type %u (%s%s)\n", pkt.type(), markerType(pkt.type()),
	       pkt.resetHint() ? ", Reset Hint" : "");
	if (pkt.scanIndex())
		printf("    Scan Index %u\n", pkt.scanIndex());
	const std::string &comment = pkt.comment();
	if (comment.length())
		printf("    Comment '%s'\n", comment.c_str());

	return false;
}

bool Parser::rxPacket(const ADARA::SyncPkt &pkt)
{
	// TODO display more fields
	printf("%u.%09u SYNC\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	return false;
}

bool Parser::rxPacket(const ADARA::HeartbeatPkt &pkt)
{
	printf("%u.%09u HEARTBEAT\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	return false;
}

bool Parser::rxPacket(const ADARA::GeometryPkt &pkt)
{
	// TODO display more fields (check that the contents do not change)
	printf("%u.%09u GEOMETRY\n", (uint32_t) (pkt.pulseId() >> 32),
	       (uint32_t) pkt.pulseId());
	return false;
}

bool Parser::rxPacket(const ADARA::BeamlineInfoPkt &pkt)
{
	printf("%u.%09u BEAMLINE INFO\n"
	       "    id '%s' short '%s' long '%s'\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.id().c_str(), pkt.shortName().c_str(),
	       pkt.longName().c_str());
	return false;
}

bool Parser::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	// TODO display more fields (check that the contents don't change)
	printf("%u.%09u DEVICE DESCRIPTOR\n"
	       "    Device %u\n",
	       (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
	       pkt.devId());

    if ( m_showDDP )
    {
        printf( "%s\n", pkt.description().c_str() );
    }

	return false;
}

bool Parser::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	if (m_showVars) {
		printf("%u.%09u U32 VARIABLE\n"
		       "    Device %u Variable %u\n"
		       "    Status %s Severity %s\n"
		       "    Value %u\n",
		       (uint32_t) (pkt.pulseId() >> 32),
		       (uint32_t) pkt.pulseId(),
		       pkt.devId(), pkt.varId(), statusString(pkt.status()),
		       severityString(pkt.severity()), pkt.value());
	}
	return false;
}

bool Parser::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	if (m_showVars) {
		printf("%u.%09u DOUBLE VARIABLE\n"
		       "    Device %u Variable %u\n"
		       "    Status %s Severity %s\n"
		       "    Value %f\n",
		       (uint32_t) (pkt.pulseId() >> 32),
		       (uint32_t) pkt.pulseId(),
		       pkt.devId(), pkt.varId(), statusString(pkt.status()),
		       severityString(pkt.severity()), pkt.value());
	}
	return false;
}

bool Parser::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	if (m_showVars) {
		printf("%u.%09u String VARIABLE\n"
		       "    Device %u Variable %u\n"
		       "    Status %s Severity %s\n"
		       "    Value '%s'\n",
		       (uint32_t) (pkt.pulseId() >> 32),
		       (uint32_t) pkt.pulseId(),
		       pkt.devId(), pkt.varId(), statusString(pkt.status()),
		       severityString(pkt.severity()), pkt.value().c_str());
	}
	return false;
}

void Parser::parse_file(FILE *f)
{
	size_t len;

	while (!feof(f)) {
		len = bufferFillLength();
		len = fread(bufferFillAddress(), 1, len, f);
		if (!len) {
			if (feof(f))
				return;
			throw std::string("read error");
		}

		bufferBytesAppended((unsigned int) len);
		if (bufferParse(0) < 0)
			throw std::string("parse error");
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
		parse_file(f);
		fclose(f);
	} catch (std::string m) {
		fclose(f);

		std::string msg(name);
		msg += ": ";
		msg += m;
		throw msg;
	}
}

void Parser::parse(int argc, char **argv)
{
        po::options_description opts("Allowed options");
        opts.add_options()
		("help,h", "Show usage information")
		("hexdump,x", "Dump the contents of each packet in hex (bytes)")
		("worddump,w", "Dump the contents of each packet in hex (words)")
		("hidevars,H", "Hide variable update packets")
    ("showddp,D", "Show payload of device descriptor packets")
        ("events,e", "Show events");

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

	m_hexDump = !!vm.count("hexdump");
	m_wordDump = !!vm.count("worddump");
	m_showEvents = !!vm.count("events");
    m_showVars = !vm.count("hidevars");
    m_showDDP = vm.count("showddp");

	if (!vm.count("file")) {
		try {
			parse_file(stdin);
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
