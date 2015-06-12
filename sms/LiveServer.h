#ifndef __LIVE_SERVER_H
#define __LIVE_SERVER_H

#include <boost/property_tree/ptree.hpp>
#include <string>
#include "ReadyAdapter.h"

extern "C" {
struct addrinfo;
}

class LiveServer {
public:
	static void config(const boost::property_tree::ptree &conf);
	static void init(void);

	LiveServer();
	~LiveServer();

	void setupListener(void);

private:
	static LiveServer *m_singleton;

	static std::string m_service;
	static std::string m_host;
	static char *m_node;

	ReadyAdapter *m_fdreg;
	struct addrinfo *m_addrinfo;
	int m_fd;

	void newConnection(void);
};

#endif /* __LIVE_SERVER_H */
