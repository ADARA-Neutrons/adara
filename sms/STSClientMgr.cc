#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "STSClientMgr.h"
#include "STSClient.h"
#include "SignalEvents.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.STSClientMgr"));

std::string STSClientMgr::m_node;
std::string STSClientMgr::m_service;
double STSClientMgr::m_connect_timeout = 15.0;
double STSClientMgr::m_reconnect_timeout = 15.0;
double STSClientMgr::m_transient_timeout = 60.0;
unsigned int STSClientMgr::m_max_connections = 3;

STSClientMgr *STSClientMgr::m_singleton;

STSClientMgr::STSClientMgr() :
	m_connect_timer(new TimerAdapter<STSClientMgr>(this,
					&STSClientMgr::connectTimeout)),
	m_reconnect_timer(new TimerAdapter<STSClientMgr>(this,
					&STSClientMgr::reconnectTimeout)),
	m_transient_timer(new TimerAdapter<STSClientMgr>(this,
					&STSClientMgr::transientTimeout)),
	m_fd(-1), m_fdreg(NULL), m_connecting(false), m_backoff(false),
	m_connections(0), m_queueMode(BALANCE), m_sendNext(OLDEST),
	m_currentRun(0)
{
	m_sigevent.sigev_notify = SIGEV_SIGNAL;
	m_sigevent.sigev_signo = SignalEvents::allocateRTsig(
			boost::bind(&STSClientMgr::lookupComplete, this, _1));

	memset(&m_gai_hints, 0, sizeof(m_gai_hints));
	m_gai_hints.ai_family = AF_INET6;
	m_gai_hints.ai_socktype = SOCK_STREAM;
	m_gai_hints.ai_protocol = IPPROTO_TCP;
	m_gai_hints.ai_flags = AI_CANONNAME | AI_V4MAPPED;

	m_mgrConnection = StorageManager::onContainerChange(
				boost::bind(&STSClientMgr::containerChange,
					    this, _1, _2));

	INFO("Remote is " << m_node << ":" << m_service);
}

STSClientMgr::~STSClientMgr()
{
	m_singleton = NULL;
	m_mgrConnection.disconnect();
	if (m_fd != -1)
		close(m_fd);
}

void STSClientMgr::containerChange(StorageContainer::SharedPtr &c,
				   bool starting)
{
	if (!c->runNumber())
		return;

	if (starting) {
		queueRun(c);
		startConnect();
	} else
		m_currentRun = 0;
}

void STSClientMgr::queueRun(StorageContainer::SharedPtr &c)
{
	std::pair<RunMap::iterator, bool> ret;

	ret = m_pendingRuns.insert(std::make_pair(c->runNumber(), c));
	if (!ret.second)
		throw std::logic_error("Duplicate run numbers");

	if (c->active())
		m_currentRun = c->runNumber();
}

void STSClientMgr::requeueRun(StorageContainer::SharedPtr &c)
{
	RunMap::iterator it;

	it = m_sendingRuns.find(c->runNumber());
	if (it != m_sendingRuns.end())
		m_sendingRuns.erase(it);

	queueRun(c);
}

StorageContainer::SharedPtr &STSClientMgr::nextRun(void)
{
	RunMap::iterator run;
	QueueMode next;

	if (m_pendingRuns.empty())
		throw std::logic_error("Trying to dequeue when empty");

	if (m_currentRun) {
		/* Always send the run we're currently recording if it
		 * isn't already being sent.
		 */
		run = m_pendingRuns.find(m_currentRun);
		m_currentRun = 0;
	} else {
		next = m_queueMode;
		if (next == BALANCE)
			next = m_sendNext;

		if (next == OLDEST) {
			run = m_pendingRuns.begin();
			m_sendNext = NEWEST;
		} else {
			run = m_pendingRuns.end();
			--run;
			m_sendNext = OLDEST;
		}
	}

	m_sendingRuns[run->first] = run->second;
	m_pendingRuns.erase(run);
	return m_sendingRuns[run->first];
}

void STSClientMgr::startConnect(void)
{
	struct gaicb *gai = &m_gai;
	const char *state;
	int rc;

	state = "idle, ";
	if (m_backoff)
		state = "backoff, ";
	if (m_connecting)
		state = "connecting, ";

	DEBUG("Checking for pending work (" << state
		<< m_connections << " of " << m_max_connections << " active, "
		<< m_pendingRuns.size() << " pending runs)");

	if (m_backoff || m_connecting || m_connections >= m_max_connections ||
					m_pendingRuns.empty())
		return;

        m_gai.ar_name = m_node.c_str();
        m_gai.ar_service = m_service.c_str();
        m_gai.ar_request = &m_gai_hints;
        m_gai.ar_result = NULL;

	rc = getaddrinfo_a(GAI_NOWAIT, &gai, 1, &m_sigevent);
	if (rc) {
		if (rc == EAI_AGAIN)
			return;

		/* TODO better message for this non-transient error */
		throw std::logic_error("STSClientMgr::startConnect");
	}

	m_connecting = true;
}

void STSClientMgr::lookupComplete(const struct signalfd_siginfo &info)
{
	struct addrinfo *ai;
	int flags, rc;

	/* Make sure we're expecting this signal, and that it comes from us. */
	if (!m_connecting || info.ssi_pid != (uint32_t) getpid() ||
				info.ssi_code != SI_ASYNCNL)
		return;

	rc = gai_error(&m_gai);
	if (rc) {
		if (rc == EAI_MEMORY || rc == EAI_SYSTEM) {
			ERROR("GAI unexpectedly returned " << rc);
		} else {
			/* TODO ratelimit lookup error (or just once per
			 * failure type?
			 */
			INFO("Lookup failed for " << m_node
				<< ":" << m_service <<
				": " << gai_strerror(rc));
		}
		connectFailed();
		return;
	}

	ai = m_gai.ar_result;

	if (logger->isDebugEnabled()) {
		char host[NI_MAXHOST], service[NI_MAXSERV];
		if (!getnameinfo(ai->ai_addr, ai->ai_addrlen,
					host, sizeof(host),
					service, sizeof(service),
					NI_NUMERICHOST | NI_NUMERICSERV)) {
			DEBUG("Connecting to " << host << ":" << service);
		} else
			DEBUG("Connecting to STS, getnameinfo failed");
	}

	m_fd = socket(ai->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0)
		goto error;

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0)
		goto error;
	if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK))
		goto error;

	rc = connect(m_fd, ai->ai_addr, ai->ai_addrlen);
	if (rc < 0)
		rc = errno;

	switch (rc) {
	case ECONNREFUSED:
		/* TODO ratelimited logging of refused connection */
		INFO("Connection refused");
		goto error;
	case EINTR:
	case EINPROGRESS:
		break;
	case 0:
		connectComplete();
		return;
	default:
		/* TODO other errors! */
		ERROR("Unexpected error " << rc << " from connect");
		goto error;
	}

	try {
		m_fdreg.reset(new ReadyAdapter(m_fd, fdrWrite,
			boost::bind(&STSClientMgr::connectComplete, this)));
	} catch (std::bad_alloc e) {
		goto error;
	}

	m_connect_timer->start(m_connect_timeout);
	return;

error:
	/* TODO ratelimited error message? */
	WARN("Failed to initiate connection");
	connectFailed();
}

void STSClientMgr::connectComplete(void)
{
	socklen_t elen = sizeof(int);
	int e, rc;

	rc = getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &e, &elen);
	if (!rc && !e) {
		DEBUG("Connected to STS");

		m_fdreg.reset();
		m_connect_timer->cancel();

		StorageContainer::SharedPtr &run = nextRun();

		try {
			new STSClient(m_fd, run, *this);
			m_connections++;
		} catch (...) {
			/* TODO narrow what we catch? */
			requeueRun(run);
			connectFailed();
			throw;
		}

		INFO("Sending run " << run->runNumber());
		m_connecting = false;
		startConnect();
		return;
	}

	if (rc < 0)
		e = errno;

	if (e == EINTR || e == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		return;
	}

	/* TODO ratelimited logging of connection issue */
	WARN("Connection to STS failed: " << strerror(e));
	connectFailed();
}

void STSClientMgr::connectFailed(void)
{
	if (m_fd != -1)
		close(m_fd);
	m_fd = -1;
	m_fdreg.reset();
	m_connect_timer->cancel();
	m_reconnect_timer->start(m_reconnect_timeout);
}

bool STSClientMgr::connectTimeout(void)
{
	/* TODO ratelimited log message */
	connectFailed();
	return false;
}

bool STSClientMgr::reconnectTimeout(void)
{
	m_connecting = false;
	startConnect();
	return false;
}

bool STSClientMgr::transientTimeout(void)
{
	m_backoff = false;
	startConnect();
	return false;
}

void STSClientMgr::clientComplete(StorageContainer::SharedPtr &c,
				  Disposition disp)
{
	/* TODO this shares code with requeueRun, find a beter place? */
	RunMap::iterator it;

	it = m_sendingRuns.find(c->runNumber());
	if (it != m_sendingRuns.end())
		m_sendingRuns.erase(it);

	switch (disp) {
	case SUCCESS:
		/* STSClient already logged our success, we just need to
		 * note that we've completed translation.
		 */
		c->markTranslated();
		break;
	case CONNECTION_LOSS:
	case TRANSIENT_FAIL:
	case INVALID_PROTOCOL:
		/* TODO need to limit the number of tries for this run before
		 * we make it a permament failure case
		 */
		/* We shouldn't pound on the STS if we keep hitting problems,
		 * back off and give it time to breathe.
		 */
		if (!m_backoff) {
			m_backoff = true;
			m_transient_timer->start(m_transient_timeout);
		}
		requeueRun(c);
		break;
	case PERMAMENT_FAIL:
		/* STSClient already logged the failure, we just need to
		 * mark it for manual processing.
		 */
		c->markManual();
		break;
	}

	m_connections--;
	startConnect();
}

void STSClientMgr::config(const boost::property_tree::ptree &conf)
{
	m_connect_timeout =
			conf.get<double>("stsclient.connect_timeout", 15.0);
	m_reconnect_timeout =
			conf.get<double>("stsclient.reconnect_timeout", 15.0);
	m_transient_timeout =
			conf.get<double>("stsclient.transient_timeout", 60.0);
	m_max_connections =
			conf.get<unsigned int>("stsclient.max_connections", 3);

	std::string uri = conf.get<std::string>("stsclient.uri", "localhost");
	const char *default_service = "31417";
	size_t pos = uri.find_first_of(':');

	if (pos != std::string::npos) {
		m_node = uri.substr(0, pos);
		if (pos != uri.length())
			m_service = uri.substr(pos + 1);
		else
			m_service = default_service;
	} else {
		m_node = uri;
		m_service = default_service;
	}
}

void STSClientMgr::init(void)
{
	m_singleton = new STSClientMgr();
}
