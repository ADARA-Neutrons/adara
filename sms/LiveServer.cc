
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.LiveServer"));

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "EPICS.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "LiveServer.h"
#include "LiveClient.h"

class ListenStringPV : public smsStringPV {
public:
	ListenStringPV(const std::string &name, LiveServer *liveServer,
			bool auto_save = false) :
		smsStringPV(name, auto_save), m_liveServer(liveServer),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			StorageManager::autoSavePV( m_pv_name, value(), &ts );
		}

		if ( ! m_liveServer->isInit() ) {
			DEBUG("ListenStringPV: " << m_pv_name << " PV value changed"
				<< " [" << value() << "]"
				<< " But Live Server Not Yet Initialized - Ignore...");
			return;
		}

		// On Any Change to the LiveServer Listener URI/Service PVs,
		// Reset the Listener Setup... :-D
		ERROR("ListenStringPV: " << m_pv_name << " PV value changed"
			<< " [" << value() << "]" << " - Reset Listener Setup...");
		m_liveServer->setupListener();
	}

private:
	LiveServer *m_liveServer;

	bool m_auto_save;
};

LiveServer *LiveServer::m_singleton;

std::string LiveServer::m_service;
std::string LiveServer::m_host;

double LiveServer::m_listen_retry;

bool LiveServer::m_send_paused_data = false;

void LiveServer::config(const boost::property_tree::ptree &conf)
{
	m_service = conf.get<std::string>("livestream.service", "31415");

	m_host = conf.get<std::string>("livestream.uri", "ANY");

	m_listen_retry = conf.get<double>("livestream.listen_retry", 5.0);

	m_send_paused_data =
		conf.get<bool>("livestream.send_paused_data", false);

	LiveClient::config(conf);
}

void LiveServer::init()
{
	m_singleton = new LiveServer();
}

LiveServer::LiveServer() :
		m_listen_timer(new TimerAdapter<LiveServer>(this,
			&LiveServer::listenRetry)),
		m_init(false), m_fdreg(NULL), m_addrinfo(NULL), m_fd(-1)
{
	// Create Run-Time Configuration PVs for Listener Params/Status

	SMSControl *ctrl = SMSControl::getInstance();
	if (!ctrl) {
		throw std::logic_error(
			"uninitialized SMSControl obj for LiveServer!");
	}

	std::string prefix(ctrl->getPVPrefix());
	prefix += ":LiveServer";

	m_pvListenStatus = boost::shared_ptr<smsErrorPV>(new
		smsErrorPV(prefix + ":ListenStatus"));

	m_pvListenRetryTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ListenRetryTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvListenerURI = boost::shared_ptr<ListenStringPV>(new
		ListenStringPV(prefix + ":ListenerURI", this,
			/* AutoSave */ true));
	m_pvListenerService = boost::shared_ptr<ListenStringPV>(new
		ListenStringPV(prefix + ":ListenerService", this,
			/* AutoSave */ true));

	m_pvSendPausedData = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":SendPausedData", /* AutoSave */ true));

	ctrl->addPV(m_pvListenStatus);
	ctrl->addPV(m_pvListenRetryTimeout);
	ctrl->addPV(m_pvListenerURI);
	ctrl->addPV(m_pvListenerService);
	ctrl->addPV(m_pvSendPausedData);

	// Initialize LiveServer PVs...

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	m_pvListenRetryTimeout->update(m_listen_retry, &now);

	m_pvListenerURI->update(m_host, &now);
	m_pvListenerService->update(m_service, &now);

	m_pvSendPausedData->update(m_send_paused_data, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	double dvalue;
	bool bvalue;

	if ( StorageManager::getAutoSavePV( m_pvListenRetryTimeout->getName(),
			dvalue, ts ) ) {
		m_listen_retry = dvalue;
		m_pvListenRetryTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvListenerURI->getName(),
			value, ts ) ) {
		m_host = value;
		m_pvListenerURI->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvListenerService->getName(),
			value, ts ) ) {
		m_service = value;
		m_pvListenerService->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvSendPausedData->getName(),
			bvalue, ts ) ) {
		m_send_paused_data = bvalue;
		m_pvSendPausedData->update(bvalue, &ts);
	}

	// We're Done Initializing Now...
	m_init = true;

	// Initialize Listener...
	setupListener();
}

LiveServer::~LiveServer()
{
	SMSControl *ctrl = SMSControl::getInstance();

	if ( m_addrinfo != NULL ) {
		freeaddrinfo( m_addrinfo );
		m_addrinfo = NULL;
	}
	if ( m_fdreg ) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if ( m_fd >= 0 ) {
		if ( ctrl->verbose() > 0 ) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close( m_fd );
		m_fd = -1;
	}
}

void LiveServer::setupListener(void)
{
	SMSControl *ctrl = SMSControl::getInstance();

	// Cancel Any Pending Listen Retry Timer...

	m_listen_timer->cancel();

	// Free Any Previous Listener Connection

	if ( m_addrinfo != NULL ) {
		freeaddrinfo( m_addrinfo );
		m_addrinfo = NULL;
	}

	if ( m_fdreg ) {
		delete m_fdreg;
		m_fdreg = NULL;
	}

	if ( m_fd >= 0 ) {
		if ( ctrl->verbose() > 0 ) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close( m_fd );
		m_fd = -1;
	}

	// Set Up New Listener Connection

	struct addrinfo hints;
	int val, rc, flags;
	char *node;

	struct timespec now;

	std::string service;
	std::string host;
	std::string msg;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Update Listener URI from PV...
	host = m_pvListenerURI->value();
	// Not Soup Yet...
	if ( !host.compare("(unset)") ) {
		msg = "Listener URI Not Set Yet - Defer...";
		goto error;
	}
	m_host = host;
	if ( !m_host.compare("ANY") )
		node = (char *) NULL;
	else
		node = (char *) m_host.c_str();

	// Update Listener Service from PV...
	service = m_pvListenerService->value();
	// Not Soup Yet...
	if ( !service.compare("(unset)") ) {
		msg = "Listener Service Not Set Yet - Defer...";
		goto error;
	}
	m_service = service;

	rc = getaddrinfo(node, m_service.c_str(), &hints, &m_addrinfo);
	if (rc) {
		msg = "Unable to convert host/service '";
		msg += m_host;
		msg += ":";
		msg += m_service;
		msg += "' to a port: ";
		msg += gai_strerror(rc);
		goto error;
	}

	m_fd = socket(m_addrinfo->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		msg = "Unable to create socket: ";
		msg += strerror(errno);
		m_fd = -1;   // just to be sure... ;-b
		goto error;
	}
	if ( ctrl->verbose() > 0 ) {
		DEBUG("New Listener Socket m_fd=" << m_fd);
	}

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0 || fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		msg = "Unable to set socket non-blocking: ";
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += strerror(errno);
		goto error_fd;
	}

	val = 1;
	if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) < 0) {
		msg = "Unable to SO_REUSEADDR: ";
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (bind(m_fd, m_addrinfo->ai_addr, m_addrinfo->ai_addrlen)) {
		msg = "Unable to bind to port ";
		msg += m_host;
		msg += ":";
		msg += m_service;
		msg += ": ";
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (listen(m_fd, 128)) {
		msg = "Unable to listen: ";
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += strerror(errno);
		goto error_fd;
	}

	INFO("setupListener(): Listening for Connections at "
		<< m_host << ":" << m_service << "...");

	try {
		m_fdreg = new ReadyAdapter(m_fd, fdrRead,
			boost::bind(&LiveServer::newConnection, this),
			ctrl->verbose());
	}
	catch (std::exception &e) {
		ERROR("setupListener(): Exception in Ready Adapter - " << e.what());
		m_fdreg = NULL; // just to be sure... ;-b
		goto error_fd;
	}
	catch (...) {
		ERROR("setupListener(): Unknown Exception in Ready Adapter!");
		m_fdreg = NULL; // just to be sure... ;-b
		goto error_fd;
	}

	INFO("setupListener(): Adapter Ready for Connections");

	// Update LiveServer Listen Status PV, All OK.
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvListenStatus->update(0, false /* major */, &now);

	return;

error_fd:

	if ( m_fd >= 0 ) {
		if ( ctrl->verbose() > 0 ) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close(m_fd);
		m_fd = -1;
	}

error:

	if ( m_addrinfo != NULL ) {
		freeaddrinfo(m_addrinfo);
		m_addrinfo = NULL;
	}

	// *Don't* Throw Exception, Just Fly Without LiveServer
	// Until We Timeout (TODO) or Change the Connection Parameters (TODO)
	// Just Log Whatever Error and Return to the Abyss...! ;-D

	ERROR("setupListener(): " << msg);

	// Update LiveServer Listen Status PV, Setup Failed!
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvListenStatus->update(1, true /* major */, &now);

	// Update Listen Retry Timeout from PV...
	m_listen_retry = m_pvListenRetryTimeout->value();
	m_listen_timer->start(m_listen_retry);

	return;
}

void LiveServer::newConnection(void)
{
	SMSControl *ctrl = SMSControl::getInstance();

	DEBUG("newConnection() entry");

	// Verify We Actually (Still) Have a Listener Socket Here... ;-D
	if ( m_fd < 0 ) {
		/* TODO Rate-Limited Logging... */
		ERROR("newConnection(): Invalid Listener Socket, Cannot Accept!");
		return;
	}

	int rc;
	rc = accept4(m_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (rc < 0) {
		int err = errno;

		/* Not really an error :-D */
		if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
				|| err == ECONNABORTED) {
			WARN("newConnection() Not-Really-An-Error"
				<< " m_fd=" << m_fd
				<< " (" << strerror(err) << ")");
			return;
		}

		/* No Descriptors/Resources... */
		if (err == ENOBUFS || err == ENOMEM || err == EMFILE
				|| err == ENFILE) {
			ERROR("newConnection() No Descriptors/Resources!"
				<< " m_fd=" << m_fd
				<< " (" << strerror(err) << ")");
			return;
		}

		/* Some Other Accept Error... */
		ERROR("newConnection() Accept Error: "
			<< " m_fd=" << m_fd << " (" << strerror(err) << ")");
		return;
	}
	if ( ctrl->verbose() > 0 ) {
		DEBUG("New Accept Socket rc=" << rc);
	}

	try {
		// TODO may want to put LiveClient on list
		// to cleanup during shutdown
		new LiveClient( this, rc );
	}
	catch (std::exception &e) {
		ERROR("newConnection(): LiveClient() Exception "
			<< "(rc=" << rc << ") - " << e.what());
		close( rc );
		return;
	}
	catch (...) {
		ERROR("newConnection(): Unknown LiveClient() Exception! "
			<< "(rc=" << rc << ")");
		close( rc );
		return;
	}

	DEBUG("newConnection() exit");
}

bool LiveServer::listenRetry(void)
{
	setupListener();
	return false;
}

bool LiveServer::getSendPausedData(void)
{
	// Update Send Paused Data Option from PV...
	m_send_paused_data = m_pvSendPausedData->value();
	return( m_send_paused_data );
}

