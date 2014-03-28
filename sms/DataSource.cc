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
			m_lastPulse = m_activePulse;
		}

		m_activePulse = pkt.pulseId();
		if (m_lastPulse == m_activePulse) {
			/* TODO rate-limited logging of duplicate pulses? */
			ERROR("Duplicate pulse from " << m_name << " src 0x"
				<< std::hex << m_hwId);
			m_dupCount++;
		} else
			m_dupCount = 0;

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
		 * duplicate pulse id (or a new pulse alltogether.)
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

	bool checkSeq(const ADARA::RawDataPkt &pkt) {
		bool ok = (pkt.pktSeq() == m_pktSeq);
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
	SMSControl *ctrl = SMSControl::getInstance();
	HWSrcMap::iterator it, end = m_hwSources.end();

	for (it = m_hwSources.begin(); it != end; it++) {
		it->second->endPulse(false);
		ctrl->unregisterEventSource(it->second->smsId());
	}

	m_hwSources.clear();
	ctrl->sourceDown(m_smsSourceId);
}

bool DataSource::timerExpired(void)
{
	switch (m_state) {
	case IDLE:
		startConnect();
		break;
	case CONNECTING:
		/* TODO only send this once until we successfully connect */
		WARN("Connection request timed out to " << m_name);
		connectionFailed();
		break;
	case ACTIVE:
		WARN("Timed out waiting for data from " << m_name);
		connectionFailed();
		break;
	}

	return false;
}

void DataSource::fdReady(void)
{
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

	try {
		if (!read(m_fd, 0, m_max_read_chunk)) {
			INFO("Connection closed with " << m_name);
			connectionFailed();
		}
	} catch (std::runtime_error e) {
		/* TODO ratelimited log of failure */
		ERROR("Exception reading from " << m_name << ": " << e.what());
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
	// XXX do duplicate checking on a per-datasource basis?
	SMSControl *ctrl = SMSControl::getInstance();
	ctrl->pulseRTDL(pkt);
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
