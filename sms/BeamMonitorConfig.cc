
#include <boost/bind.hpp>
#include <sstream>
#include <string>
#include <stdint.h>
#include <time.h>

#include "ADARA.h"
#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "BeamMonitorConfig.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.BeamMonitorConfig"));

class BeamMonitorInfo {

public:

	class BeamMonIdPV : public smsUint32PV {
	public:
		BeamMonIdPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t id = value();

			WARN("BeamMonIdPV: CHANGING BEAM MONITOR ID in Config! "
				<< m_pv_name << " Re-Numbered from " << m_info->getId()
				<< " to " << id << "! (Never Use This. :-)");

			m_info->setId(id);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	static gddAppFuncTableStatus getFormatEnums(gdd &in)
	{
		aitFixedString *str;
		fixedStringDestructor *des;

		str = new aitFixedString[2];
		if (!str)
			return S_casApp_noMemory;

		des = new fixedStringDestructor;
		if (!des) {
			delete [] str;
			return S_casApp_noMemory;
		}
		strncpy(str[0].fixed_string, "event", sizeof(str[0].fixed_string));
		strncpy(str[1].fixed_string, "histo", sizeof(str[1].fixed_string));

		in.setDimension(1);
		in.setBound(0, 0, 2);
		in.putRef(str, des);

		return S_cas_success;
	}

	class BeamMonFormatPV : public smsBooleanPV {
	public:
		BeamMonFormatPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsBooleanPV(name), m_config(config), m_info(info) {}

		gddAppFuncTableStatus getEnums(gdd &in)
		{
			return getFormatEnums(in);
		}

		aitEnum bestExternalType(void) const
		{
			return aitEnumEnum16;
		}

		void changed(void)
		{
			bool do_histo = value();

			std::string newFormat = ( do_histo ) ? "histo" : "event";

			INFO("BeamMonFormatPV: Changing Beam Monitor "
				<< m_info->getId() << " Output Format for "
				<< m_pv_name << " from " << m_info->getFormat()
				<< " to " << newFormat);

			m_info->setFormat(newFormat);

			// Update Event/Histo Counts in Config...
			m_config->updateFormatCounts(do_histo);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	class BeamMonOffsetPV : public smsUint32PV {
	public:
		BeamMonOffsetPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofOffset = value();

			INFO("BeamMonOffsetPV: Changing Beam Monitor "
				<< m_info->getId() << " TOF Offset for "
				<< m_pv_name << " from " << m_info->getTofOffset()
				<< " to " << tofOffset);

			m_info->setTofOffset(tofOffset);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	class BeamMonMaxPV : public smsUint32PV {
	public:
		BeamMonMaxPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofMax = value();

			INFO("BeamMonMaxPV: Changing Beam Monitor "
				<< m_info->getId() << " Maximum TOF for "
				<< m_pv_name << " from " << m_info->getTofMax()
				<< " to " << tofMax);

			m_info->setTofMax(tofMax);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	class BeamMonBinPV : public smsUint32PV {
	public:
		BeamMonBinPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofBin = value();

			INFO("BeamMonBinPV: Changing Beam Monitor "
				<< m_info->getId() << " TOF Histogram Bin Size for "
				<< m_pv_name << " from " << m_info->getTofBin()
				<< " to " << tofBin);

			if ( tofBin < 1 )
			{
				ERROR("BeamMonBinPV: TOF Histogram Bin Size < 1!"
					<< " Setting to 1.");
				tofBin = 1;
			}

			m_info->setTofBin(tofBin);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	class BeamMonDistancePV : public smsFloat64PV {
	public:
		BeamMonDistancePV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info) :
			smsFloat64PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			double distance = value();

			INFO("BeamMonDistancePV: Changing Beam Monitor "
				<< m_info->getId() << " Distance for "
				<< m_pv_name << " from " << m_info->getDistance()
				<< " to " << distance);

			m_info->setDistance(distance);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;
	};

	BeamMonitorInfo(BeamMonitorConfig *config,
			uint32_t index, uint32_t id, std::string format,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double distance) :
		m_config(config), m_index(index), m_id(id), m_format(format),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_distance(distance)
	{
		// Create PVs for Live Beam Monitor Config Controls...

		SMSControl *ctrl = SMSControl::getInstance();

		std::string prefix(ctrl->getBeamlineId());
		prefix += ":SMS";
		prefix += ":BeamMonitor:";

		std::stringstream ss;
		ss << ( m_index + 1 );  // Index SMS Control PV from 1... ;-D
		prefix += ss.str();

		m_pvId = boost::shared_ptr<BeamMonIdPV>( new
			BeamMonIdPV(prefix + ":Id", m_config, this) );

		m_pvFormat = boost::shared_ptr<BeamMonFormatPV>( new
			BeamMonFormatPV(prefix + ":Format", m_config, this) );

		m_pvOffset = boost::shared_ptr<BeamMonOffsetPV>( new
			BeamMonOffsetPV(prefix + ":TofOffset", m_config, this) );

		m_pvMax = boost::shared_ptr<BeamMonMaxPV>( new
			BeamMonMaxPV(prefix + ":MaxTof", m_config, this) );

		m_pvBin = boost::shared_ptr<BeamMonBinPV>( new
			BeamMonBinPV(prefix + ":TofBin", m_config, this) );

		m_pvDistance = boost::shared_ptr<BeamMonDistancePV>( new
			BeamMonDistancePV(prefix + ":Distance", m_config, this) );

		ctrl->addPV(m_pvId);
		ctrl->addPV(m_pvFormat);
		ctrl->addPV(m_pvOffset);
		ctrl->addPV(m_pvMax);
		ctrl->addPV(m_pvBin);
		ctrl->addPV(m_pvDistance);

		// Initialize Beam Monitor Config PVs...

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		m_pvId->update(m_id, &ts);

		if ( !m_format.compare("histo") )
			m_pvFormat->update(1, &ts);
		else // if ( !m_format.compare("event") ), or anything else...
			m_pvFormat->update(0, &ts);

		m_pvOffset->update(m_tofOffset, &ts);
		m_pvMax->update(m_tofMax, &ts);
		m_pvBin->update(m_tofBin, &ts);

		m_pvDistance->update(m_distance, &ts);
	}

	// Gets...

	uint32_t getId(void) const { return m_id; }

	std::string getFormat(void) const { return m_format; }

	uint32_t getTofOffset(void) const { return m_tofOffset; }
	uint32_t getTofMax(void) const { return m_tofMax; }
	uint32_t getTofBin(void) const { return m_tofBin; }

	double getDistance(void) const { return m_distance; }

	// Sets...

	void setId(uint32_t id) { m_id = id; }

	void setFormat(std::string format)
		{ m_format = format; }

	void setTofOffset(uint32_t tofOffset)
		{ m_tofOffset = tofOffset; }
	void setTofMax(uint32_t tofMax)
		{ m_tofMax = tofMax; }
	void setTofBin(uint32_t tofBin)
		{ m_tofBin = tofBin; }

	void setDistance(double distance)
		{ m_distance = distance; }

	// Update Prologue Packet Contents for This Beam Monitor Index...
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

	// Parent Class...
	BeamMonitorConfig *m_config;

	uint32_t m_index;

	uint32_t m_id;

	std::string m_format;

	uint32_t m_tofOffset;
	uint32_t m_tofMax;
	uint32_t m_tofBin;

	double m_distance;

	boost::shared_ptr<BeamMonIdPV> m_pvId;
	boost::shared_ptr<BeamMonFormatPV> m_pvFormat;
	boost::shared_ptr<BeamMonOffsetPV> m_pvOffset;
	boost::shared_ptr<BeamMonMaxPV> m_pvMax;
	boost::shared_ptr<BeamMonBinPV> m_pvBin;
	boost::shared_ptr<BeamMonDistancePV> m_pvDistance;
};

BeamMonitorConfig::BeamMonitorConfig(
		const boost::property_tree::ptree & conf)
{
	boost::property_tree::ptree::const_iterator it;
	std::string conf_prefix("monitor ");
	size_t plen = conf_prefix.length();

	// Count how many Beam Monitors we have defined...
	m_numBeamMonitors = 0;
	for (it = conf.begin(); it != conf.end(); ++it) {
		if (!it->first.compare(0, plen, conf_prefix)) {
			m_numBeamMonitors++;
		}
	}

	// Are We _Only_ Ever Saving Beam Monitor Events...?
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
	clock_gettime(CLOCK_REALTIME, &ts);

	uint32_t *fields = (uint32_t *) m_packet;

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

	// Accumulate Format Request Counts...
	std::string format;
	m_numEvent = 0;
	m_numHisto = 0;

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, conf_prefix))
			continue;

		std::istringstream buffer( it->first.substr(plen) );
		buffer >> bmonId;

		format = it->second.get<std::string>("format", "event");
		if ( !format.compare("event") )
			m_numEvent++;
		else if ( !format.compare("histo") )
			m_numHisto++;

		tofOffset = it->second.get<uint32_t>("offset", 0);
		tofMax = it->second.get<uint32_t>("max", -1);
		tofBin = it->second.get<uint32_t>("bin", 1);

		if ( tofBin < 1 )
		{
			ERROR("BeamMonitorConfig: TOF Histogram Bin Size < 1!"
				<< " Setting to 1.");
			tofBin = 1;
		}

		distance = it->second.get<double>("distance", 0.0);

		DEBUG("Beam Monitor " << bmonId << " Histo Config:"
			<< " format=" << format
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " distance=" << distance);

		BeamMonitorInfo *bmonInfo = new BeamMonitorInfo(this,
			index++, bmonId, format, tofOffset, tofMax, tofBin, distance);

		bmonInfo->updatePacket(m_packet);

		bmonInfos.push_back(bmonInfo);
	}

	// Make Sure it's "All or Nothing"...! ;-D
	if ( m_numEvent > 0 && m_numHisto > 0 ) {
		ERROR("*Mixed* Beam Monitor Output Format Configurations! "
			<< m_numEvent << " Monitor(s) with Event Format, "
			<< m_numHisto << " Monitor(s) with Histogram Format."
			<< " Defaulting to ALL Event-Based Beam Monitor Formatting!");
	}

	// Create PV for Number of Beam Monitors...

	SMSControl *ctrl = SMSControl::getInstance();

	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS";

	m_pvNumBeamMonitors = boost::shared_ptr<smsUint32PV>( new
						smsUint32PV(prefix + ":Control:NumBeamMonitors") );
								// yeah, we're not really "Control" here...

	ctrl->addPV(m_pvNumBeamMonitors);

	m_pvNumBeamMonitors->update(m_numBeamMonitors, &ts);  // use time above

	// Set Up Callback for Adding Beam Monitor Config to Prologue...
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

void BeamMonitorConfig::updateFormatCounts(bool new_histo)
{
	if ( new_histo ) {
		m_numHisto++;
		m_numEvent--;
	}
	else {
		m_numEvent++;
		m_numHisto--;
	}
}

void BeamMonitorConfig::resetPacketTime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint32_t *fields = (uint32_t *) m_packet;

	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
}

void BeamMonitorConfig::onPrologue(void)
{
	// We are in Histogram Beam Monitor Mode, Add to Prologue...!
	// TODO: *Only* _Start_ Sending Beam Monitor Config on New Run Boundary!
	if ( m_numEvent == 0 && m_numHisto > 0 ) {

		// Update Prologue Packet with Latest Beam Monitor Configs...
		std::vector<BeamMonitorInfo *>::iterator bmi;
		for (bmi=bmonInfos.begin(); bmi != bmonInfos.end(); ++bmi) {
			DEBUG("Updating Beam Monitor " << (*bmi)->getId()
				<< " Config for Prologue.");
			(*bmi)->updatePacket(m_packet);
		}

		// Add Combined Beam Monitor Config Packet to Prologue
		StorageManager::addPrologue(m_packet, m_packetSize);
	}
}

