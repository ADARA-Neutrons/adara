
#include <boost/bind.hpp>
#include <sstream>
#include <string>
#include <stdint.h>
#include <time.h>

#include "ADARA.h"
#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "DetectorBankSet.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DetectorBankSet"));

class DetectorBankSetInfo {

public:

	class DetBankSetNamePV : public smsStringPV {
	public:
		DetBankSetNamePV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsStringPV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			std::string name = value();

			WARN("DetBankSetNamePV:"
				<< " CHANGING Detector Bank Set Name in Config! "
				<< m_pv_name << " Re-Named from " << m_info->getName()
				<< " to " << name << "! (Never Use This. :-)");

			m_info->setName(name);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	static gddAppFuncTableStatus getFormatEnums(gdd &in)
	{
		aitFixedString *str;
		fixedStringDestructor *des;

		str = new aitFixedString[4];
		if (!str)
			return S_casApp_noMemory;

		des = new fixedStringDestructor;
		if (!des) {
			delete [] str;
			return S_casApp_noMemory;
		}
		strncpy(str[0].fixed_string, "none", sizeof(str[0].fixed_string));
		strncpy(str[1].fixed_string, "event", sizeof(str[1].fixed_string));
		strncpy(str[2].fixed_string, "histo", sizeof(str[2].fixed_string));
		strncpy(str[3].fixed_string, "both", sizeof(str[3].fixed_string));

		in.setDimension(1);
		in.setBound(0, 0, 4);
		in.putRef(str, des);

		return S_cas_success;
	}

	class DetBankSetFormatPV : public smsUint32PV {
	public:
		DetBankSetFormatPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

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
			uint32_t oldFlags = m_info->getFlags();

			std::string oldFormat;
			if ( oldFlags == 0 )
				oldFormat = "none";
			else if ( oldFlags == 1 )
				oldFormat = "event";
			else if ( oldFlags == 2 )
				oldFormat = "histo";
			else if ( oldFlags == 3 )
				oldFormat = "both";

			uint32_t newFlags = value();

			std::string newFormat;
			if ( newFlags == 0 )
				newFormat = "none";
			else if ( newFlags == 1 )
				newFormat = "event";
			else if ( newFlags == 2 )
				newFormat = "histo";
			else if ( newFlags == 3 )
				newFormat = "both";

			INFO("DetBankSetFormatPV: Changing Detector Bank Set "
				<< m_info->getName() << " Output Format for "
				<< m_pv_name << " from " << oldFormat
				<< " to " << newFormat);

			m_info->setFlags(newFlags);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	class DetBankSetOffsetPV : public smsUint32PV {
	public:
		DetBankSetOffsetPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofOffset = value();

			INFO("DetBankSetOffsetPV: Changing Detector Bank Set "
				<< m_info->getName() << " TOF Offset for "
				<< m_pv_name << " from " << m_info->getTofOffset()
				<< " to " << tofOffset);

			m_info->setTofOffset(tofOffset);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	class DetBankSetMaxPV : public smsUint32PV {
	public:
		DetBankSetMaxPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofMax = value();

			INFO("DetBankSetMaxPV: Changing Detector Bank Set "
				<< m_info->getName() << " Maximum TOF for "
				<< m_pv_name << " from " << m_info->getTofMax()
				<< " to " << tofMax);

			m_info->setTofMax(tofMax);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	class DetBankSetBinPV : public smsUint32PV {
	public:
		DetBankSetBinPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsUint32PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			uint32_t tofBin = value();

			INFO("DetBankSetBinPV: Changing Detector Bank Set "
				<< m_info->getName() << " TOF Histogram Bin Size for "
				<< m_pv_name << " from " << m_info->getTofBin()
				<< " to " << tofBin);

			if ( tofBin < 1 )
			{
				ERROR("DetBankSetBinPV: TOF Histogram Bin Size < 1!"
					<< " Setting to 1.");
				tofBin = 1;
			}

			m_info->setTofBin(tofBin);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	class DetBankSetThrottlePV : public smsFloat64PV {
	public:
		DetBankSetThrottlePV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsFloat64PV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			double throttle = value();

			INFO("DetBankSetBinPV: Changing Detector Bank Set "
				<< m_info->getName() << " Throttle Frequency for "
				<< m_pv_name << " from " << m_info->getThrottle()
				<< " to " << throttle);

			m_info->setThrottle(throttle);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	DetectorBankSetInfo(DetectorBankSet *config,
			uint32_t index, std::string name, uint32_t flags,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double throttle) :
		m_config(config), m_index(index), m_name(name), m_flags(flags),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_throttle(throttle)
	{
		// Create PVs for Live Detector Bank Set Config Controls...

		SMSControl *ctrl = SMSControl::getInstance();

		std::string prefix(ctrl->getBeamlineId());
		prefix += ":SMS";
		prefix += ":DetectorBankSet:";

		std::stringstream ss;
		ss << ( m_index + 1 );  // Index SMS Control PV from 1... ;-D
		prefix += ss.str();

		m_pvName = boost::shared_ptr<DetBankSetNamePV>( new
			DetBankSetNamePV(prefix + ":Name", m_config, this) );

		m_pvFormat = boost::shared_ptr<DetBankSetFormatPV>( new
			DetBankSetFormatPV(prefix + ":Format", m_config, this) );

		m_pvOffset = boost::shared_ptr<DetBankSetOffsetPV>( new
			DetBankSetOffsetPV(prefix + ":TofOffset", m_config, this) );

		m_pvMax = boost::shared_ptr<DetBankSetMaxPV>( new
			DetBankSetMaxPV(prefix + ":MaxTof", m_config, this) );

		m_pvBin = boost::shared_ptr<DetBankSetBinPV>( new
			DetBankSetBinPV(prefix + ":TofBin", m_config, this) );

		m_pvThrottle = boost::shared_ptr<DetBankSetThrottlePV>( new
			DetBankSetThrottlePV(prefix + ":Throttle", m_config, this) );

		ctrl->addPV(m_pvName);
		ctrl->addPV(m_pvFormat);
		ctrl->addPV(m_pvOffset);
		ctrl->addPV(m_pvMax);
		ctrl->addPV(m_pvBin);
		ctrl->addPV(m_pvThrottle);

		// Initialize Detector Bank Set Config PVs...

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		m_pvName->update(m_name, &ts);

		m_pvFormat->update(m_flags, &ts);

		m_pvOffset->update(m_tofOffset, &ts);
		m_pvMax->update(m_tofMax, &ts);
		m_pvBin->update(m_tofBin, &ts);

		m_pvBin->update(m_throttle, &ts);
	}

	// Gets...

	std::string getName(void) const { return m_name; }

	uint32_t getFlags(void) const { return m_flags; }

	uint32_t getTofOffset(void) const { return m_tofOffset; }
	uint32_t getTofMax(void) const { return m_tofMax; }
	uint32_t getTofBin(void) const { return m_tofBin; }

	double getThrottle(void) const { return m_throttle; }

	// Sets...

	void setName(std::string name) { m_name = name; }

	void setFlags(uint32_t flags)
		{ m_flags = flags; }

	void setTofOffset(uint32_t tofOffset)
		{ m_tofOffset = tofOffset; }
	void setTofMax(uint32_t tofMax)
		{ m_tofMax = tofMax; }
	void setTofBin(uint32_t tofBin)
		{ m_tofBin = tofBin; }

	void setThrottle(uint32_t throttle)
		{ m_throttle = throttle; }

	// Update Prologue Packet Contents for This Detector Bank Set Index...
	void updatePacket(uint8_t *m_packet)
	{
		uint32_t *fields = (uint32_t *) m_packet;

		// fields[(m_index * 6) + 5] = m_name; // TODO XXX Whoa...

		fields[(m_index * 6) + 6] = m_tofOffset;
		fields[(m_index * 6) + 7] = m_tofMax;
		fields[(m_index * 6) + 8] = m_tofBin;

		*((double *) &(fields[(m_index * 6) + 9])) = m_throttle;
	}

private:

	// Parent Class...
	DetectorBankSet *m_config;

	uint32_t m_index;

	std::string m_name;

	uint32_t m_flags;

	uint32_t m_tofOffset;
	uint32_t m_tofMax;
	uint32_t m_tofBin;

	double m_throttle;

	boost::shared_ptr<DetBankSetNamePV> m_pvName;
	boost::shared_ptr<DetBankSetFormatPV> m_pvFormat;
	boost::shared_ptr<DetBankSetOffsetPV> m_pvOffset;
	boost::shared_ptr<DetBankSetMaxPV> m_pvMax;
	boost::shared_ptr<DetBankSetBinPV> m_pvBin;

	boost::shared_ptr<DetBankSetThrottlePV> m_pvThrottle;
};

DetectorBankSet::DetectorBankSet(
		const boost::property_tree::ptree & conf)
{
	boost::property_tree::ptree::const_iterator it;
	std::string conf_prefix("bankset ");
	size_t plen = conf_prefix.length();

	// Count how many Detector Bank Sets we have defined...
	m_numDetBankSets = 0;
	for (it = conf.begin(); it != conf.end(); ++it) {
		if (!it->first.compare(0, plen, conf_prefix)) {
			m_numDetBankSets++;
		}
	}

	// Create PV for Number of Detector Bank Sets...

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	SMSControl *ctrl = SMSControl::getInstance();

	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS";

	m_pvNumDetBankSets = boost::shared_ptr<smsUint32PV>( new
						smsUint32PV(prefix + ":Control:NumDetBankSets") );
								// yeah, we're not really "Control" here...

	ctrl->addPV(m_pvNumDetBankSets);

	m_pvNumDetBankSets->update(m_numDetBankSets, &now);

	// Are We _Only_ Ever Saving Detector Bank Set Events...?
	if ( m_numDetBankSets == 0 ) {
		INFO("No Detector Bank Set Configurations Found.");
		return;
	}

	// Allocate Prologue Packet...

	m_sectionSize = sizeof(double) + (4 * sizeof(uint32_t));
	m_payloadSize = sizeof(uint32_t) + (m_numDetBankSets * m_sectionSize);
	m_packetSize = m_payloadSize + sizeof(ADARA::Header);

	m_packet = new uint8_t[m_packetSize];

	// Initialize Prologue Packet...

	uint32_t *fields = (uint32_t *) m_packet;

	fields[0] = m_payloadSize;
	fields[1] = ADARA::PacketType::BEAM_MONITOR_CONFIG_V0;
	fields[2] = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = now.tv_nsec;

	fields[4] = m_numDetBankSets;

	// Extract Each Detector Bank Set Config...

	uint32_t index = 0;

	std::string detBankSetName;

	uint32_t flags;

	uint32_t tofOffset;
	uint32_t tofMax;
	uint32_t tofBin;

	double throttle;

	// Accumulate Format Request Counts...
	std::string format;

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, conf_prefix))
			continue;

		detBankSetName = it->first.substr(plen);

		format = it->second.get<std::string>("format", "event");

		// Set Format Flags...
		flags = 0;
		if ( !format.compare("histo") )
			flags |= 2;
		else if ( !format.compare("both") )
			flags |= 1 + 2;
		else // if ( !format.compare("event") )
			flags |= 1;

		std::string banklist =
			it->second.get<std::string>("banklist", "none");

		tofOffset = it->second.get<uint32_t>("offset", 0);
		tofMax = it->second.get<uint32_t>("max", -1);
		tofBin = it->second.get<uint32_t>("bin", 1);

		if ( tofBin < 1 )
		{
			ERROR("DetectorBankSet: TOF Histogram Bin Size < 1!"
				<< " Setting to 1.");
			tofBin = 1;
		}

		throttle = it->second.get<double>("throttle", 0.0);

		DEBUG("Detector Bank Set " << detBankSetName << " Histo Config:"
			<< " banklist=[" << banklist << "]"
			<< " format=" << format
			<< " flags=" << flags
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " throttle=" << throttle);

		DetectorBankSetInfo *detBankSetInfo = new DetectorBankSetInfo(this,
			index++, detBankSetName, flags, tofOffset, tofMax, tofBin,
			throttle);

		detBankSetInfo->updatePacket(m_packet);

		detBankSetInfos.push_back(detBankSetInfo);
	}

	// Set Up Callback for Adding Detector Bank Set Config to Prologue...
	m_connection = StorageManager::onPrologue(
				boost::bind(&DetectorBankSet::onPrologue, this));
}

DetectorBankSet::~DetectorBankSet()
{
	std::vector<DetectorBankSetInfo *>::iterator dbs;
	for (dbs=detBankSetInfos.begin(); dbs != detBankSetInfos.end(); ++dbs) {
		delete (*dbs);
	}
	detBankSetInfos.clear();

	m_connection.disconnect();
}

void DetectorBankSet::resetPacketTime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint32_t *fields = (uint32_t *) m_packet;

	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
}

void DetectorBankSet::onPrologue(void)
{
	// We are in Histogram Detector Bank Set Mode, Add to Prologue...!
	// TODO: *Only* _Start_ Sending Detector Bank Set Config on
	//    New Run Boundary!
	if ( m_numEvent == 0 && m_numHisto > 0 ) {

		// Update Prologue Packet with Latest Detector Bank Set Configs...
		std::vector<DetectorBankSetInfo *>::iterator dbs;
		for (dbs=detBankSetInfos.begin();
				dbs != detBankSetInfos.end(); ++dbs)
		{
			DEBUG("Updating Detector Bank Set " << (*dbs)->getName()
				<< " Config for Prologue.");
			(*dbs)->updatePacket(m_packet);
		}

		// Add Combined Detector Bank Set Config Packet to Prologue
		StorageManager::addPrologue(m_packet, m_packetSize);
	}
}

