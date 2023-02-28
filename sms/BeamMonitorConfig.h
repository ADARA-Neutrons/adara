#ifndef __BEAMMONITORCONFIG_H
#define __BEAMMONITORCONFIG_H

#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>

#include <boost/smart_ptr.hpp>

#include <stdint.h>

class BeamMonitorInfo;
class smsUint32PV;
class smsBooleanPV;

class BeamMonitorConfig : boost::noncopyable {
public:
	BeamMonitorConfig(const boost::property_tree::ptree & conf);
	~BeamMonitorConfig();

	void updateFormatCounts(uint32_t old_formatFlags,
		uint32_t new_formatFlags);

	uint32_t beamMonCount(void) { return m_numBeamMonitors; }

	uint32_t sectionSize(void) { return m_sectionSize; }

	void resetPacketTime(void);

private:
	std::vector<BeamMonitorInfo *> bmonInfos;

	uint32_t m_numBeamMonitors;

	bool m_alwaysSendBMConfig;

	uint32_t m_numEvent;
	uint32_t m_numHisto;

	uint32_t m_sectionSize;
	uint32_t m_payloadSize;
	uint32_t m_packetSize;

	uint8_t *m_packet;

	boost::shared_ptr<smsUint32PV> m_pvNumBeamMonitors;
	boost::shared_ptr<smsBooleanPV> m_pvAlwaysSendBMConfig;

	boost::signals2::connection m_connection;

	void onPrologue( bool capture_last );
};

#endif /* __BEAMMONITORCONFIG_H */
