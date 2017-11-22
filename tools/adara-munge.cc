#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "ADARAUtils.h"

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x1000000

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

class MungeParser : public ADARA::POSIXParser {
public:
	MungeParser() :
		ADARA::POSIXParser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
		m_posixRead(false), m_lowRate(false),
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
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);
	bool rxPacket(const ADARA::VariableU32ArrayPkt &pkt);
	bool rxPacket(const ADARA::VariableDoubleArrayPkt &pkt);

	using ADARA::POSIXParser::rxPacket;

private:
	bool m_posixRead;
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
	// - correct timing by 677.790806607 seconds...
	if ( pkt.devId() == 2 )
	{
		std::cerr << "*** Found FitSam Device (2) Double Variable Update!"
			<< std::endl;
		uint32_t sec = (uint32_t) (pkt.pulseId() >> 32);
		uint32_t nsec = (uint32_t) pkt.pulseId();
		sec += 677;
		nsec += 790806607;
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
		po::options_description opts("Allowed options");
		opts.add_options()
		("help,h", "Show usage information")
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
		} catch (po::unknown_option e) {
			std::cerr << argv[0] << ": " << e.what() << std::endl
				<< std::endl << opts << std::endl;
			exit(2);
		}

		if (vm.count("help")) {
			std::cerr << opts << std::endl;
			exit(2);
		}

	m_posixRead = vm.count("posixread");
	m_lowRate = vm.count("low");
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
	std::cerr << "Entry ADARA Munge" << std::endl;
	MungeParser mp;
	std::cerr << "AAA" << std::endl;
	mp.parse(argc, argv);
	std::cerr << "BBB" << std::endl;
	return 0;
}

