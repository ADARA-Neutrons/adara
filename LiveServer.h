#ifndef __LIVE_SERVER_H
#define __LIVE_SERVER_H

#include <string>
#include "ReadyAdapter.h"

class LiveServer {
public:
	LiveServer(const std::string &service);

private:
	ReadyAdapter *m_fdreg;
	int m_fd;

	void newConnection(void);

	friend class ReadyAdapter;
};

#endif /* __LIVE_SERVER_H */
