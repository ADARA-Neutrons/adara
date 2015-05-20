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

std::string LiveServer::m_service;
std::string LiveServer::m_host;
char *LiveServer::m_node;

LiveServer *LiveServer::m_singleton;

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

LiveServer::LiveServer()
{
	struct addrinfo hints, *ai;
	std::string msg;
	int val, rc, flags;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	rc = getaddrinfo(m_node, m_service.c_str(), &hints, &ai);
	if (rc) {
		msg = "Unable to convert host/service '";
		msg += m_host;
		msg += ":";
		msg += m_service;
		msg += "' to a port: ";
		msg += gai_strerror(rc);
		throw std::runtime_error(msg);
	}

	m_fd = socket(ai->ai_addr->sa_family, SOCK_STREAM, 0);
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

	if (bind(m_fd, ai->ai_addr, ai->ai_addrlen)) {
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

	INFO("LiveServer() listening for connections at "
		<< m_host << ":" << m_service << "...");

	try {
		m_fdreg = new ReadyAdapter(m_fd, fdrRead,
					boost::bind(&LiveServer::newConnection,
						    this));
	} catch(...) {
		ERROR("Unknown LiveServer() Exception in Ready Adapter");
		close(m_fd);
		freeaddrinfo(ai);
		throw;
	}

	INFO("LiveServer() adapter ready for connections");

	return;

error_fd:
	close(m_fd);

error:
	freeaddrinfo(ai);
	throw std::runtime_error(msg);
}

void LiveServer::newConnection(void)
{
	DEBUG("newConnection() entry");

	int rc;

	rc = accept4(m_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (rc < 0) {
		int e = errno;

		if (e == EINTR || e == EAGAIN || e == EWOULDBLOCK ||
							e == ECONNABORTED) {
			DEBUG("newConnection() not-really-an-error exit");
			/* Not really an error */
			return;
		}

		if (e == ENOBUFS || e == ENOMEM || e == EMFILE || e == ENFILE) {
			/* TODO log no descriptors */
			DEBUG("newConnection() no-descriptors exit");
			return;
		}

		std::string msg("LiveServer::newConnection() accept error: ");
		msg += strerror(e);
		throw std::runtime_error(msg);
	}

	try {
		new LiveClient(rc);
	} catch (...) {
		ERROR("Unknown LiveClient() Exception in newConnection()");
		close(rc);
	}

	/* TODO may want to put on list to cleanup during shutdown */

	DEBUG("newConnection() exit");
}

