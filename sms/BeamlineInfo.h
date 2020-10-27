#ifndef __BEAMLINE_INFO_H
#define __BEAMLINE_INFO_H

#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <stdint.h>
#include <string>

class BeamlineInfo : boost::noncopyable {
public:
	BeamlineInfo(uint32_t targetStationNumber,
			const std::string &id,
			const std::string &shortName,
			const std::string &longName);
	~BeamlineInfo();

private:
	uint8_t *m_packet;
	uint32_t m_packetSize;
	boost::signals2::connection m_connection;

	void onPrologue( bool capture_last );
};

#endif /* __BEAMLINE_INFO_H */
