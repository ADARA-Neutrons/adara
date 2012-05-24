#ifndef __GEOMETRY_H
#define __GEOMETRY_H

#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>
#include <string>

class Geometry : boost::noncopyable {
public:
	Geometry(const std::string &path);
	~Geometry();

private:
	uint8_t *m_packet;
	uint32_t m_packetSize;
	boost::signals::connection m_connection;

	void onPrologue(void);
};

#endif /* __GEOMETRY_H */
