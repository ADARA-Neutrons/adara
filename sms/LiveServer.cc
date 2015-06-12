#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "LiveServer.h"
#include "LiveClient.h"
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.LiveServer"));

LiveServer *LiveServer::m_singleton;

std::string LiveServer::m_service;
std::string LiveServer::m_host;
char *LiveServer::m_node;

void LiveServer::config(const boost::property_tree::ptree &conf)
{
	m_service = conf.get<std::string>("livestream.service", "31415");
	m_host = conf.get<std::string>("livestream.uri", "ANY");
	if ( !m_host.compare("ANY") )
		m_node = (char *) NULL;
	else
		m_node = (char *) m_host.c_str();
	LiveClient::config(conf);
}

void LiveServer::init()
{
	m_singleton = new LiveServer();
}

LiveServer::LiveServer() :
		m_fdreg(NULL), m_addrinfo(NULL), m_fd(-1)
{
	setupListener();
}

LiveServer::~LiveServer()
{
	if ( m_addrinfo != NULL ) {
		freeaddrinfo( m_addrinfo );
		m_addrinfo = NULL;
	}
	if ( m_fdreg ) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if ( m_fd >= 0 ) {
		close( m_fd );
		m_fd = -1;
	}
}

void LiveServer::setupListener(void)
{
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
		close( m_fd );
		m_fd = -1;
	}

	// Set Up New Listener Connection

	struct addrinfo hints;
	int val, rc, flags;
	std::string msg;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	rc = getaddrinfo(m_node, m_service.c_str(), &hints, &m_addrinfo);
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
		goto error;
	}

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0 || fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		msg = "Unable to set socket non-blocking: ";
		msg += strerror(errno);
		goto error_fd;
	}

	val = 1;
	if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) < 0) {
		msg = "Unable to SO_REUSEADDR: ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (bind(m_fd, m_addrinfo->ai_addr, m_addrinfo->ai_addrlen)) {
		msg = "Unable to bind to port ";
		msg += m_host;
		msg += ":";
		msg += m_service;
		msg += ": ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (listen(m_fd, 128)) {
		msg = "Unable to listen: ";
		msg += strerror(errno);
		goto error_fd;
	}

	INFO("setupListener(): Listening for Connections at "
		<< m_host << ":" << m_service << "...");

	try {
		m_fdreg = new ReadyAdapter(m_fd, fdrRead,
			boost::bind(&LiveServer::newConnection,
				this));
	}
	catch (std::exception &e) {
		ERROR("setupListener(): Exception in Ready Adapter - " << e.what());
		goto error_fdreg;
	}
	catch (...) {
		ERROR("setupListener(): Unknown Exception in Ready Adapter!");
		goto error_fdreg;
	}

	INFO("setupListener(): Adapter Ready for Connections");

	return;

error_fdreg:

	if ( m_fdreg ) {
		delete m_fdreg;
		m_fdreg = NULL;
	}

error_fd:

	if ( m_fd >= 0 ) {
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
	return;
}

void LiveServer::newConnection(void)
{
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
				<< " (" << strerror(err) << ")");
			return;
		}

		/* No Descriptors/Resources... */
		if (err == ENOBUFS || err == ENOMEM || err == EMFILE
				|| err == ENFILE) {
			ERROR("newConnection() No Descriptors/Resources!"
				<< " (" << strerror(err) << ")");
			return;
		}

		/* Some Other Accept Error... */
		ERROR("newConnection() Accept Error: " << strerror(err));
		return;
	}

	try {
		// TODO may want to put LiveClient on list
		// to cleanup during shutdown
		new LiveClient( rc );
	}
	catch (std::exception &e) {
		ERROR("newConnection(): LiveClient() Exception - " << e.what());
		close( rc );
		return;
	}
	catch (...) {
		ERROR("newConnection(): Unknown LiveClient() Exception!");
		close( rc );
		return;
	}

	DEBUG("newConnection() exit");
}

