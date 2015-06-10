
#include <boost/bind.hpp>
#include <string>
#include <sstream>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "ADARA.h"
#include "ADARAPackets.h"
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

			INFO("DetBankSetNamePV:"
				<< " Changing Detector Bank Set Name in Config - "
				<< m_pv_name << " Re-Named from " << m_info->getName()
				<< " to " << name);
 
			m_info->setName(name);

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	class DetBankSetBanklistPV : public smsStringPV {
	public:
		DetBankSetBanklistPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsStringPV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			std::string rawBanklist = value();

			std::vector<uint32_t> banklist =
				m_config->extractBankList( rawBanklist );

			std::string oldBanklist =
				m_config->getBanklistStr( m_info->getBanklist() );

			std::string newBanklist =
				m_config->getBanklistStr( banklist );

			INFO("DetBankSetBanklistPV: Changing Detector Bank Set "
				<< m_info->getName() << " Banks List for "
				<< m_pv_name << " from " << oldBanklist
				<< " to " << newBanklist);

			m_info->setBanklist( banklist );

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

		uint32_t flag;

		// None
		flag = 0;
		strncpy( str[flag].fixed_string, "none",
			sizeof(str[flag].fixed_string));

		// Event
		flag = ADARA::DetectorBankSetsPkt::EVENT_FORMAT;
		strncpy(str[flag].fixed_string, "event",
			sizeof(str[flag].fixed_string));

		// Histo
		flag = ADARA::DetectorBankSetsPkt::HISTO_FORMAT;
		strncpy(str[flag].fixed_string, "histo",
			sizeof(str[flag].fixed_string));

		// Histo
		flag = ADARA::DetectorBankSetsPkt::EVENT_FORMAT
			| ADARA::DetectorBankSetsPkt::HISTO_FORMAT;
		strncpy(str[flag].fixed_string, "both",
			sizeof(str[flag].fixed_string));

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
			else if ( oldFlags == ADARA::DetectorBankSetsPkt::EVENT_FORMAT )
				oldFormat = "event";
			else if ( oldFlags == ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
				oldFormat = "histo";
			else if ( oldFlags ==
					( ADARA::DetectorBankSetsPkt::EVENT_FORMAT
						| ADARA::DetectorBankSetsPkt::HISTO_FORMAT ) ) {
				oldFormat = "both";
			}

			uint32_t newFlags = value();

			std::string newFormat;
			if ( newFlags == 0 )
				newFormat = "none";
			else if ( newFlags == ADARA::DetectorBankSetsPkt::EVENT_FORMAT )
				newFormat = "event";
			else if ( newFlags == ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
				newFormat = "histo";
			else if ( newFlags ==
					( ADARA::DetectorBankSetsPkt::EVENT_FORMAT
						| ADARA::DetectorBankSetsPkt::HISTO_FORMAT ) ) {
				newFormat = "both";
			}

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
			smsFloat64PV(name, 0.0), m_config(config), m_info(info) {}

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

	class DetBankSetSuffixPV : public smsStringPV {
	public:
		DetBankSetSuffixPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info) :
			smsStringPV(name), m_config(config), m_info(info) {}

		void changed(void)
		{
			std::string suffix = value();

			INFO("DetBankSetSuffixPV: Changing Detector Bank Set "
				<< m_info->getName() << " Throttle NXentry Suffix for "
				<< m_pv_name << " from " << m_info->getSuffix()
				<< " to " << suffix);

			m_info->setSuffix(suffix);

			// The Suffix String got Sanitized or Truncated, Update PV...!
			if ( suffix.compare( m_info->getSuffix() ) ) {
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				update(m_info->getSuffix(), &ts);
			}

			// Reset Timestamp on Prologue Packet...
			m_config->resetPacketTime();
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;
	};

	DetectorBankSetInfo(DetectorBankSet *config,
			uint32_t index, uint32_t sectionOffset, std::string name,
			std::vector<uint32_t> banklist, uint32_t flags,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double throttle, std::string suffix) :
		m_config(config), m_index(index), m_sectionOffset(sectionOffset),
		m_name(name), m_banklist(banklist), m_flags(flags),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_throttle(throttle), m_suffix(suffix)
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

		m_pvBanks = boost::shared_ptr<DetBankSetBanklistPV>( new
			DetBankSetBanklistPV(prefix + ":Banklist", m_config, this) );

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

		m_pvSuffix = boost::shared_ptr<DetBankSetSuffixPV>( new
			DetBankSetSuffixPV(prefix + ":Suffix", m_config, this) );

		ctrl->addPV(m_pvName);
		ctrl->addPV(m_pvBanks);
		ctrl->addPV(m_pvFormat);
		ctrl->addPV(m_pvOffset);
		ctrl->addPV(m_pvMax);
		ctrl->addPV(m_pvBin);
		ctrl->addPV(m_pvThrottle);
		ctrl->addPV(m_pvSuffix);

		// Initialize Detector Bank Set Config PVs...

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		m_pvName->update(m_name, &ts);

		m_pvBanks->update(m_config->getBanklistStr( m_banklist ), &ts);

		m_pvFormat->update(m_flags, &ts);

		m_pvOffset->update(m_tofOffset, &ts);
		m_pvMax->update(m_tofMax, &ts);
		m_pvBin->update(m_tofBin, &ts);

		m_pvThrottle->update(m_throttle, &ts);

		m_pvSuffix->update(m_suffix, &ts);

		// Initialize Changed Flag...
		m_changed = true;
	}

	// Gets...

	std::string getName(void) const { return m_name; }

	std::vector<uint32_t> getBanklist(void) const { return m_banklist; }

	uint32_t getFlags(void) const { return m_flags; }

	uint32_t getTofOffset(void) const { return m_tofOffset; }
	uint32_t getTofMax(void) const { return m_tofMax; }
	uint32_t getTofBin(void) const { return m_tofBin; }

	double getThrottle(void) const { return m_throttle; }

	std::string getSuffix(void) const { return m_suffix; }

	// Sets...

	void setName(std::string name)
	{
		// Just generate a warning if the name is too long...
		//    - only for human consumption anyway...
		m_config->truncateString(
			name, ADARA::DetectorBankSetsPkt::SET_NAME_SIZE,
			"setName()", "Detector Bank Set Name", false );
		m_name = name;
		m_changed = true;
	}

	void setBanklist(std::vector<uint32_t> banklist)
		{ m_banklist = banklist; m_changed = true; }

	void setFlags(uint32_t flags)
		{ m_flags = flags; m_changed = true; }

	void setTofOffset(uint32_t tofOffset)
		{ m_tofOffset = tofOffset; m_changed = true; }
	void setTofMax(uint32_t tofMax)
		{ m_tofMax = tofMax; m_changed = true; }
	void setTofBin(uint32_t tofBin)
		{ m_tofBin = tofBin; m_changed = true; }

	void setThrottle(uint32_t throttle)
		{ m_throttle = throttle; m_changed = true; }

	void setSuffix(std::string suffix)
	{
		// Sanitize Suffix String for NeXus File NXentry Usage...
		if ( m_config->sanitizeSuffix( suffix ) ) {
			ERROR("setSuffix(): Sanitized"
				<< " Throttle NXentry Suffix for Detector Bank Set "
				<< m_suffix << " to " << suffix);
		}

		// Truncate Suffix String to Max Size for Network Packet...
		m_config->truncateString(
			suffix, ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE,
			"setSuffix()", "Throttle NXentry Suffix", true );

		m_suffix = suffix;

		m_changed = true;
	}

	// Did Anything Change since the last Prologue Packet Update...?
	bool changed() { return m_changed; }

	// Update Prologue Packet Contents for This Detector Bank Set Index...
	void updatePacket(uint8_t *m_packet)
	{
		uint32_t *fields = (uint32_t *) m_packet;

		uint32_t index = 0;

		size_t len;

		// Detector Bank Set Name
		//    - limited to SET_NAME_SIZE characters total (not incl '\0')
		//    - (don't send '\0' over the network, use all the space :-)
		memset((void *) &(fields[m_sectionOffset + index]),
			'\0', ADARA::DetectorBankSetsPkt::SET_NAME_SIZE );
		len = m_name.length();
		if ( len > ADARA::DetectorBankSetsPkt::SET_NAME_SIZE )
			len = ADARA::DetectorBankSetsPkt::SET_NAME_SIZE;
		strncpy((char *) &(fields[m_sectionOffset + index]),
			m_name.c_str(), len );
		index += ADARA::DetectorBankSetsPkt::SET_NAME_SIZE
			/ sizeof(uint32_t);

		fields[m_sectionOffset + index++] = m_flags;

		fields[m_sectionOffset + index++] = m_banklist.size();

		for (std::vector<uint32_t>::iterator b=m_banklist.begin();
				b != m_banklist.end(); ++b)
		{
			fields[m_sectionOffset + index++] = *b;
		}

		fields[m_sectionOffset + index++] = m_tofOffset;
		fields[m_sectionOffset + index++] = m_tofMax;
		fields[m_sectionOffset + index++] = m_tofBin;

		*((double *) &(fields[m_sectionOffset + index])) = m_throttle;
		index += 2;

		// Throttle NeXus NXentry Suffix
		//    - limited to THROTTLE_SUFFIX_SIZE chars total (not incl '\0')
		//    - (don't send '\0' over the network, use all the space :-)
		memset((void *) &(fields[m_sectionOffset + index]),
			'\0', ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE );
		len = m_suffix.length();
		if ( len > ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE )
			len = ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE;
		strncpy((char *) &(fields[m_sectionOffset + index]),
			m_suffix.c_str(), len );
		index += ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE
			/ sizeof(uint32_t);

		m_changed = false;
	}

private:

	// Parent Class...
	DetectorBankSet *m_config;

	uint32_t m_index;

	uint32_t m_sectionOffset;

	std::string m_name;

	std::vector<uint32_t> m_banklist;

	uint32_t m_flags;

	uint32_t m_tofOffset;
	uint32_t m_tofMax;
	uint32_t m_tofBin;

	double m_throttle;

	std::string m_suffix;

	bool m_changed;

	boost::shared_ptr<DetBankSetNamePV> m_pvName;
	boost::shared_ptr<DetBankSetBanklistPV> m_pvBanks;
	boost::shared_ptr<DetBankSetFormatPV> m_pvFormat;
	boost::shared_ptr<DetBankSetOffsetPV> m_pvOffset;
	boost::shared_ptr<DetBankSetMaxPV> m_pvMax;
	boost::shared_ptr<DetBankSetBinPV> m_pvBin;

	boost::shared_ptr<DetBankSetThrottlePV> m_pvThrottle;

	boost::shared_ptr<DetBankSetSuffixPV> m_pvSuffix;
};

DetectorBankSet::DetectorBankSet(
		const boost::property_tree::ptree & conf)
{
	boost::property_tree::ptree::const_iterator it;
	std::string conf_prefix("bankset ");
	size_t b, e, plen = conf_prefix.length();

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

	// Extract Each Detector Bank Set Config...

	uint32_t index = 0;

	std::string detBankSetName;

	std::string format;

	uint32_t flags;

	uint32_t tofOffset;
	uint32_t tofMax;
	uint32_t tofBin;

	double throttle;

	std::string suffix;

	// Starting Section Offset
	//    - step past Prologue Packet Header & numDetBankSets (1)...
	uint32_t headerOffset = sizeof(ADARA::Header) / sizeof(uint32_t);
	uint32_t sectionOffset = headerOffset + 1;   // numDetBankSets...

	// Base Section Count (in terms of 4-byte field array elements)
	//    - leaves space for specific number of banks in a given list...
	uint32_t baseSectionCount = 0
		// name, SET_NAME_SIZE characters...
		+ ( ADARA::DetectorBankSetsPkt::SET_NAME_SIZE / sizeof(uint32_t) )
		+ 2 // format flags & bank list count
		+ 0 // # of banks in list, t.b.d. per set via m_banklist.size()...
		+ 3 // histogram parameters (offset, max, bin)
		+ 2 // throttle rate (double)
		// throttle suffix, THROTTLE_SUFFIX_SIZE characters...
		+ ( ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE
			/ sizeof(uint32_t) )
		;

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, conf_prefix))
			continue;

		b = it->first.find_first_of('\"', plen);
		// Starting Quote Found...
		if (b != std::string::npos) {
			e = it->first.find_first_of('\"', ++b); // strip off quote...
			// No Ending Quote Found... (Just use string length...)
			if (e == std::string::npos) {
				e = it->first.length();
			}
			else e--; // strip off quote...
		}
		// No Starting Quote (Malformed, but try to wing it...)
		else {
			b = plen;
			e = it->first.length();
		}

		// Handle Empty or Missing Name...
		// (Apparently this never happens, as ptree eats the trailing space
		//    and we fail to match the prefix, so the section is ignored.)
		if ( b == e ) {
			detBankSetName = "NoName";
		}
		// Extract Name String from (Any) Quotes...
		else {
			detBankSetName = it->first.substr(b, e - b + 1);
		}

		// Warn of Impending String Truncation...
		truncateString(
			detBankSetName, ADARA::DetectorBankSetsPkt::SET_NAME_SIZE,
			"DetectorBankSet()", "Detector Bank Set Name", false );

		format = it->second.get<std::string>("format", "event");

		// Set Format Flags...
		flags = 0;
		if ( !format.compare("histo") )
			flags |= ADARA::DetectorBankSetsPkt::HISTO_FORMAT;
		else if ( !format.compare("both") ) {
			flags |= ADARA::DetectorBankSetsPkt::EVENT_FORMAT
				| ADARA::DetectorBankSetsPkt::HISTO_FORMAT;
		}
		else // if ( !format.compare("event") )
			flags |= ADARA::DetectorBankSetsPkt::EVENT_FORMAT;

		std::string banklistStr =
			it->second.get<std::string>("banklist", "none");

		std::vector<uint32_t> banklist = extractBankList(banklistStr);

		std::string newBanklist = getBanklistStr( banklist );

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

		suffix = it->second.get<std::string>("suffix", "throttled");

		// Sanitize Suffix String for NeXus File NXentry Usage...
		if ( sanitizeSuffix( suffix ) ) {
			WARN("DetectorBankSet: Sanitized"
				<< " Throttle NXentry Suffix for Detector Bank Set "
				<< detBankSetName
				<< " to " << suffix);
		}

		// Truncate Suffix String to Max Size for Network Packet...
		truncateString(
			suffix, ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE,
			"DetectorBankSet()", "Throttle NXentry Suffix", true );

		DEBUG("Detector Bank Set " << detBankSetName << " Config:"
			<< " index=" << index
			<< " sectionOffset=" << sectionOffset
			<< " banklist=" << newBanklist
			<< " format=" << format
			<< " flags=" << flags
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " throttle=" << throttle
			<< " suffix=" << suffix);

		DetectorBankSetInfo *detBankSetInfo = new DetectorBankSetInfo(this,
			index++, sectionOffset, detBankSetName, banklist, flags,
			tofOffset, tofMax, tofBin, throttle, suffix);

		detBankSetInfos.push_back(detBankSetInfo);

		// Increment Section Offset for Next Detector Bank Set...
		sectionOffset += baseSectionCount + banklist.size();
	}

	// Allocate Prologue Packet...

	m_payloadSize = ( sectionOffset - headerOffset ) * sizeof(uint32_t);
	m_packetSize = m_payloadSize + sizeof(ADARA::Header);

	m_packet = new uint8_t[m_packetSize];

	// Initialize Prologue Packet...

	uint32_t *fields = (uint32_t *) m_packet;

	fields[0] = m_payloadSize;
	fields[1] = ADARA::PacketType::DETECTOR_BANK_SETS_V0;
	fields[2] = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = now.tv_nsec;

	fields[4] = m_numDetBankSets;

	// Update Prologue Packet for Each Detector Bank Set...

	std::vector<DetectorBankSetInfo *>::iterator dbs;
	for (dbs=detBankSetInfos.begin(); dbs != detBankSetInfos.end(); ++dbs)
	{
		(*dbs)->updatePacket(m_packet);
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

// Inspired by Jilles De Wit on StackOverflow... ;-D
std::vector<uint32_t> DetectorBankSet::extractBankList(
		std::string banklistStr )
{
	std::vector<uint32_t> banklist;

	std::string sep = "[, ]";

	uint32_t bank;

	size_t b, e;

	b = 0;

	while ( b < banklistStr.length() )
	{
		e = banklistStr.find_first_of( sep, b );

		if ( e == std::string::npos )
			e = banklistStr.length();

		// Discard Empty Tokens...
		if ( b != e )
		{
			std::istringstream buffer( banklistStr.substr(b, e - b) );
			buffer >> bank;

			banklist.push_back(bank);

			b = e + 1;
		}

		else b++;
	}

	return banklist;
}

std::string DetectorBankSet::getBanklistStr(
		std::vector<uint32_t> banklist )
{
	std::stringstream ss;

	ss << "[";

	bool first = true;
	for (std::vector<uint32_t>::iterator b=banklist.begin();
			b != banklist.end(); ++b)
	{
		if ( first )
			first = false;
		else
			ss << ", ";

		ss << *b;
	}

	ss << "]";

	return ss.str();
}

bool DetectorBankSet::truncateString( std::string & str, size_t sz,
		std::string caller, std::string desc, bool is_error )
{
	bool changed = false;

	if ( str.length() > sz ) {
		std::stringstream ss;
		ss << caller << ": " << desc << " Too Long!"
			<< " length(" << str << ")=" << str.length()
			<< " > " << ( sz ) << ", ";
		// Error! Actually Change the String...!
		if ( is_error ) {
			ss << "truncated to: " << str.substr( 0, sz );
			ERROR( ss.str() );
			str = str.substr( 0, sz );
			changed = true;
		}
		// Just a Warning, Leave String "As Is"...
		else {
			ss << "will be truncated to: " << str.substr( 0, sz );
			WARN( ss.str() );
		}
	}

	return( changed );
}

bool DetectorBankSet::sanitizeSuffix( std::string & suffix )
{
	std::string bad = " \t\'\",.;:<>[]{}()|/\\?~!@#$%^&*+=";

	size_t next, last;

	bool changed = false;

	last = 0;

	while ( (next = suffix.find_first_of( bad, last ))
			!= std::string::npos )
	{
		if ( next != last + 1 || last == 0 )
			suffix.replace( next, 1, "-" );   // replace space with '-'...
		else {
			suffix.replace( next, 1, "" );   // just remove double-spaces...
		}

		changed = true;

		last = next;
	}

	return( changed );
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
	// Update Prologue Packet with Latest Detector Bank Set Configs...
	std::vector<DetectorBankSetInfo *>::iterator dbs;
	for (dbs=detBankSetInfos.begin(); dbs != detBankSetInfos.end(); ++dbs)
	{
		if ( (*dbs)->changed() ) {
			DEBUG("Updating Detector Bank Set " << (*dbs)->getName()
				<< " Config for Prologue.");
			(*dbs)->updatePacket(m_packet);
		}
	}

	// Add Combined Detector Bank Set Config Packet to Prologue
	StorageManager::addPrologue(m_packet, m_packetSize);
}

