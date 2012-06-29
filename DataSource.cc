#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <boost/bind.hpp>
#include <stdexcept>

#include "EPICS.h"
#include "DataSource.h"
#include "SMSControl.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DataSource"));

double DataSource::m_connect_retry = 15.0;
double DataSource::m_connect_timeout = 5.0;
double DataSource::m_data_timeout = 5.0;

unsigned int DataSource::m_max_read_chunk = 4 * 1024 * 1024;

DataSource::DataSource(const std::string &uri, uint32_t id) :
	m_uri(uri), m_fdreg(NULL), m_timer(NULL), m_addrinfo(NULL),
	m_state(IDLE), m_sourceId(id), m_fd(-1), m_newPulse(false),
	m_lastPulseId(0), m_dupCount(0), m_expectedPktSeq(0), m_pulseEOP(false)
{
	std::string node;
	std::string service("31416");
	struct addrinfo hints;
	size_t pos = uri.find_first_of(':');
	int rc;

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
		std::string msg("Unable to lookup data source '");
		msg += uri;
		msg += "': ";
		msg += gai_strerror(rc);
		throw std::runtime_error(msg);
	}

	try {
		m_timer = new TimerAdapter<DataSource>(this);
	} catch(...) {
		freeaddrinfo(m_addrinfo);
		throw;
	}

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

void DataSource::connectionFailed(void)
{
	m_timer->cancel();

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
	endPulse(false);
	m_lastPulseId = 0;
	SMSControl::getInstance()->sourceDown(m_sourceId);
}

bool DataSource::timerExpired(void)
{
	switch (m_state) {
	case IDLE:
		startConnect();
		break;
	case CONNECTING:
		/* TODO only send this once until we successfully connect */
		WARN("Connection request timed out to " << m_uri);
		connectionFailed();
		break;
	case ACTIVE:
		WARN("Timed out waiting for data from " << m_uri);
		connectionFailed();
		break;
	}

	return false;
}

void DataSource::fdReady(void)
{
	switch (m_state) {
	case IDLE:
		throw std::runtime_error("Invalid state");
	case CONNECTING:
		connectComplete();
		break;
	case ACTIVE:
		dataReady();
		break;
	}
}

void DataSource::startConnect(void)
{
	int flags, rc;

	/* Clear out any old state from the ADARA parser. */
	reset();

	m_fd = socket(m_addrinfo->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0)
		goto error;

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0)
		goto error_fd;
	if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK))
		goto error_fd;

	rc = connect(m_fd, m_addrinfo->ai_addr, m_addrinfo->ai_addrlen);
	if (rc < 0)
		rc = errno;

	switch (rc) {
	case ECONNREFUSED:
		/* TODO ratelimited logging of refused connection */
		WARN("Connection refused by " << m_uri);
		goto error_fd;
	case EINTR:
	case EINPROGRESS:
		m_state = CONNECTING;
		break;
	case 0:
		INFO("Connection established to " << m_uri);
		m_state = ACTIVE;
		SMSControl::getInstance()->sourceUp(m_sourceId);
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
		SMSControl::getInstance()->sourceUp(m_sourceId);

		INFO("Connection established to " << m_uri);
		return;
	}

	if (rc < 0)
		e = errno;

	if (e == EINTR || e == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		return;
	}

	/* TODO ratelimited logging of connection issue */
	WARN("Connection request to " << m_uri << " failed: " << strerror(e));
	connectionFailed();
}

void DataSource::dataReady(void)
{
	m_timer->cancel();
	m_timer->start(m_data_timeout);

	try {
		if (!read(m_fd, m_max_read_chunk)) {
			INFO("Connection closed with " << m_uri);
			connectionFailed();
		}
	} catch (std::runtime_error e) {
		/* TODO ratelimited log of failure */
		ERROR("Exception reading from " << m_uri << ": " << e.what());
		connectionFailed();
	}
}

bool DataSource::rxPacket(const ADARA::Packet &pkt)
{
	switch (pkt.type()) {
	case ADARA::PacketType::HEARTBEAT_V0:
	case ADARA::PacketType::SYNC_V0:
		/* We don't care about these packets, just drop them */
		return false;
	case ADARA::PacketType::RAW_EVENT_V0:
	case ADARA::PacketType::RTDL_V0:
	case ADARA::PacketType::DEVICE_DESC_V0:
	case ADARA::PacketType::VAR_VALUE_U32_V0:
	case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
	case ADARA::PacketType::VAR_VALUE_STRING_V0:
		return Parser::rxPacket(pkt);
	default:
		/* We don't expect to see any other packet types here. */
		return rxUnknownPkt(pkt);
	}
}

bool DataSource::rxUnknownPkt(const ADARA::Packet &pkt)
{
	ERROR("Unknown packet from" << m_uri);
	return true;
}

bool DataSource::rxOversizePkt(const ADARA::PacketHeader *hdr, 
			       const uint8_t *chunk, unsigned int chunk_offset,
			       unsigned int chunk_len)
{
	ERROR("Oversized packet from" << m_uri);
	return true;
}

void DataSource::endPulse(bool dup)
{
	SMSControl *ctrl = SMSControl::getInstance();

	/* Do we even have a pulse to close out? */
	if (!m_lastPulseId)
		return;

	if (!m_pulseEOP) {
		/* TODO rate-limited logging of dropped packets */
		ERROR("Lost packet from" << m_uri);
		ctrl->markPartial(m_lastPulseId, m_dupCount);
	}

	ctrl->markComplete(m_lastPulseId, m_dupCount, m_sourceId);

	m_newPulse = true;
	m_expectedPktSeq = 0;
	m_pulseEOP = false;
	if (dup) {
		/* TODO rate-limited logging of duplicate pulses? */
		ERROR("Duplicate pulse from" << m_uri);
		m_dupCount++;
	} else
		m_dupCount = 0;
}

bool DataSource::checkPulseInvariants(const ADARA::RawDataPkt &pkt)
{
	/* These fields should be the same for every Raw Data Event
	 * in this pulse. If not, then we have a duplicate pulse on
	 * our hands.
	 */
	return pkt.flavor() == m_pulseFlavor &&
	       pkt.pulseCharge() == m_pulseCharge &&
	       pkt.veto() == m_pulseVeto &&
	       pkt.cycle() == m_pulseCycle &&
	       pkt.timingStatus() == m_pulseTimingStatus &&
	       pkt.intraPulseTime() == m_pulseIntraTime &&
	       pkt.tofOffset() == m_pulseTofOffset;
}

bool DataSource::rxPacket(const ADARA::RawDataPkt &pkt)
{
	SMSControl *ctrl = SMSControl::getInstance();

	/* We check for the end of pulses here as well as the RTDL handler,
	 * as it is not clear that they cannot be lost by the preprocessor.
	 * If we already know we're in a new pulse, there's no reason to
	 * perform these checks.
	 */
	if (!m_newPulse) {
		if (pkt.pulseId() != m_lastPulseId)
			endPulse(false);
		else if (m_pulseEOP || pkt.pktSeq() < m_expectedPktSeq ||
			 !checkPulseInvariants(pkt))
			endPulse(true);
	}

	if (m_newPulse) {
		m_newPulse = false;
		m_lastPulseId = pkt.pulseId();
		m_pulseFlavor = pkt.flavor();
		m_pulseCharge = pkt.pulseCharge();
		m_pulseVeto = pkt.veto();
		m_pulseCycle = pkt.cycle();
		m_pulseTimingStatus = pkt.timingStatus();
		m_pulseIntraTime = pkt.intraPulseTime();
		m_pulseTofOffset = pkt.tofOffset();
	}

	if (pkt.pktSeq() != m_expectedPktSeq)
		ctrl->markPartial(pkt.pulseId(), m_dupCount);

	m_expectedPktSeq = pkt.pktSeq() + 1;
	if (pkt.endOfPulse())
		m_pulseEOP = true;

	ctrl->pulseEvents(pkt, m_sourceId, m_dupCount);
	return false;
}

bool DataSource::rxPacket(const ADARA::RTDLPkt &pkt)
{
	SMSControl *ctrl = SMSControl::getInstance();

	/* Getting an RTDL packet means the last pulse is complete, we
	 * just need to know if the new one is a duplicate or not.
	 *
	 * If we don't get an RTDL for some reason, we'll detect duplicate
	 * pulses from the Raw Event packets.
	 */
	endPulse(pkt.pulseId() == m_lastPulseId);
	m_lastPulseId = pkt.pulseId();

	ctrl->pulseRTDL(pkt, m_sourceId, m_dupCount);
	return false;
}

bool DataSource::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	SMSControl::getInstance()->updateDescriptor(pkt, m_sourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_sourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_sourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	SMSControl::getInstance()->updateValue(pkt, m_sourceId);
	return false;
}
