#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <stdexcept>

#include <fdManager.h>
#include <epicsTimer.h>
#include "DataSource.h"
#include "StorageManager.h"

#include <stdio.h>

double DataSource::m_connect_retry = 15.0;
double DataSource::m_connect_timeout = 5.0;
double DataSource::m_data_timeout = 5.0;

unsigned int DataSource::m_max_read_chunk = 4 * 1024 * 1024;

class DataSourceReadyFd : public fdReg {
public:
	DataSourceReadyFd(DataSource *obj, fdRegType reg) :
		fdReg(obj->m_fd, reg), m_obj(obj) { }

private:
	DataSource *m_obj;

	void callBack(void) { m_obj->fdReady(); }
};

class DataSourceTimer : public epicsTimerNotify {
public:
	DataSourceTimer(DataSource *obj) :
		m_obj(obj), m_timer(fileDescriptorManager.createTimer()) {}
	virtual ~DataSourceTimer() { m_timer.destroy(); }

	void start(double delaySeconds) { m_timer.start(*this, delaySeconds); }
	void cancel(void) { m_timer.cancel(); }

private:
	DataSource *m_obj;
	epicsTimer &m_timer;

	expireStatus expire(const epicsTime &currentTime) {
		m_obj->timerExpired();
		return expireStatus(noRestart);
	}
};

DataSource::DataSource(const std::string &uri) :
	m_fdreg(NULL), m_timer(NULL), m_addrinfo(NULL),
	m_state(IDLE), m_fd(-1)
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

	m_timer = new DataSourceTimer(this);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_CANONNAME | AI_V4MAPPED;

fprintf(stderr, "Looking up %s %s\n", node.c_str(), service.c_str());

	rc = getaddrinfo(node.c_str(), service.c_str(), &hints, &m_addrinfo);
	if (rc) {
		/* Clean up to avoid leaking resources */
		delete m_timer;

		std::string msg("Unable to lookup data source '");
		msg += uri;
		msg += "': ";
		msg += gai_strerror(rc);
		throw std::runtime_error(msg);
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
fprintf(stderr, "%s\n", __func__);
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
}

void DataSource::timerExpired(void)
{
	switch (m_state) {
	case IDLE:
		startConnect();
		break;
	case CONNECTING:
		/* TODO ratelimited logging of connection failure */
		connectionFailed();
		break;
	case ACTIVE:
		/* TODO ratelimited logging of conneciton failure */
		connectionFailed();
		break;
	}
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
		goto error_fd;
	case EINTR:
	case EINPROGRESS:
		m_state = CONNECTING;
		break;
	case 0:
		m_state = ACTIVE;
	}

	try {
		/* We won't notice that the connect completed until we get
		 * the first packet from the source unless we look for the
		 * connection becoming writable.
		 */
		if (m_state == CONNECTING)
			m_fdreg = new DataSourceReadyFd(this, fdrWrite);
		else
			m_fdreg = new DataSourceReadyFd(this, fdrRead);
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
		m_fdreg = new DataSourceReadyFd(this, fdrRead);

		m_timer->cancel();
		m_timer->start(m_data_timeout);
		m_state = ACTIVE;

		/* TODO log connection to source */
		return;
	}

	if (rc < 0)
		e = errno;

	if (e == EINTR || e == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		return;
	}

	/* TODO ratelimited logging of connection issue */
	connectionFailed();
}

void DataSource::dataReady(void)
{
	m_timer->cancel();
	m_timer->start(m_data_timeout);

	try {
		if (!read(m_fd, m_max_read_chunk))
			connectionFailed();
	} catch (ADARA::Exception) {
		/* TODO ratelimited log of failure */
		connectionFailed();
	}
}

bool DataSource::rxPacket(const ADARA::Packet &pkt)
{
//fprintf(stderr, "%s type 0x%08x len %u\n", __func__, pkt.type(), pkt.packet_length());
	switch (pkt.type()) {
	case ADARA::ADARA_PKT_HEARTBEAT_V0:
	case ADARA::ADARA_PKT_SYNC_V0:
		/* We don't care about these packets, just drop them */
		return false;
	case ADARA::ADARA_PKT_RAW_EVENT_V0:
	case ADARA::ADARA_PKT_RTDL_V0:
	case ADARA::ADARA_PKT_DEVICE_DESC_V0:
	case ADARA::ADARA_PKT_VAR_VALUE_U32_V0:
	case ADARA::ADARA_PKT_VAR_VALUE_DOUBLE_V0:
	case ADARA::ADARA_PKT_VAR_VALUE_STRING_V0:
		return Parser::rxPacket(pkt);
	default:
		/* We don't expect to see any other packet types here. */
		return rxUnknownPkt(pkt);
	}
}

bool DataSource::rxUnknownPkt(const ADARA::Packet &pkt)
{
fprintf(stderr, "%s\n", __func__);
	// TODO ratelimited log
	connectionFailed();
	return true;
}

bool DataSource::rxOversizePkt(const ADARA::PacketHeader *hdr, 
			       const uint8_t *chunk, unsigned int chunk_offset,
			       unsigned int chunk_len)
{
	// TODO ratelimited log
	connectionFailed();
	return true;
}

bool DataSource::rxPacket(const ADARA::RawDataPkt &pkt)
{
	// TODO
	StorageManager::addPacket(pkt.packet(), pkt.packet_length());
	return false;
}

bool DataSource::rxPacket(const ADARA::RTDLPkt &pkt)
{
	// TODO
	StorageManager::addPacket(pkt.packet(), pkt.packet_length());
	return false;
}

bool DataSource::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	// TODO
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	// TODO
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	// TODO
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	// TODO
	return false;
}
