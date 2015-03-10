#include <boost/bind.hpp>
#include <sstream>
#include <string>
#include <stdint.h>

#include "ADARA.h"
#include "BeamMonitorConfig.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.BeamMonitorConfig"));

BeamMonitorConfig::BeamMonitorConfig(
		const boost::property_tree::ptree & conf)
{
	std::string prefix("monitor ");
	boost::property_tree::ptree::const_iterator it;
	size_t plen = prefix.length();

	uint32_t payloadSize, onePacketSize, *fields;
	struct timespec ts;

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

	payloadSize = 24;   // 4 x uint32_t and 1 double...
	onePacketSize = payloadSize + sizeof(ADARA::Header);
	m_packetSize = numBeamMonitors * onePacketSize;

	m_packet = new uint8_t[m_packetSize];

	fields = (uint32_t *) m_packet;

	uint32_t bmonIndex = 0;

	uint32_t bmonId;

	uint32_t tofOffset;
	uint32_t tofMax;
	uint32_t tofBin;

	double distance;

	clock_gettime(CLOCK_REALTIME, &ts);

	// Append Each Beam Monitor Config to Packet Set...
	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, prefix))
			continue;

		fields[bmonIndex + 0] = payloadSize;
		fields[bmonIndex + 1] = ADARA::PacketType::BEAM_MONITOR_CONFIG_V0;
		fields[bmonIndex + 2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
		fields[bmonIndex + 3] = ts.tv_nsec;

		std::istringstream buffer( it->first.substr(plen) );
		buffer >> bmonId;

		fields[bmonIndex + 4] = bmonId;

		tofOffset = it->second.get<uint32_t>("offset", 0);
		tofMax = it->second.get<uint32_t>("max", -1);
		tofBin = it->second.get<uint32_t>("bin", 1);

		fields[bmonIndex + 5] = tofOffset;
		fields[bmonIndex + 6] = tofMax;
		fields[bmonIndex + 7] = tofBin;

		distance = it->second.get<double>("distance", 0.0);

		*((double *) &(fields[bmonIndex + 8])) = distance;

		DEBUG("Beam Monitor " << bmonId << " Histo Config:"
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " distance=" << distance);

		// Next Beam Monitor Config...
		bmonIndex += 10;
	}

	m_connection = StorageManager::onPrologue(
				boost::bind(&BeamMonitorConfig::onPrologue, this));
}

BeamMonitorConfig::~BeamMonitorConfig()
{
	delete [] m_packet;
	m_connection.disconnect();
}

void BeamMonitorConfig::onPrologue(void)
{
	StorageManager::addPrologue(m_packet, m_packetSize);
}

