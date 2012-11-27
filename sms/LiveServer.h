#ifndef __LIVE_SERVER_H
#define __LIVE_SERVER_H

#include <boost/property_tree/ptree.hpp>
#include <string>
#include "ReadyAdapter.h"

class LiveServer {
public:
	static void config(const boost::property_tree::ptree &conf);
	static void init(void);

private:
	LiveServer();

	ReadyAdapter *m_fdreg;
	int m_fd;

	static std::string m_service;
	static LiveServer *m_singleton;

	void newConnection(void);
};

#endif /* __LIVE_SERVER_H */
