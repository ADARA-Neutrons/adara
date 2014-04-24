#ifndef __EVENT_FD_H
#define __EVENT_FD_H

#include <boost/function.hpp>
#include <fdManager.h>
#include <sys/eventfd.h>
#include <errno.h>

#include <memory>
#include <stdexcept>

#include "ReadyAdapter.h"

class EventFd {
public:
	typedef boost::function<void (fdRegType)> callback;

	EventFd() { init(0); }
	EventFd(callback cb) {
		init(EFD_NONBLOCK);
		m_ready.reset(new ReadyAdapter(m_fd, fdrRead, cb));
	}

	/* TODO some day, we may need to try to read from a blocking object
	 * without blocking; at that point we can revisit the guards we
	 * have in place.
	 */
	uint64_t read(void) {
		if (!m_ready.get()) {
			throw std::runtime_error("Calling EventFd::read on a "
						 "blocking object");
		}

		return do_read();
	}

	uint64_t block(void) {
		if (m_ready.get()) {
			throw std::runtime_error("Calling EventFd::block on a "
						 "non-blocking object");
		}

		return do_read();
	}

	void signal(uint64_t val = 1) {
		if (write(m_fd, &val, sizeof(val)) != sizeof(val)) {
			int e = errno;
			std::string msg("Unable to write to eventfd: ");
			msg += strerror(e);
			throw std::runtime_error(msg);
		}
	}

	~EventFd() { close(m_fd); }

private:
	std::auto_ptr<ReadyAdapter> m_ready;
	int m_fd;

	void init(int flags) {
		m_fd = eventfd(0, flags);
		if (m_fd < 0) {
			int e = errno;
			std::string msg("Unable to create eventfd: ");
			msg += strerror(e);
			throw std::runtime_error(msg);
		}
	}

	uint64_t do_read(void) {
		uint64_t val;
		ssize_t rc;

		rc = ::read(m_fd, &val, sizeof(val));
		if (rc != 8) {
			if (errno != EAGAIN) {
				int e = errno;
				std::string msg("Unable to read eventfd: ");
				msg += strerror(e);
				throw std::runtime_error(msg);
			} else
				val = ~0;
		}

		return val;
	}
};

#endif /* __EVENT_FD_H */
