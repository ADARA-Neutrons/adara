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
	BeamMonitorInfo(uint32_t index, uint32_t id,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double distance) : m_index(index), m_id(id),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_distance(distance)
	{ }

	uint32_t getId(void) const { return m_id; }

	void updatePacket(uint8_t *m_packet)
	{
		uint32_t *fields = (uint32_t *) m_packet;

		fields[(m_index * 6) + 5] = m_id;

		fields[(m_index * 6) + 6] = m_tofOffset;
		fields[(m_index * 6) + 7] = m_tofMax;
		fields[(m_index * 6) + 8] = m_tofBin;

		*((double *) &(fields[(m_index * 6) + 9])) = m_distance;
	}

private:
	uint32_t m_index;

	uint32_t m_id;

	uint32_t m_tofOffset;
	uint32_t m_tofMax;
	uint32_t m_tofBin;

	double m_distance;
};

BeamMonitorConfig::BeamMonitorConfig(
		const boost::property_tree::ptree & conf)
{
	std::string prefix("monitor ");
	boost::property_tree::ptree::const_iterator it;
	size_t plen = prefix.length();

	// Count how many Beam Monitors we have defined...
	std::string format;
	uint32_t numEvent = 0, numHisto = 0;
	for (it = conf.begin(); it != conf.end(); ++it) {
		if (!it->first.compare(0, plen, prefix)) {
			format = it->second.get<std::string>("format", "event");
			if ( !format.compare("event") )
				numEvent++;
			else if ( !format.compare("histo") )
				numHisto++;
		}
	}

	// Make Sure it's "All or Nothing"...! ;-D
	if ( numEvent > 0 && numHisto > 0 ) {
		ERROR("*Mixed* Beam Monitor Output Format Configurations! "
			<< numEvent << " Monitor(s) with Event Format, "
			<< numHisto << " Monitor(s) with Histogram Format."
			<< " Defaulting to ALL Event-Based Beam Monitor Formatting!");
		return;
	}

	m_numBeamMonitors = numHisto;

	// We're saving Beam Monitor Events...
	if ( m_numBeamMonitors == 0 ) {
		INFO("No Beam Monitor Histogramming Configurations Found.");
		return;
	}

	// Allocate Prologue Packet...
	m_sectionSize = sizeof(double) + (4 * sizeof(uint32_t));
	m_payloadSize = sizeof(uint32_t) + (m_numBeamMonitors * m_sectionSize);
	m_packetSize = m_payloadSize + sizeof(ADARA::Header);

	m_packet = new uint8_t[m_packetSize];

	// Initialize Prologue Packet...

	struct timespec ts;

	uint32_t *fields;

	fields = (uint32_t *) m_packet;

	clock_gettime(CLOCK_REALTIME, &ts);

	fields[0] = m_payloadSize;
	fields[1] = ADARA::PacketType::BEAM_MONITOR_CONFIG_V0;
	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;

	fields[4] = m_numBeamMonitors;

	// Extract Each Beam Monitor Config...

	uint32_t index = 0;

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

		DEBUG("Beam Monitor " << bmonId << " Histo Config:"
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " distance=" << distance);

		BeamMonitorInfo *bmonInfo = new BeamMonitorInfo(index++,
			bmonId, tofOffset, tofMax, tofBin, distance);

		bmonInfo->updatePacket(m_packet);

		bmonInfos.push_back(bmonInfo);
	}

	m_connection = StorageManager::onPrologue(
				boost::bind(&BeamMonitorConfig::onPrologue, this));
}

BeamMonitorConfig::~BeamMonitorConfig()
{
	std::vector<BeamMonitorInfo *>::iterator bmi;
	for (bmi=bmonInfos.begin(); bmi != bmonInfos.end(); ++bmi) {
		delete (*bmi);
	}
	bmonInfos.clear();

	m_connection.disconnect();
}

void BeamMonitorConfig::onPrologue(void)
{
	std::vector<BeamMonitorInfo *>::iterator bmi;
	for (bmi=bmonInfos.begin(); bmi != bmonInfos.end(); ++bmi) {
		DEBUG("Updating Beam Monitor " << (*bmi)->getId()
			<< " Config for Prologue.");
		(*bmi)->updatePacket(m_packet);
	}

	StorageManager::addPrologue(m_packet, m_packetSize);
}

