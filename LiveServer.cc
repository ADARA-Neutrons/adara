#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#include "EPICS.h"
#include "LiveServer.h"
#include "LiveClient.h"

LiveServer::LiveServer(const std::string &service)
{
	struct addrinfo hints, *ai;
	std::string msg;
	int val, rc, flags;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	rc = getaddrinfo(NULL, service.c_str(), &hints, &ai);
	if (rc) {
		msg = "Unable to convert service '";
		msg += service;
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
		msg += service;
		msg += ": ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (listen(m_fd, 128)) {
		msg = "Unable to listen: ";
		msg += strerror(errno);
		goto error_fd;
	}

	try {
		m_fdreg = new ReadyAdapter<LiveServer>(m_fd, fdrRead, this);
	} catch(...) {
		close(m_fd);
		freeaddrinfo(ai);
		throw;
	}

	return;

error_fd:
	close(m_fd);

error:
	freeaddrinfo(ai);
	throw std::runtime_error(msg);
}

void LiveServer::fdReady(fdRegType type)
{
	int rc;

	rc = accept4(m_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (rc < 0) {
		int e = errno;

		if (e == EINTR || e == EAGAIN || e == EWOULDBLOCK ||
							e == ECONNABORTED) {
			/* Not really an error */
			return;
		}

		if (e == ENOBUFS || e == ENOMEM || e == EMFILE || e == ENFILE) {
			/* TODO log no descriptors */
			return;
		}

		std::string msg("LiveServer::fdReady accept error: ");
		msg += strerror(e);
		throw std::runtime_error(msg);
	}

	new LiveClient(rc);

	/* TODO may want to put on list to cleanup during shutdown */
}
