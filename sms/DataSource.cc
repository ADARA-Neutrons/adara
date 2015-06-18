
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string>
#include <sstream>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#include <boost/bind.hpp>
#include <stdexcept>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "DataSource.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

#include "utils.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DataSource"));

RateLimitedLogging::History RLLHistory_DataSource;

// Rate-Limited Logging IDs...
#define RLL_DROPPED_PACKETS           0
#define RLL_LOCAL_DUPLICATE_PULSE     1
#define RLL_LOCAL_SAWTOOTH_PULSE      2
#define RLL_RAWDATA_PULSE_IN_PAST     3
#define RLL_RAWDATA_PULSE_IN_FUTURE   4
#define RLL_LOCAL_PACKET_SEQUENCE     5
#define RLL_WONT_CONN                 6
#define RLL_TRYING_CONN               7
#define RLL_CONN_REFUSED              8
#define RLL_CONN_REQUEST_ERROR        9
#define RLL_CONN_FAILED              10
#define RLL_READ_EXCEPTION           11
#define RLL_READ_DELAY               12
#define RLL_PULSEID_ZERO             13
#define RLL_UNKNOWN_PACKET           14
#define RLL_OVERSIZE_PACKET          15
#define RLL_LOCAL_DUPLICATE_RTDL     16
#define RLL_LOCAL_SAWTOOTH_RTDL      17
#define RLL_LOCAL_RTDL_SEQUENCE      18
#define RLL_RTDL_PULSE_IN_PAST       19
#define RLL_RTDL_PULSE_IN_FUTURE     20
#define RLL_HEARTBEAT                21

// Pulse Time Sanity Check Constants
#define FACILITY_START_TIME 512715600 // EPICS Sat Apr  1 00:00:00 EST 2006
#define SECS_PER_WEEK 604800 // 60 * 60 * 24 * 7

class HWSource {
public:
	HWSource(const std::string &name, int32_t hwIndex,
		uint32_t hwId, uint32_t smsId,
		boost::shared_ptr<smsUint32PV> & pvHWSourceHwId,
		boost::shared_ptr<smsUint32PV> & pvHWSourceSmsId,
		boost::shared_ptr<smsUint32PV> & pvHWSourceEventBandwidthSecond,
		boost::shared_ptr<smsUint32PV> & pvHWSourceEventBandwidthMinute,
		boost::shared_ptr<smsUint32PV> & pvHWSourceEventBandwidthTenMin ) :
		m_hwIndex(hwIndex),
		m_pvHWSourceHwId(pvHWSourceHwId),
		m_pvHWSourceSmsId(pvHWSourceSmsId),
		m_pvHWSourceEventBandwidthSecond(pvHWSourceEventBandwidthSecond),
		m_pvHWSourceEventBandwidthMinute(pvHWSourceEventBandwidthMinute),
		m_pvHWSourceEventBandwidthTenMin(pvHWSourceEventBandwidthTenMin),
		m_name(name), m_hwId(hwId), m_smsId(smsId),
		m_activePulse(0), m_lastPulse(0), m_dupCount(0), m_pulseGood(true),
		m_trueNew(true)
	{
		// Initialize "RTDL Packets with No Data Packets" Count...
		m_rtdlNoDataCount = 0;

		// Initialize Event Bandwidth Statistics
		m_event_count_second = 0;
		m_event_count_minute = 0;
		m_event_count_tenmin = 0;

		// Initialize HWSource Bandwidth PVs...
		if ( m_hwIndex >= 0 ) {
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			m_pvHWSourceHwId->update(m_hwId, &now);
			m_pvHWSourceSmsId->update(m_smsId, &now);
			m_pvHWSourceEventBandwidthSecond->update(
				m_event_count_second, &now);
			m_pvHWSourceEventBandwidthMinute->update(
				m_event_count_minute, &now);
			m_pvHWSourceEventBandwidthTenMin->update(
				m_event_count_tenmin, &now);
		}
	}

	uint64_t pulse(void) const { return m_activePulse; }
	uint64_t lastPulse(void) const { return m_lastPulse; }
	uint32_t dupCount(void) const { return m_dupCount; }
	uint32_t hwId(void) const { return m_hwId; }
	uint32_t smsId(void) const { return m_smsId; }

	void endPulse(bool eop = true) {

		if (!m_activePulse)
			return;

		SMSControl *ctrl = SMSControl::getInstance();

		if (!eop) {
#if 0
			/* We currently get this on every other pulse for
			 * 30 Hz operation; disable the message until
			 * we get a better fix.
			 */
			/* Rate-limited logging of dropped packets */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_DROPPED_PACKETS, m_name, 2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "Dropped packet from " << m_name << " src 0x"
					<< std::hex << m_hwId << std::dec);
			}
#endif
			if ( m_pulseGood )
				ctrl->markPartial(m_activePulse, m_dupCount);

			m_trueNew = false;
		}
		else {
			m_trueNew = true;
		}

		if ( m_pulseGood )
			ctrl->markComplete(m_activePulse, m_dupCount, m_smsId);

		m_lastPulse = m_activePulse;
		m_activePulse = 0;
	}

	bool newPulse(const ADARA::RawDataPkt &pkt) {

		if (m_activePulse) {
			/* We didn't see an end-of-packet, so clear out
			 * the previous pulse here.
			 */
			endPulse(false);
		}

		m_activePulse = pkt.pulseId();

		m_pulseGood = true;

		// check for Duplicate Pulses in event packets...
		if (m_activePulse == m_lastPulse) {
			/* Rate-limited logging of duplicate pulses */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_DUPLICATE_PULSE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "newPulse(RawDataPkt): Local Duplicate pulse from "
					<< m_name
					<< std::hex << " src=0x" << m_hwId
					<< " pulseId=0x" << m_activePulse << std::dec
					<< " trueNew=" << m_trueNew);
			}
			dumpPulseInvariants(pkt);
			m_dupCount++;
		} else
			m_dupCount = 0;

		// also check for SAWTOOTH Pulse Times in event packets...
		if (m_activePulse < m_lastPulse) {
			/* Rate-limited logging of local sawtooth pulses */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_SAWTOOTH_PULSE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "newPulse(RawDataPkt): Local SAWTOOTH RawData"
					<< " from " << m_name
					<< std::hex << " m_lastPulse=" << m_lastPulse
					<< " m_activePulse=" << m_activePulse << std::dec
					<< " cycle=" << pkt.cycle()
					<< " vetoFlags=" << pkt.vetoFlags());
			}
		}

		// strip off pulse nanoseconds...
		time_t sec = m_activePulse >> 32;
		// check for "totally bogus" pulses, in distant past/future... ;-b
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		time_t future = 
			now.tv_sec - ADARA::EPICS_EPOCH_OFFSET + SECS_PER_WEEK;
		// before SNS time began... ;-D
		if ( sec < FACILITY_START_TIME )
		{
			/* Rate-limited logging of Bogus Pulses from Distant Past! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_RAWDATA_PULSE_IN_PAST, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "*** Dropping Bogus RawData Pulse Time"
					<< " from Distant Past (Before Facility Start Time)!"
					<< std::hex << " pulseId=0x"
						<< m_activePulse << std::dec
					<< " (" << sec << " < " << FACILITY_START_TIME << ")"
					<< " (" << m_name << ")");
			}
			m_pulseGood = false;
		}
		// more than a week into the future...! :-o
		else if ( sec > future )
		{
			/* Rate-limited logging of Bogus Pulses from Distant Future! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_RAWDATA_PULSE_IN_FUTURE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "*** Dropping Bogus RawData Pulse Time"
					<< " from Distant Future (Over One Week from Now)!"
					<< std::hex << " pulseId=0x"
						<< m_activePulse << std::dec
					<< " (" << sec << " > " << future << ")"
					<< " (" << m_name << ")");
			}
			m_pulseGood = false;
		}

		m_flavor = pkt.flavor();
		m_intraPulse = pkt.intraPulseTime();
		m_charge = pkt.pulseCharge();
		m_cycle = pkt.cycle();
		m_vetoFlags = pkt.vetoFlags();
		m_timingStatus = pkt.timingStatus();
		m_pktSeq = 0;

		return( m_pulseGood );
	}

	bool checkPulseInvariants(const ADARA::RawDataPkt &pkt) {
		/* These fields should not change for any packet in
		 * the current pulse. If they do, then this is a
		 * duplicate pulse id (or a new pulse all together.)
		 */
		return !(pkt.pulseId() == m_activePulse &&
			 pkt.flavor() == m_flavor &&
			 pkt.pulseCharge() == m_charge &&
			 pkt.vetoFlags() == m_vetoFlags &&
			 pkt.cycle() == m_cycle &&
			 pkt.timingStatus() == m_timingStatus &&
			 pkt.intraPulseTime() == m_intraPulse);
	}

	bool pulseGood() {
		return( m_pulseGood );
	}

	void dumpPulseInvariants(const ADARA::RawDataPkt &pkt) {
		ERROR("dumpPulseInvariants():"
			<< std::hex << " pulseId=0x" << pkt.pulseId()
				<< "(0x" << m_activePulse << ")" << std::dec
			<< " flavor=" << pkt.flavor() << "(" << m_flavor << ")"
			<< " pulseCharge=" << pkt.pulseCharge()
				<< "(" << m_charge << ")"
			<< " vetoFlags=" << pkt.vetoFlags()
				<< "(" << m_vetoFlags << ")"
			<< " cycle=" << pkt.cycle() << "(" << m_cycle << ")"
			<< " timingStatus=" << (uint32_t) pkt.timingStatus()
				<< "(" << (uint32_t) m_timingStatus << ")"
			<< " intraPulseTime=" << pkt.intraPulseTime()
				<< "(" << m_intraPulse << ")");
	}

	bool checkSeq(const ADARA::RawDataPkt &pkt) {
		bool ok = (pkt.pktSeq() == m_pktSeq);
		/* if ( !ok ) {
			// Rate-limited logging of packet sequence out-of-order?
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_PACKET_SEQUENCE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< "checkSeq() Local Packet Sequence Out-of-Order: "
					<< pkt.pktSeq() << " != " << m_pktSeq
					<< std::hex << " m_activePulse=0x" << m_activePulse
					<< " hwId=0x" << m_hwId << std::dec);
			}
		} */
		m_pktSeq++;
		return !ok;
	}

	// "RTDL Packets with No Data Packets" Count
	uint32_t	m_rtdlNoDataCount;

	// HWSource PV Index from Parent DataSource Class
	int32_t		m_hwIndex;

	// Event Bandwidth Statistics
	uint32_t	m_event_count_second;
	uint32_t	m_event_count_minute;
	uint32_t	m_event_count_tenmin;

	boost::shared_ptr<smsUint32PV> m_pvHWSourceHwId;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceSmsId;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthTenMin;

private:
	const std::string &m_name;

	uint32_t	m_hwId;
	uint32_t	m_smsId;

	uint64_t	m_activePulse;
	uint64_t	m_lastPulse;
	uint32_t	m_dupCount;
	uint16_t	m_pktSeq;
	bool		m_pulseGood;

	/* Pulse invariants -- these should not change between raw event
	 * packets for a given pulse, so we can use them to help detect
	 * duplicate pulse IDs
	 */
	ADARA::PulseFlavor::Enum m_flavor;
	uint32_t	m_intraPulse;
	uint32_t	m_charge;
	uint16_t	m_cycle;
	uint16_t	m_vetoFlags;
	uint8_t		m_timingStatus;
	bool		m_trueNew;
};


DataSource::DataSource(const std::string &name, bool enabled,
			const std::string &uri, uint32_t id,
			double connect_retry, double connect_timeout,
			double data_timeout, bool ignore_eop,
			unsigned int read_chunk, uint32_t rtdlNoDataThresh) :
	m_name(uri), m_basename(name), m_uri(uri),
	m_fdreg(NULL), m_timer(NULL), m_addrinfo(NULL),
	m_state(DISABLED), m_smsSourceId(id), m_fd(-1),
	m_connect_retry(connect_retry), m_connect_timeout(connect_timeout),
	m_data_timeout(data_timeout), m_ignore_eop(ignore_eop),
	m_max_read_chunk(read_chunk), m_rtdlNoDataThresh(rtdlNoDataThresh)
{
	m_name += " (";
	m_name += m_basename;
	m_name += ")";

	m_enabled = enabled;

	parseURI(uri);

	// Initialize Pulse/Event Bandwidth Statistics

	m_last_second = -1;
	m_pulse_count_second = 0;
	m_event_count_second = 0;
	m_last_minute = -1;
	m_pulse_count_minute = 0;
	m_event_count_minute = 0;
	m_last_tenmin = -1;
	m_pulse_count_tenmin = 0;
	m_event_count_tenmin = 0;

	// Create Run-Time Status and Configuration PVs Per Data Source...

	SMSControl *ctrl = SMSControl::getInstance();

	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS";
	prefix += ":DataSource:";

	std::stringstream ss;
	ss << m_smsSourceId;
	prefix += ss.str();

	m_pvName = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":Name"));

	m_pvDataURI = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":DataURI"));

	m_pvEnabled = boost::shared_ptr<smsEnabledPV>(new
		smsEnabledPV(prefix + ":Enabled", this));

	m_pvConnected = boost::shared_ptr<smsConnectedPV>(new
		smsConnectedPV(prefix + ":Connected"));

	m_pvConnectRetryTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectRetryTimeout", 0.0));

	m_pvConnectTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectTimeout", 0.0));

	m_pvDataTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":DataTimeout", 0.0));

	m_pvIgnoreEoP = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":IgnoreEoP"));

	m_pvMaxReadChunk = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":MaxReadChunk"));

	m_pvRTDLNoDataThresh = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":RTDLNoDataThresh"));

	m_pvPulseBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthSecond"));

	m_pvEventBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthSecond"));

	m_pvPulseBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthMinute"));

	m_pvEventBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthMinute"));

	m_pvPulseBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthTenMin"));

	m_pvEventBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthTenMin"));

	m_pvNumHWSources = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":NumHWSources"));

	ctrl->addPV(m_pvName);
	ctrl->addPV(m_pvDataURI);
	ctrl->addPV(m_pvEnabled);
	ctrl->addPV(m_pvConnected);
	ctrl->addPV(m_pvConnectRetryTimeout);
	ctrl->addPV(m_pvConnectTimeout);
	ctrl->addPV(m_pvDataTimeout);
	ctrl->addPV(m_pvIgnoreEoP);
	ctrl->addPV(m_pvMaxReadChunk);
	ctrl->addPV(m_pvRTDLNoDataThresh);

	ctrl->addPV(m_pvPulseBandwidthSecond);
	ctrl->addPV(m_pvEventBandwidthSecond);
	ctrl->addPV(m_pvPulseBandwidthMinute);
	ctrl->addPV(m_pvEventBandwidthMinute);
	ctrl->addPV(m_pvPulseBandwidthTenMin);
	ctrl->addPV(m_pvEventBandwidthTenMin);

	ctrl->addPV(m_pvNumHWSources);

	// Initialize Data Source PVs...
	// (All except "Enabled"!  Save that for later... :-)

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	m_pvName->update(m_name, &now);
	m_pvDataURI->update(uri, &now);
	m_pvConnected->disconnected();
	m_pvConnectRetryTimeout->update(m_connect_retry, &now);
	m_pvConnectTimeout->update(m_connect_timeout, &now);
	m_pvDataTimeout->update(m_data_timeout, &now);
	m_pvIgnoreEoP->update(m_ignore_eop, &now);
	m_pvRTDLNoDataThresh->update(m_rtdlNoDataThresh, &now);

	m_pvPulseBandwidthSecond->update(m_pulse_count_second, &now);
	m_pvEventBandwidthSecond->update(m_event_count_second, &now);
	m_pvPulseBandwidthMinute->update(m_pulse_count_minute, &now);
	m_pvEventBandwidthMinute->update(m_event_count_minute, &now);
	m_pvPulseBandwidthTenMin->update(m_pulse_count_tenmin, &now);
	m_pvEventBandwidthTenMin->update(m_event_count_tenmin, &now);

	m_pvNumHWSources->update(0, &now);

	// Initialize Max Read Chunk PV (construct string)...
	std::stringstream ssMRC;
	ssMRC << m_max_read_chunk;
	m_pvMaxReadChunk->update(ssMRC.str(), &now);

	// Set Up Data Source Connection Timer...
	try {
		m_timer = new TimerAdapter<DataSource>(this);
	}
	catch (std::exception &e) {
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
		std::string msg("Unable to Create TimerAdapter for Data Source ");
		msg += m_name;
		msg += ": Bailing! ";
		msg += e.what();
		ERROR("DataSource(): " << msg);
		throw std::runtime_error(msg);
	}
	catch (...) {
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
		std::string msg("Unable to Create TimerAdapter for Data Source ");
		msg += m_name;
		msg += ": Bailing! ";
		msg += "Unknown Exception.";
		ERROR("DataSource(): " << msg);
		throw std::runtime_error(msg);
	}

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;
	m_dupRTDL = 0;

	m_last_pkt_type = -1;
	m_last_pkt_len = -1;
	m_last_pkt_sec = -1;
	m_last_pkt_nsec = -1;

	m_rtdl_pkt_counts = -1;
	m_data_pkt_counts = -1;

	m_readDelay = false;

	// "Enabled" PV Update Triggers "startConnect()" when Enabled... :-D
	m_pvEnabled->update(m_enabled, &now);
}

DataSource::~DataSource()
{
	if ( m_addrinfo != NULL ) {
		freeaddrinfo(m_addrinfo);
		m_addrinfo = NULL;
	}
	delete m_timer;
	if (m_fdreg)
		delete m_fdreg;
	if (m_fd != -1)
		close(m_fd);
}

void DataSource::parseURI(std::string uri)
{
	std::string node;
	std::string service("31416");
	struct addrinfo hints;
	size_t pos = uri.find_first_of(':');
	int rc;

	// Extract Desired Service, if found...
	// Set Node to Any Remainder.
	if (pos != std::string::npos) {
		node = uri.substr(0, pos);
		if (pos != uri.length())
			service = uri.substr(pos + 1);
	} else
		node = uri;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_CANONNAME | AI_V4MAPPED;

	// Free Any Previous Address Info...
	if ( m_addrinfo != NULL ) {
		freeaddrinfo(m_addrinfo);
		m_addrinfo = NULL;
	}

	rc = getaddrinfo(node.c_str(), service.c_str(), &hints, &m_addrinfo);
	if (rc) {
		// *Don't* Throw Exception Here, Just Log Loudly & Thwart Connect!
		std::string msg("Unable to lookup data source ");
		msg += m_name;
		msg += ": ";
		msg += gai_strerror(rc);
		ERROR("parseURI(): " << msg << " - *** Won't Attempt Connect!");
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
	}
}

void DataSource::unregisterHWSources(bool isSourceDown, std::string why)
{
	/* Complete any outstanding pulses, and inform the manager
	 * of our change of status
	 */
	SMSControl *ctrl = SMSControl::getInstance();
	HWSrcMap::iterator it;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	for (it = m_hwSources.begin(); it != m_hwSources.end(); it++) {
		INFO("Unregistering Event Source " << it->second->smsId()
			<< " for " << why << " Data Source " << m_name);
		it->second->endPulse(false);
		// Reset HWSource PV Index Bit, Clear Out SMS Id PV Value...
		if ( it->second->m_hwIndex >= 0 ) {
			it->second->m_pvHWSourceSmsId->update(-1, &now);
			m_hwIndices.reset( it->second->m_hwIndex );
		}
		ctrl->unregisterEventSource(it->second->smsId());
	}

	m_hwSources.clear();

	// Update Number of HWSources PV...
	m_pvNumHWSources->update(0, &now);

	if (isSourceDown)
		ctrl->sourceDown(m_smsSourceId);
}

void DataSource::dumpLastReadStats(std::string who)
{
	INFO(who << ": " << m_name
		<< " Last Packet:"
		<< " type=0x" << std::hex << m_last_pkt_type << std::dec
		<< " sec=" << m_last_pkt_sec
		<< " nsec=" << m_last_pkt_nsec
		<< " len=" << m_last_pkt_len
		<< " last_bytes_read=" << Parser::last_bytes_read
		<< " last_pkts_parsed=" << Parser::last_pkts_parsed
		<< " last_total_bytes=" << Parser::last_total_bytes
		<< " last_total_packets=" << Parser::last_total_packets
		<< " last_read_count=" << Parser::last_read_count
		<< " last_loop_count=" << Parser::last_loop_count
		<< " last_elapsed=" << Parser::last_elapsed
		<< " last_read_elapsed=" << Parser::last_read_elapsed
		<< " last_read_elapsed_total=" << Parser::last_read_elapsed_total
		<< " last_parse_elapsed=" << Parser::last_parse_elapsed
		<< " last_parse_elapsed_total=" << Parser::last_parse_elapsed_total
		<< " last_last_bytes_read=" << Parser::last_last_bytes_read
		<< " last_last_pkts_parsed=" << Parser::last_last_pkts_parsed
		<< " last_last_total_bytes=" << Parser::last_last_total_bytes
		<< " last_last_total_packets=" << Parser::last_last_total_packets
		<< " last_last_read_count=" << Parser::last_last_read_count
		<< " last_last_loop_count=" << Parser::last_last_loop_count
		<< " last_last_elapsed=" << Parser::last_last_elapsed
		<< " last_last_read_elapsed=" << Parser::last_last_read_elapsed
		<< " last_last_read_elapsed_total="
			<< Parser::last_last_read_elapsed_total
		<< " last_last_parse_elapsed=" << Parser::last_last_parse_elapsed
		<< " last_last_parse_elapsed_total="
			<< Parser::last_last_parse_elapsed_total);
}

void DataSource::connectionFailed(bool dumpStats, bool dumpDiscarded,
		State new_state)
{
	m_timer->cancel();

	if (dumpStats)
		dumpLastReadStats("connectionFailed()");

	if (dumpDiscarded) {
		// Dump Discarded Packet Statistics...
		std::string log_info;
		Parser::getDiscardedPacketsLogString(log_info);
		INFO(log_info);
		// Reset Discarded Packet Statistics...
		Parser::resetDiscardedPacketsStats();
	}

	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd != -1) {
		close(m_fd);
		m_fd = -1;
	}

	m_state = new_state;

	/* Complete any outstanding pulse, and inform the manager of our
	 * failure
	 */
	unregisterHWSources(true, "Disconnected");

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;

	if (m_state != DISABLED) {
		// Update Connect Retry Timeout from PV...
		m_connect_retry = m_pvConnectRetryTimeout->value();
		m_timer->start(m_connect_retry);
	}
}

bool DataSource::timerExpired(void)
{
	// DEBUG("timerExpired() entry");

	switch (m_state) {

		case DISABLED:
		{
			WARN("Ignoring Timeout for Disabled Data Source " << m_name);
			if ( m_readDelay )
				m_readDelay = false; // reset flag set by SMSControl...
			break;
		}

		case IDLE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Connect Retry Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Connect Retry Timeout from PV...
				m_connect_retry = m_pvConnectRetryTimeout->value();
				m_timer->start(m_connect_retry);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				startConnect();
			}
			break;
		}

		case CONNECTING:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Connect Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Connect Timeout from PV...
				m_connect_timeout = m_pvConnectTimeout->value();
				m_timer->start(m_connect_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				// Leave m_pvConnected in its current state, latch failures
				WARN("Connection request timed out to " << m_name);
				connectionFailed(false, false, IDLE);
			}
			break;
		}

		case ACTIVE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Data Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Data Timeout from PV...
				m_data_timeout = m_pvDataTimeout->value();
				m_timer->start(m_data_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				WARN("Timed out waiting for data from " << m_name);
				m_pvConnected->failed();
				connectionFailed(true, true, IDLE);
			}
			break;
		}
	}

	// DEBUG("timerExpired() exit");

	return false;
}

void DataSource::fdReady(void)
{
	// DEBUG("fdReady() entry m_state=" << m_state);

	switch (m_state) {
		case DISABLED:
			WARN("Ignoring Data Ready for Disabled Data Source "
				<< m_name);
			break;
		case IDLE:
			ERROR("fdReady(): Invalid Idle State! Bailing...");
			throw std::logic_error("Invalid state");
		case CONNECTING:
			connectComplete();
			break;
		case ACTIVE:
			dataReady();
			break;
	}

	// DEBUG("fdReady() exit");
}

void DataSource::startConnect(void)
{
	std::string log_info;
	int flags, rc;

	/* Clear out any old state from the ADARA parser. */
	reset();

	// Update Data URI from PV...
	std::string uri = m_pvDataURI->value();
	if ( uri != m_uri ) {
		INFO("Setting New Data URI from PV: " << uri);
		m_uri = uri;
		// Regenerate DataSource Name...
		m_name = uri;
		m_name += " (";
		m_name += m_basename;
		m_name += ")";
		// Parse New URI...
		parseURI(uri);
		// Update DataSource Name PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvName->update(m_name, &now);
	}

	// Did the Address Lookup Succeed...?
	if ( m_addrinfo == NULL ) {
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_WONT_CONN, m_name,
				600, 3, 10, log_info ) ) {
			ERROR(log_info << "startConnect():"
				<< " Invalid Address Info/Lookup for Data Source "
				<< m_name << " - *** Won't Attempt to Connect...!");
		}
		goto error;
	}

	// Ready to Connect...
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_TRYING_CONN, m_name,
			600, 3, 10, log_info ) ) {
		INFO(log_info << "Trying connection to " << m_name);
	}
	m_pvConnected->trying_to_connect();

	m_fd = socket(m_addrinfo->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		ERROR("Error creating socket for " << m_name);
		m_fd = -1;   // just to be sure... ;-b
		goto error;
	}

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0) {
		ERROR("Error getting socket flags for " << m_name);
		goto error_fd;
	}
	if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK)) {
		ERROR("Error setting socket flags for " << m_name);
		goto error_fd;
	}

	rc = connect(m_fd, m_addrinfo->ai_addr, m_addrinfo->ai_addrlen);
	if (rc < 0)
		rc = errno;

	switch (rc) {
		case ECONNREFUSED:
			/* Rate-limited logging of refused connection */
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_CONN_REFUSED, m_name,
					600, 3, 10, log_info ) ) {
				WARN(log_info << "Connection refused by " << m_name);
			}
			goto error_fd;
		case EINTR:
		case EINPROGRESS:
			m_state = CONNECTING;
			break;
		case 0:
			INFO("Connection established to " << m_name);
			m_state = ACTIVE;
			m_pvConnected->connected();
			SMSControl::getInstance()->sourceUp(m_smsSourceId);
			break;
		default:
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_CONN_REQUEST_ERROR, m_name,
					600, 3, 10, log_info ) ) {
				WARN(log_info
					<< "Unknown connection request error for " << m_name
					<< ": " << strerror(rc) << " (Ignoring!)");
			}
	}

	/* TODO handle any other error here */

	try {
		/* We won't notice that the connect completed until we get
		 * the first packet from the source unless we look for the
		 * connection becoming writable.
		 */
		fdRegType type = (m_state == CONNECTING) ? fdrWrite : fdrRead;
		m_fdreg = new ReadyAdapter(m_fd, type,
				boost::bind(&DataSource::fdReady, this));
	} catch (std::bad_alloc e) {
		ERROR("Bad Alloc Error for " << m_name
			<< " adapter: " << e.what());
		goto error_fd;
	}

	// Update Connect Timeout from PV...
	m_connect_timeout = m_pvConnectTimeout->value();
	m_timer->start(m_connect_timeout);
	return;

error_fd:
	close(m_fd);
	m_fd = -1;

error:
	m_pvConnected->failed();
	connectionFailed(false, false, IDLE);
}

void DataSource::connectComplete(void)
{
	socklen_t elen = sizeof(int);
	int e, rc;

	rc = getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &e, &elen);
	if (!rc && !e) {
		delete m_fdreg;
		m_fdreg = new ReadyAdapter(m_fd, fdrRead,
				boost::bind(&DataSource::fdReady, this));

		m_timer->cancel();

		// Update Data Timeout from PV...
		m_data_timeout = m_pvDataTimeout->value();
		m_timer->start(m_data_timeout);

		m_state = ACTIVE;

		m_pvConnected->connected();

		SMSControl::getInstance()->sourceUp(m_smsSourceId);

		INFO("Connection established to " << m_name);
		return;
	}

	if (rc < 0)
		e = errno;

	if (e == EINTR || e == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		return;
	}

	/* Rate-limited logging of connection issue */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_CONN_FAILED, m_name, 600, 3, 10, log_info ) ) {
		WARN(log_info << "Connection request to " << m_name
			<< " failed: " << strerror(e));
	}
	// Leave m_pvConnected in its current state, latch failures
	connectionFailed(false, false, IDLE);
}

void DataSource::dataReady(void)
{
	std::string log_info;

	m_timer->cancel();

	// Update Data Timeout from PV...
	m_data_timeout = m_pvDataTimeout->value();
	m_timer->start(m_data_timeout);

	struct timespec readStart;
	clock_gettime(CLOCK_REALTIME, &readStart);

 	// reset read delayed flag, starting a new read now...
	SMSControl *ctrl = SMSControl::getInstance();
	ctrl->resetSourcesReadDelay();

	bool readOk = true;

	m_rtdl_pkt_counts = 0;
	m_data_pkt_counts = 0;

	// Update Max Read Chunk from PV...
	std::string val = m_pvMaxReadChunk->value();
	unsigned int tmp_max_read_chunk;
	try {
		tmp_max_read_chunk = parse_size(val);
	} catch (std::runtime_error e) {
		std::string msg("Unable to parse read size for source '");
		msg += m_name;
		msg += "': ";
		msg += e.what();
		// *Don't* Throw std::runtime_error(msg), Just Log Instead...
		ERROR("dataReady(): " << msg << " - Revert to Original"
			<< " m_max_read_chunk=" << m_max_read_chunk);
		// String parse failed, revert to original value...
		tmp_max_read_chunk = m_max_read_chunk;
	}
	if ( tmp_max_read_chunk != m_max_read_chunk ) {
		m_max_read_chunk = tmp_max_read_chunk;
		// Log the change...
		std::stringstream ssMRC;
		ssMRC << "Setting Max Read Chunk Size for " << m_name;
		ssMRC << " to ";
		ssMRC << m_max_read_chunk;
		ssMRC << " (" << val << ")";
		INFO(ssMRC.str());
	}

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		if (!read(m_fd, log_info, 4000, m_max_read_chunk)) {
			INFO("Connection closed with " << m_name
				<< " log_info=(" << log_info << ")");
			m_pvConnected->disconnected();
			connectionFailed(true, true, IDLE);
			readOk = false;
		}
	} catch (std::runtime_error e) {
		/* Rate-limited log of failure */
		std::string log_info;
		bool dumpStats = false;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_READ_EXCEPTION, m_name, 60, 5, 10, log_info ) ) {
			ERROR(log_info << "Exception reading from " << m_name
				<< ": " << e.what());
			dumpStats = true;
		}
		m_pvConnected->failed();
		connectionFailed(dumpStats, true, IDLE);
		readOk = false;
	}

	if ( readOk )
	{
		struct timespec readEnd;
		clock_gettime(CLOCK_REALTIME, &readEnd);

		double elapsed = calcDiffSeconds( readEnd, readStart );

 		// set read delayed flag...!
		if ( elapsed > 2.0 )
		{
			/* Rate-limited logging of read delay threshold */
			std::string log_info;
			bool dumpStats = false;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_READ_DELAY, m_name, 600, 10, 30, log_info ) ) {
				ERROR(log_info
					<< "dataReady(): Read Delay Threshold Exceeded"
					<< " elapsed=" << elapsed << " (" << m_name << ")"
					<< " log_info=(" << log_info << ")"
					<< " m_rtdl_pkt_counts=" << m_rtdl_pkt_counts
					<< " m_data_pkt_counts=" << m_data_pkt_counts);
				dumpStats = true;
			}
			ctrl->setSourcesReadDelay();
			if ( dumpStats ) dumpLastReadStats("dataReady() (Read Delay)");
		}
	}
}

void DataSource::enabled(void)
{
	DEBUG("*** Data Source " << m_name << " Enabled!");

	// Change Internal State to Default "Idle", We're Enabled Now...
	m_state = IDLE;

	startConnect();
}

void DataSource::disabled(void)
{
	DEBUG("*** Data Source " << m_name << " Disabled!");

	// Mark Connection State as "Disconnected"...
	m_pvConnected->disconnected();

	// Close Down Socket, Change Internal State to DISABLED...
	// (so we won't do anything... ;-)
	connectionFailed(true, true, DISABLED);
}

bool DataSource::rxPacket(const ADARA::Packet &pkt)
{
	// Once in a blue moon, dump "Discarded Packet" statistics... ;-D
	static uint64_t dump_count = 0;
	if ( !( ++dump_count % 1000000 ) ) {
		std::string log_info;
		Parser::getDiscardedPacketsLogString(log_info);
		INFO("rxPacket(): " << log_info);
	}

	// Save "Last Packet" Info for Debugging...
	m_last_pkt_type = pkt.type();
	m_last_pkt_len = pkt.payload_length();
	m_last_pkt_sec = pkt.timestamp().tv_sec;
	m_last_pkt_nsec = pkt.timestamp().tv_nsec;

	// INFO("rxPacket() type=0x" << std::hex << m_last_pkt_type << std::dec
		// << " sec=" << m_last_pkt_sec
		// << " nsec=" << m_last_pkt_nsec
		// << " len=" << m_last_pkt_len );

	switch (pkt.type()) {
	case ADARA::PacketType::HEARTBEAT_V0:
		/* We actually *do* care about these packets after all;
		 * we need to Unregister Any DataSource that sends us one...!
		 * (or else we'll buffer everything else and swell up & pop! ;-)
		 */
		return Parser::rxPacket(pkt);
	case ADARA::PacketType::SYNC_V0:
	case ADARA::PacketType::DATA_DONE_V0:
		/* We don't care about these packets, just drop them */
		return false;
	case ADARA::PacketType::RAW_EVENT_V0:
	case ADARA::PacketType::MAPPED_EVENT_V0:
	case ADARA::PacketType::RTDL_V0:
	case ADARA::PacketType::SOURCE_LIST_V0:
	case ADARA::PacketType::DEVICE_DESC_V0:
	case ADARA::PacketType::VAR_VALUE_U32_V0:
	case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
	case ADARA::PacketType::VAR_VALUE_STRING_V0:
		/* We use a 0 pulse id to indicate that we don't have an
		 * active pulse, and nothing should ever send one to us.
		 */
		if (!pkt.pulseId()) {
			/* Rate-limited logging of pulse id 0 */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_PULSEID_ZERO, m_name, 2, 10, 100, log_info ) ) {
				WARN(log_info << "Received pulse id 0 from " << m_name);
			}
			return false;
		}
		return Parser::rxPacket(pkt);
	default:
		/* We don't expect to see any other packet types here. */
		return rxUnknownPkt(pkt);
	}
}

bool DataSource::rxUnknownPkt(const ADARA::Packet &pkt)
{
	/* Rate-limited logging of unknown packet types */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_UNKNOWN_PACKET, m_name, 2, 10, 100, log_info ) ) {
		ERROR(log_info << "Unknown packet type " << pkt.type()
			<< " from " << m_name);
	}
	return true;
}

bool DataSource::rxOversizePkt( const ADARA::PacketHeader *hdr, 
		const uint8_t *UNUSED(chunk),
		unsigned int UNUSED(chunk_offset),
		unsigned int chunk_len )
{
	/* Rate-limited logging of oversized packets */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_OVERSIZE_PACKET, m_name, 2, 10, 100, log_info ) ) {
		// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
		if (hdr) {
			ERROR(log_info << "Oversized packet"
				<< " at " << hdr->timestamp().tv_sec
				<< "." << hdr->timestamp().tv_nsec
				<< " of type 0x" << std::hex << hdr->type() << std::dec
				<< " payload_length=" << hdr->payload_length()
				<< " from " << m_name);
		} else {
			ERROR(log_info << "Oversized packet"
				<< " chunk_len=" << chunk_len
				<< " from " << m_name);
		}
	}
	return true;
}

void DataSource::resetPacketStats(void)
{
	// Dump Total Discarded Packet Statistics before Reset...
	std::string log_info;
	Parser::getDiscardedPacketsLogString(log_info);
	INFO("resetPacketStats(): Totals Before Reset - " << log_info);

	// Reset Discarded Packet Statistics...
	Parser::resetDiscardedPacketsStats();
}

HWSource &DataSource::getHWSource(uint32_t hwId)
{
	HWSrcMap::iterator it;

	it = m_hwSources.find(hwId);

	if (it == m_hwSources.end()) {

		SMSControl *ctrl = SMSControl::getInstance();
		uint32_t smsId = ctrl->registerEventSource(hwId);

		// Get Next Available HW Index for PVs...
		size_t i, max = m_hwIndices.size();
		int32_t hwIndex = -1; // default, result if no free Ids remain...
		for (i = 0; i < max && hwIndex < 0; i++) {
			if (!m_hwIndices[i]) {
				m_hwIndices.set(i);
				DEBUG("getHWSource() assigning PV hwIndex=" << i
					<< " to HWSource hwId=" << hwId
					<< " smsId=" << smsId);
				hwIndex = i;
			}
		}

		boost::shared_ptr<smsUint32PV> pvHwId;
		boost::shared_ptr<smsUint32PV> pvSmsId;
		boost::shared_ptr<smsUint32PV> pvEventBwSecond;
		boost::shared_ptr<smsUint32PV> pvEventBwMinute;
		boost::shared_ptr<smsUint32PV> pvEventBwTenMin;

		// Create/Get Persistent EPICS PVs for This HWSource Instance...
		if ( hwIndex >= 0 ) {

			// Make New HWSource PVs, as needed...
			if ( (uint32_t) hwIndex >= m_pvHWSourceHwIds.size() ) {

				std::string prefix(ctrl->getBeamlineId());
				prefix += ":SMS";
				prefix += ":DataSource:";

				std::stringstream ss;
				ss << m_smsSourceId;
				prefix += ss.str();

				prefix += ":HWSource:";

				std::stringstream ss2;
				ss2 << hwIndex + 1; // count from 1 like everything else
				prefix += ss2.str();

				// HWSource Hw Id...
				m_pvHWSourceHwIds.resize(hwIndex + 1);
				m_pvHWSourceHwIds[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":HwId"));
				ctrl->addPV(m_pvHWSourceHwIds[hwIndex]);

				// HWSource SMS Id...
				m_pvHWSourceSmsIds.resize(hwIndex + 1);
				m_pvHWSourceSmsIds[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":SmsId"));
				ctrl->addPV(m_pvHWSourceSmsIds[hwIndex]);

				// HWSource Event Bandwidth Second...
				m_pvHWSourceEventBandwidthSecond.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthSecond[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthSecond"));
				ctrl->addPV(m_pvHWSourceEventBandwidthSecond[hwIndex]);

				// HWSource Event Bandwidth Minute...
				m_pvHWSourceEventBandwidthMinute.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthMinute[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthMinute"));
				ctrl->addPV(m_pvHWSourceEventBandwidthMinute[hwIndex]);

				// HWSource Event Bandwidth Ten Minutes...
				m_pvHWSourceEventBandwidthTenMin.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthTenMin[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthTenMin"));
				ctrl->addPV(m_pvHWSourceEventBandwidthTenMin[hwIndex]);
			}

			pvHwId = m_pvHWSourceHwIds[hwIndex];
			pvSmsId = m_pvHWSourceSmsIds[hwIndex];
			pvEventBwSecond = m_pvHWSourceEventBandwidthSecond[hwIndex];
			pvEventBwMinute = m_pvHWSourceEventBandwidthMinute[hwIndex];
			pvEventBwTenMin = m_pvHWSourceEventBandwidthTenMin[hwIndex];
		}
		else {
			DEBUG("getHWSource(): Out of PV Indices"
				<< " for HWSource hwId=" << hwId
				<< " smsId=" << smsId << "!");
			// *Don't* throw an exception here, _Not_ mission critical!
		}

		// Create New HWSource... (Pass In Id and Bandwidth PVs...)
		HWSrcPtr src( new HWSource(
			m_name, hwIndex, hwId, smsId, pvHwId, pvSmsId,
			pvEventBwSecond, pvEventBwMinute, pvEventBwTenMin ) );

		// Add to HWSource List for This DataSource...
		it = m_hwSources.insert(HWSrcMap::value_type(hwId, src)).first;

		// Update Number of HWSources PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvNumHWSources->update(m_hwSources.size(), &now);
	}

	return *(it->second);
}

bool DataSource::rxPacket(const ADARA::RawDataPkt &pkt)
{
	return( handleDataPkt(&pkt, false) );
}

bool DataSource::rxPacket(const ADARA::MappedDataPkt &pkt)
{
	return( handleDataPkt(dynamic_cast<const ADARA::RawDataPkt *>(&pkt),
		true) );
}

bool DataSource::handleDataPkt(const ADARA::RawDataPkt *pkt,
		bool is_mapped)
{
	HWSource &hw_src = getHWSource(pkt->sourceID());

	// Reset "RTDL Packets with No Data Packets" Count...
	hw_src.m_rtdlNoDataCount = 0;

	/* Check that the fields are consistent with the pulse we are
	 * currently processing. If not, then we've started a new pulse.
	 * The HWSource class will take care of missing end-of-pulse
	 * markers and duplicate pulse ids.
	 */
	bool good_pulse = true;
	if (hw_src.checkPulseInvariants(*pkt))
		good_pulse = hw_src.newPulse(*pkt);
	else
		good_pulse = hw_src.pulseGood();

	if ( good_pulse )
	{
		SMSControl *ctrl = SMSControl::getInstance();
		ctrl->pulseEvents(*pkt, hw_src.hwId(), hw_src.dupCount(),
			is_mapped);

		if (hw_src.checkSeq(*pkt))
			ctrl->markPartial(pkt->pulseId(), hw_src.dupCount());
	}

	// Sometimes we just can't rely on end-of-pulse being set correctly ;-b
	m_ignore_eop = m_pvIgnoreEoP->value();
	if (!m_ignore_eop && pkt->endOfPulse())
		hw_src.endPulse();

	// Count Events in Various Statistics...

	// Data Count Per "Read"...
	m_data_pkt_counts++;

	// Get Current Time for Bandwidth Statistics
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	// Event Count Per Second
	if ( m_last_second != now.tv_sec ) {
		// Update Bandwidth Count Per Second PVs...
		updateBandwidthSecond( now );
		// Reset Last Second
		m_last_second = now.tv_sec;
	}
	else {
		m_event_count_second += pkt->num_events();
		hw_src.m_event_count_second += pkt->num_events();
	}

	// Event Count Per Minute
	uint32_t min = now.tv_sec / 60;
	if ( m_last_minute != min ) {
		// Update Bandwidth Count Per Minute PVs...
		updateBandwidthMinute( now );
		// Reset Last Minute
		m_last_minute = min;
	}
	else {
		m_event_count_minute += pkt->num_events();
		hw_src.m_event_count_minute += pkt->num_events();
	}

	// Event Count Per Ten Minutes
	uint32_t tenmin = now.tv_sec / 600;
	if ( m_last_tenmin != tenmin ) {
		// Update Bandwidth Count Per Ten Minutes PVs...
		updateBandwidthTenMin( now );
		// Reset Last Ten Minutes
		m_last_tenmin = tenmin;
	}
	else {
		m_event_count_tenmin += pkt->num_events();
		hw_src.m_event_count_tenmin += pkt->num_events();
	}

	return false;
}

bool DataSource::rxPacket(const ADARA::RTDLPkt &pkt)
{
	/* RTDL packets come in as the pulse occurs, but the events for
	 * them may not show up for some time. Just forward them to
	 * SMSControl.
	 */

	bool drop_pulse = false;

	// do duplicate checking on a per-datasource basis
	if (pkt.pulseId() == m_lastRTDLPulseId) {
		/* Rate-limited logging of duplicate RTDLs */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_DUPLICATE_RTDL, m_name,
					2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< "rxPacket(RTDLPkt): Local Duplicate RTDL"
				<< " from " << m_name
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags());
		}
		m_dupRTDL++;
	}
	else m_dupRTDL = 0;

	// also check for "Local" SAWTOOTH, from within given DataSource stream
	if (pkt.pulseId() < m_lastRTDLPulseId) {
		/* Rate-limited logging of local sawtooth RTDLs */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_SAWTOOTH_RTDL, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< "rxPacket(RTDLPkt): Local SAWTOOTH RTDL from " << m_name
				<< std::hex << " m_lastRTDLPulseId=0x" << m_lastRTDLPulseId
				<< " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags());
		}
	}

	// done with this last pulseid...
	m_lastRTDLPulseId = pkt.pulseId();

	// strip off pulse nanoseconds...
	time_t sec = pkt.pulseId() >> 32;

	// check for "totally bogus" pulse times, in distant past/future... ;-b
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	time_t future = 
		now.tv_sec - ADARA::EPICS_EPOCH_OFFSET + SECS_PER_WEEK;

	// before SNS time began... ;-D
	if ( sec < FACILITY_START_TIME )
	{
		/* Rate-limited logging of Bogus RTDLs from the Distant Past! */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_RTDL_PULSE_IN_PAST, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< "*** Dropping Bogus RTDL Pulse Time"
				<< " from Distant Past (Before Facility Start Time)!"
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " (" << sec << " < " << FACILITY_START_TIME << ")"
				<< " (" << m_name << ")");
		}
		drop_pulse = true;
	}

	// more than a week into the future...! :-o
	else if ( sec > future )
	{
		/* Rate-limited logging of Bogus RTDLs from the Distant Future! */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_RTDL_PULSE_IN_FUTURE, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< "*** Dropping Bogus RTDL Pulse Time"
				<< " from Distant Future (Over One Week from Now)!"
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " (" << sec << " > " << future << ")"
				<< " (" << m_name << ")");
		}
		drop_pulse = true;
	}

	// just for yuks, check the cycle sequence
	if (m_lastRTDLCycle && pkt.cycle() != ((m_lastRTDLCycle + 1) % 600)) {
		/* Rate-limited logging of RTDL cycle out of sequence */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_RTDL_SEQUENCE, m_name,
				2, 10, 100, log_info ) ) {
			WARN(log_info
				<< "rxPacket(RTDLPkt): Local RTDL Cycle Out of Sequence"
				<< " from " << m_name
				<< " m_lastRTDLCycle=" << m_lastRTDLCycle
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags());
		}
	}
	m_lastRTDLCycle = pkt.cycle();

	// Ok, Process Pulse Now (Maybe... :-)
	if ( !drop_pulse ) {
		SMSControl *ctrl = SMSControl::getInstance();
		ctrl->pulseRTDL(pkt, m_dupRTDL);
	}

	// Check for Run-Away Data Sources...
	// (Lots of RTDLs filling up our internal Pulse Buffer,
	// with No RawDataPkts to release them... ;-b)

	// Update RTDL "No Data" Threshold from PV...
	m_rtdlNoDataThresh = m_pvRTDLNoDataThresh->value();

	SMSControl *ctrl = SMSControl::getInstance();
	HWSrcMap::iterator it = m_hwSources.begin();

	while ( it != m_hwSources.end() ) {

		if ( ++(it->second->m_rtdlNoDataCount) > m_rtdlNoDataThresh ) {

			ERROR("Run-Away Data Source " << m_name
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< ", " << it->second->m_rtdlNoDataCount
				<< " (> " << m_rtdlNoDataThresh << ")"
				<< " RTDL Pulses without a Corresponding RawDataPkt!"
				<< " Unregistering Event Source " << it->second->smsId());

			// Reset HWSource PV Index Bit, Clear Out SMS Id PV Value...
			if ( it->second->m_hwIndex >= 0 ) {
				it->second->m_pvHWSourceSmsId->update(-1, &now);
				m_hwIndices.reset( it->second->m_hwIndex );
			}

			ctrl->unregisterEventSource(it->second->smsId());

			// Remove Hardware Source from Map...
			// (Note: iterator increments to next element,
			// but returns current element for deletion... :-)
			m_hwSources.erase(it++);
		}
		else {
			++it;
		}
	}

	// Update Number of HWSources PV...
	m_pvNumHWSources->update(m_hwSources.size(), &now);

	// Count Pulse in Various Statistics...

	// RTDL Count Per "Read"...
	m_rtdl_pkt_counts++;

	// Pulse Count Per Second
	if ( m_last_second != now.tv_sec ) {
		// Update Bandwidth Count Per Second PVs...
		updateBandwidthSecond( now );
		// Reset Last Second
		m_last_second = now.tv_sec;
	}
	else {
		m_pulse_count_second++;
	}

	// Pulse Count Per Minute
	uint32_t min = now.tv_sec / 60;
	if ( m_last_minute != min ) {
		// Update Bandwidth Count Per Minute PVs...
		updateBandwidthMinute( now );
		// Reset Last Minute
		m_last_minute = min;
	}
	else {
		m_pulse_count_minute++;
	}

	// Pulse Count Per Ten Minutes
	uint32_t tenmin = now.tv_sec / 600;
	if ( m_last_tenmin != tenmin ) {
		// Update Bandwidth Count Per Ten Minutes PVs...
		updateBandwidthTenMin( now );
		// Reset Last Ten Minutes
		m_last_tenmin = tenmin;
	}
	else {
		m_pulse_count_tenmin++;
	}

	return false;
}

void DataSource::updateBandwidthSecond( struct timespec & now )
{
	// Update Bandwidth Count Per Second PVs...
	m_pvPulseBandwidthSecond->update(m_pulse_count_second, &now);
	m_pvEventBandwidthSecond->update(m_event_count_second, &now);

	// Reset Counters for Next Second...
	m_pulse_count_second = 0;
	m_event_count_second = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			it->second->m_pvHWSourceEventBandwidthSecond->update(
				it->second->m_event_count_second, &now);
		}
		it->second->m_event_count_second = 0;
	}
}

void DataSource::updateBandwidthMinute( struct timespec & now )
{
	// Update Bandwidth Count Per Minute PVs...
	m_pvPulseBandwidthMinute->update(m_pulse_count_minute, &now);
	m_pvEventBandwidthMinute->update(m_event_count_minute, &now);

	// Reset Counters for Next Minute...
	m_pulse_count_minute = 0;
	m_event_count_minute = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			it->second->m_pvHWSourceEventBandwidthMinute->update(
				it->second->m_event_count_minute, &now);
		}
		it->second->m_event_count_minute = 0;
	}
}

void DataSource::updateBandwidthTenMin( struct timespec & now )
{
	// Update Bandwidth Count Per Ten Minutes PVs...
	m_pvPulseBandwidthTenMin->update(m_pulse_count_tenmin, &now);
	m_pvEventBandwidthTenMin->update(m_event_count_tenmin, &now);

	// Reset Counters for Next Ten Minutes...
	m_pulse_count_tenmin = 0;
	m_event_count_tenmin = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			it->second->m_pvHWSourceEventBandwidthTenMin->update(
				it->second->m_event_count_tenmin, &now);
		}
		it->second->m_event_count_tenmin = 0;
	}
}

bool DataSource::rxPacket(const ADARA::SourceListPkt &pkt)
{
	const uint32_t *ids = pkt.ids();

	/* TODO note undeclared ids when we get raw event packets */
	for (uint32_t i = 0; i < pkt.num_ids(); i++) {
		getHWSource(ids[i]);
	}
	return false;
}

bool DataSource::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	SMSControl::getInstance()->updateDescriptor(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::HeartbeatPkt &UNUSED(pkt))
{
	/* Rate-limited logging of heartbeat packets */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_HEARTBEAT, m_name, 60, 3, 10, log_info ) ) {
		INFO(log_info << "Heartbeat Packet for " << m_name);
	}

	// In case this DataSource was formerly registered and sending events,
	// we need to *Unregister All Registered SourceIds* when we receive a
	// Heartbeat packet, so we won't hold back any other DataSources and
	// swell up to buffer-explode...! ;-O

	/* Complete any outstanding pulses, and inform the manager of our
	 * now-idle state (not down, just idle... :-)
	 */
	unregisterHWSources(false, "Now-Idle");

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;

	return false;
}

