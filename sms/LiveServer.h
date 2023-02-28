#ifndef __LIVE_SERVER_H
#define __LIVE_SERVER_H

#include <boost/property_tree/ptree.hpp>
#include <string>
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

extern "C" {
struct addrinfo;
}

class smsErrorPV;
class smsFloat64PV;
class ListenStringPV;
class smsBooleanPV;

class LiveServer {
public:
	static void config(const boost::property_tree::ptree &conf);
	static void init(void);

	LiveServer();
	~LiveServer();

	void setupListener(void);

	bool getSendPausedData(void);

	bool isInit(void) { return m_init; }

private:
	static LiveServer *m_singleton;

	static std::string m_service;
	static std::string m_host;

	std::auto_ptr<TimerAdapter<LiveServer> > m_listen_timer;

	static double m_listen_retry;

	static bool m_send_paused_data;

	bool m_init;

	ReadyAdapter *m_fdreg;
	struct addrinfo *m_addrinfo;
	int m_fd;

	void newConnection(void);

	bool listenRetry(void);

	boost::shared_ptr<smsErrorPV> m_pvListenStatus;

	boost::shared_ptr<smsFloat64PV> m_pvListenRetryTimeout;

	boost::shared_ptr<ListenStringPV> m_pvListenerURI;
	boost::shared_ptr<ListenStringPV> m_pvListenerService;

	boost::shared_ptr<smsBooleanPV> m_pvSendPausedData;

	friend class TimerAdapter<LiveServer>;
};

#endif /* __LIVE_SERVER_H */
