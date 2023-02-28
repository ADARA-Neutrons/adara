
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.SignalEvents"));

#include <unistd.h>
#include <errno.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "EPICS.h"
#include "ReadyAdapter.h"
#include "SignalEvents.h"
#include "SMSControl.h"

SignalEvents::SignalEvents()
	: m_read(NULL), m_fd(-1)
{
}

SignalEvents::~SignalEvents()
{
	SMSControl *ctrl = SMSControl::getInstance();
	if ( m_read ) {
		delete m_read;
		m_read = NULL;   // just to be sure... ;-b
	}

	if ( m_fd >= 0 ) {
		if ( ctrl->verbose() > 0 ) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close( m_fd );
		m_fd = -1;   // just to be sure... ;-b
	}
}

void SignalEvents::check_init(void)
{
	SMSControl *ctrl = SMSControl::getInstance();

	if (m_fd >= 0)
		return;

	m_sig_map.resize(SIGRTMAX+1);

	sigemptyset(&m_sig_set);
	m_fd = signalfd(-1, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC);
	if (m_fd < 0) {
		const char *err = strerror(errno);
		std::string msg("Unable to create signalfd: ");
		msg += err;
		ERROR(msg);
		m_fd = -1;   // just to be sure... ;-b
		if ( m_read ) {
			delete m_read;
			m_read = NULL;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	}
	if ( ctrl->verbose() > 0 ) {
		DEBUG("New SignalEvent SignalFD m_fd=" << m_fd);
	}

	// Free Any Previous ReadyAdapter...
	if ( m_read ) {
		delete m_read;
		m_read = NULL;   // just to be sure... ;-b
	}

	try {
		m_read = new ReadyAdapter(m_fd, fdrRead,
			boost::bind(&SignalEvents::signaled, this),
			ctrl->verbose());
	} catch (std::exception &e) {
		std::string msg(
			"Exception Creating ReadyAdapter in check_init() - ");
		msg += e.what();
		ERROR(msg);
		m_read = NULL;   // just to be sure... ;-b
		if (m_fd >= 0) {
			if ( ctrl->verbose() > 0 ) {
				DEBUG("Close m_fd=" << m_fd);
			}
			close(m_fd);
			m_fd = -1;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	} catch (...) {
		std::string msg(
			"Unknown Exception Creating ReadyAdapter in check_init()");
		ERROR(msg);
		m_read = NULL;   // just to be sure... ;-b
		if (m_fd >= 0) {
			if ( ctrl->verbose() > 0 ) {
				DEBUG("Close m_fd=" << m_fd);
			}
			close(m_fd);
			m_fd = -1;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	}
}

void SignalEvents::registerHandler(int sig, cbFunc cb)
{
	SMSControl *ctrl = SMSControl::getInstance();

	int rc;

	check_init();

	// Check for Duplicate Signal...
	if (sigismember(&m_sig_set, sig)) {
		std::string msg("Registering duplicate signal ");
		msg += boost::lexical_cast<std::string>(sig);
		if ( m_read ) {
			delete m_read;
			m_read = NULL;   // just to be sure... ;-b
		}
		if (m_fd >= 0) {
			if (ctrl->verbose() > 0) {
				DEBUG("Close m_fd=" << m_fd);
			}
			close(m_fd);
			m_fd = -1;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	}

	// Check File Descriptor...
	if (m_fd < 0) {
		std::string msg("registerHandler(): Invalid File Descriptor!");
		msg += " signal=";
		msg += boost::lexical_cast<std::string>(sig);
		msg += " m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		if ( m_read ) {
			delete m_read;
			m_read = NULL;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	}

	m_sig_map[sig] = cb;
	sigaddset(&m_sig_set, sig);

	if (signalfd(m_fd, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC) < 0) {
		const char *err = strerror(errno);
		std::string msg("Unable to add signal ");
		msg += boost::lexical_cast<std::string>(sig);
		msg += " to signalfd: ";
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += err;
		sigdelset(&m_sig_set, sig);
		if ( m_read ) {
			delete m_read;
			m_read = NULL;   // just to be sure... ;-b
		}
		if (m_fd >= 0) {
			if (ctrl->verbose() > 0) {
				DEBUG("Close m_fd=" << m_fd);
			}
			close(m_fd);
			m_fd = -1;   // just to be sure... ;-b
		}
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
		if ( m_read ) {
			delete m_read;
			m_read = NULL;   // just to be sure... ;-b
		}
		if (m_fd >= 0) {
			signalfd(m_fd, &m_sig_set, SFD_NONBLOCK | SFD_CLOEXEC);
			if (ctrl->verbose() > 0) {
				DEBUG("Close m_fd=" << m_fd);
			}
			close(m_fd);
			m_fd = -1;   // just to be sure... ;-b
		}
		throw std::runtime_error(msg);
	}
}

int SignalEvents::allocateRTsig(cbFunc cb)
{
	SMSControl *ctrl = SMSControl::getInstance();

	int sig;

	check_init();

	for (sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
		if (!sigismember(&m_sig_set, sig)) {
			registerHandler(sig, cb);
			return sig;
		}
	}

	std::string msg("Unable to allocate RT signal");
	if ( m_read ) {
		delete m_read;
		m_read = NULL;   // just to be sure... ;-b
	}
	if (m_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close(m_fd);
		m_fd = -1;   // just to be sure... ;-b
	}
	throw std::runtime_error(msg);
}

bool SignalEvents::valid(void)
{
	return( ( m_fd >= 0 ) && ( m_read ) );
}

void SignalEvents::signaled(void)
{
	SMSControl *ctrl = SMSControl::getInstance();

	// Note: *Don't* Throw Exceptions in signaled()...!
	// (It will unnecessarily crash the SMS, so just let whatever
	// communication "time out" and retry...! ;-D)

	// DEBUG("signaled entry");

	struct signalfd_siginfo info;
	int rc;

	for (;;) {

		// Check File Descriptor...
		if (m_fd < 0) {
			ERROR("signaled(): Invalid File Descriptor!"
				<< " m_fd=" << m_fd
				<< " Freeing ReadyAdapter and Exiting Loop!");
			if ( m_read ) {
				delete m_read;
				m_read = NULL;   // just to be sure... ;-b
			}
			return;
		}

		// NOTE: This is Standard C Library read()... ;-o
		rc = read(m_fd, &info, sizeof(info));
		if (rc <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				return;

			rc = errno;

			ERROR("signaled(): Fatal Read Error"
				<< " m_fd=" << m_fd
				<< " - " << strerror(rc)
				<< ", Freeing ReadyAdapter and Exiting Loop!");
			if ( m_read ) {
				delete m_read;
				m_read = NULL;   // just to be sure... ;-b
			}
			if (m_fd >= 0) {
				if (ctrl->verbose() > 0) {
					DEBUG("Close m_fd=" << m_fd);
				}
				close(m_fd);
				m_fd = -1;   // just to be sure... ;-b
			}
			return;
		}

		m_sig_map[info.ssi_signo](info);
	}

	// DEBUG("signaled exit");
}

