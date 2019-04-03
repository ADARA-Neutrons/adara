
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.BeamMonitorConfig"));

#include <sstream>
#include <string>

#include <stdint.h>
#include <time.h>

#include <boost/bind.hpp>

#include "ADARA.h"
#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "BeamMonitorConfig.h"
#include "StorageManager.h"

class BeamMonitorInfo {

public:

	class BeamMonIdPV : public smsUint32PV {
	public:
		BeamMonIdPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				uint32_t min = 0, uint32_t max = INT32_MAX,
				bool auto_save = false) :
			smsUint32PV(name, min, max, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			uint32_t id = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << id;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( id != m_info->getId() )
			{
				WARN("BeamMonIdPV: CHANGING BEAM MONITOR ID in Config! "
					<< m_pv_name << " Re-Numbered from " << m_info->getId()
					<< " to " << id << "! (Never Use This. :-)");

				m_info->setId(id);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
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
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				bool auto_save = false) :
			smsBooleanPV(name, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

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

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				// Use String Representation of Boolean for AutoSave File...
				// (No Special Enum String Support in AutoSave...!)
				std::string bvalstr = ( do_histo ) ? "true" : "false";
				StorageManager::autoSavePV( m_pv_name, bvalstr, &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( newFormat.compare( m_info->getFormat() ) )
			{
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
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
	};

	class BeamMonOffsetPV : public smsUint32PV {
	public:
		BeamMonOffsetPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				uint32_t min = 0, uint32_t max = INT32_MAX,
				bool auto_save = false) :
			smsUint32PV(name, min, max, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			uint32_t tofOffset = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << tofOffset;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( tofOffset != m_info->getTofOffset() )
			{
				INFO("BeamMonOffsetPV: Changing Beam Monitor "
					<< m_info->getId() << " TOF Offset for "
					<< m_pv_name << " from " << m_info->getTofOffset()
					<< " to " << tofOffset);

				m_info->setTofOffset(tofOffset);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
	};

	class BeamMonMaxPV : public smsUint32PV {
	public:
		BeamMonMaxPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				uint32_t min = 0, uint32_t max = INT32_MAX,
				bool auto_save = false) :
			smsUint32PV(name, min, max, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			uint32_t tofMax = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << tofMax;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( tofMax != m_info->getTofMax() )
			{
				INFO("BeamMonMaxPV: Changing Beam Monitor "
					<< m_info->getId() << " Maximum TOF for "
					<< m_pv_name << " from " << m_info->getTofMax()
					<< " to " << tofMax);

				m_info->setTofMax(tofMax);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
	};

	class BeamMonBinPV : public smsUint32PV {
	public:
		BeamMonBinPV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				uint32_t min = 0, uint32_t max = INT32_MAX,
	            bool auto_save = false) :
			smsUint32PV(name, min, max, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			uint32_t tofBin = value();

			if ( tofBin < 1 )
			{
				ERROR("BeamMonBinPV: TOF Histogram Bin Size < 1!"
					<< " Setting to 1.");
				tofBin = 1;
			}

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << tofBin;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( tofBin != m_info->getTofBin() )
			{
				INFO("BeamMonBinPV: Changing Beam Monitor "
					<< m_info->getId() << " TOF Histogram Bin Size for "
					<< m_pv_name << " from " << m_info->getTofBin()
					<< " to " << tofBin);

				m_info->setTofBin(tofBin);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
	};

	class BeamMonDistancePV : public smsFloat64PV {
	public:
		BeamMonDistancePV(const std::string &name,
				BeamMonitorConfig *config, BeamMonitorInfo *info,
				double min = FLOAT64_MIN, double max = FLOAT64_MAX,
				double epsilon = FLOAT64_EPSILON,
		        bool auto_save = false) :
			smsFloat64PV(name, min, max, epsilon, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			double distance = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << std::setprecision(17) << distance;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( !approximatelyEqual( distance, m_info->getDistance(),
					m_epsilon ) )
			{
				INFO("BeamMonDistancePV: Changing Beam Monitor "
					<< m_info->getId() << " Distance for "
					<< m_pv_name << std::setprecision(17)
					<< " from " << m_info->getDistance()
					<< " to " << distance);

				m_info->setDistance(distance);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		BeamMonitorConfig *m_config;
		BeamMonitorInfo *m_info;

		bool m_auto_save;
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
			BeamMonIdPV(prefix + ":Id", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvFormat = boost::shared_ptr<BeamMonFormatPV>( new
			BeamMonFormatPV(prefix + ":Format", m_config, this,
				/* AutoSave */ true) );

		m_pvOffset = boost::shared_ptr<BeamMonOffsetPV>( new
			BeamMonOffsetPV(prefix + ":TofOffset", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvMax = boost::shared_ptr<BeamMonMaxPV>( new
			BeamMonMaxPV(prefix + ":MaxTof", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvBin = boost::shared_ptr<BeamMonBinPV>( new
			BeamMonBinPV(prefix + ":TofBin", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvDistance = boost::shared_ptr<BeamMonDistancePV>( new
			BeamMonDistancePV(prefix + ":Distance", m_config, this,
				FLOAT64_MIN, FLOAT64_MAX, FLOAT64_EPSILON,
				/* AutoSave */ true) );

		ctrl->addPV(m_pvId);
		ctrl->addPV(m_pvFormat);
		ctrl->addPV(m_pvOffset);
		ctrl->addPV(m_pvMax);
		ctrl->addPV(m_pvBin);
		ctrl->addPV(m_pvDistance);

		// Initialize Beam Monitor Config PVs...

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		m_pvId->update(m_id, &now);

		if ( !m_format.compare("histo") )
			m_pvFormat->update(1, &now);
		else // if ( !m_format.compare("event") ), or anything else...
			m_pvFormat->update(0, &now);

		m_pvOffset->update(m_tofOffset, &now);
		m_pvMax->update(m_tofMax, &now);
		m_pvBin->update(m_tofBin, &now);

		m_pvDistance->update(m_distance, &now);

		/* Restore Any PVs to AutoSaved Config Values... */

		struct timespec ts;
		uint32_t uvalue;
		double dvalue;
		bool bvalue;

		if ( StorageManager::getAutoSavePV(
				m_pvId->getName(), uvalue, ts ) ) {
			// Don't Manually Set "m_id" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvId->update(uvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvFormat->getName(), bvalue, ts ) ) {
			// Don't Manually Set "m_format" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvFormat->update(bvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvOffset->getName(), uvalue, ts ) ) {
			// Don't Manually Set "m_tofOffset" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvOffset->update(uvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvMax->getName(), uvalue, ts ) ) {
			// Don't Manually Set "m_tofMax" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvMax->update(uvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvBin->getName(), uvalue, ts ) ) {
			// Don't Manually Set "m_tofBin" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvBin->update(uvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvDistance->getName(), dvalue, ts ) ) {
			// Don't Manually Set "m_distance" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvDistance->update(dvalue, &ts);
		}

		// Initialize Changed Flag...
		m_changed = true;
	}

	// Gets...

	uint32_t getId(void) const { return m_id; }

	std::string getFormat(void) const { return m_format; }

	uint32_t getTofOffset(void) const { return m_tofOffset; }
	uint32_t getTofMax(void) const { return m_tofMax; }
	uint32_t getTofBin(void) const { return m_tofBin; }

	double getDistance(void) const { return m_distance; }

	// Sets...

	void setId(uint32_t id)
		{ m_id = id; m_changed = true; }

	void setFormat(std::string format)
		{ m_format = format; m_changed = true; }

	void setTofOffset(uint32_t tofOffset)
		{ m_tofOffset = tofOffset; m_changed = true; }
	void setTofMax(uint32_t tofMax)
		{ m_tofMax = tofMax; m_changed = true; }
	void setTofBin(uint32_t tofBin)
		{ m_tofBin = tofBin; m_changed = true; }

	void setDistance(double distance)
		{ m_distance = distance; m_changed = true; }

	// Did Anything Change since the last Prologue Packet Update...?
	bool changed() { return m_changed; }

	// Update Prologue Packet Contents for This Beam Monitor Index...
	void updatePacket(uint8_t *m_packet)
	{
		uint32_t *fields = (uint32_t *) m_packet;

		fields[(m_index * 6) + 5] = m_id;

		fields[(m_index * 6) + 6] = m_tofOffset;
		fields[(m_index * 6) + 7] = m_tofMax;
		fields[(m_index * 6) + 8] = m_tofBin;

		*((double *) &(fields[(m_index * 6) + 9])) = m_distance;

		m_changed = false;
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

	bool m_changed;

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

	// Create PV for Number of Beam Monitors...

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	SMSControl *ctrl = SMSControl::getInstance();

	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS";

	m_pvNumBeamMonitors = boost::shared_ptr<smsUint32PV>( new
						smsUint32PV(prefix + ":Control:NumBeamMonitors") );
								// yeah, we're not really "Control" here...

	ctrl->addPV(m_pvNumBeamMonitors);

	m_pvNumBeamMonitors->update(m_numBeamMonitors, &now);

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

	uint32_t *fields = (uint32_t *) m_packet;

	fields[0] = m_payloadSize;
	fields[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::BEAM_MONITOR_CONFIG_TYPE,
		ADARA::PacketType::BEAM_MONITOR_CONFIG_VERSION );
	fields[2] = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = now.tv_nsec;

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
			if ( (*bmi)->changed() ) {
				DEBUG("Updating Beam Monitor " << (*bmi)->getId()
					<< " Config for Prologue.");
				(*bmi)->updatePacket(m_packet);
			}
		}

		// Add Combined Beam Monitor Config Packet to Prologue
		StorageManager::addPrologue(m_packet, m_packetSize);
	}
}

