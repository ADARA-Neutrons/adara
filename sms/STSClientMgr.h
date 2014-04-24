#ifndef __STS_CLIENT_MGR_H
#define __STS_CLIENT_MGR_H

#include <boost/property_tree/ptree.hpp>
#include <string>
#include <map>

#include "StorageManager.h"
#include "StorageContainer.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

class STSClient;

class STSClientMgr {
public:
	static void config(const boost::property_tree::ptree &conf);
	static void init(void);

        static STSClientMgr *getInstance(void) { return m_singleton; }
	void queueRun(StorageContainer::SharedPtr &c);
	void startConnect(void);

	enum Disposition { SUCCESS, TRANSIENT_FAIL, PERMAMENT_FAIL,
				CONNECTION_LOSS, INVALID_PROTOCOL };
	enum QueueMode { BALANCE, OLDEST, NEWEST };

private:
	STSClientMgr();
	~STSClientMgr();

	typedef boost::signals::connection connection;
	typedef std::map<uint32_t, StorageContainer::SharedPtr> RunMap;

	std::auto_ptr<TimerAdapter<STSClientMgr> > m_connect_timer;
	std::auto_ptr<TimerAdapter<STSClientMgr> > m_reconnect_timer;
	std::auto_ptr<TimerAdapter<STSClientMgr> > m_transient_timer;
	int m_fd;
	std::auto_ptr<ReadyAdapter> m_fdreg;
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
	static double m_reconnect_timeout;
	static double m_transient_timeout;
	static unsigned int m_max_connections;

	static STSClientMgr *m_singleton;

	void containerChange(StorageContainer::SharedPtr &, bool);

	void requeueRun(StorageContainer::SharedPtr &c);
	StorageContainer::SharedPtr &nextRun(void);

	void lookupComplete(const struct signalfd_siginfo &info);
	void connectComplete(void);
	void connectFailed(void);
	bool connectTimeout(void);

	bool reconnectTimeout(void);
	bool transientTimeout(void);

	void clientComplete(StorageContainer::SharedPtr &c,
			    Disposition disp);

	friend class STSClient;
	friend class TimerAdapter<STSClientMgr>;
};

#endif /* __STS_CLIENT_MGR_H */
