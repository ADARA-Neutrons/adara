#ifndef __BEAMMONITORCONFIG_H
#define __BEAMMONITORCONFIG_H

#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>

class BeamMonitorInfo;

class BeamMonitorConfig : boost::noncopyable {
public:
	BeamMonitorConfig(const boost::property_tree::ptree & conf);
	~BeamMonitorConfig();

private:
	std::vector<BeamMonitorInfo *> bmonInfos;

	uint32_t m_numBeamMonitors;

	uint32_t m_sectionSize;
	uint32_t m_payloadSize;
	uint32_t m_packetSize;

	uint8_t *m_packet;

	boost::signals2::connection m_connection;

	void onPrologue(void);
};

#endif /* __BEAMMONITORCONFIG_H */
