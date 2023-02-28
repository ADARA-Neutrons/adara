#ifndef __GEOMETRY_H
#define __GEOMETRY_H

#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <stdint.h>
#include <string>

class Geometry : boost::noncopyable {
public:
	Geometry(const std::string &path);
	~Geometry();

private:
	uint8_t *m_packet;
	uint32_t m_packetSize;
	boost::signals2::connection m_connection;

	void onPrologue( bool capture_last );
};

#endif /* __GEOMETRY_H */
