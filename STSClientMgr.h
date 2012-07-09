#ifndef __STS_CLIENT_MGR_H
#define __STS_CLIENT_MGR_H

#include <string>
#include <map>

#include "StorageManager.h"
#include "StorageContainer.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"

class STSClient;

class STSClientMgr {
public:
	STSClientMgr(const std::string &uri);
	~STSClientMgr();

	enum Disposition { SUCCESS, TRANSIENT_FAIL, PERMAMENT_FAIL,
				CONNECTION_LOSS };
	enum QueueMode { BALANCE, OLDEST, NEWEST };

private:
	typedef boost::signals::connection connection;
	typedef std::map<uint32_t, StorageManager::ContainerSharedPtr> RunMap;

	std::auto_ptr<TimerAdapter<STSClientMgr> > m_connect_timer;
	std::auto_ptr<TimerAdapter<STSClientMgr> > m_reconnect_timer;
	std::string m_node;
	std::string m_service;
	int m_fd;
	std::auto_ptr<ReadyAdapter> m_fdreg;
	bool m_connecting;
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

	static double m_connect_timeout;
	static double m_reconnect_timeout;
	static unsigned int m_max_connections;

	void containerChange(StorageManager::ContainerSharedPtr &, bool);

	void queueRun(StorageManager::ContainerSharedPtr &c);
	void requeueRun(StorageManager::ContainerSharedPtr &c);
	StorageManager::ContainerSharedPtr &nextRun(void);

	void startConnect(void);
	void lookupComplete(const struct signalfd_siginfo &info);
	void connectComplete(void);
	void connectFailed(void);
	bool connectTimeout(void);

	bool reconnectTimeout(void);

	void clientComplete(StorageManager::ContainerSharedPtr &c,
			    Disposition disp);

	friend class STSClient;
	friend class TimerAdapter<STSClientMgr>;
};

#endif /* __STS_CLIENT_MGR_H */
