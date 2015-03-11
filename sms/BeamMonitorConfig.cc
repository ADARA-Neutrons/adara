#include <boost/bind.hpp>
#include <sstream>
#include <string>
#include <stdint.h>

#include "ADARA.h"
#include "BeamMonitorConfig.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.BeamMonitorConfig"));

class BeamMonitorInfo {
public:
	BeamMonitorInfo(uint32_t id,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double distance) : m_id(id),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_distance(distance)
	{
		DEBUG("Beam Monitor " << id << " Histo Config:"
			<< " tofOffset=" << m_tofOffset
			<< " tofMax=" << m_tofMax
			<< " tofBin=" << m_tofBin
			<< " distance=" << m_distance);

		m_payloadSize = 24;   // 4 x uint32_t and 1 double...
		m_packetSize = m_payloadSize + sizeof(ADARA::Header);

		m_packet = new uint8_t[m_packetSize];

		updatePacket();
	}

	~BeamMonitorInfo() { delete [] m_packet; }

	uint32_t getId(void) const { return m_id; }
	uint8_t *getPacket(void) const { return m_packet; }
	uint32_t getPacketSize(void) const { return m_packetSize; }

	void updatePacket(void)
	{
		struct timespec ts;

		uint32_t *fields;

		fields = (uint32_t *) m_packet;

		clock_gettime(CLOCK_REALTIME, &ts);

		fields[0] = m_payloadSize;
		fields[1] = ADARA::PacketType::BEAM_MONITOR_CONFIG_V0;
		fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
		fields[3] = ts.tv_nsec;

		fields[4] = m_id;

		fields[5] = m_tofOffset;
		fields[6] = m_tofMax;
		fields[7] = m_tofBin;

		*((double *) &(fields[8])) = m_distance;
	}

private:
	uint32_t m_id;
	uint32_t m_tofOffset;
	uint32_t m_tofMax;
	uint32_t m_tofBin;
	double m_distance;

	uint32_t m_payloadSize;
	uint32_t m_packetSize;
	uint8_t *m_packet;
};

BeamMonitorConfig::BeamMonitorConfig(
		const boost::property_tree::ptree & conf)
{
	std::string prefix("monitor ");
	boost::property_tree::ptree::const_iterator it;
	size_t plen = prefix.length();

	// Count how many Beam Monitors we have defined...
	uint32_t numBeamMonitors = 0;
	for (it = conf.begin(); it != conf.end(); ++it) {
		if (!it->first.compare(0, plen, prefix))
			numBeamMonitors++;
	}

	// We're saving Beam Monitor Events...
	if ( numBeamMonitors == 0 ) {
		INFO("No Beam Monitor Histogramming Configurations Found.");
		return;
	}

	// Extract Each Beam Monitor Config...

	uint32_t bmonId;

	uint32_t tofOffset;
	uint32_t tofMax;
	uint32_t tofBin;

	double distance;

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, prefix))
			continue;

		std::istringstream buffer( it->first.substr(plen) );
		buffer >> bmonId;

		tofOffset = it->second.get<uint32_t>("offset", 0);
		tofMax = it->second.get<uint32_t>("max", -1);
		tofBin = it->second.get<uint32_t>("bin", 1);

		distance = it->second.get<double>("distance", 0.0);

		bmonInfo.push_back(
			new BeamMonitorInfo(bmonId,
				tofOffset, tofMax, tofBin, distance) );
	}

	m_connection = StorageManager::onPrologue(
				boost::bind(&BeamMonitorConfig::onPrologue, this));
}

BeamMonitorConfig::~BeamMonitorConfig()
{
	std::vector<BeamMonitorInfo *>::iterator bmi;
	for (bmi=bmonInfo.begin(); bmi != bmonInfo.end(); ++bmi) {
		delete (*bmi);
	}
	bmonInfo.clear();

	m_connection.disconnect();
}

void BeamMonitorConfig::onPrologue(void)
{
	std::vector<BeamMonitorInfo *>::iterator bmi;
	
	for (bmi=bmonInfo.begin(); bmi != bmonInfo.end(); ++bmi) {
		DEBUG("Adding Beam Monitor " << (*bmi)->getId()
			<< " Config to Prologue.");
		StorageManager::addPrologue(
			(*bmi)->getPacket(), (*bmi)->getPacketSize());
	}
}

