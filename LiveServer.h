#ifndef __LIVE_SERVER_H
#define __LIVE_SERVER_H

#include <string>
#include "ReadyAdapter.h"

class LiveServer {
public:
	LiveServer(const std::string &service);

private:
	ReadyAdapter<LiveServer> *m_fdreg;
	int m_fd;

	void fdReady(fdRegType type);

	friend class ReadyAdapter<LiveServer>;
};

#endif /* __LIVE_SERVER_H */
