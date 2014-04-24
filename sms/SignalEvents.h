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
	static void registerHandler(int sig, cbFunc cb);

	static int allocateRTsig(cbFunc cb);

private:
	static std::vector<cbFunc> m_sig_map;
	static ReadyAdapter *m_read;
	static int m_fd;
	static sigset_t m_sig_set;

	static void check_init(void);
	static void signaled(void);
};

#endif /* __SIGNAL_EVENTS_H */
