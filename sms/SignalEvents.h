#ifndef __SIGNAL_EVENTS_H
#define __SIGNAL_EVENTS_H

#include <signal.h>
#include <sys/signalfd.h>

#include <boost/function.hpp>
#include <vector>

extern "C" {
struct signalfd_siginfo;
}

class ReadyAdapter;

class SignalEvents {
public:
	typedef boost::function<void (const struct signalfd_siginfo &)> cbFunc;

	SignalEvents();
	~SignalEvents();

	void registerHandler(int sig, cbFunc cb);

	int allocateRTsig(cbFunc cb);

	bool valid();

private:
	std::vector<cbFunc> m_sig_map;
	ReadyAdapter *m_read;
	int m_fd;
	sigset_t m_sig_set;

	void check_init(void);
	void signaled(void);
};

#endif /* __SIGNAL_EVENTS_H */
