#ifndef __STC_CLIENT_MGR_H
#define __STC_CLIENT_MGR_H

#include <boost/property_tree/ptree.hpp>
#include <string>
#include <map>
#include <stdint.h>

#include "StorageContainer.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"
#include "SignalEvents.h"

class STCClient;

class smsFloat64PV;
class MaxConnectionsPV;
class smsUint32PV;
class smsBooleanPV;
class smsStringPV;

class STCClientMgr {
public:
	static void config(const boost::property_tree::ptree &conf);
	static void init(void);

	static STCClientMgr *getInstance(void) { return m_singleton; }
	void queueRun(StorageContainer::SharedPtr &c);
	void startConnect(void);

	enum Disposition { SUCCESS, TRANSIENT_FAIL, PERMAMENT_FAIL,
				CONNECTION_LOSS, INVALID_PROTOCOL };
	enum QueueMode { BALANCE, OLDEST, NEWEST };

private:
	STCClientMgr();
	~STCClientMgr();

	typedef boost::signals2::connection connection;
	typedef std::map<uint32_t, StorageContainer::SharedPtr> RunMap;

	SignalEvents *m_signalEvents;
	TimerAdapter<STCClientMgr> *m_connect_timer;
	TimerAdapter<STCClientMgr> *m_reconnect_timer;
	TimerAdapter<STCClientMgr> *m_transient_timer;
	TimerAdapter<STCClientMgr> *m_lookup_timer;
	int m_fd;
	ReadyAdapter *m_fdreg;
	bool m_connecting;
	bool m_backoff;
	unsigned int m_connections;
	connection m_mgrConnection;
	QueueMode m_queueMode;
	QueueMode m_sendNext;

	RunMap m_pendingRuns;
	RunMap m_sendingRuns;
	uint32_t m_currentRun;

	struct gaicb m_gai;
	struct sigevent m_sigevent;
	struct addrinfo m_gai_hints;

	static std::string m_node;
	static std::string m_service;
	static double m_connect_timeout;
	static double m_connect_retry;
	static double m_transient_timeout;
	static unsigned int m_max_connections;
	static uint32_t m_max_requeue_count;
	static bool m_send_paused_data;

	static STCClientMgr *m_singleton;

	void containerChange(StorageContainer::SharedPtr &, bool);

	void dequeueRun(StorageContainer::SharedPtr &c);

	StorageContainer::SharedPtr &nextRun(void);

	void lookupComplete(const struct signalfd_siginfo &info);
	void connectComplete(void);
	void connectFailed(void);
	bool connectTimeout(void);

	bool reconnectTimeout(void);
	bool transientTimeout(void);
	bool lookupTimeout(void);

	void clientComplete(StorageContainer::SharedPtr &c,
		Disposition disp, std::string reason);

	boost::shared_ptr<smsFloat64PV> m_pvConnectTimeout;
	boost::shared_ptr<smsFloat64PV> m_pvConnectRetryTimeout;
	boost::shared_ptr<smsFloat64PV> m_pvTransientTimeout;
	boost::shared_ptr<MaxConnectionsPV> m_pvMaxConnections;
	boost::shared_ptr<smsUint32PV> m_pvMaxRequeueCount;
	boost::shared_ptr<smsBooleanPV> m_pvSendPausedData;
	boost::shared_ptr<smsStringPV> m_pvServiceURI;

	friend class STCClient;
	friend class TimerAdapter<STCClientMgr>;
};

#endif /* __STC_CLIENT_MGR_H */
