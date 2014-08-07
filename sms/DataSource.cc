#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#include <boost/bind.hpp>
#include <stdexcept>

#include "EPICS.h"
#include "DataSource.h"
#include "SMSControl.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DataSource"));


class HWSource {
public:
	HWSource(const std::string &name, uint32_t hwId, uint32_t smsId) :
		m_name(name), m_hwId(hwId), m_smsId(smsId), m_activePulse(0),
		m_lastPulse(0), m_dupCount(0)
	{ }

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
			/* TODO rate-limited logging of dropped packets */
			ERROR("Lost packet from " << m_name << " src 0x"
				<< std::hex << m_hwId);
#endif
			ctrl->markPartial(m_activePulse, m_dupCount);
		}

		ctrl->markComplete(m_activePulse, m_dupCount, m_smsId);

		m_lastPulse = m_activePulse;
		m_activePulse = 0;
	}

	void newPulse(const ADARA::RawDataPkt &pkt) {
		if (m_activePulse) {
			/* We didn't see an end-of-packet, so clear out
			 * the previous pulse here.
			 */
			endPulse(false);
		}

		m_activePulse = pkt.pulseId();

		// check for Duplicate Pulses in event packets...
		if (m_activePulse == m_lastPulse) {
			/* TODO rate-limited logging of duplicate pulses? */
			ERROR("newPulse(RawDataPkt): Duplicate pulse from " << m_name
				<< " src=0x" << std::hex << m_hwId << std::dec
				<< " pulseId=" << m_activePulse);
			dumpPulseInvariants(pkt);
			m_dupCount++;
		} else
			m_dupCount = 0;

		// also check for SAWTOOTH Pulse Times in event packets...
		if (m_activePulse < m_lastPulse) {
			ERROR("newPulse(RawDataPkt): Local SAWTOOTH RawData"
				<< " m_lastPulse=" << m_lastPulse
				<< " m_activePulse=" << m_activePulse
				<< " cycle=" << pkt.cycle()
				<< " veto=" << pkt.veto());
		}

		m_flavor = pkt.flavor();
		m_intraPulse = pkt.intraPulseTime();
		m_tofOffset = pkt.tofOffset();
		m_charge = pkt.pulseCharge();
		m_cycle = pkt.cycle();
		m_veto = pkt.veto();
		m_timingStatus = pkt.timingStatus();
		m_pktSeq = 0;
	}

	bool checkPulseInvariants(const ADARA::RawDataPkt &pkt) {
		/* These fields should not change for any packet in
		 * the current pulse. If they do, then this is a
		 * duplicate pulse id (or a new pulse all together.)
		 */
		return !(pkt.pulseId() == m_activePulse &&
			 pkt.flavor() == m_flavor &&
			 pkt.pulseCharge() == m_charge &&
			 pkt.veto() == m_veto &&
			 pkt.cycle() == m_cycle &&
			 pkt.timingStatus() == m_timingStatus &&
			 pkt.intraPulseTime() == m_intraPulse &&
			 pkt.tofOffset() == m_tofOffset);
	}

	void dumpPulseInvariants(const ADARA::RawDataPkt &pkt) {
		ERROR("dumpPulseInvariants():"
			<< std::hex << " pulseId=0x" << pkt.pulseId()
				<< "(0x" << m_activePulse << ")" << std::dec
			<< " flavor=" << pkt.flavor() << "(" << m_flavor << ")"
			<< " pulseCharge=" << pkt.pulseCharge()
				<< "(" << m_charge << ")"
			<< " veto=" << pkt.veto() << "(" << m_veto << ")"
			<< " cycle=" << pkt.cycle() << "(" << m_cycle << ")"
			<< " timingStatus=" << (uint32_t) pkt.timingStatus()
				<< "(" << (uint32_t) m_timingStatus << ")"
			<< " intraPulseTime=" << pkt.intraPulseTime()
				<< "(" << m_intraPulse << ")"
			<< " tofOffset=" << pkt.tofOffset()
				<< "(" << m_tofOffset << ")");
	}

	bool checkSeq(const ADARA::RawDataPkt &pkt) {
		bool ok = (pkt.pktSeq() == m_pktSeq);
		if ( !ok ) {
			ERROR("checkSeq() Packet Sequence Out-of-Order: "
				<< pkt.pktSeq() << " != " << m_pktSeq
				<< std::hex << " m_activePulse=0x" << m_activePulse
				<< " hwId=0x" << m_hwId);
		}
		m_pktSeq++;
		return !ok;
	}

private:
	const std::string &m_name;

	uint32_t	m_hwId;
	uint32_t	m_smsId;
	uint64_t	m_activePulse;
	uint64_t	m_lastPulse;
	uint32_t	m_dupCount;
	uint16_t	m_pktSeq;

	/* Pulse invariants -- these should not change between raw event
	 * packets for a given pulse, so we can use them to help detect
	 * duplicate pulse IDs
	 */
	ADARA::PulseFlavor::Enum m_flavor;
	uint32_t	m_intraPulse;
	uint32_t	m_tofOffset;
	uint32_t	m_charge;
	uint16_t	m_cycle;
	uint16_t	m_veto;
	uint8_t		m_timingStatus;
};


DataSource::DataSource(const std::string &name, const std::string &uri,
		       uint32_t id, double connect_retry,
		       double connect_timeout, double data_timeout,
		       unsigned int read_chunk) :
	m_name(uri), m_fdreg(NULL), m_timer(NULL), m_addrinfo(NULL),
	m_state(IDLE), m_smsSourceId(id), m_fd(-1),
	m_connect_retry(connect_retry), m_connect_timeout(connect_timeout),
	m_data_timeout(data_timeout), m_max_read_chunk(read_chunk)
{
	std::string node;
	std::string service("31416");
	struct addrinfo hints;
	size_t pos = uri.find_first_of(':');
	int rc;

	m_name += " (";
	m_name += name;
	m_name += ")";

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

	rc = getaddrinfo(node.c_str(), service.c_str(), &hints, &m_addrinfo);
	if (rc) {
		std::string msg("Unable to lookup data source ");
		msg += m_name;
		msg += ": ";
		msg += gai_strerror(rc);
		throw std::runtime_error(msg);
	}

	try {
		m_timer = new TimerAdapter<DataSource>(this);
	} catch(...) {
		freeaddrinfo(m_addrinfo);
		throw;
	}

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;
	m_dupRTDL = 0;

	m_last_pkt_type = -1;
	m_last_pkt_len = -1;
	m_last_pkt_sec = -1;
	m_last_pkt_nsec = -1;

	m_readDelay = false;

	startConnect();
}

DataSource::~DataSource()
{
	freeaddrinfo(m_addrinfo);
	delete m_timer;
	if (m_fdreg)
		delete m_fdreg;
	if (m_fd != -1)
		close(m_fd);
}

void DataSource::unregisterHWSources(bool isSourceDown)
{
	/* Complete any outstanding pulses, and inform the manager
	 * of our change of status
	 */
	SMSControl *ctrl = SMSControl::getInstance();
	HWSrcMap::iterator it, end = m_hwSources.end();

	for (it = m_hwSources.begin(); it != end; it++) {
		it->second->endPulse(false);
		ctrl->unregisterEventSource(it->second->smsId());
	}

	m_hwSources.clear();

	if (isSourceDown)
		ctrl->sourceDown(m_smsSourceId);
}

void DataSource::connectionFailed(void)
{
	m_timer->cancel();

	INFO("connectionFailed() " << m_name
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
		<< " last_last_bytes_read=" << Parser::last_last_bytes_read
		<< " last_last_pkts_parsed=" << Parser::last_last_pkts_parsed
		<< " last_last_total_bytes=" << Parser::last_last_total_bytes
		<< " last_last_total_packets=" << Parser::last_last_total_packets
		<< " last_last_read_count=" << Parser::last_last_read_count
		<< " last_last_loop_count=" << Parser::last_last_loop_count
		<< " last_last_elapsed=" << Parser::last_last_elapsed);

	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd != -1) {
		close(m_fd);
		m_fd = -1;
	}
	m_state = IDLE;
	m_timer->start(m_connect_retry);

	/* Complete any outstanding pulse, and inform the manager of our
	 * failure
	 */
	unregisterHWSources(true);
}

bool DataSource::timerExpired(void)
{
	// DEBUG("timerExpired() entry");

	switch (m_state) {

		case IDLE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Connect Retry Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
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
				m_timer->start(m_connect_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				WARN("Connection request timed out to " << m_name);
				connectionFailed();
			}
			break;
		}

		case ACTIVE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Data Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				m_timer->start(m_data_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				WARN("Timed out waiting for data from " << m_name);
				connectionFailed();
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
	case IDLE:
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
	int flags, rc;

	/* Clear out any old state from the ADARA parser. */
	reset();

	m_fd = socket(m_addrinfo->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		ERROR("Error creating socket for " << m_name);
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
		/* TODO ratelimited logging of refused connection */
		WARN("Connection refused by " << m_name);
		goto error_fd;
	case EINTR:
	case EINPROGRESS:
		m_state = CONNECTING;
		break;
	case 0:
		INFO("Connection established to " << m_name);
		m_state = ACTIVE;
		SMSControl::getInstance()->sourceUp(m_smsSourceId);
	default:
		WARN("Unknown connection request error for " << m_name
			<< ": " << strerror(rc) << " (Ignoring!)");
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
		ERROR("Bad Alloc Error for " << m_name << " adapter: " << e.what());
		goto error_fd;
	}

	m_timer->start(m_connect_timeout);
	return;

error_fd:
	close(m_fd);
	m_fd = -1;

error:
	connectionFailed();
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
		m_timer->start(m_data_timeout);
		m_state = ACTIVE;
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

	/* TODO ratelimited logging of connection issue */
	WARN("Connection request to " << m_name << " failed: " << strerror(e));
	connectionFailed();
}

void DataSource::dataReady(void)
{
	m_timer->cancel();
	m_timer->start(m_data_timeout);

	struct timespec readStart;
	clock_gettime(CLOCK_REALTIME, &readStart);

 	// reset read delayed flag, starting a new read now...
	SMSControl *ctrl = SMSControl::getInstance();
	ctrl->resetSourcesReadDelay();

	bool readOk = true;

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		if (!read(m_fd, 4000, m_max_read_chunk)) {
			INFO("Connection closed with " << m_name);
			connectionFailed();
			readOk = false;
		}
	} catch (std::runtime_error e) {
		/* TODO ratelimited log of failure */
		ERROR("Exception reading from " << m_name << ": " << e.what());
		connectionFailed();
		readOk = false;
	}

	if ( readOk )
	{
		struct timespec readEnd;
		clock_gettime(CLOCK_REALTIME, &readEnd);

		double elapsed = (double) ( readEnd.tv_sec - readStart.tv_sec )
			+ (double) ( ( readEnd.tv_nsec - readStart.tv_nsec ) / 1e9 );

 		// set read delayed flag...!
		if ( elapsed > 2.0 )
		{
			ERROR("dataReady(): Read Delay Threshold Exceeded"
				<< " elapsed=" << elapsed << " (" << m_name << ")");
			ctrl->setSourcesReadDelay();
		}
	}
}

bool DataSource::rxPacket(const ADARA::Packet &pkt)
{
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
		/* We don't care about these packets, just drop them */
		return false;
	case ADARA::PacketType::RAW_EVENT_V0:
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
			WARN("Received pulse id 0 from " << m_name);
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
	ERROR("Unknown packet from " << m_name);
	return true;
}

bool DataSource::rxOversizePkt(const ADARA::PacketHeader *hdr, 
			       const uint8_t *chunk, unsigned int chunk_offset,
			       unsigned int chunk_len)
{
	ERROR("Oversized packet from " << m_name);
	return true;
}

HWSource &DataSource::getHWSource(uint32_t hwId)
{
	HWSrcMap::iterator it;

	it = m_hwSources.find(hwId);
	if (it == m_hwSources.end()) {
		/* XXX Error handling? */
		SMSControl *ctrl = SMSControl::getInstance();
		uint32_t smsId = ctrl->registerEventSource(hwId);
		HWSrcPtr src(new HWSource(m_name, hwId, smsId));
		it = m_hwSources.insert(HWSrcMap::value_type(hwId, src)).first;
	}

	return *(it->second);
}

bool DataSource::rxPacket(const ADARA::RawDataPkt &pkt)
{
	HWSource &hw_src = getHWSource(pkt.sourceID());

	/* Check that the fields are consistent with the pulse we are
	 * currently processing. If not, then we've started a new pulse.
	 * The HWSource class will take care of missing end-of-pulse
	 * markers and duplicate pulse ids.
	 */
	if (hw_src.checkPulseInvariants(pkt))
		hw_src.newPulse(pkt);

	SMSControl *ctrl = SMSControl::getInstance();
	ctrl->pulseEvents(pkt, hw_src.hwId(), hw_src.dupCount());

	if (hw_src.checkSeq(pkt))
		ctrl->markPartial(pkt.pulseId(), hw_src.dupCount());

	if (pkt.endOfPulse())
		hw_src.endPulse();

	return false;
}

bool DataSource::rxPacket(const ADARA::RTDLPkt &pkt)
{
	/* RTDL packets come in as the pulse occurs, but the events for
	 * them may not show up for some time. Just forward them to
	 * SMSControl.
	 */

	// do duplicate checking on a per-datasource basis
	if (pkt.pulseId() == m_lastRTDLPulseId) {
		ERROR("rxPacket(RTDLPkt): Duplicate RTDL"
			<< " pulseId=" << pkt.pulseId()
			<< " cycle=" << pkt.cycle()
			<< " veto=" << pkt.veto());
		m_dupRTDL++;
	}
	else m_dupRTDL = 0;

	// also check for "Local" SAWTOOTH, from within given DataSource stream
	if (pkt.pulseId() < m_lastRTDLPulseId) {
		ERROR("rxPacket(RTDLPkt): Local SAWTOOTH RTDL"
			<< " m_lastRTDLPulseId=" << m_lastRTDLPulseId
			<< " pulseId=" << pkt.pulseId()
			<< " cycle=" << pkt.cycle()
			<< " veto=" << pkt.veto());
	}

	// done with this last pulseid...
	m_lastRTDLPulseId = pkt.pulseId();

	// just for yuks, check the cycle sequence
	if (m_lastRTDLCycle && pkt.cycle() != ((m_lastRTDLCycle + 1) % 600)) {
		WARN("rxPacket(RTDLPkt): RTDL Cycle Out of Sequence"
			<< " m_lastRTDLCycle=" << m_lastRTDLCycle
			<< " pulseId=" << pkt.pulseId()
			<< " cycle=" << pkt.cycle()
			<< " veto=" << pkt.veto());
	}
	m_lastRTDLCycle = pkt.cycle();

	SMSControl *ctrl = SMSControl::getInstance();
	ctrl->pulseRTDL(pkt, m_dupRTDL);

	return false;
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

bool DataSource::rxPacket(const ADARA::HeartbeatPkt &pkt)
{
	INFO("Heartbeat Packet for " << m_name);

	// In case this DataSource was formerly registered and sending events,
	// we need to *Unregister All Registered SourceIds* when we receive a
	// Heartbeat packet, so we won't hold back any other DataSources and
	// swell up to buffer-explode...! ;-O

	/* Complete any outstanding pulses, and inform the manager of our
	 * now-idle state (not down, just idle... :-)
	 */
	unregisterHWSources(false);

	return false;
}

