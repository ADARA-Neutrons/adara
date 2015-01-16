#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "STSClientMgr.h"
#include "STSClient.h"
#include "SignalEvents.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.STSClientMgr"));

class MaxConnectionsPV : public smsUint32PV {
public:
	MaxConnectionsPV(const std::string &name, STSClientMgr *stsClientMgr) :
		smsUint32PV(name), m_stsClientMgr(stsClientMgr) {}

private:
	STSClientMgr *m_stsClientMgr;

	void changed(void)
	{
		// Give Peace a Chance...
		// When we change the Max Number of STS Connections,
		// see if we have anything new to do now... ;-D
		DEBUG("MaxConnectionsPV: " << m_pv_name
			<< " PV value changed, Start Any STS Client Connections...");
		m_stsClientMgr->startConnect();
	}
};

std::string STSClientMgr::m_node;
std::string STSClientMgr::m_service;
double STSClientMgr::m_connect_timeout = 15.0;
double STSClientMgr::m_connect_retry = 15.0;
double STSClientMgr::m_transient_timeout = 60.0;
unsigned int STSClientMgr::m_max_connections = 3;
uint32_t STSClientMgr::m_max_requeue_count = 5;

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

	// Create Run-Time Configuration PVs for STS Client...

	SMSControl *ctrl = SMSControl::getInstance();
	if (!ctrl) {
		throw std::logic_error(
			"uninitialized SMSControl obj for STSClientMgr!");
	}

	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS";
	prefix += ":STSClient";

	m_pvConnectTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectTimeout"));

	m_pvConnectRetry = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectRetry"));

	m_pvTransientTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":TransientTimeout"));

	m_pvMaxConnections = boost::shared_ptr<MaxConnectionsPV>(new
		MaxConnectionsPV(prefix + ":MaxConnections", this));

	m_pvMaxRequeueCount = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MaxRequeueCount"));

	m_pvServiceURI = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":ServiceURI"));

	ctrl->addPV(m_pvConnectTimeout);
	ctrl->addPV(m_pvConnectRetry);
	ctrl->addPV(m_pvTransientTimeout);
	ctrl->addPV(m_pvMaxConnections);
	ctrl->addPV(m_pvMaxRequeueCount);
	ctrl->addPV(m_pvServiceURI);

	// Initialize STS Client PVs...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvConnectTimeout->update(m_connect_timeout, &now);
	m_pvConnectRetry->update(m_connect_retry, &now);
	m_pvTransientTimeout->update(m_transient_timeout, &now);
	m_pvMaxConnections->update(m_max_connections, &now);
	m_pvMaxRequeueCount->update(m_max_requeue_count, &now);
	m_pvServiceURI->update(m_node + ":" + m_service, &now);

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

void STSClientMgr::dequeueRun(StorageContainer::SharedPtr &c)
{
	RunMap::iterator it;

	it = m_sendingRuns.find(c->runNumber());
	if (it != m_sendingRuns.end())
		m_sendingRuns.erase(it);
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

	// Update Max Connections from PV...
	m_max_connections = m_pvMaxConnections->value();

	DEBUG("Checking for pending work (" << state
		<< m_connections << " of " << m_max_connections << " active, "
		<< m_pendingRuns.size() << " pending runs)");

	if (m_backoff || m_connecting || m_connections >= m_max_connections ||
					m_pendingRuns.empty())
		return;

	// Update STS Service URI from PV...
	std::string uri = m_pvServiceURI->value();
	const char *default_service = "31417";
	size_t pos = uri.find_first_of(':');
	std::string node, service;
	if (pos != std::string::npos) {
		node = uri.substr(0, pos);
		if (pos != uri.length())
			service = uri.substr(pos + 1);
		else
			service = default_service;
	} else {
		node = uri;
		service = default_service;
	}
	if ( node != m_node || service != m_service ) {
		DEBUG("startConnect(): Updating STS Service URI from PV: "
			<< m_node << ":" << m_service
			<< " -> " << node << ":" << service);
		m_node = node;
		m_service = service;
	}

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
			/* TODO rate-limited lookup error (or just once per
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
		/* TODO rate-limited logging of refused connection */
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

	// Update Connect Timeout from PV...
	m_connect_timeout = m_pvConnectTimeout->value();
	m_connect_timer->start(m_connect_timeout);
	return;

error:
	/* TODO rate-limited error message? */
	WARN("Failed to initiate connection");
	connectFailed();
}

void STSClientMgr::connectComplete(void)
{
	DEBUG("connectComplete() entry");

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
			ERROR("connectComplete() - STSClient() failed?");
			/* TODO narrow what we catch? */
			dequeueRun(run); // clean up...
			queueRun(run); // re-queue run...
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
		DEBUG("connectComplete() odd-but-harmless exit");
		return;
	}

	/* TODO rate-limited logging of connection issue */
	WARN("Connection to STS failed: " << strerror(e));
	connectFailed();

	DEBUG("connectComplete() failed exit");
}

void STSClientMgr::connectFailed(void)
{
	if (m_fd != -1)
		close(m_fd);
	m_fd = -1;
	m_fdreg.reset();
	m_connect_timer->cancel();
	// Update Connect Retry Timeout from PV...
	m_connect_retry = m_pvConnectRetry->value();
	m_reconnect_timer->start(m_connect_retry);
}

bool STSClientMgr::connectTimeout(void)
{
	/* TODO rate-limited log message */
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
	// clean up...
	dequeueRun(c);

	switch (disp) {
	case SUCCESS:
		/* STSClient already logged our success, we just need to
		 * note that we've completed translation.
		 */
		c->markTranslated();
		break;
	case CONNECTION_LOSS:
	case INVALID_PROTOCOL:
		/* We shouldn't pound on the STS if we keep hitting problems,
		 * back off and give it time to breathe.
		 */
		if (!m_backoff) {
			m_backoff = true;
			// Update Transient Timeout from PV...
			m_transient_timeout = m_pvTransientTimeout->value();
			m_transient_timer->start(m_transient_timeout);
		}
		queueRun(c); // re-queue run...
		break;
	case TRANSIENT_FAIL:
		/* Limit the number of retries for this run before
		 * we make it a permanent failure case. [leerw]
		 */
		// Update Max Requeue Count from PV...
		m_max_requeue_count = m_pvMaxRequeueCount->value();
		ERROR("Transient Failure Run=" << c->runNumber()
			<< " disp=" << disp
			<< " requeueCount=" << c->getRequeueCount()
			<< "/" << m_max_requeue_count);
		// Maxed Out Re-Queue Retries, Mark for Manual!
		if ( c->getRequeueCount() >= m_max_requeue_count ) {
			ERROR("Maximum Re-Queue Count Reached!"
				<< " Marking Run " << c->runNumber()
				<< " for Manual Translation");
			c->markManual();
		}
		// Re-Queue Run to Try Again...
		else {
			// Increment Re-Queue Count
			c->incrRequeueCount();
			ERROR("Requeueing Run " << c->runNumber()
				<< ", Re-Queue #" << c->getRequeueCount());
			/* We shouldn't pound on the STS if we keep hitting problems,
			 * back off and give it time to breathe.
			 */
			if (!m_backoff) {
				m_backoff = true;
				// Update Transient Timeout from PV...
				m_transient_timeout = m_pvTransientTimeout->value();
				m_transient_timer->start(m_transient_timeout);
			}
			queueRun(c); // re-queue run...
		}
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
	m_connect_retry =
			conf.get<double>("stsclient.connect_retry",
				conf.get<double>("stsclient.reconnect_timeout", 15.0) );
	m_transient_timeout =
			conf.get<double>("stsclient.transient_timeout", 60.0);
	m_max_connections =
			conf.get<unsigned int>("stsclient.max_connections", 3);
	m_max_requeue_count =
			conf.get<uint32_t>("stsclient.max_requeue_count", 5);

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
