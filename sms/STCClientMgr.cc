
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.STCClientMgr"));

#include <string>
#include <sstream>

#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "StorageManager.h"
#include "StorageContainer.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "STCClientMgr.h"
#include "STCClient.h"
#include "SignalEvents.h"

RateLimitedLogging::History RLLHistory_STCClientMgr;

// Rate-Limited Logging IDs...
#define RLL_STC_CLIENT_LOOKUP_FAILED   0
#define RLL_STC_CONNECTION_REFUSED     1
#define RLL_STC_CONNECTION_UNAVAIL     2
#define RLL_STC_CONNECTION_INTR        3
#define RLL_STC_CONNECTION_INPROGRESS  4
#define RLL_STC_UNEXPECTED_CONN_ERROR  5
#define RLL_STC_FAILED_TO_CONNECT      6
#define RLL_STC_CONNECTION_FAILED      7
#define RLL_STC_CONNECTION_TIMED_OUT   8
#define RLL_STC_BOGUS_LOOKUP_SIGNAL    9

class MaxConnectionsPV : public smsUint32PV {
public:
	MaxConnectionsPV(const std::string &name, STCClientMgr *stcClientMgr,
			uint32_t min = 0, uint32_t max = INT32_MAX,
			bool auto_save = false) :
		smsUint32PV(name, min, max, auto_save),
		m_stcClientMgr(stcClientMgr),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			std::stringstream ss;
			ss << value();
			StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
		}

		// Give Peace a Chance...
		// When we change the Max Number of STC Connections,
		// see if we have anything new to do now... ;-D
		DEBUG("MaxConnectionsPV: " << m_pv_name
			<< " PV Value Changed to " << value()
			<< ", Start Any STC Client Connections...");
		m_stcClientMgr->startConnect();
	}

private:
	STCClientMgr *m_stcClientMgr;

	bool m_auto_save;
};

std::string STCClientMgr::m_node;
std::string STCClientMgr::m_service;
double STCClientMgr::m_connect_timeout = 15.0;
double STCClientMgr::m_connect_retry = 15.0;
double STCClientMgr::m_transient_timeout = 60.0;
unsigned int STCClientMgr::m_max_connections = 3;
uint32_t STCClientMgr::m_max_requeue_count = 5;
bool STCClientMgr::m_send_paused_data = false;

STCClientMgr *STCClientMgr::m_singleton;

STCClientMgr::STCClientMgr() :
	m_signalEvents(new SignalEvents()),
	m_connect_timer(new TimerAdapter<STCClientMgr>(this,
					&STCClientMgr::connectTimeout)),
	m_reconnect_timer(new TimerAdapter<STCClientMgr>(this,
					&STCClientMgr::reconnectTimeout)),
	m_transient_timer(new TimerAdapter<STCClientMgr>(this,
					&STCClientMgr::transientTimeout)),
	m_lookup_timer(new TimerAdapter<STCClientMgr>(this,
					&STCClientMgr::lookupTimeout)),
	m_fd(-1), m_fdreg(NULL), m_connecting(false), m_backoff(false),
	m_connections(0), m_queueMode(BALANCE), m_sendNext(OLDEST),
	m_currentRun(0)
{
	m_sigevent.sigev_notify = SIGEV_SIGNAL;
	m_sigevent.sigev_signo = m_signalEvents->allocateRTsig(
		boost::bind(&STCClientMgr::lookupComplete, this, _1));

	m_gai.ar_name = (char *) NULL;
	m_gai.ar_service = (char *) NULL;
	m_gai.ar_request = (struct addrinfo *) NULL;
	m_gai.ar_result = (struct addrinfo *) NULL;

	memset(&m_gai_hints, 0, sizeof(m_gai_hints));
	m_gai_hints.ai_family = AF_INET6;
	m_gai_hints.ai_socktype = SOCK_STREAM;
	m_gai_hints.ai_protocol = IPPROTO_TCP;
	m_gai_hints.ai_flags = AI_CANONNAME | AI_V4MAPPED;

	m_mgrConnection = StorageManager::onContainerChange(
		boost::bind(&STCClientMgr::containerChange, this, _1, _2));

	// Create Run-Time Configuration PVs for STC Client...

	SMSControl *ctrl = SMSControl::getInstance();
	if (!ctrl) {
		throw std::logic_error(
			"uninitialized SMSControl obj for STCClientMgr!");
	}

	std::string prefix(ctrl->getPVPrefix());
	prefix += ":STCClient";

	m_pvConnectTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvConnectRetryTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectRetryTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvTransientTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":TransientTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvMaxConnections = boost::shared_ptr<MaxConnectionsPV>(new
		MaxConnectionsPV(prefix + ":MaxConnections", this,
			0, INT32_MAX, /* AutoSave */ true));

	m_pvMaxRequeueCount = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MaxRequeueCount", 0, INT32_MAX,
			/* AutoSave */ true));

	m_pvSendPausedData = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":SendPausedData", /* AutoSave */ true));

	m_pvServiceURI = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":ServiceURI", /* AutoSave */ true));

	ctrl->addPV(m_pvConnectTimeout);
	ctrl->addPV(m_pvConnectRetryTimeout);
	ctrl->addPV(m_pvTransientTimeout);
	ctrl->addPV(m_pvMaxConnections);
	ctrl->addPV(m_pvMaxRequeueCount);
	ctrl->addPV(m_pvSendPausedData);
	ctrl->addPV(m_pvServiceURI);

	// Initialize STC Client PVs...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvConnectTimeout->update(m_connect_timeout, &now);
	m_pvConnectRetryTimeout->update(m_connect_retry, &now);
	m_pvTransientTimeout->update(m_transient_timeout, &now);
	m_pvMaxConnections->update(m_max_connections, &now);
	m_pvMaxRequeueCount->update(m_max_requeue_count, &now);
	m_pvSendPausedData->update(m_send_paused_data, &now);
	m_pvServiceURI->update(m_node + ":" + m_service, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	uint32_t uvalue;
	double dvalue;
	bool bvalue;

	if ( StorageManager::getAutoSavePV( m_pvConnectTimeout->getName(),
			dvalue, ts ) ) {
		m_connect_timeout = dvalue;
		m_pvConnectTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvConnectRetryTimeout->getName(),
			dvalue, ts ) ) {
		m_connect_retry = dvalue;
		m_pvConnectRetryTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvTransientTimeout->getName(),
			dvalue, ts ) ) {
		m_transient_timeout = dvalue;
		m_pvTransientTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvMaxConnections->getName(),
			uvalue, ts ) ) {
		m_max_connections = uvalue;
		m_pvMaxConnections->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvMaxRequeueCount->getName(),
			uvalue, ts ) ) {
		m_max_requeue_count = uvalue;
		m_pvMaxRequeueCount->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvSendPausedData->getName(),
			bvalue, ts ) ) {
		m_send_paused_data = bvalue;
		m_pvSendPausedData->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvServiceURI->getName(),
			value, ts ) ) {
		// Update STC Service URI from PV AutoSave Value...
		std::string uri = value;
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
		DEBUG("Updating STC Service URI from AutoSave PV Value: "
			<< m_node << ":" << m_service
			<< " -> " << node << ":" << service);
		m_node = node;
		m_service = service;
		m_pvServiceURI->update(m_node + ":" + m_service, &ts);
	}

	// Done...

	INFO("Remote is " << m_node << ":" << m_service);
}

STCClientMgr::~STCClientMgr()
{
	SMSControl *ctrl = SMSControl::getInstance();
	if (m_connect_timer) {
		m_connect_timer->cancel();
		delete m_connect_timer;
		m_connect_timer = NULL;
	}
	if (m_reconnect_timer) {
		m_reconnect_timer->cancel();
		delete m_reconnect_timer;
		m_reconnect_timer = NULL;
	}
	if (m_transient_timer) {
		m_transient_timer->cancel();
		delete m_transient_timer;
		m_transient_timer = NULL;
	}
	if (m_lookup_timer) {
		m_lookup_timer->cancel();
		delete m_lookup_timer;
		m_lookup_timer = NULL;
	}
	m_singleton = NULL;
	m_mgrConnection.disconnect();
	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close(m_fd);
		m_fd = -1;
	}
}

void STCClientMgr::containerChange(StorageContainer::SharedPtr &c,
		bool starting)
{
	if (!c->runNumber())
		return;

	if (starting) {
		StorageManager::sendComBus(c->runNumber(), c->propId(),
			std::string("SMS Start Run Sent to STC"));
		queueRun(c);
		startConnect();
	} else
		m_currentRun = 0;
}

void STCClientMgr::queueRun(StorageContainer::SharedPtr &c)
{
	std::pair<RunMap::iterator, bool> ret;

	ret = m_pendingRuns.insert(std::make_pair(c->runNumber(), c));
	if (!ret.second) {
		ERROR("queueRun(): Duplicate Run Number " << c->runNumber()
			<< ", Already in Pending Run Queue! Ignoring...");
		return;
	}

	if (c->active())
		m_currentRun = c->runNumber();
}

void STCClientMgr::dequeueRun(StorageContainer::SharedPtr &c)
{
	RunMap::iterator it;

	it = m_sendingRuns.find(c->runNumber());
	if (it != m_sendingRuns.end())
		m_sendingRuns.erase(it);
}

StorageContainer::SharedPtr &STCClientMgr::nextRun(void)
{
	RunMap::iterator run;
	QueueMode next;

	if (m_pendingRuns.empty())
		throw std::logic_error("Trying to Dequeue When Pending Runs Empty");

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

	// Save Run Number for Return Indexing, as it will be Freed...! ;-D
	// (Thanks Valgrind! ;-D)
	uint32_t runNumber = run->first;
	m_sendingRuns[runNumber] = run->second;
	m_pendingRuns.erase(run);
	return m_sendingRuns[runNumber];
}

void STCClientMgr::startConnect(void)
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

	if (m_backoff || m_connecting || m_connections >= m_max_connections
			|| m_pendingRuns.empty()) {
		return;
	}

	// Update STC Service URI from PV...
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
		DEBUG("startConnect(): Updating STC Service URI from PV: "
			<< m_node << ":" << m_service
			<< " -> " << node << ":" << service);
		m_node = node;
		m_service = service;
	}

	m_gai.ar_name = m_node.c_str();
	m_gai.ar_service = m_service.c_str();
	m_gai.ar_request = &m_gai_hints;

	// Free Any Previous AddrInfo Results Returned...
	if ( m_gai.ar_result != NULL ) {
		freeaddrinfo( m_gai.ar_result );
		m_gai.ar_result = NULL;
	}

	// I'm only paranoid if they're not actually out to get me... ;-D
	DEBUG("startConnect():"
		<< " Requesting STC Network Address and Service Translation"
		<< " Name=" << m_node
		<< " Service=" << m_service
		<< " Hints: Family=" << m_gai_hints.ai_family
			<< " (" << ( AF_INET6 ) << ")"
		<< " Socktype=" << m_gai_hints.ai_socktype
			<< " (" << ( SOCK_STREAM ) << ")"
		<< " Protocol=" << m_gai_hints.ai_protocol
			<< " (" << ( IPPROTO_TCP ) << ")"
		<< " Flags=" << m_gai_hints.ai_flags
			<< " (" << ( AI_CANONNAME | AI_V4MAPPED ) << ")");

	// Make Sure Something is Still Listening for Us...! ;-D
	if ( !(m_signalEvents->valid()) ) {
		ERROR("startConnect(): Whoa...! SignalEvents Went Invalid...!"
			<< " Re-Allocate RT Signal...");
		m_sigevent.sigev_notify = SIGEV_SIGNAL;
		m_sigevent.sigev_signo = m_signalEvents->allocateRTsig(
			boost::bind(&STCClientMgr::lookupComplete, this, _1));
	}

	rc = getaddrinfo_a(GAI_NOWAIT, &gai, 1, &m_sigevent);
	if (rc) {
		// *Don't* Throw Exception, But Log Potentially Non-Transient Error!
		// (and just return to Try Again Later... (via timeout, etc))
		// (Note: "Transient" error indicated by EAI_AGAIN...?)
		/* Rate-limited lookup error (or just once per failure type?) */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CLIENT_LOOKUP_FAILED, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info
				<< "Asynchronous Lookup Failed to Enqueue for STC at "
				<< m_node << ":" << m_service << " - " << gai_strerror(rc));
		}
		return;
	}

	// Make Sure We Eventually Come Back, Even If Lookup Never Returns...!
	// (Cheat and Use Transient Lookup Timeout for Lookups... ;-D)
	m_lookup_timer->start(m_transient_timeout);

	m_connecting = true;
}

void STCClientMgr::lookupComplete(const struct signalfd_siginfo &info)
{
	SMSControl *ctrl = SMSControl::getInstance();

	struct addrinfo *ai;
	int flags, rc;

	// Ok, We Made It Back from the Asynchronous Lookup... ;-D
	m_lookup_timer->cancel();

	/* Make sure we're expecting this signal, and that it comes from us. */
	if ( !m_connecting || info.ssi_pid != (uint32_t) getpid()
			|| info.ssi_code != SI_ASYNCNL ) {
		/* Rate-limited lookup error (or just once per failure type?) */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_BOGUS_LOOKUP_SIGNAL, "none",
				60, 3, 10, log_info ) ) {
			ERROR(log_info
				<< "Unexpected Asynchronous Lookup Complete Signal for STC?"
				<< " Ignoring."
				<< " m_connecting=" << m_connecting
				<< " info.ssi_pid=" << info.ssi_pid
				<< " getpid()=" << getpid()
				<< " info.ssi_code=" << info.ssi_code);
		}
		return;
	}

	std::string log_info;

	rc = gai_error(&m_gai);
	if (rc) {
		// It doesn't really matter What the Error Code was (so I hope :-)
		// just Log It, Mark the Connection as Failed, and then
		// return to Try Again Later... (via timeout, etc)
		/* Rate-limited lookup error (or just once per failure type?) */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CLIENT_LOOKUP_FAILED, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info << "Asynchronous Lookup Failed for STC at "
				<< m_node << ":" << m_service << " - " << gai_strerror(rc));
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
			DEBUG("Connecting to STC at " << host << ":" << service);
		} else {
			DEBUG("Connecting to STC, getnameinfo failed");
		}
	}

 	// Free Any Previous ReadyAdapter...
	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}

	// Free Any Previous File Descriptor...
	// (If it got passed down into STCClient(), then we already cleared it!)
	if (m_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close(m_fd);
		m_fd = -1;
	}

	// Get New Socket for _Next_ (Possibly Concurrent) STCClient()...!
	m_fd = socket(ai->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		m_fd = -1;   // just to be sure... ;-b
		goto error;
	}
	if (ctrl->verbose() > 0) {
		DEBUG("New Socket m_fd=" << m_fd);
	}

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
		/* Rate-limited logging of refused connection */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CONNECTION_REFUSED, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info << "Connection Refused for STC at "
				<< m_node << ":" << m_service
				<< " (m_fd=" << m_fd << ")");
		}
		goto error;
	case EAGAIN:
		/* Rate-limited logging of resource temporarily unavailable */
		/* [Not that Paranoid...! ;-D] */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CONNECTION_UNAVAIL, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info << "Connection "
				<< "Resource Temporarily Unavailable for STC at "
				<< m_node << ":" << m_service << " - Ignoring..."
				<< " (m_fd=" << m_fd << ")");
		}
		break;
	case EINTR:
		/* [PARANOID] Rate-limited logging of interrupted connection */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CONNECTION_INTR, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info << "Connection Interrupted for STC at "
				<< m_node << ":" << m_service << " - Ignoring..."
				<< " (m_fd=" << m_fd << ")");
		}
		break;
	case EINPROGRESS:
		/* [PARANOID] Rate-limited logging of already-in-progress connect */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_CONNECTION_INPROGRESS, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			// Apparently, This Happens A Lot... ;-Q  Make it just "Info"!
			INFO(log_info << "Connection In Progress for STC at "
				<< m_node << ":" << m_service << " - Ignoring..."
				<< " (m_fd=" << m_fd << ")");
		}
		break;
	case 0:
		DEBUG("Fast Connect!");
		connectComplete();
		return;
	default:
		/* TODO other errors! */
		/* Rate-limited logging of unexpected connection error */
		if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
				RLL_STC_UNEXPECTED_CONN_ERROR, m_node + ":" + m_service,
				60, 3, 10, log_info ) ) {
			ERROR(log_info << "Unexpected Error from Connect to STC at "
				<< m_node << ":" << m_service
				<< " (m_fd=" << m_fd << ")"
				<< " - " << strerror(rc));
		}
		goto error;
	}
 
	// File Descriptor Already Checked Above...
	try {
		m_fdreg = new ReadyAdapter(m_fd, fdrWrite,
			boost::bind(&STCClientMgr::connectComplete, this),
			ctrl->verbose());
	} catch (std::exception &e) {
		ERROR("Exception in lookupComplete()"
			<< " Creating ReadyAdapter Write - " << e.what());
		m_fdreg = NULL; // just to be sure... ;-b
		goto error;
	} catch (...) {
		ERROR("Unknown Exception in lookupComplete()"
			<< " Creating ReadyAdapter Write");
		m_fdreg = NULL; // just to be sure... ;-b
		goto error;
	}

	// Update Connect Timeout from PV...
	m_connect_timeout = m_pvConnectTimeout->value();
	m_connect_timer->start(m_connect_timeout);
	DEBUG("Waiting for Connect to STC at "
		<< m_node << ":" << m_service
		<< " - Timeout " << m_connect_timeout << " Seconds");
	return;

error:

	/* Rate-limited connection failure message */
	if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
			RLL_STC_FAILED_TO_CONNECT, m_node + ":" + m_service,
			60, 3, 10, log_info ) ) {
		ERROR(log_info << "Failed to Initiate Connection to STC at "
			<< m_node << ":" << m_service
			<< " (m_fd=" << m_fd << ")");
	}
	connectFailed();
}

void STCClientMgr::connectComplete(void)
{
	DEBUG("connectComplete() entry");

	socklen_t errlen = sizeof(int);
	int err, rc;

	std::string log_info;

	// Check File Descriptor...
	if (m_fd < 0) {
		ERROR("connectComplete(): Invalid File Descriptor for STC");
		connectFailed();
		return;
	}

	rc = getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
	if (!rc && !err) {
		DEBUG("Connected to STC at " << m_node << ":" << m_service
			<< " (m_fd=" << m_fd << ")");

		if (m_fdreg) {
			delete m_fdreg;
			m_fdreg = NULL;
		}

		m_connections++;

		m_connect_timer->cancel();

		// Update Send Paused Data Option from PV...
		// (STCClient reads _Once_ on init...)
		m_send_paused_data = m_pvSendPausedData->value();

		StorageContainer::SharedPtr &run = nextRun();

		try {
			new STCClient(m_fd, run, *this);
		}
		catch (std::exception &e) {
			// Don't Throw Exception Here, Just Re-Queue Run to Try Again...
			/* Rate-limited logging of connection issue */
			if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
					RLL_STC_CONNECTION_FAILED, m_node + ":" + m_service,
					60, 3, 10, log_info ) ) {
				ERROR(log_info << "Connection Failed to STC at "
					<< m_node << ":" << m_service << " - "
					<< "STCClient() failed?"
					<< " Re-Queueing Run " << run->runNumber() << "... "
					<< e.what());
			}
			m_connections--; // clientComplete() won't get called...
			// Note: Must Re-Queue Run _Before_ De-Queue,
			// So Any Rescan Run Containers Aren't Deallocated...! ;-D
			queueRun(run); // re-queue Run on Pending list...
			dequeueRun(run); // clean up Run from Sending list...
			connectFailed();
			return;
		}
		catch (...) {
			// Don't Throw Exception Here, Just Re-Queue Run to Try Again...
			/* Rate-limited logging of connection issue */
			if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
					RLL_STC_CONNECTION_FAILED, m_node + ":" + m_service,
					60, 3, 10, log_info ) ) {
				ERROR(log_info << "Connection Failed to STC at "
					<< m_node << ":" << m_service << " - "
					<< "STCClient() failed?"
					<< " Re-Queueing Run " << run->runNumber() << "... "
					<< "Unknown Exception.");
			}
			m_connections--; // clientComplete() won't get called...
			// Note: Must Re-Queue Run _Before_ De-Queue,
			// So Any Rescan Run Containers Aren't Deallocated...! ;-D
			queueRun(run); // re-queue Run on Pending list...
			dequeueRun(run); // clean up Run from Sending list...
			connectFailed();
			return;
		}

		INFO("Sending Run " << run->runNumber() << " to STC at "
			<< m_node << ":" << m_service);
		// Clear Out This Socket/File Descriptor, We've Handed It Off Now...
		// (We *Don't* Want to Close It Out from Under the STCClient...!)
		m_fd = -1;
		m_connecting = false;
		startConnect();
		return;
	}

	if (rc < 0)
		err = errno;

	if (err == EAGAIN || err == EINTR || err == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		DEBUG("connectComplete() Odd-But-Harmless Connection Failure"
			<< " to STC at " << m_node << ":" << m_service << " - "
			<< strerror(err) << " (" << err << ")");
		return;
	}

	/* Rate-limited logging of connection issue */
	if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
			RLL_STC_CONNECTION_FAILED, m_node + ":" + m_service,
			60, 3, 10, log_info ) ) {
		ERROR(log_info << "Connection Failed to STC at "
			<< m_node << ":" << m_service << " - "
			<< strerror(err) << " (" << err << ")");
	}
	connectFailed();

	DEBUG("connectComplete() failed exit");
}

void STCClientMgr::connectFailed(void)
{
	SMSControl *ctrl = SMSControl::getInstance();
	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("connectFailed(): Close m_fd=" << m_fd);
		}
		close(m_fd);
		m_fd = -1;
	}
	m_connect_timer->cancel();
	// Update Connect Retry Timeout from PV...
	m_connect_retry = m_pvConnectRetryTimeout->value();
	m_reconnect_timer->start(m_connect_retry);
}

bool STCClientMgr::connectTimeout(void)
{
	/* Rate-limited connection timed out message */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_STCClientMgr,
			RLL_STC_CONNECTION_TIMED_OUT, m_node + ":" + m_service,
			60, 3, 10, log_info ) ) {
		ERROR(log_info << "Timed Out Connecting to STC at "
			<< m_node << ":" << m_service);
	}
	connectFailed();
	return false;
}

bool STCClientMgr::reconnectTimeout(void)
{
	DEBUG("reconnectTimeout(): Restarting STC Connection...");
	m_connecting = false;
	startConnect();
	return false;
}

bool STCClientMgr::transientTimeout(void)
{
	DEBUG("transientTimeout(): Retrying STC Connection...");
	m_backoff = false;
	startConnect();
	return false;
}

bool STCClientMgr::lookupTimeout(void)
{
	DEBUG("lookupTimeout(): Restarting STC Connection...");
	m_connecting = false;
	startConnect();
	return false;
}

void STCClientMgr::clientComplete(StorageContainer::SharedPtr &c,
		Disposition disp, std::string reason)
{
	// clean up...
	dequeueRun(c);

	std::string result;
	switch (disp) {
	case SUCCESS:
		/* STCClient already logged our success, we just need to
		 * note that we've completed translation.
		 */
		result = "Translation Succeeded";
		if ( !reason.empty() )
			result += " - " + reason;
		StorageManager::sendComBus(c->runNumber(), c->propId(), result);
		c->markTranslated();
		break;
	case CONNECTION_LOSS:
	case INVALID_PROTOCOL:
		/* We shouldn't pound on the STC if we keep hitting problems,
		 * back off and give it time to breathe.
		 */
		if (!m_backoff) {
			m_backoff = true;
			// Update Transient Timeout from PV...
			m_transient_timeout = m_pvTransientTimeout->value();
			m_transient_timer->start(m_transient_timeout);
		}
		result = "STC Connection Error";
		if ( !reason.empty() )
			result += " - " + reason;
		StorageManager::sendComBus(c->runNumber(), c->propId(), result);
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
			result = "Needs Manual Translation";
			if ( !reason.empty() )
				result += " - " + reason;
			StorageManager::sendComBus(c->runNumber(), c->propId(), result);
			c->markManual();
		}
		// Re-Queue Run to Try Again...
		else {
			// Increment Re-Queue Count
			c->incrRequeueCount();
			ERROR("Re-Queueing Run " << c->runNumber()
				<< ", Re-Queue #" << c->getRequeueCount());
			result = "STC transient Error";
			if ( !reason.empty() )
				result += " - " + reason;
			StorageManager::sendComBus(c->runNumber(), c->propId(), result);
			/* We shouldn't pound on the STC if we keep hitting problems,
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
		/* STCClient already logged the failure, we just need to
		 * mark it for manual processing.
		 */
		result = "Needs Manual Translation";
		if ( !reason.empty() )
			result += " - " + reason;
		StorageManager::sendComBus(c->runNumber(), c->propId(), result);
		c->markManual();
		break;
	}

	m_connections--;
	startConnect();
}

void STCClientMgr::config(const boost::property_tree::ptree &conf)
{
	m_connect_timeout =
			conf.get<double>("stcclient.connect_timeout", 15.0);
	m_connect_retry =
			conf.get<double>("stcclient.connect_retry",
				conf.get<double>("stcclient.reconnect_timeout", 15.0) );
	m_transient_timeout =
			conf.get<double>("stcclient.transient_timeout", 60.0);
	m_max_connections =
			conf.get<unsigned int>("stcclient.max_connections", 3);
	m_max_requeue_count =
			conf.get<uint32_t>("stcclient.max_requeue_count", 5);
	m_send_paused_data =
			conf.get<bool>("stcclient.send_paused_data", false);

	std::string uri = conf.get<std::string>("stcclient.uri", "localhost");
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

void STCClientMgr::init(void)
{
	m_singleton = new STCClientMgr();
}
