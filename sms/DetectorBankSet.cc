
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DetectorBankSet"));

#include <string>
#include <sstream>

#include <string.h>
#include <stdint.h>
#include <time.h>

#include <boost/bind.hpp>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "ADARAUtils.h"
#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "DetectorBankSet.h"
#include "StorageManager.h"

class DetectorBankSetInfo {

public:

	class DetBankSetNamePV : public smsStringPV {
	public:
		DetBankSetNamePV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
				bool auto_save = false) :
			smsStringPV(name, auto_save), m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			std::string name = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				StorageManager::autoSavePV( m_pv_name, name, &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( name.compare( m_info->getName() ) )
			{
				INFO("DetBankSetNamePV:"
					<< " Changing Detector Bank Set Name in Config - "
					<< m_pv_name << " Re-Named from " << m_info->getName()
					<< " to " << name);
 
				m_info->setName(name);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetBanklistPV : public smsStringPV {
	public:
		DetBankSetBanklistPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
				bool auto_save = false) :
			smsStringPV(name, auto_save), m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			std::string rawBanklist = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				StorageManager::autoSavePV( m_pv_name, rawBanklist, &ts );
			}

			std::vector<uint32_t> banklist;
			Utils::parseArrayString( rawBanklist, banklist );

			std::string oldBanklist;
			Utils::printArrayString( m_info->getBanklist(), oldBanklist );

			std::string newBanklist;
			Utils::printArrayString( banklist, newBanklist );

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( newBanklist.compare( oldBanklist ) )
			{
				INFO("DetBankSetBanklistPV: Changing Detector Bank Set "
					<< m_info->getName() << " Banks List for "
					<< m_pv_name << " from " << oldBanklist
					<< " to " << newBanklist);

				m_info->setBanklist( banklist );

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
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
		flag = ADARA::EVENT_FORMAT;
		strncpy(str[flag].fixed_string, "event",
			sizeof(str[flag].fixed_string));

		// Histo
		flag = ADARA::HISTO_FORMAT;
		strncpy(str[flag].fixed_string, "histo",
			sizeof(str[flag].fixed_string));

		// Both Event & Histo
		flag = ADARA::EVENT_FORMAT | ADARA::HISTO_FORMAT;
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
				DetectorBankSet *config, DetectorBankSetInfo *info,
				uint32_t min = 0, uint32_t max = INT32_MAX,
				bool auto_save = false) :
			smsUint32PV(name, min, max, auto_save),
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
			uint32_t oldFormatFlags = m_info->getFormatFlags();

			std::string oldFormatStr;
			if ( oldFormatFlags == 0 )
				oldFormatStr = "none";
			else if ( oldFormatFlags == ADARA::EVENT_FORMAT )
				oldFormatStr = "event";
			else if ( oldFormatFlags == ADARA::HISTO_FORMAT )
				oldFormatStr = "histo";
			else if ( oldFormatFlags ==
					( ADARA::EVENT_FORMAT | ADARA::HISTO_FORMAT ) ) {
				oldFormatStr = "both";
			}

			uint32_t newFormatFlags = value();

			std::string newFormatStr;
			if ( newFormatFlags == 0 )
				newFormatStr = "none";
			else if ( newFormatFlags == ADARA::EVENT_FORMAT )
				newFormatStr = "event";
			else if ( newFormatFlags == ADARA::HISTO_FORMAT )
				newFormatStr = "histo";
			else if ( newFormatFlags ==
					( ADARA::EVENT_FORMAT | ADARA::HISTO_FORMAT ) ) {
				newFormatStr = "both";
			}

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << newFormatFlags;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( newFormatStr.compare( oldFormatStr ) )
			{
				INFO("DetBankSetFormatPV: Changing Detector Bank Set "
					<< m_info->getName() << " Output Format for "
					<< m_pv_name << " from " << oldFormatStr
					<< " to " << newFormatStr);

				m_info->setFormatFlags(newFormatFlags);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetOffsetPV : public smsUint32PV {
	public:
		DetBankSetOffsetPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
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
				INFO("DetBankSetOffsetPV: Changing Detector Bank Set "
					<< m_info->getName() << " TOF Offset for "
					<< m_pv_name << " from " << m_info->getTofOffset()
					<< " to " << tofOffset);

				m_info->setTofOffset(tofOffset);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetMaxPV : public smsUint32PV {
	public:
		DetBankSetMaxPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
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
				INFO("DetBankSetMaxPV: Changing Detector Bank Set "
					<< m_info->getName() << " Maximum TOF for "
					<< m_pv_name << " from " << m_info->getTofMax()
					<< " to " << tofMax);

				m_info->setTofMax(tofMax);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetBinPV : public smsUint32PV {
	public:
		DetBankSetBinPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
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
				ERROR("DetBankSetBinPV: TOF Histogram Bin Size < 1!"
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
				INFO("DetBankSetBinPV: Changing Detector Bank Set "
					<< m_info->getName() << " TOF Histogram Bin Size for "
					<< m_pv_name << " from " << m_info->getTofBin()
					<< " to " << tofBin);

				m_info->setTofBin(tofBin);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetThrottlePV : public smsFloat64PV {
	public:
		DetBankSetThrottlePV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
				double min = 0.0, double max = FLOAT64_MAX,
				double epsilon = FLOAT64_EPSILON,
				bool auto_save = false) :
			smsFloat64PV(name, min, max, epsilon, auto_save),
			m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			double throttle = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				std::stringstream ss;
				ss << std::setprecision(17) << throttle;
				StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( !approximatelyEqual( throttle, m_info->getThrottle(),
					m_epsilon ) )
			{
				INFO("DetBankSetThrottlePV: Changing Detector Bank Set "
					<< m_info->getName() << " Throttle Frequency for "
					<< m_pv_name << std::setprecision(17)
					<< " from " << m_info->getThrottle()
					<< " to " << throttle);

				m_info->setThrottle(throttle);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	class DetBankSetSuffixPV : public smsStringPV {
	public:
		DetBankSetSuffixPV(const std::string &name,
				DetectorBankSet *config, DetectorBankSetInfo *info,
				bool auto_save = false) :
			smsStringPV(name, auto_save), m_config(config), m_info(info),
			m_auto_save(auto_save) {}

		void changed(void)
		{
			std::string suffix = value();

			if ( m_auto_save && !m_first_set )
			{
				// AutoSave PV Value Change...
				struct timespec ts;
				m_value->getTimeStamp(&ts);
				StorageManager::autoSavePV( m_pv_name, suffix, &ts );
			}

			bool changed = false;

			// Sanitize Suffix String for NeXus File NXentry Usage...
			if ( Utils::sanitizeString( suffix,
					false /* a_preserve_uri */ ) )
			{
				ERROR("DetBankSetSuffixPV changed(): Sanitized"
					<< " Throttle NXentry Suffix for Detector Bank Set "
					<< value() << " to " << suffix);
				changed = true;
			}

			// Truncate Suffix String to Max Size for Network Packet...
			if ( m_config->truncateString(
				suffix, ADARA::DetectorBankSetsPkt::THROTTLE_SUFFIX_SIZE,
				"DetBankSetSuffixPV changed()",
				"Throttle NXentry Suffix", true ) )
			{
				changed = true;
			}

			// Did Our Internal State _Really_ Change...? (i.e. Startup...)
			if ( suffix.compare( m_info->getSuffix() ) )
			{
				INFO("DetBankSetSuffixPV: Changing Detector Bank Set "
					<< m_info->getName() << " Throttle NXentry Suffix for "
					<< m_pv_name << " from " << m_info->getSuffix()
					<< " to " << suffix);

				m_info->setSuffix(suffix);

				// Reset Timestamp on Prologue Packet...
				m_config->resetPacketTime();
			}

			// The Suffix String got Sanitized or Truncated, Update PV...!
			if ( changed )
			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				update(m_info->getSuffix(), &ts);
			}
		}

	private:
		DetectorBankSet *m_config;
		DetectorBankSetInfo *m_info;

		bool m_auto_save;
	};

	DetectorBankSetInfo(DetectorBankSet *config,
			uint32_t index, uint32_t sectionOffset, std::string name,
			std::vector<uint32_t> banklist, uint32_t formatFlags,
			uint32_t tofOffset, uint32_t tofMax, uint32_t tofBin,
			double throttle, std::string suffix) :
		m_config(config), m_index(index), m_sectionOffset(sectionOffset),
		m_name(name), m_banklist(banklist), m_formatFlags(formatFlags),
		m_tofOffset(tofOffset), m_tofMax(tofMax), m_tofBin(tofBin),
		m_throttle(throttle), m_suffix(suffix)
	{
		// Create PVs for Live Detector Bank Set Config Controls...

		SMSControl *ctrl = SMSControl::getInstance();

		std::string prefix(ctrl->getPVPrefix());
		prefix += ":DetectorBankSet:";

		std::stringstream ss;
		ss << ( m_index + 1 );  // Index SMS Control PV from 1... ;-D
		prefix += ss.str();

		m_pvName = boost::shared_ptr<DetBankSetNamePV>( new
			DetBankSetNamePV(prefix + ":Name", m_config, this,
				/* AutoSave */ true) );

		m_pvBanks = boost::shared_ptr<DetBankSetBanklistPV>( new
			DetBankSetBanklistPV(prefix + ":Banklist", m_config, this,
				/* AutoSave */ true) );

		m_pvFormat = boost::shared_ptr<DetBankSetFormatPV>( new
			DetBankSetFormatPV(prefix + ":Format", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvOffset = boost::shared_ptr<DetBankSetOffsetPV>( new
			DetBankSetOffsetPV(prefix + ":TofOffset", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvMax = boost::shared_ptr<DetBankSetMaxPV>( new
			DetBankSetMaxPV(prefix + ":MaxTof", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvBin = boost::shared_ptr<DetBankSetBinPV>( new
			DetBankSetBinPV(prefix + ":TofBin", m_config, this,
				0, INT32_MAX, /* AutoSave */ true) );

		m_pvThrottle = boost::shared_ptr<DetBankSetThrottlePV>( new
			DetBankSetThrottlePV(prefix + ":Throttle", m_config, this,
				0.0, FLOAT64_MAX, FLOAT64_EPSILON, /* AutoSave */ true) );

		m_pvSuffix = boost::shared_ptr<DetBankSetSuffixPV>( new
			DetBankSetSuffixPV(prefix + ":Suffix", m_config, this,
				/* AutoSave */ true) );

		ctrl->addPV(m_pvName);
		ctrl->addPV(m_pvBanks);
		ctrl->addPV(m_pvFormat);
		ctrl->addPV(m_pvOffset);
		ctrl->addPV(m_pvMax);
		ctrl->addPV(m_pvBin);
		ctrl->addPV(m_pvThrottle);
		ctrl->addPV(m_pvSuffix);

		// Initialize Detector Bank Set Config PVs...

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		m_pvName->update(m_name, &now);

		std::string banklistStr;
		Utils::printArrayString( m_banklist, banklistStr );
		m_pvBanks->update(banklistStr, &now);

		m_pvFormat->update(m_formatFlags, &now);

		m_pvOffset->update(m_tofOffset, &now);
		m_pvMax->update(m_tofMax, &now);
		m_pvBin->update(m_tofBin, &now);

		m_pvThrottle->update(m_throttle, &now);

		m_pvSuffix->update(m_suffix, &now);

		/* Restore Any PVs to AutoSaved Config Values... */

		struct timespec ts;
		std::string value;
		uint32_t uvalue;
		double dvalue;

		if ( StorageManager::getAutoSavePV(
				m_pvName->getName(), value, ts ) ) {
			// Don't Manually Set "m_name" String Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvName->update(value, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvBanks->getName(), value, ts ) ) {
			// Don't Manually Set "m_banklist" String (Array) Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvBanks->update(value, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvFormat->getName(), uvalue, ts ) ) {
			// Don't Manually Set "m_formatFlags" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvFormat->update(uvalue, &ts);
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
				m_pvThrottle->getName(), dvalue, ts ) ) {
			// Don't Manually Set "m_throttle" Value Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvThrottle->update(dvalue, &ts);
		}

		if ( StorageManager::getAutoSavePV(
				m_pvSuffix->getName(), value, ts ) ) {
			// Don't Manually Set "m_suffix" String Here...
			// Let "changed()" Do *All* It's Stuff... ;-D
			m_pvSuffix->update(value, &ts);
		}

		// Initialize Changed Flag...
		m_changed = true;
	}

	// Gets...

	std::string getName(void) const { return m_name; }

	std::vector<uint32_t> getBanklist(void) const { return m_banklist; }

	uint32_t getFormatFlags(void) const { return m_formatFlags; }

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

	void setFormatFlags(uint32_t formatFlags)
		{ m_formatFlags = formatFlags; m_changed = true; }

	void setTofOffset(uint32_t tofOffset)
		{ m_tofOffset = tofOffset; m_changed = true; }
	void setTofMax(uint32_t tofMax)
		{ m_tofMax = tofMax; m_changed = true; }
	void setTofBin(uint32_t tofBin)
		{ m_tofBin = tofBin; m_changed = true; }

	void setThrottle(uint32_t throttle)
		{ m_throttle = throttle; m_changed = true; }

	void setSuffix(std::string suffix)
		{ m_suffix = suffix; m_changed = true; }

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

		fields[m_sectionOffset + index++] = m_formatFlags;

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

	uint32_t m_formatFlags;

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

DetectorBankSet::DetectorBankSet(const boost::property_tree::ptree & conf)
	: m_packet(NULL)
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

	std::string prefix(ctrl->getPVPrefix());

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

	uint32_t formatFlags;

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
		formatFlags = 0;
		if ( !format.compare("histo") )
			formatFlags |= ADARA::HISTO_FORMAT;
		else if ( !format.compare("both") ) {
			formatFlags |= ADARA::EVENT_FORMAT | ADARA::HISTO_FORMAT;
		}
		else // if ( !format.compare("event") )
			formatFlags |= ADARA::EVENT_FORMAT;

		std::string banklistStr =
			it->second.get<std::string>("banklist", "none");

		std::vector<uint32_t> banklist;
		Utils::parseArrayString( banklistStr, banklist );

		std::string newBanklist;
		Utils::printArrayString( banklist, newBanklist );

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
		if ( Utils::sanitizeString( suffix,
				false /* a_preserve_uri */ ) ) {
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
			<< " formatFlags=" << formatFlags
			<< " tofOffset=" << tofOffset
			<< " tofMax=" << tofMax
			<< " tofBin=" << tofBin
			<< " throttle=" << throttle
			<< " suffix=" << suffix);

		DetectorBankSetInfo *detBankSetInfo = new DetectorBankSetInfo(this,
			index++, sectionOffset, detBankSetName, banklist, formatFlags,
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
	fields[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::DETECTOR_BANK_SETS_TYPE,
		ADARA::PacketType::DETECTOR_BANK_SETS_VERSION );
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
				boost::bind(&DetectorBankSet::onPrologue, this, _1));
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

void DetectorBankSet::resetPacketTime(void)
{
	// Whoa... Make Sure Some Premature PV Change Doesn't Try to Call This
	// _Before_ the Packet has been Allocated...! ;-o
	if ( m_packet == NULL ) {
		DEBUG("resetPacketTime():"
			<< " Premature, NULL Packet - Ignoring Packet Time Reset...");
		return;
	}

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint32_t *fields = (uint32_t *) m_packet;

	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
}

void DetectorBankSet::onPrologue( bool UNUSED(capture_last) )
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

