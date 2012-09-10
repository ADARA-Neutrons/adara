#include "EPICS.h"
#include "ReadyAdapter.h"
#include "SignalEvents.h"

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include <unistd.h>
#include <errno.h>

ReadyAdapter *SignalEvents::m_read;
int SignalEvents::m_fd = -1;
sigset_t SignalEvents::m_sig_set;
std::vector<SignalEvents::cbFunc> SignalEvents::m_sig_map;

void SignalEvents::check_init(void)
{
	if (m_fd != -1)
		return;

	m_sig_map.resize(SIGRTMAX);

	sigemptyset(&m_sig_set);
	m_fd = signalfd(-1, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC);
	if (m_fd < 0) {
		const char *err = strerror(errno);
		std::string msg("Unable to create signalfd: ");
		msg += err;
		throw std::runtime_error(msg);
	}

	try {
		m_read = new ReadyAdapter(m_fd, fdrRead,
					  boost::bind(&SignalEvents::signaled));
	} catch (...) {
		close(m_fd);
		throw;
	}
}

void SignalEvents::registerHandler(int sig, cbFunc cb)
{
	int rc;

	check_init();
	if (sigismember(&m_sig_set, sig)) {
		std::string msg("Registering duplicate signal ");
		msg += boost::lexical_cast<std::string>(sig);
		throw std::runtime_error(msg);
	}

	m_sig_map[sig] = cb;
	sigaddset(&m_sig_set, sig);

	if (signalfd(m_fd, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC) < 0) {
		const char *err = strerror(errno);
		std::string msg("Unable to add signal ");
		msg += boost::lexical_cast<std::string>(sig);
		msg += " to signalfd: ";
		msg += err;
		sigdelset(&m_sig_set, sig);
		throw std::runtime_error(msg);
	}

	rc = pthread_sigmask(SIG_BLOCK, &m_sig_set, NULL);
	if (rc) {
		std::string msg("Unable to block signal ");
		msg += boost::lexical_cast<std::string>(sig);
		msg += ": ";
		msg += strerror(rc);

		/* Try to remove this signal from the signalfd, but we've
		 * got no guarantee it will work, and no recourse if it
		 * doesn't.
		 */
		sigdelset(&m_sig_set, sig);
		signalfd(m_fd, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC);
		throw std::runtime_error(msg);
	}
}

int SignalEvents::allocateRTsig(cbFunc cb)
{
	int sig;

	check_init();

	for (sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
		if (!sigismember(&m_sig_set, sig)) {
			registerHandler(sig, cb);
			return sig;
		}
	}

	std::string msg("Unable to allocate RT signal");
	throw std::runtime_error(msg);
}

void SignalEvents::signaled(void)
{
	struct signalfd_siginfo info;
	int rc;

	for (;;) {
		rc = read(m_fd, &info, sizeof(info));
		if (rc <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				return;

			rc = errno;

			std::string msg;
			msg = "Fatal error in SignalEvents::signaled: ";
			msg += strerror(rc);
			throw std::runtime_error(msg);
		}

		m_sig_map[info.ssi_signo](info);
	}
}
