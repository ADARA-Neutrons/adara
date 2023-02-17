
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.Control"));

#include <string>
#include <sstream>
#include <map>

#include <time.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>

#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "StorageManager.h"
#include "DataSource.h"
#include "RunInfo.h"
#include "Geometry.h"
#include "PixelMap.h"
#include "BeamlineInfo.h"
#include "BeamMonitorConfig.h"
#include "DetectorBankSet.h"
#include "MetaDataMgr.h"
#include "FastMeta.h"
#include "Markers.h"
#include "utils.h"

#include "snsTiming.h"

#include <cadef.h>

RateLimitedLogging::History RLLHistory_SMSControl;

// Rate-Limited Logging IDs...
#define RLL_GLOBAL_SAWTOOTH_PULSE        0
#define RLL_INTERLEAVED_GLOBAL_SAWTOOTH  1
#define RLL_GLOBAL_SAWTOOTH_LAST         2
#define RLL_PULSE_BUFFER_OVERFLOW        3
#define RLL_SET_SOURCES_READ_DELAY       4
#define RLL_MISSING_RTDL_PROTON_CHARGE   5
#define RLL_UNKNOWN_FAST_META_PIXEL_ID   6
#define RLL_RTDL_OUT_OF_ORDER_WITH_DATA  7
#define RLL_PULSE_PCHG_UNCORRECTED       8
#define RLL_PULSE_PCHG_BUFFER_EMPTY      9
#define RLL_NO_RTDL_FOR_PULSE           10
#define RLL_CHOPPER_SYNC_ISSUE          11
#define RLL_CHOPPER_GLITCH_ISSUE        12
#define RLL_BOGUS_PULSE_ENERGY_ZERO     13
#define RLL_BOGUS_PULSE_ENERGY_BETA     14

ReadyAdapter *SMSControl::m_fdregChannelAccess = NULL;

uint32_t SMSControl::m_targetStationNumber;

std::string SMSControl::m_version;
std::string SMSControl::m_facility;
std::string SMSControl::m_beamlineId;
std::string SMSControl::m_beamlineShortName;
std::string SMSControl::m_beamlineLongName;
std::string SMSControl::m_geometryPath;
std::string SMSControl::m_pixelMapPath;

uint32_t SMSControl::m_instanceId;

std::string SMSControl::m_pvPrefix;

std::string SMSControl::m_primaryPVPrefix;

std::string SMSControl::m_altPrimaryPVPrefix;

uint32_t SMSControl::m_noEoPPulseBufferSize;
uint32_t SMSControl::m_maxPulseBufferSize;

bool SMSControl::m_noRTDLPulses;

uint64_t SMSControl::m_interPulseTimeChopGlitchMin;
uint64_t SMSControl::m_interPulseTimeChopGlitchMax;

uint64_t SMSControl::m_interPulseTimeChopperMin;
uint64_t SMSControl::m_interPulseTimeChopperMax;

uint64_t SMSControl::m_interPulseTimeMin;
uint64_t SMSControl::m_interPulseTimeMax;
bool SMSControl::m_doPulsePchgCorrect;
bool SMSControl::m_doPulseVetoCorrect;

bool SMSControl::m_sendSampleInRunInfo;
bool SMSControl::m_savePixelMap;

bool SMSControl::m_useAncientRunStatusPkt;

bool SMSControl::m_allowNonOneToOnePixelMapping;

bool SMSControl::m_useOrigPixelMappingPkt;

bool SMSControl::m_notesCommentAutoReset; // Note: Live PV in Markers...!
bool SMSControl::m_runNotesUpdatesEnabled;

uint32_t SMSControl::m_intermittentDataThreshold;

uint32_t SMSControl::m_neutronEventStateBits;
uint32_t SMSControl::m_neutronEventStateMask;
bool SMSControl::m_neutronEventSortByState;

bool SMSControl::m_ignoreInterleavedSawtooth;

uint32_t SMSControl::m_monitorTOFBits;
uint32_t SMSControl::m_monitorTOFMask;

uint32_t SMSControl::m_chopperTOFBits;
uint32_t SMSControl::m_chopperTOFMask;

uint32_t SMSControl::m_verbose;

class PopPulseBufferPV : public smsInt32PV {
public:
	PopPulseBufferPV(const std::string &name) :
		smsInt32PV(name) {}

	void changed(void)
	{
		int32_t pop_state = value();

		INFO("PopPulseBufferPV " << m_pv_name << " set to " << pop_state);

		// Do Nothing, Default State...
		if ( pop_state == 0 )
			return;

		// Pop Desired Pulse from Buffer by Index...
		SMSControl *ctrl = SMSControl::getInstance();
		ctrl->popPulseBuffer(pop_state);

		// Reset Pop Pulse Buffer PV State to Default State (0)...
		DEBUG("Resetting PopPulseBufferPV " << m_pv_name << " to 0.");
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		update(0, &now);
	}
};

class LogLevelPV : public smsStringPV {
public:
	LogLevelPV(const std::string &name) :
		smsStringPV(name) {}

	void changed(void)
	{
		std::string logLevelStr = value();
		if ( !logLevelStr.compare("OFF") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getOff());
		else if ( !logLevelStr.compare("FATAL") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getFatal());
		else if ( !logLevelStr.compare("ERROR") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getError());
		else if ( !logLevelStr.compare("WARN") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getWarn());
		else if ( !logLevelStr.compare("INFO") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getInfo());
		else if ( !logLevelStr.compare("DEBUG") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getDebug());
		else if ( !logLevelStr.compare("TRACE") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getTrace());
		else if ( !logLevelStr.compare("ALL") )
			log4cxx::Logger::getRootLogger()
				->setLevel(log4cxx::Level::getAll());
		else {
			ERROR("LogLevelPV::changed(): Unknown LogLevel String"
				<< " [" << logLevelStr << "]"
				<< " - Ignoring, Reverting to Former LogLevel...");
			// Restore Log4CXX LogLevel String PV to Current LogLevel...
			LevelPtr logLevel =
				log4cxx::Logger::getRootLogger()->getLevel();
			ERROR("LogLevelPV::changed(): Setting LogLevel String PV to"
				<< " [" << logLevel->toString() << "]");
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			update(logLevel->toString(), &now);
		}
	}
};

// Read-Only String PV for SMS Version...
class VersionPV : public smsStringPV {
public:
	VersionPV(const std::string &name) :
		smsStringPV(name) {}

	// No External Writes Allowed...!
	bool allowUpdate(const gdd &)
	{
		return false;
	}
};

// Read-Only Integer PV for "ParADARA" SMS Instance ID...
class InstanceIdPV : public smsUint32PV {
public:
	InstanceIdPV(const std::string &name,
			uint32_t min = 0, uint32_t max = INT32_MAX,
			bool auto_save = false) :
		smsUint32PV(name, min, max, auto_save) {}

	// No External Writes Allowed...!
	bool allowUpdate(const gdd &)
	{
		return false;
	}
};

// "PV Prefix" PV Class for "ParADARA", Trigger Re-Subscribe on Change
class PVPrefixPV : public smsStringPV {
public:
	PVPrefixPV(const std::string &name, bool auto_save = false) :
		smsStringPV(name, auto_save), m_auto_save(auto_save) {}

	void changed(void)
	{
		std::string new_pv_prefix = value();

		INFO("PVPrefixPV: PV " << m_pv_name
			<< " set to " << new_pv_prefix);

		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			StorageManager::autoSavePV( m_pv_name, new_pv_prefix, &ts );
		}

		SMSControl *ctrl = SMSControl::getInstance();

		// *** ParADARA ***
		// For *Secondary* SMS Instances,
		// Create Channel Access Subscriptions
		// To SMS Primary "Recording", "RunNumber" and "Paused" PVs... ;-D

		if ( ctrl->getInstanceId() != 0 )
		{
			DEBUG("PVPrefixPV:"
				<< " Secondary SMS Instance, Id " << ctrl->getInstanceId()
				<< " - Construct Primary SMS PV Prefix String...");

			std::string PrimaryPVPrefix = ctrl->getBeamlineId() + ":SMS";

			if ( new_pv_prefix.size()
					&& new_pv_prefix.compare( "(unset)" ) )
			{
				DEBUG("PVPrefixPV:"
					<< " Using Alternate Primary SMS PV Prefix String"
					<< " [" << new_pv_prefix << "]");
				PrimaryPVPrefix = new_pv_prefix;
			}

			// SMSControl() ctor hasn't fully executed yet.. Wait...! ;-D
			if ( !std::string("NotYetInitialized").compare(
					ctrl->getPrimaryPVPrefix() ) )
			{
				DEBUG("PVPrefixPV:"
					<< " SMSControl Instance Not Yet Fully Initialized..."
					<< " Ignore Primary SMS PV Prefix"
					<< " [" << PrimaryPVPrefix << "]"
					<< " For Now...");
			}

			// Did the Resulting Primary SMS PV Prefix Actually Change...?
			else if ( PrimaryPVPrefix.compare(
					ctrl->getPrimaryPVPrefix() ) )
			{
				DEBUG("PVPrefixPV: Re-Subscribe to Primary SMS PVs"
					<< " Using New Primary SMS PV Prefix"
					<< " [" << PrimaryPVPrefix << "]");

				ctrl->setPrimaryPVPrefix( PrimaryPVPrefix );

				// Unsubscribe from Any Current External Run Control PVs...
				ctrl->unsubscribePrimaryPVs();

				// Re-Subscribe to External Run Control PVs
				// with New Prefix...
				ctrl->subscribeToPrimaryPVs( PrimaryPVPrefix );
			}

			// No Change to Primary SMS PV Prefix...
			else
			{
				DEBUG("PVPrefixPV: No Change to Primary SMS PV Prefix"
					<< " [" << PrimaryPVPrefix << "]"
					<< " Ignore...");
			}
		}

		// Not a Secondary SMS Instance...
		else
		{
			DEBUG("PVPrefixPV:"
				<< " Primary SMS Instance, No Need for Subscribing"
				<< " to External Run Control PVs. Ignore...");
		}
	}

private:
	bool m_auto_save;
};

class CleanShutdownPV : public smsTriggerPV {
public:
	CleanShutdownPV(const std::string &name) :
		smsTriggerPV(name) {}

	void triggered(struct timespec *ts)
	{
		DEBUG("CleanShutdownPV " << m_pv_name << " Triggered."
			<< " Cleanly Shutting Down SMS Daemon."
			<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		exit(0);
	}
};

SMSControl *SMSControl::m_singleton = NULL;

void SMSControl::config(const boost::property_tree::ptree &conf)
{
	m_version = conf.get<std::string>("sms.version");

	std::string base = conf.get<std::string>("sms.basedir");
	base += "/conf";

	m_geometryPath = conf.get<std::string>("sms.geometry_file", "");
	if (!m_geometryPath.length())
		m_geometryPath = base + "/geometry.xml";

	m_pixelMapPath = conf.get<std::string>("sms.pixelmap_file", "");
	if (!m_pixelMapPath.length())
		m_pixelMapPath = base + "/pixelmap";

	m_facility = conf.get<std::string>("sms.facility", "SNS");
	INFO("Experimental Facility " << m_facility << ".");

	m_targetStationNumber = conf.get<uint32_t>("sms.target_station", 1);
	INFO("Operating on Neutron Facility Target Station "
		<< m_targetStationNumber << ".");

	m_beamlineId = conf.get<std::string>("sms.beamline_id", "");
	m_beamlineShortName =
			conf.get<std::string>("sms.beamline_shortname", "");
	m_beamlineLongName =
			conf.get<std::string>("sms.beamline_longname", "");

	// "ParADARA" SMS Instance Id...
	m_instanceId = conf.get<uint32_t>("sms.instance_id", 0);
	INFO("SMS Instance ID = " << m_instanceId
		<< ( ( m_instanceId == 0 ) ? " (PRIMARY)" : " (SECONDARY)" ) );

	// "ParADARA" Alternate Primary SMS PV Prefix...
	m_altPrimaryPVPrefix =
		conf.get<std::string>("sms.alt_primary_pv_prefix", "(unset)");
	INFO("SMS Alternate Primary PV Prefix = "
		<< "[" << m_altPrimaryPVPrefix << "]");

	/* Addendum 7/2014: for some legacy dcomserver implementations,
	 * the neutron events and meta-data events can interleave and/or
	 * arrive "out of order", so we can optionally enforce a
	 * fixed-number-of-pulse buffering, to ensure complete pulses.
	 * Defaults to 0 (which means "no such buffering :-).
	 */
	m_noEoPPulseBufferSize =
			conf.get<uint32_t>("sms.no_eop_pulse_buffer_size", 0);
	if (m_noEoPPulseBufferSize) {
		INFO("Setting No-EoP-Pulse-Buffer-Size to "
			<< m_noEoPPulseBufferSize << ".");
	}

	m_maxPulseBufferSize =
			conf.get<uint32_t>("sms.max_pulse_buffer_size", 1000);
	INFO("Setting Max Pulse Buffer Size to "
		<< m_maxPulseBufferSize << ".");

	m_noRTDLPulses = conf.get<bool>("sms.no_rtdl_pulses", false);
	INFO("Setting No RTDL Pulses to " << m_noRTDLPulses << ".");

	m_doPulsePchgCorrect =
			conf.get<bool>("sms.do_pulse_pcharge_correction", true);
	INFO("Setting Do Pulse Proton Charge Correction to "
		<< m_doPulsePchgCorrect << ".");

	m_doPulseVetoCorrect =
			conf.get<bool>("sms.do_pulse_veto_correction", true);
	INFO("Setting Do Pulse Veto Flags Correction to "
		<< m_doPulseVetoCorrect << ".");

	m_sendSampleInRunInfo =
			conf.get<bool>("sms.send_sample_in_run_info", false);
	INFO("Setting Send Sample in Run Info to "
		<< m_sendSampleInRunInfo << ".");

	m_savePixelMap = conf.get<bool>("sms.save_pixel_map", false);
	INFO("Setting Save Pixel Map in NeXus to " << m_savePixelMap << ".");

	m_useAncientRunStatusPkt =
		conf.get<bool>("sms.use_ancient_run_status_pkt", false);
	INFO("Setting Use Ancient Run Status Packet Format to "
		<< m_useAncientRunStatusPkt << ".");

	m_allowNonOneToOnePixelMapping =
			conf.get<bool>("sms.allow_non_one_to_one_pixel_mappings",
				false);
	INFO("Setting Allow Non-One-to-One Pixel Mapping to "
		<< m_allowNonOneToOnePixelMapping << ".");

	m_useOrigPixelMappingPkt =
			conf.get<bool>("sms.use_orig_pixel_mapping_pkt", false);
	INFO("Setting Use Original Pixel Mapping Packet to "
		<< m_useOrigPixelMappingPkt << ".");

	m_notesCommentAutoReset =
			conf.get<bool>("sms.run_notes_auto_reset", true);
	INFO("Setting Run Notes Auto Reset to "
		<< m_notesCommentAutoReset << ".");

	m_runNotesUpdatesEnabled =
			conf.get<bool>("sms.run_notes_updates_enabled", true);
	INFO("Setting Run Notes Updates Enabled to "
		<< m_runNotesUpdatesEnabled << ".");

	m_intermittentDataThreshold =
			conf.get<uint32_t>("sms.intermittent_data_threshold", 9);
	INFO("Setting Intermittent Data Threshold to "
		<< m_intermittentDataThreshold << ".");

	m_neutronEventStateBits =
			conf.get<uint32_t>("sms.neutron_event_state_bits", 0);
	INFO("Setting Neutron Event State Bits to "
		<< m_neutronEventStateBits << "."
		<< ( (m_neutronEventStateBits > 0) ? " STATES ACTIVATED!" : "" ) );

	// Set Neutron Event State Mask Based on Number of Bits...
	if ( m_neutronEventStateBits > 0 ) {
		m_neutronEventStateMask =
			( ((uint32_t) -1) << (28 - m_neutronEventStateBits) )
				& 0x0FFFFFFF;
		INFO("Setting Neutron Event State Mask to 0x"
			<< std::hex << m_neutronEventStateMask << std::dec << ".");
	}

	m_neutronEventSortByState =
			conf.get<bool>("sms.neutron_event_sort_by_state", false);
	INFO("Setting Neutron Event Sort By State to "
		<< m_neutronEventSortByState << ".");

	m_ignoreInterleavedSawtooth =
		conf.get<bool>("sms.ignore_interleaved_sawtooth", true);
	INFO("Setting Ignore-Interleaved-Global-SAWTOOTH Flag to "
		<< ( ( m_ignoreInterleavedSawtooth ) ? "True" : "False" ));

	m_monitorTOFBits = conf.get<uint32_t>("sms.beam_monitor_tof_bits", 21);
	INFO("Setting Beam Monitor TOF Bits to " << m_monitorTOFBits << ".");

	// Set Beam Monitor TOF Mask Based on Number of Bits...
	m_monitorTOFMask = ((uint32_t) -1) >> (32 - m_monitorTOFBits);
	INFO("Setting Beam Monitor TOF Mask to 0x"
		<< std::hex << m_monitorTOFMask << std::dec << ".");

	m_chopperTOFBits = conf.get<uint32_t>("sms.chopper_tof_bits", 21);
	INFO("Setting Chopper TOF Bits to " << m_chopperTOFBits << ".");

	// Set Chopper TOF Mask Based on Number of Bits...
	m_chopperTOFMask = ((uint32_t) -1) >> (32 - m_chopperTOFBits);
	INFO("Setting Chopper TOF Mask to 0x"
		<< std::hex << m_chopperTOFMask << std::dec << ".");

	m_verbose = conf.get<uint32_t>("sms.verbose", 0);
	INFO("Setting SMS Verbose Value to " << m_verbose << ".");

	if (!m_beamlineId.length())
		throw std::runtime_error("Missing beamline ID");
	if (!m_beamlineShortName.length())
		throw std::runtime_error("Missing beamline short name");
	if (!m_beamlineLongName.length())
		throw std::runtime_error("Missing beamline long name");
}

void SMSControl::init(void)
{
	m_singleton = new SMSControl();

	m_singleton->EPICSInit();
}

void SMSControl::late_config(const boost::property_tree::ptree &conf)
{
	SMSControl *ctrl = getInstance();
	if (!ctrl)
		throw std::logic_error("late_config on uninitialized obj");

	ctrl->m_bmonConfig.reset(new BeamMonitorConfig(conf));

	ctrl->m_detBankSets.reset(new DetectorBankSet(conf));

	ctrl->addSources(conf);
	ctrl->m_fastmeta->addDevices(conf);
}

void SMSControl::addSources(const boost::property_tree::ptree &conf)
{
	std::string name, prefix("source ");
	boost::property_tree::ptree::const_iterator it;
	size_t b, e, plen = prefix.length();

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, prefix))
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
			name = "NoName";
		}
		// Extract Name String from (Any) Quotes...
		else {
			name = it->first.substr(b, e - b + 1);
		}

		bool enabled = !(it->second.count("disabled"));

		if (!enabled) {
			INFO("Ignoring disabled source '" << name << "' (for now).");
		}

		addSource(name, it->second, enabled);
	}
}

void SMSControl::addSource(const std::string &name,
			const boost::property_tree::ptree &info, bool enabled)
{
	boost::property_tree::ptree::const_assoc_iterator uri;
	bool required;
	double connect_retry, connect_timeout, data_timeout;
	uint32_t data_timeout_retry;
	unsigned int chunk_size;
	bool ignore_eop;
	bool ignore_local_sawtooth;
	Markers::PassThru ignore_annotation_pkts;
	std::string ignore_annotation_pkts_str;
	bool mixed_data_packets;
	bool check_source_sequence;
	bool check_pulse_sequence;
	uint32_t max_pulse_seq_list;
	uint32_t rtdlNoDataThresh;
	bool save_input_stream;

	uri = info.find("uri");
	if (uri == info.not_found()) {
		std::string msg("Source '");
		msg += name;
		msg += "' is missing URI";
		throw std::runtime_error(msg);
	}

	std::string val = info.get<std::string>("readsize", "4M");
	try {
		chunk_size = parse_size(val);
	} catch (std::runtime_error e) {
		std::string msg("Unable to parse read size for source '");
		msg += name;
		msg += "': ";
		msg += e.what();
		throw std::runtime_error(msg);
	}

	required = info.get<bool>("required", false);
	connect_retry = info.get<double>("connect_retry", 15.0);
	connect_timeout = info.get<double>("connect_timeout", 5.0);
	data_timeout = info.get<double>("data_timeout", 3.0);
	data_timeout_retry = info.get<uint32_t>("data_timeout_retry", 3);
	ignore_eop = info.get<bool>("ignore_eop", false);
	ignore_local_sawtooth = info.get<bool>("ignore_local_sawtooth", false);
	ignore_annotation_pkts_str =
		info.get<std::string>("ignore_annotation_pkts", "ignore");
	mixed_data_packets = info.get<bool>("mixed_data_packets", false);
	check_source_sequence = info.get<bool>("check_source_sequence", true);
	check_pulse_sequence = info.get<bool>("check_pulse_sequence", true);
	max_pulse_seq_list = info.get<uint32_t>("max_pulse_seq_list", 32);
	rtdlNoDataThresh = info.get<uint32_t>("rtdl_no_data_thresh", 100);
	save_input_stream = info.get<bool>("save_input_stream", false);

	// Should probably let someone know if we think _This_ Data Source
	// is *Required* for Data Collection, since the SMS will Block Running!
	if (required) {
		ERROR("Data Source " << uri->second.data() << " (" << name << ")"
			<< " Marked as *Required* for Data Collection!");
	}

	// Should probably let someone know if we're flying by the
	// seat of our pants and "Self Synchronizing", Ignoring End-of-Pulse...
	if (ignore_eop) {
		DEBUG("Ignore-EOP Flag Set to True for " << name
			<< " - Self Synchronizing Pulses!");
	}

	// Should probably let someone know if we're gonna
	// Ignore Local SAWTOOTH Time Inconsistencies for this Data Source...
	if (ignore_local_sawtooth) {
		DEBUG("Ignore-Local-SAWTOOTH Flag Set to True for " << name
			<< " - Ignoring Out-of-Time-Order Pulse Times!");
	}

	// Should probably let someone know whether or not we're gonna
	// Ignore Annotation Packets for this Data Source...
	for ( uint32_t i=0 ; i < ignore_annotation_pkts_str.size() ; i++ ) {
		ignore_annotation_pkts_str[i] =
			tolower( ignore_annotation_pkts_str[i] );
	}
	ignore_annotation_pkts =
		( ignore_annotation_pkts_str.compare( "ignore" ) ?
			( ignore_annotation_pkts_str.compare( "passthru" ) ?
				Markers::EXECUTE : Markers::PASSTHRU ) : Markers::IGNORE );
	DEBUG("Ignore-Annotation-Packets Flag Set to "
		<< "\"" << ignore_annotation_pkts_str << "\""
		<<  " (" << ignore_annotation_pkts << ")"
		<< " for " << name << " -"
		<< ( ( ignore_annotation_pkts == Markers::IGNORE ) ?
			"Ignoring" :
			( ( ignore_annotation_pkts == Markers::PASSTHRU ) ?
				"Passing Through" : "Executing" ) )
		<< " External Marker Replay Driving...");

	// Should probably let someone know if we're Mixing Data Packets
	// with Both Neutron Events and Meta-Data Events... ;-D
	if (mixed_data_packets) {
		DEBUG("Mixed Data Packets Flag Set to True for " << name
			<< " - No Auto-Deduce Sub-60Hz Pulse for Proton Charge Zero!");
	}

	// Should probably let someone know if we're _Not_ gonna
	// Check Data Packet Source Sequence Numbers...! ;-D
	if (!check_source_sequence) {
		DEBUG("Data Packet Source Sequence Checking Set to False for "
			<< name << " - Per Source Packet, No Reset...");
	}

	// Should probably let someone know if we're _Not_ gonna
	// Check Data Packet Source Sequence Numbers...! ;-D
	if (!check_pulse_sequence) {
		DEBUG("Data Packet Pulse Sequence Checking Set to False for "
			<< name << " - Per Event Packet, Resets Per Pulse...");
	}

	// Should probably let someone know what our
	// Max Pulse Sequence List Size will be...! ;-D
	else {
		DEBUG("Max Pulse Sequence List (Buffer) Size Set to "
			<< max_pulse_seq_list << " for " << name);
	}

	// Probably should also log if we're Saving All the Input Stream
	// from this particular Data Source... (not something you'd really
	// wanna do all the time in production... :-)
	if (save_input_stream) {
		DEBUG("Save Input Stream Set to True for Data Source "
			<< name << "!");
	}

	boost::shared_ptr<DataSource> src(new DataSource(name,
							 enabled,
							 required,
							 uri->second.data(),
							 m_nextSrcId,
							 connect_retry,
							 connect_timeout,
							 data_timeout,
							 data_timeout_retry,
							 ignore_eop,
							 ignore_local_sawtooth,
							 ignore_annotation_pkts,
							 mixed_data_packets,
							 check_source_sequence,
							 check_pulse_sequence,
							 max_pulse_seq_list,
							 chunk_size,
							 rtdlNoDataThresh,
							 save_input_stream));
	m_dataSources.push_back(src);

	// Add Another DataSource Max Time Entry
	struct timespec init_ts;
	init_ts.tv_sec = 0; // EPICS Time...!
	init_ts.tv_nsec = 0;
	m_dataSourcesMaxTimes.push_back(init_ts);

	// Update Number of Data Sources PV...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvNumDataSources->update(m_nextSrcId, &now); // count from 1!

	/* We save source ID 0 for internal use. */
	m_nextSrcId++;
	/* TODO check against the max number of sources? */
}

void ca_exception_handler( struct exception_handler_args args )
{
	const char *pName = ( args.chid ) ? ca_name( args.chid ) : "(Unknown)";

	ERROR("ca_exception_handler():"
		<< " Caught EPICS Exception!"
		<< " Context=[" << args.ctx << "]"
		<< " - with Request"
		<< " ChannelId=[" << pName << "]"
		<< " Stat=[" << args.stat << "]"
		<< " Operation=[" << args.op << "]"
		<< " DataType=[" << dbr_type_to_text( args.type ) << "]"
		<< " Count=[" << args.count << "]"
		<< ", Continuing...!");
}

void SMSControl::ca_ready(void)
{
	DEBUG("ca_ready() Entry...");

	SMSControl *ctrl = SMSControl::getInstance();

	if ( ctrl )
	{
		// Flush EPICS I/O...
		int ca_status;

		ca_status = ca_poll();

		if ( !( ca_status & CA_M_SUCCESS ) )
		{
			// This is "O.K." for ca_poll() and ca_pend_event()... ;-D
			if ( ca_status == ECA_TIMEOUT )
			{
				DEBUG("ca_ready():"
					<< " EPICS Channel Access I/O"
					<< " Successfully Flushed.");
			}
			else
			{
				ERROR("ca_ready():"
					<< " EPICS Channel Access Error in ca_poll(): "
					<< ca_message(ca_status) );
			}
		}
		else
		{
			DEBUG("ca_ready(): EPICS Channel Access I/O Flushed.");
		}
	}
	else
	{
		ERROR("ca_ready(): NULL SMSControl Instance! Retry Later...");
	}
}

void ca_fd_notify( void *arg, int fd, int opened )
{
	DEBUG("ca_fd_notify():"
		<< " arg=" << std::hex << (long) arg << std::dec
		<< " fd=" << fd
		<< " opened=" << opened);

	// Create Yet-Another-ReadyAdapter... ;-D

	if ( SMSControl::m_fdregChannelAccess ) {
		DEBUG("ca_fd_notify(): Deleting Previous ReadAdapter"
			<< " SMSControl::m_fdregChannelAccess=0x" << std::hex
			<< (long) SMSControl::m_fdregChannelAccess << std::dec);
		delete SMSControl::m_fdregChannelAccess;
		SMSControl::m_fdregChannelAccess = NULL;
	}

	DEBUG("ca_fd_notify():"
		<< " Creating ReadyAdapter for EPICS Channel Access fd=" << fd);

	SMSControl *ctrl = SMSControl::getInstance();

	try {
		SMSControl::m_fdregChannelAccess = new ReadyAdapter(fd, fdrRead,
				boost::bind( &SMSControl::ca_ready, ctrl ),
				1 /* verbose */ );
	} catch (std::exception &e) {
		ERROR( "Exception in ca_fd_notify()"
			<< " Creating ReadyAdapter for EPICS Channel Access"
			<< " File Descriptor fd=" << fd << ": " << e.what());
		SMSControl::m_fdregChannelAccess = NULL; // just to be sure... ;-b
	} catch (...) {
		ERROR( "Unknown Exception in ca_fd_notify()"
			<< " Creating ReadyAdapter for EPICS Channel Access"
			<< " File Descriptor fd=" << fd);
		SMSControl::m_fdregChannelAccess = NULL; // just to be sure... ;-b
	}
}

// Notify the IPTS-ITEMS IOC that "We're Alive" and request that it
// Re-Send the IPTS Proposal and ITEMS Sample Information PVs...
void SMSControl::IPTS_ITEMS_Resend(void)
{
	std::string resendPVName = m_beamlineId + ":CS:IPTS_ITEMS:SMS:Resend";
	chid resend_chid;
	int ca_status;

	// Create Channel for Resend PV...
	if ( !( (ca_status =
			ca_create_channel( (const char *) resendPVName.c_str(),
					0, 0, 0, &resend_chid ))
				& CA_M_SUCCESS ) )
	{
		ERROR("IPTS-ITEMS Resend - "
			<< "Channel Access Error in "
			<< "ca_create_channel( " << resendPVName << " ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("IPTS-ITEMS Resend - "
		<< "Channel Access Channel for PV \""
		<< resendPVName << "\" Successfully Created");

	double timeout = 3.0; // Channel Access Pending I/O Timeout (seconds)
	if ( !( (ca_status = ca_pend_io( timeout )) & CA_M_SUCCESS ) )
	{
		ERROR("IPTS-ITEMS Resend - "
			<< "Channel Access Error in Channel Connect "
			<< "ca_pend_io( timeout=" << timeout << " ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("IPTS-ITEMS Resend - "
		<< "Channel Access Pending I/O Successful for Channel Connect");

	uint32_t resend = 1; // "Resend"... (tho value seems to not matter :-)
	if ( !( (ca_status = ca_put( DBR_ENUM, resend_chid, &resend ))
				& CA_M_SUCCESS ) )
	{
		ERROR("IPTS-ITEMS Resend - "
			<< "Channel Access Error in "
			<< "ca_put( DBR_ENUM, resend_chid=" << resend_chid
				<< ", resend=" << resend << " ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("IPTS-ITEMS Resend Channel Access Put Request Successful");

	if ( !( (ca_status = ca_pend_io( timeout )) & CA_M_SUCCESS ) )
	{
		ERROR("IPTS-ITEMS Resend - "
			<< "Channel Access Error in Put Request "
			<< "ca_pend_io( timeout=" << timeout << " ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("IPTS-ITEMS Resend - "
		<< "Channel Access Pending I/O Successful, Resend Request Sent!");
}

void SMSControl::EPICSInit(void)
{
	// Create EPICS Channel Access Context and Exception Handler
	// - Both for the IPTS-ITEMS Resend ca_put(), as well as for
	// Monitoring Any Primary SMS Servers for Recording/Run Numbers...

	int ca_status;

	if ( !( (ca_status =
			ca_context_create( ca_disable_preemptive_callback ))
				& CA_M_SUCCESS ) )
	{
		ERROR("EPICS Channel Access Error in "
			<< "ca_context_create( ca_disable_preemptive_callback ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("EPICS Channel Access Context Successfully Created");
	m_epics_context = ca_current_context();

	// Install EPICS Channel Access File Descriptor Notification Callback
	if ( !( (ca_status = ca_add_fd_registration( ca_fd_notify, 0 ))
				& CA_M_SUCCESS ) )
	{
		ERROR("EPICS Channel Access Error in "
			<< "ca_add_fd_registration( ca_fd_notify, 0 ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("EPICS Channel Access"
		<< " File Descriptor Notify Callback Installed");

	// Install Non-Default (And Non-Terminating!) Channel Access
	// Exception Handler for the SMSD...! ;-D
	if ( !( (ca_status =
			ca_add_exception_event( ca_exception_handler , 0 ))
				& CA_M_SUCCESS ) )
	{
		ERROR("EPICS Channel Access Error in "
			<< "ca_add_exception_event( ca_exception_handler, 0 ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("EPICS Channel Access Exception Handler Installed");

	// Trigger IPTS-ITEMS Resend...
	IPTS_ITEMS_Resend();

	// *** ParADARA ***
	// For *Secondary* SMS Instances, Create Channel Access Subscriptions
	// To SMS Primary "Recording", "RunNumber" and "Paused" PVs... ;-D
	if ( m_instanceId != 0 ) {

		DEBUG("Secondary SMS Instance Id " << m_instanceId
			<< " - Construct Primary SMS PV Prefix String...");

		std::string PrimaryPVPrefix = m_beamlineId + ":SMS";

		if ( m_altPrimaryPVPrefix.size()
				&& m_altPrimaryPVPrefix.compare( "(unset)" ) )
		{
			DEBUG("Using Alternate Primary SMS PV Prefix String = "
				<< "[" << m_altPrimaryPVPrefix << "]");
			PrimaryPVPrefix = m_altPrimaryPVPrefix;
		}

		if ( PrimaryPVPrefix.compare( m_primaryPVPrefix ) )
		{
			DEBUG("Subscribe to Primary SMS PVs"
				<< " Using Primary SMS PV Prefix"
				<< " [" << PrimaryPVPrefix << "]");

			m_primaryPVPrefix = PrimaryPVPrefix;

			subscribeToPrimaryPVs( PrimaryPVPrefix );
		}
	}
}

SMSControl::SMSControl() :
	m_currentRunNumber(0), m_recording(false),
	m_nextSrcId(1), // Note: Must Start From 1, SMS Internal Uses 0...!
	m_numConnectedDataSources(0),
	m_noRegisteredEventSources(true), m_noRegisteredEventSourcesCount(0),
	m_lastPulseId(0), m_lastRingPeriod(0),
	m_monitorReserve(1024), m_bankReserve(4096),
	m_chopperReserve(128), m_fastMetaReserve(16),
	m_meta(new MetaDataMgr), m_fastmeta(new FastMeta(m_meta))
{
	// Initialize the Primary SMS PV Prefix to "Not Ready"... ;-b
	// (For the PVPrefixPV::changed() method,
	// which gets called prematurely... ;-b)
	m_primaryPVPrefix = "NotYetInitialized";

	// Initialize Control PVs...
	m_pvPrefix = m_beamlineId;
	m_pvPrefix += ":SMS";

	// *** ParADARA ***
	// Include Any *Secondary* SMS Instance Id in EPICS PV Prefix...!
	// (Omit Instance Id for *Primary* SMS Instance...)
	if ( m_instanceId != 0 ) {
		std::stringstream ss;
		ss << ":" << m_instanceId;
		m_pvPrefix += ss.str();
	}

	m_pvVersion = boost::shared_ptr<VersionPV>(new
						VersionPV(m_pvPrefix + ":Version"));

	m_pvLogLevel = boost::shared_ptr<LogLevelPV>(new
						LogLevelPV(m_pvPrefix + ":LogLevel"));

	m_pvRecording = boost::shared_ptr<smsRecordingPV>(new
						smsRecordingPV(m_pvPrefix, this));
	m_pvRunNumber = boost::shared_ptr<smsRunNumberPV>(new
						smsRunNumberPV(m_pvPrefix));

	m_markers = boost::shared_ptr<Markers>(new
						Markers(this, m_notesCommentAutoReset));

	m_pvSummary = boost::shared_ptr<smsErrorPV>(new
						smsErrorPV(m_pvPrefix + ":Summary"));

	m_pvSummaryReason = boost::shared_ptr<smsStringPV>(new
						smsStringPV(m_pvPrefix + ":SummaryReason"));

	m_pvInstanceId = boost::shared_ptr<InstanceIdPV>(new
						InstanceIdPV(m_pvPrefix + ":InstanceId"));

	m_pvAltPrimaryPVPrefix = boost::shared_ptr<PVPrefixPV>(new
						PVPrefixPV(m_pvPrefix + ":AltPrimaryPVPrefix",
						/* AutoSave */ true));

	m_pvRunNotesUpdatesEnabled = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix
							+ ":RunNotesUpdatesEnabled",
						/* AutoSave */ true));

	m_pvNoEoPPulseBufferSize = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "NoEoPPulseBufferSize", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvMaxPulseBufferSize = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "MaxPulseBufferSize", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvPopPulseBuffer = boost::shared_ptr<PopPulseBufferPV>(new
						PopPulseBufferPV(m_pvPrefix + ":Control:"
							+ "PopPulseBuffer"));

	m_pvNoRTDLPulses = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix + ":Control:"
							+ "NoRTDLPulses",
						/* AutoSave */ true));

	m_pvDoPulsePchgCorrect = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix + ":Control:"
							+ "DoPulsePchgCorrect",
						/* AutoSave */ true));

	m_pvDoPulseVetoCorrect = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix + ":Control:"
							+ "DoPulseVetoCorrect",
						/* AutoSave */ true));

	m_pvIntermittentDataThreshold = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "IntermittentDataThreshold", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvNeutronEventStateBits = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "NeutronEventStateBits", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvNeutronEventSortByState = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix + ":Control:"
							+ "NeutronEventSortByState",
						/* AutoSave */ true));

	m_pvIgnoreInterleavedSawtooth = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(m_pvPrefix + ":Control:"
							+ "IgnoreInterleavedGlobalSAWTOOTH",
						/* AutoSave */ true));

	m_pvMonitorTOFBits = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "BeamMonitorTOFBits", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvChopperTOFBits = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "ChopperTOFBits", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvVerbose = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "Verbose", 0, INT32_MAX,
						/* AutoSave */ true));

	m_pvNumDataSources = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "NumDataSources"));

	m_pvNumLiveClients = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(m_pvPrefix + ":Control:"
							+ "NumLiveClients"));

	// The Kill Switch. ["NEVER USE THIS!" Lol... (Except for Valgrind) :-]
	m_pvCleanShutdown = boost::shared_ptr<CleanShutdownPV>(new
						CleanShutdownPV(m_pvPrefix + ":CleanShutdown"));

	addPV(m_pvVersion);
	addPV(m_pvLogLevel);
	addPV(m_pvRecording);
	addPV(m_pvRunNumber);
	addPV(m_pvSummary);
	addPV(m_pvSummaryReason);
	addPV(m_pvInstanceId);
	addPV(m_pvAltPrimaryPVPrefix);
	addPV(m_pvRunNotesUpdatesEnabled);
	addPV(m_pvNoEoPPulseBufferSize);
	addPV(m_pvMaxPulseBufferSize);
	addPV(m_pvPopPulseBuffer);
	addPV(m_pvNoRTDLPulses);
	addPV(m_pvDoPulsePchgCorrect);
	addPV(m_pvDoPulseVetoCorrect);
	addPV(m_pvIntermittentDataThreshold);
	addPV(m_pvNeutronEventStateBits);
	addPV(m_pvNeutronEventSortByState);
	addPV(m_pvIgnoreInterleavedSawtooth);
	addPV(m_pvMonitorTOFBits);
	addPV(m_pvChopperTOFBits);
	addPV(m_pvVerbose);
	addPV(m_pvNumDataSources);
	addPV(m_pvNumLiveClients);
	addPV(m_pvCleanShutdown);

	// Initialize Config/Info PVs...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	// Initialize Version PV to Usual SMS Daemon Version String...
	m_pvVersion->update(m_version, &now);

	// Initialize Log4CXX LogLevel String PV...
	LevelPtr logLevel = log4cxx::Logger::getRootLogger()->getLevel();
	DEBUG("SMSControl(): Starting LogLevel is"
		<< " [" << logLevel->toString() << "]");
	m_pvLogLevel->update(logLevel->toString(), &now);

	// Initialize the System Summary Status/Reason...

	m_reasonBase = "System Uninitialized";

	m_summaryRunInfo = false;
	m_reasonRunInfo = "RunInfo Not Yet Set";

	m_summaryDataSources = false;
	m_reasonDataSources = "DataSources Not Yet Created";

	m_summaryOther = true;
	m_reasonOther = "";

	// Update Overall Summary and Reason
	// - "false": Don't Set Base Reason (we just set it :-)
	// - "true": Do Log Status as Error
	// - "true": Major Error
	setSummaryReason( false, true, true );

	// Set the "ParADARA" SMS Instance Id PV...
	m_pvInstanceId->update(m_instanceId, &now);

	// Set the "ParADARA" Alternate Primary SMS PV Prefix String...
	m_pvAltPrimaryPVPrefix->update(m_altPrimaryPVPrefix, &now);

	// Set the Run Notes Updates Enabled During the Run...
	m_pvRunNotesUpdatesEnabled->update(m_runNotesUpdatesEnabled, &now);

	// Initialize "Oldest" DataSource Max Time
	m_oldestMaxDataSourceTime.tv_sec = 0; // EPICS Time...!
	m_oldestMaxDataSourceTime.tv_nsec = 0;

	// Initialize "Newest" DataSource Max Time
	m_newestMaxDataSourceTime.tv_sec = 0; // EPICS Time...!
	m_newestMaxDataSourceTime.tv_nsec = 0;

	// Push "Zero-Index" DataSource Max Time Entry (We Count From 1)
	struct timespec init_ts;
	init_ts.tv_sec = 0; // EPICS Time...!
	init_ts.tv_nsec = 0;
	m_dataSourcesMaxTimes.push_back(init_ts);

	// Initialize No End-of-Pulse Buffer Size PV from Config Value...
	m_pvNoEoPPulseBufferSize->update(m_noEoPPulseBufferSize, &now);

	// Initialize Max Pulse Buffer Size PV from Config Value...
	m_pvMaxPulseBufferSize->update(m_maxPulseBufferSize, &now);

	// Initialize Pop Pulse Buffer PV to Zero...
	m_pvPopPulseBuffer->update(0, &now);

	// Initialize No RTDL Pulses PV from Config Value...
	m_pvNoRTDLPulses->update(m_noRTDLPulses, &now);

	// Initialize Pulse Proton Charge Correction PV & InterPulse Time Range
	uint64_t baseInterPulseTime = NANO_PER_SECOND_LL / CYCLE_FREQUENCY;
	m_interPulseTimeChopGlitchMin = 90 * 2 * baseInterPulseTime / 100;
	m_interPulseTimeChopGlitchMax = 110 * 2 * baseInterPulseTime / 100;
	m_interPulseTimeChopperMin = 90 * baseInterPulseTime / 100;
	m_interPulseTimeChopperMax = 110 * baseInterPulseTime / 100;
	m_interPulseTimeMin = 77 * baseInterPulseTime / 100;
	m_interPulseTimeMax = 123 * baseInterPulseTime / 100;
	m_pvDoPulsePchgCorrect->update(m_doPulsePchgCorrect, &now);
	m_pvDoPulseVetoCorrect->update(m_doPulseVetoCorrect, &now);

	// Initialize Intermittent Data	Threshold PV...
	m_pvIntermittentDataThreshold->update(
		m_intermittentDataThreshold, &now);

	// Initialize Neutron Event State Bits PV...
	m_pvNeutronEventStateBits->update( m_neutronEventStateBits, &now);

	// Initialize Neutron Event Sort By State PV...
	m_pvNeutronEventSortByState->update( m_neutronEventSortByState, &now);

	// Initialize Ignore Interleaved-Global-SAWTOOTH Flag...
	m_pvIgnoreInterleavedSawtooth->update(
		m_ignoreInterleavedSawtooth, &now );

	// Initialize Beam Monitor TOF Bits PV...
	m_pvMonitorTOFBits->update( m_monitorTOFBits, &now);

	// Initialize Chopper TOF Bits PV...
	m_pvChopperTOFBits->update( m_chopperTOFBits, &now);

	// Initialize SMS Verbose Value PV...
	m_pvVerbose->update( m_verbose, &now);

	// Initialize Fast "Last Pulse" Lookup...
	PulseIdentifier m_lastPid(-1,-1);

	// Initialize the Live Client Index List PV...
	m_pvNumLiveClients->update(0, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	uint32_t uvalue;
	bool bvalue;

	if ( StorageManager::getAutoSavePV(
			m_pvAltPrimaryPVPrefix->getName(), value, ts ) ) {
		m_altPrimaryPVPrefix = value;
		m_pvAltPrimaryPVPrefix->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvRunNotesUpdatesEnabled->getName(), bvalue, ts ) ) {
		m_runNotesUpdatesEnabled = bvalue;
		m_pvRunNotesUpdatesEnabled->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvNoEoPPulseBufferSize->getName(), uvalue, ts ) ) {
		m_noEoPPulseBufferSize = uvalue;
		m_pvNoEoPPulseBufferSize->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvMaxPulseBufferSize->getName(), uvalue, ts ) ) {
		m_maxPulseBufferSize = uvalue;
		m_pvMaxPulseBufferSize->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvNoRTDLPulses->getName(), bvalue, ts ) ) {
		m_noRTDLPulses = bvalue;
		m_pvNoRTDLPulses->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvDoPulsePchgCorrect->getName(), bvalue, ts ) ) {
		m_doPulsePchgCorrect = bvalue;
		m_pvDoPulsePchgCorrect->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvDoPulseVetoCorrect->getName(), bvalue, ts ) ) {
		m_doPulseVetoCorrect = bvalue;
		m_pvDoPulseVetoCorrect->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvIntermittentDataThreshold->getName(), uvalue, ts ) ) {
		m_intermittentDataThreshold = uvalue;
		m_pvIntermittentDataThreshold->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvNeutronEventStateBits->getName(), uvalue, ts ) ) {
		m_neutronEventStateBits = uvalue;
		m_pvNeutronEventStateBits->update(uvalue, &ts);

		// Set Neutron Event State Mask Based on Number of Bits...
		if ( m_neutronEventStateBits > 0 ) {
			m_neutronEventStateMask =
				( ((uint32_t) -1) << (28 - m_neutronEventStateBits) )
					& 0x0FFFFFFF;
			INFO("Setting Neutron Event State Mask to 0x"
				<< std::hex << m_neutronEventStateMask << std::dec << ".");
		}
	}

	if ( StorageManager::getAutoSavePV(
			m_pvNeutronEventSortByState->getName(), bvalue, ts ) ) {
		m_neutronEventSortByState = bvalue;
		m_pvNeutronEventSortByState->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvIgnoreInterleavedSawtooth->getName(), bvalue, ts ) ) {
		m_ignoreInterleavedSawtooth = bvalue;
		m_pvIgnoreInterleavedSawtooth->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvMonitorTOFBits->getName(), uvalue, ts ) ) {
		m_monitorTOFBits = uvalue;
		m_pvMonitorTOFBits->update(uvalue, &ts);

		// Set Beam Monitor TOF Mask Based on Number of Bits...
		m_monitorTOFMask = ((uint32_t) -1) >> (32 - m_monitorTOFBits);
		INFO("Setting Beam Monitor TOF Mask to 0x"
			<< std::hex << m_monitorTOFMask << std::dec << ".");
	}

	if ( StorageManager::getAutoSavePV(
			m_pvChopperTOFBits->getName(), uvalue, ts ) ) {
		m_chopperTOFBits = uvalue;
		m_pvChopperTOFBits->update(uvalue, &ts);

		// Set Chopper TOF Mask Based on Number of Bits...
		m_chopperTOFMask = ((uint32_t) -1) >> (32 - m_chopperTOFBits);
		INFO("Setting Chopper TOF Mask to 0x"
			<< std::hex << m_chopperTOFMask << std::dec << ".");
	}

	if ( StorageManager::getAutoSavePV(
			m_pvVerbose->getName(), uvalue, ts ) ) {
		m_verbose = uvalue;
		m_pvVerbose->update(uvalue, &ts);
	}

	// Initialize Next Run Number...
	m_nextRunNumber = StorageManager::getNextRun();
	if (!m_nextRunNumber)
		throw std::runtime_error("Unable to Get Next Run Number");

	m_beamlineInfo.reset(new BeamlineInfo(m_targetStationNumber,
			m_beamlineId, m_beamlineShortName, m_beamlineLongName));
	m_runInfo.reset(new RunInfo(m_facility, m_beamlineId, this,
		m_sendSampleInRunInfo, m_savePixelMap));
	m_geometry.reset(new Geometry(m_geometryPath));
	m_pixelMap.reset(new PixelMap(m_pixelMapPath,
		m_allowNonOneToOnePixelMapping, m_useOrigPixelMappingPkt));

	m_maxBank = m_pixelMap->numBanks() + PixelMap::REAL_BANK_OFFSET;

	// Note: "Number" of States Includes State 0...
	m_numStatesLast = 1;
	m_numStatesResetCount = 10;
}

SMSControl::~SMSControl()
{
	if (m_fdregChannelAccess) {
		delete m_fdregChannelAccess;
		m_fdregChannelAccess = NULL;
	}
}

// Update SMS Verbose Value from PV...
void SMSControl::updateVerbose(void)
{
	// Get Latest SMS Verbose Value from PV...
	uint32_t tmp = m_pvVerbose->value();

	// Check if SMS Verbose Value Changed, Log It...
	if ( tmp != m_verbose )
	{
		DEBUG("updateVerbose(): SMS Verbose Value Changed from "
			<< m_verbose << " to " << tmp);
		m_verbose = tmp;
	}
}

void SMSControl::show(unsigned level) const
{
	caServer::show(level);
}

pvExistReturn SMSControl::pvExistTest(const casCtx &ctx, const caNetAddr &,
			const char *pv_name)
{
	/* This is the new version, but just call to the deprecated one
	 * since we don't currently deal with access control on a per-net
	 * basis.
	 */
	return pvExistTest(ctx, pv_name);
}

pvExistReturn SMSControl::pvExistTest(const casCtx &UNUSED(ctx),
	const char *pv_name)
{
	if (m_pv_map.find(pv_name) != m_pv_map.end())
		return pverExistsHere;
	return pverDoesNotExistHere;
}

pvAttachReturn SMSControl::pvAttach(const casCtx &UNUSED(ctx),
	const char *pv_name)
{
	std::map<std::string, PVSharedPtr>::iterator iter;

	iter = m_pv_map.find(pv_name);
	if (iter == m_pv_map.end())
		return pverDoesNotExistHere;

	return *(iter->second);
}

void SMSControl::subscribeToPrimaryPVs( std::string PrimaryPVPrefix )
{
	DEBUG("subscribeToPrimaryPVs(): Subscribing to External Primary PVs"
		<< " with PrimaryPVPrefix = [" << PrimaryPVPrefix << "]");

	// Subscribe to the Primary "Recording" PV...
	m_extRecordingPV = ExternalPVPtr(new ExternalPV("Recording",
		PrimaryPVPrefix + ":Recording", PV_ENUM));
	subscribePV( m_extRecordingPV );

	// Subscribe to the Primary "RunNumber" PV...
	m_extRunNumberPV = ExternalPVPtr(new ExternalPV("RunNumber",
		PrimaryPVPrefix + ":RunNumber", PV_UINT));
	subscribePV( m_extRunNumberPV );

	// Subscribe to the Primary "Paused" PV...
	m_extPausedPV = ExternalPVPtr(new ExternalPV("Paused",
		PrimaryPVPrefix + ":Paused", PV_ENUM));
	subscribePV( m_extPausedPV );

	// Flush EPICS I/O...
	ca_flush_io();
}

void SMSControl::unsubscribePrimaryPVs(void)
{
	DEBUG("unsubscribePrimaryPVs():"
		<< " Unsubscribing from External Primary PVs");

	// Unsubscribe from the Primary "Recording" PV...
	unsubscribePV( m_extRecordingPV );

	// Unsubscribe from the Primary "RunNumber" PV...
	unsubscribePV( m_extRunNumberPV );

	// Unsubscribe from the Primary "Paused" PV...
	unsubscribePV( m_extPausedPV );

	// Flush EPICS I/O...
	ca_flush_io();
}

void SMSControl::subscribePV( ExternalPVPtr pv )
{
	DEBUG("subscribePV(): Subscribing to External Primary "
		<< pv->m_name << " PV with Connection String "
		<< pv->m_connection);

	ChanInfo info;
	info.m_pv = pv;

	// Create a CA channel
	if ( ca_create_channel( pv->m_connection.c_str(),
				&SMSControl::epicsConnectionHandler,
				0 /* void *PUSER */, 0 /* priority */,
				&info.m_chid )
			== ECA_NORMAL )
	{
		// Note: don't flush I/O here
		// - subscribeToPrimaryPVs() method will call it

		// Update channel info and PV name index structures
		m_chan_info[info.m_chid] = info;
		m_pv_index[pv->m_connection] = info.m_chid;

		DEBUG("subscribePV(): Connected chid: " << info.m_chid
			<< " for External Primary " << pv->m_name
			<< " PV " << pv->m_connection);
	}
	else
	{
		ERROR("subscribePV(): Failed to create channel"
			<< " for External Primary " << pv->m_name
			<< " PV " << pv->m_connection);
	}
}

void SMSControl::unsubscribePV( ExternalPVPtr pv )
{
	DEBUG("unsubscribePV(): Unsubscribing from External Primary "
		<< pv->m_name << " PV with Connection String "
		<< pv->m_connection);

	// Unregister Any Ready Adapter for PV Channel...???
	// Only if No Open EPICS PV Channels Yet Exist...??? ;-D
	// Nope, as it happens, ca_add_fd_registration() only ever
	// returns *1* Pseudo Channel Access File Descriptor (After R3.14),
	// so we *Don't* need to Unregister/Reregister a ReadyAdapter. ;-D

	// Unsubscribe from PV Channel...

	std::map<std::string,chid>::iterator ipv =
		m_pv_index.find( pv->m_connection );

	if ( ipv != m_pv_index.end() )
	{
		DEBUG("unsubscribePV(): Disconnecting channel"
			<< " for External Primary " << pv->m_name
			<< " PV " << pv->m_connection);

		std::map<chid,ChanInfo>::iterator ich =
			m_chan_info.find( ipv->second );

		if ( ich != m_chan_info.end() )
		{
			DEBUG("unsubscribePV(): Found Existing Channel"
				<< " for External Primary " << pv->m_name
				<< " PV " << pv->m_connection);

			if ( ich->second.m_subscribed )
			{
				DEBUG("unsubscribePV(): Clearing Subscription"
					<< " for External Primary " << pv->m_name
					<< " PV " << pv->m_connection);
				
				ca_clear_subscription( ich->second.m_evid );
			}

			DEBUG("unsubscribePV(): Clearing Channel"
				<< " for External Primary " << pv->m_name
				<< " PV " << pv->m_connection);
			
			ca_clear_channel( ich->second.m_chid );

			// Update channel info index structures

			DEBUG("unsubscribePV(): Erasing Channel Info"
				<< " for External Primary " << pv->m_name
				<< " PV " << pv->m_connection);

			m_chan_info.erase( ich );

			DEBUG("unsubscribePV(): Disconnecting Channel Info"
				<< " for External Primary " << pv->m_name
				<< " PV " << pv->m_connection);

			// Don't flush I/O here
			// - unsubscribePrimaryPVs() method will call it
		}
		else
		{
			DEBUG("unsubscribePV(): Warning: No Channel Info Found"
				<< " for External Primary " << pv->m_name
				<< " PV " << pv->m_connection);
		}

		// Update name index structures
		m_pv_index.erase( ipv );
	}

	else
	{
		ERROR("unsubscribePV(): Failed to disconnect channel"
			<< " for External Primary " << pv->m_name
			<< " PV " << pv->m_connection
			<< " - Not Found");
	}
}

/**
 * @brief Converts EPICS DB record type to EPICS time record type
 * @param a_rec_type - Input value to convert
 * @return EPICS time type if value is valid; throws otherwise
 */
int32_t
SMSControl::epicsToTimeRecordType( uint32_t a_rec_type )
{
    switch ( a_rec_type )
    {
    case DBR_STRING:    return DBR_TIME_STRING;
    case DBR_SHORT:     return DBR_TIME_SHORT;
    case DBR_FLOAT:     return DBR_TIME_FLOAT;
    case DBR_ENUM:      return DBR_TIME_ENUM;
    case DBR_CHAR:      return DBR_TIME_CHAR;
    case DBR_LONG:      return DBR_TIME_LONG;
    case DBR_DOUBLE:    return DBR_TIME_DOUBLE;
    default:
		ERROR("epicsToTimeRecordType():"
			<< " Invalid EPICS DB record type: " << a_rec_type
			<< " - Defaulting to DBR_TIME_STRING...");
		return DBR_TIME_STRING;
    }
}

/**
 * @brief Converts EPICS DB record type to EPICS control record type
 * @param a_rec_type - Input value to convert
 * @return EPICS control type if value is valid; throws otherwise
 */
int32_t
SMSControl::epicsToCtrlRecordType( uint32_t a_rec_type )
{
    switch ( a_rec_type )
    {
    case DBR_STRING:    return DBR_CTRL_STRING;
    case DBR_SHORT:     return DBR_CTRL_SHORT;
    case DBR_FLOAT:     return DBR_CTRL_FLOAT;
    case DBR_ENUM:      return DBR_CTRL_ENUM;
    case DBR_CHAR:      return DBR_CTRL_CHAR;
    case DBR_LONG:      return DBR_CTRL_LONG;
    case DBR_DOUBLE:    return DBR_CTRL_DOUBLE;
    default:
		ERROR("epicsToCtrlRecordType():"
			<< " Invalid EPICS DB record type: " << a_rec_type
			<< " - Defaulting to DBR_CTRL_STRING...");
		return DBR_CTRL_STRING;
    }
}

/**
 * @brief Checks if argument is a valid EPICS time record
 * @param a_rec_type - Input value to check
 * @return True if value is valid; false otherwise
 */
bool
SMSControl::epicsIsTimeRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_TIME_STRING && a_rec_type <= DBR_TIME_DOUBLE )
        return true;
    else
        return false;
}

/**
 * @brief Checks if argument is a valid EPICS control record
 * @param a_rec_type - Input value to check
 * @return True if value is valid; false otherwise
 */
bool
SMSControl::epicsIsCtrlRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_CTRL_STRING && a_rec_type <= DBR_CTRL_DOUBLE )
        return true;
    else
        return false;
}

/**
 * @brief Converts EPICS record type to ADARA PV type
 * @param a_rec_type - Input value to convert
 * @return PVType
 */
SMSControl::PVType
SMSControl::epicsToPVType( uint32_t a_rec_type, uint32_t a_elem_count )
{
    switch ( a_rec_type )
    {
        case DBR_STRING:    return PV_STR;

        case DBR_ENUM:      return PV_ENUM;

        case DBR_SHORT:
        case DBR_LONG:
        {
            // Just a (Scalar) Integer...
            if ( a_elem_count <= 1 )
                return PV_INT;
            // Actually a Variable Length Integer Array...!
            else 
                return PV_INT_ARRAY;
        }

        case DBR_FLOAT:
        case DBR_DOUBLE:
        {
            // Just a (Scalar) Float...
            if ( a_elem_count <= 1 )
                return PV_REAL;
            // Actually a Variable Length Float Array...!
            else 
                return PV_REAL_ARRAY;
        }

        case DBR_CHAR:
        {
            // Just a (Scalar) Character...
            if ( a_elem_count <= 1 )
                return PV_UINT;
            // Actually a Variable Length Character String...!
            else 
                return PV_STR;
        }

        default:
			ERROR("epicsToPVType():"
				<< " Invalid EPICS DB record type: " << a_rec_type
				<< " - Defaulting to String...");
			return PV_STR;
    }
}

/**
 * @brief Handles EPICS connection status callbacks
 * @param a_args - EPICS callback arguments
 *
 * This method handles EPICS channel connection Up/Down events. When a
 * connection is established, a subscription is created for time-value
 * events, and the state machine is triggered to ask for channel metadata
 * (INFO_NEEDED). When a connection is lost, the event subscription is
 * dropped and a PV disconnect packet is emitted.
 */
void
SMSControl::epicsConnectionHandler( struct connection_handler_args a_args )
{
	DEBUG("epicsConnectionHandler()"
		<< " a_args.op=" << a_args.op
		<< " a_args.chid=" << a_args.chid);

	SMSControl *ctrl = SMSControl::getInstance();

	try
	{
		// Connection established?
		if ( a_args.op == CA_OP_CONN_UP )
		{
			DEBUG("epicsConnectionHandler(): Connection Up!");

			std::map<chid,ChanInfo>::iterator ich =
				ctrl->m_chan_info.find( a_args.chid );

			if ( ich != ctrl->m_chan_info.end() )
			{
				DEBUG("epicsConnectionHandler(): Channel Connected"
					<< " for External Primary " << ich->second.m_pv->m_name
					<< " PV " << ich->second.m_pv->m_connection);

				ich->second.m_connected = true;

				chtype type = ca_field_type( a_args.chid );
				unsigned long elem_count = ca_element_count( a_args.chid );
				if ( VALID_DB_FIELD( type ) )
				{
					// Save native type
					ich->second.m_ca_type = type;
					ich->second.m_ca_elem_count = elem_count;
					//cout <<  "chan: " << ich->first
						// << " ca_type = " << type << " for PV: "
						// << ich->second.m_pv->m_connection << endl;

					std::string pvStr = "";
					if ( ich->second.m_pv != NULL )
					{
						pvStr = " for External Primary "
							+ ich->second.m_pv->m_name
							+ " PV " + ich->second.m_pv->m_connection;
					}

					if ( ca_create_subscription(
							epicsToTimeRecordType( type ), 0,
							ich->second.m_chid,
							DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
							&epicsEventHandler, 0 /* void *USERARG */,
							&ich->second.m_evid ) == ECA_NORMAL )
					{
						ERROR("epicsConnectionHandler():"
							<< " Subscription created" << pvStr);

						ich->second.m_subscribed = true;
					}
					else
					{
						ERROR("epicsConnectionHandler():"
							<< " Failed to create subscription" << pvStr);
					}

					ca_flush_io();

					// Note: there is no way to know if the metadata
					// on this channel has or hasn't changed,
					// so assume it has changed

					// There is NO ctrl record for EPICS string types
					if ( ich->second.m_pv->m_type == PV_STR )
					{
						ERROR("epicsConnectionHandler():"
							<< " String PV, Info Available" << pvStr);
						ich->second.m_chan_state = INFO_AVAILABLE;
					}
					else
					{
						ERROR("epicsConnectionHandler():"
							<< " Not A String PV, Info Needed" << pvStr);
						ich->second.m_chan_state = INFO_NEEDED;
					}

					// If PV Channel Info is Needed, Go Get It... ;-D
					if ( ich->second.m_chan_state == INFO_NEEDED )
					{
						// Trigger Callback on PV Event Channel...
						if ( ca_get_callback(
								epicsToCtrlRecordType(
									ich->second.m_ca_type ),
								ich->first, &epicsEventHandler,
								0 /* void *USERARG */ ) == ECA_NORMAL )
						{
							ERROR("epicsConnectionHandler():"
								<< " PV Channel Get Callback Trigger"
								<< pvStr << " Successful,"
								" Info Pending...");

							ich->second.m_chan_state = INFO_PENDING;
						}
						else
						{
							ERROR("epicsConnectionHandler():"
								<< " PV Channel Get Callback Trigger"
								<< pvStr
								<< " Failed to Get Channel Info...!");
						}
					}

					// Is the PV Channel Info Already Available...?
					if ( ich->second.m_chan_state == INFO_AVAILABLE )
					{
						// Set PV Meta-Data Info... ;-D
						ich->second.m_pv->m_type =
							epicsToPVType( ich->second.m_ca_type,
								ich->second.m_ca_elem_count );
						ich->second.m_pv->m_elem_count =
							ich->second.m_ca_elem_count;
						ich->second.m_pv->m_units =
							ich->second.m_ca_units;

						ich->second.m_chan_state = READY;
					}
				}
			}

			else
			{
				DEBUG("epicsConnectionHandler():"
					<< " Warning: chid=" << a_args.chid
					<< " Not Found...!");
			}
		}

		// Connection lost?
		else if ( a_args.op == CA_OP_CONN_DOWN )
		{
			DEBUG("epicsConnectionHandler(): Connection Down...");

			std::map<chid,ChanInfo>::iterator ich =
				ctrl->m_chan_info.find( a_args.chid );

			if ( ich != ctrl->m_chan_info.end() )
			{
				DEBUG("epicsConnectionHandler(): Channel Disconnected"
					<< " for External Primary " << ich->second.m_pv->m_name
					<< " PV " << ich->second.m_pv->m_connection);

				ich->second.m_connected = false;

				std::string pvStr = "";
				if ( ich->second.m_pv != NULL )
				{
					pvStr = " for External Primary "
						+ ich->second.m_pv->m_name
						+ " PV " + ich->second.m_pv->m_connection;
				}

				if ( ich->second.m_subscribed )
				{
					ERROR("epicsConnectionHandler():"
						<< " Clearing subscription (Down?)" << pvStr);

					ca_clear_subscription( ich->second.m_evid );

					ich->second.m_subscribed = false;
				}

				// Set var state to disconnected
				ich->second.m_pv_state.m_status = epicsAlarmComm;
				ich->second.m_pv_state.m_severity = epicsSevMajor;
			}

			else
			{
				DEBUG("epicsConnectionHandler():"
					<< " Warning: chid=" << a_args.chid
					<< " Not Found...!");
			}
		}
	}
	catch( std::exception &e )
	{
		ERROR("epicsConnectionHandler():" << " Exception!" << e.what());
	}
	catch(...)
	{
		ERROR("epicsConnectionHandler():" << " Unknown Exception!");
	}
}

template<typename T>
void
SMSControl::updateState( const void *a_src, PVState &a_state )
{
	DEBUG("updateState():"
		<< " a_src=0x" << std::hex << (long) a_src << std::dec
		<< " a_src->stamp.secPastEpoch="
			<< ((T*)a_src)->stamp.secPastEpoch
		<< " a_src->stamp.nsec=" << ((T*)a_src)->stamp.nsec
		<< " a_src->status=" << ((T*)a_src)->status
		<< " a_src->severity=" << ((T*)a_src)->severity);

    if ( ((T*)a_src)->stamp.secPastEpoch == 0 )
    {
        // Use a local timestamp if a valid timestamp has
        // not yet been received
        if ( a_state.m_time.sec == 0 )
        {
            a_state.m_time.sec = (uint32_t)time(0)
				- ADARA::EPICS_EPOCH_OFFSET;
            a_state.m_time.nsec = 0;
        }
    }
    else
    {
        // It's *OK*" if the timestamp goes backwards, Let It Be! ;-D
        // (always just pass the data through; maybe something else
        // later on can sort it out... ;-)
        a_state.m_time.sec = ((T*)a_src)->stamp.secPastEpoch;
        a_state.m_time.nsec = ((T*)a_src)->stamp.nsec;
    }

    a_state.m_status = ((T*)a_src)->status;
    a_state.m_severity = ((T*)a_src)->severity;
}

uint32_t
SMSControl::uint32ValueOf( PVType a_type, PVState &a_state )
{
	uint32_t uint_val = 0;

    switch ( a_type )
    {
    case PV_ENUM:
    case PV_UINT:
		return a_state.m_uint_val;
        break;

    case PV_INT:
		return (uint32_t) a_state.m_int_val;
        break;

    case PV_REAL:
		return (uint32_t) a_state.m_double_val;
        break;

    case PV_STR:
		// Just Try to Lexical Cast the String into an Unsigned Int... ;-b
		uint_val =
			boost::lexical_cast<uint32_t>( a_state.m_str_val.c_str() );
		ERROR("uint32ValueOf(): Warning: Converted String PV to UInt32"
			<< " [" << a_state.m_str_val << "] -> [" << uint_val << "]");
		return uint_val;
		break;

    case PV_INT_ARRAY:
		// Meh, Just Return First Array Element (If Any)... ;-b
		if ( a_state.m_elem_count > 0 )
		{
        	if ( a_state.m_short_array != NULL )
			{
				uint_val = (uint32_t) a_state.m_short_array[0];
				ERROR("uint32ValueOf(): Warning:"
					<< " Using First Value of Short Integer Array PV: "
					<< "[" << uint_val << "]");
			}
			else if ( a_state.m_long_array != NULL )
			{
				uint_val = (uint32_t) a_state.m_long_array[0];
				ERROR("uint32ValueOf(): Warning:"
					<< " Using First Value of Long Integer Array PV: "
					<< "[" << uint_val << "]");
			}
			else
			{
				uint_val = 0;
				ERROR("uint32ValueOf():"
					<< " Missing Integer Array Data for PV!"
					<< " Returning 0.");
			}
		}
		else
		{
			uint_val = 0;
			ERROR("uint32ValueOf():"
				<< " No Integer Array Data Elements for PV!"
				<< " Returning 0.");
		}
		return uint_val;
		break;

    case PV_REAL_ARRAY:
		// Meh, Just Return First Array Element (If Any)... ;-b
		if ( a_state.m_elem_count > 0 )
		{
        	if ( a_state.m_float_array != NULL )
			{
				uint_val = (uint32_t) a_state.m_float_array[0];
				ERROR("uint32ValueOf(): Warning:"
					<< " Using First Value of Float Array PV: "
					<< "[" << uint_val << "]");
			}
			else if ( a_state.m_double_array != NULL )
			{
				uint_val = (uint32_t) a_state.m_double_array[0];
				ERROR("uint32ValueOf(): Warning:"
					<< " Using First Value of Double Array PV: "
					<< "[" << uint_val << "]");
			}
			else
			{
				uint_val = 0;
				ERROR("uint32ValueOf():"
					<< " Missing Real Array Data for PV!"
					<< " Returning 0.");
			}
		}
		else
		{
			uint_val = 0;
			ERROR("uint32ValueOf():"
				<< " No Real Array Data Elements for PV!"
				<< " Returning 0.");
		}
		return uint_val;
		break;
    }

	// Never Use This... ;-D
	return uint_val;
}

bool
SMSControl::boolValueOf( PVType a_type, PVState &a_state )
{
	bool bool_val = false;

    switch ( a_type )
    {
    case PV_ENUM:
    case PV_UINT:
		return( a_state.m_uint_val != 0 );
        break;

    case PV_INT:
		return( a_state.m_int_val != 0 );
        break;

    case PV_REAL:
		return( approximatelyEqual( a_state.m_double_val, 0.0, 0.0001 ) );
        break;

    case PV_STR:
		// Just Try to Lexical Cast the String into an Unsigned Int... ;-b
		bool_val =
			boost::lexical_cast<bool>( a_state.m_str_val.c_str() );
		ERROR("boolValueOf(): Warning: Converted String PV to Bool"
			<< " [" << a_state.m_str_val << "] -> [" << bool_val << "]");
		return bool_val;
		break;

    case PV_INT_ARRAY:
		// Meh, Just Return First Array Element (If Any)... ;-b
		if ( a_state.m_elem_count > 0 )
		{
        	if ( a_state.m_short_array != NULL )
			{
				bool_val = (bool) a_state.m_short_array[0];
				ERROR("boolValueOf(): Warning:"
					<< " Using First Value of Short Integer Array PV:"
					<< " [" << a_state.m_short_array[0] << "] ->"
					<< " [" << bool_val << "]");
			}
			else if ( a_state.m_long_array != NULL )
			{
				bool_val = (bool) a_state.m_long_array[0];
				ERROR("boolValueOf(): Warning:"
					<< " Using First Value of Long Integer Array PV:"
					<< " [" << a_state.m_long_array[0] << "] ->"
					<< " [" << bool_val << "]");
			}
			else
			{
				bool_val = false;
				ERROR("boolValueOf():"
					<< " Missing Integer Array Data for PV!"
					<< " Returning False.");
			}
		}
		else
		{
			bool_val = false;
			ERROR("boolValueOf():"
				<< " No Integer Array Data Elements for PV!"
				<< " Returning False.");
		}
		return bool_val;
		break;

    case PV_REAL_ARRAY:
		// Meh, Just Return First Array Element (If Any)... ;-b
		if ( a_state.m_elem_count > 0 )
		{
        	if ( a_state.m_float_array != NULL )
			{
				bool_val = approximatelyEqual( a_state.m_float_array[0],
					0.0, 0.0001 );
				ERROR("boolValueOf(): Warning:"
					<< " Using First Value of Float Array PV:"
					<< " [" << a_state.m_float_array[0] << "] ->"
					<< " [" << bool_val << "]");
			}
			else if ( a_state.m_double_array != NULL )
			{
				bool_val = approximatelyEqual( a_state.m_double_array[0],
					0.0, 0.0001 );
				ERROR("boolValueOf(): Warning:"
					<< " Using First Value of Double Array PV:"
					<< " [" << a_state.m_double_array[0] << "] ->"
					<< " [" << bool_val << "]");
			}
			else
			{
				bool_val = false;
				ERROR("boolValueOf():"
					<< " Missing Real Array Data for PV!"
					<< " Returning False.");
			}
		}
		else
		{
			bool_val = false;
			ERROR("boolValueOf():"
				<< " No Real Array Data Elements for PV!"
				<< " Returning False.");
		}
		return bool_val;
		break;
    }

	// Never Use This... ;-D
	return bool_val;
}

/**
 * @brief Handles EPICS channel events
 * @param a_args - EPICS callback arguments
 *
 * This method handles EPICS data and metadata channel events.
 */
void
SMSControl::epicsEventHandler( struct event_handler_args a_args )
{
	DEBUG("epicsEventHandler():"
		<< " a_args.type=" << a_args.type
		<< " a_args.chid=" << a_args.chid
		<< " a_args.status=" << a_args.status);

	SMSControl *ctrl = SMSControl::getInstance();

	try
	{
		// Valid status / db record?
		if ( a_args.status != ECA_NORMAL || a_args.dbr == 0 )
		{
			ERROR("epicsEventHandler(): Invalid Arguments"
				<< " a_args.status=0x"
					<< std::hex << a_args.status << std::dec
				<< " a_args.dbr=0x"
					<< std::hex << a_args.dbr << std::dec
				<< " - Bail...!");
			return;
		}

		// Data event?
		if ( epicsIsTimeRecordType( a_args.type ) )
		{
			DEBUG("epicsEventHandler(): Data Event");

			// Valid EPICS channel?
			std::map<chid,ChanInfo>::iterator ich =
				ctrl->m_chan_info.find( a_args.chid );

			if ( ich != ctrl->m_chan_info.end() )
			{
				DEBUG("epicsEventHandler(): Value Update"
					<< " for External Primary " << ich->second.m_pv->m_name
					<< " PV " << ich->second.m_pv->m_connection);

				// Extract PV state from type-specific data structure
				PVState &state = ich->second.m_pv_state;

				std::string ckPvStr = "";
				if ( ich->second.m_pv != NULL )
				{
					ckPvStr = " for External Primary "
						+ ich->second.m_pv->m_name
						+ " PV " + ich->second.m_pv->m_connection;
				}

				bool state_changed = false;

				switch ( a_args.type )
				{
				case DBR_TIME_STRING:
					state.m_str_val =
						((struct dbr_time_string *)a_args.dbr)->value;
					state.m_elem_count = a_args.count; // Always 1 String?
					ctrl->updateState<struct dbr_time_string>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got String PV Value Update" << ckPvStr
						<< " state.m_str_val=[" << state.m_str_val << "]"
						<< " state.m_elem_count=" << state.m_elem_count
						<< " state.m_str_val.size()="
							<< state.m_str_val.size());
					state_changed = true;
					break;
				case DBR_TIME_SHORT:
					// Could be Scalar Numerical Value
					// -OR- Variable Length Numerical Array...!
					//	-> Therefore, Set *Both* Value Fields...! ;-D
					state.m_int_val = (int32_t)
						((struct dbr_time_short *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					if ( a_args.count > 1 ) // Minimum Array Size is 2...
					{
						state.m_short_array = new int16_t[a_args.count];
						int16_t *values = (int16_t *)
							&((struct dbr_time_short *)a_args.dbr)->value;
						for ( uint32_t i=0 ; i < a_args.count ; i++ )
						{
							state.m_short_array[i] = values[i];
						}
					}
					ctrl->updateState<struct dbr_time_short>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got Short PV Value Update" << ckPvStr);
					state_changed = true;
					break;
				case DBR_TIME_FLOAT:
					// Could be Scalar Numerical Value
					// -OR- Variable Length Numerical Array...!
					//	-> Therefore, Set *Both* Value Fields...! ;-D
					state.m_double_val = (double)
						((struct dbr_time_float *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					if ( a_args.count > 1 ) // Minimum Array Size is 2...
					{
						state.m_float_array = new float[a_args.count];
						float *values = (float *)
							&((struct dbr_time_float *)a_args.dbr)->value;
						for ( uint32_t i=0 ; i < a_args.count ; i++ )
						{
							state.m_float_array[i] = values[i];
						}
					}
					ctrl->updateState<struct dbr_time_float>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got Float PV Value Update" << ckPvStr);
					state_changed = true;
					break;
				case DBR_TIME_ENUM:
					state.m_uint_val = (uint32_t)
						((struct dbr_time_enum *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					ctrl->updateState<struct dbr_time_enum>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got Enum PV Value Update" << ckPvStr);
					state_changed = true;
					break;
				case DBR_TIME_CHAR:
					// Could be (Scalar Numerical) Character
					// -OR- Variable Length Character String...!
					//	-> Therefore, Set *Both* Value Fields...! ;-D
					state.m_uint_val = (uint32_t)
						((struct dbr_time_char *)a_args.dbr)->value;
					state.m_str_val = (char *)
						&((struct dbr_time_char *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					ctrl->updateState<struct dbr_time_char>(
						a_args.dbr, state );
					// *** Trim String Value to Specified Length!!
					if ( state.m_elem_count < state.m_str_val.size() )
					{
						DEBUG("epicsEventHandler():"
							<< " Got Char PV Value Update TRIMMING LENGTH"
							<< ckPvStr << " ORIG "
							<< " state.m_str_val=["
								<< state.m_str_val << "]"
							<< " state.m_elem_count=" << state.m_elem_count
							<< " state.m_str_val.size()="
								<< state.m_str_val.size());
						state.m_str_val.erase( state.m_elem_count );
					}
					DEBUG("epicsEventHandler():"
						<< " Got Char PV Value Update" << ckPvStr
						<< " state.m_str_val=["
							<< state.m_str_val << "]"
						<< " state.m_elem_count=" << state.m_elem_count
						<< " state.m_str_val.size()="
							<< state.m_str_val.size());
					state_changed = true;
					break;
				case DBR_TIME_LONG:
					// Could be Scalar Numerical Value
					// -OR- Variable Length Numerical Array...!
					//	-> Therefore, Set *Both* Value Fields...! ;-D
					state.m_int_val = (int32_t)
						((struct dbr_time_long *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					if ( a_args.count > 1 ) // Minimum Array Size is 2...
					{
						state.m_long_array = new int32_t[a_args.count];
						int32_t *values = (int32_t *)
							&((struct dbr_time_long *)a_args.dbr)->value;
						for ( uint32_t i=0 ; i < a_args.count ; i++ )
						{
							state.m_long_array[i] = values[i];
						}
					}
					ctrl->updateState<struct dbr_time_long>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got Long PV Value Update" << ckPvStr);
					state_changed = true;
					break;
				case DBR_TIME_DOUBLE:
					// Could be Scalar Numerical Value
					// -OR- Variable Length Numerical Array...!
					//	-> Therefore, Set *Both* Value Fields...! ;-D
					state.m_double_val = (double)
						((struct dbr_time_double *)a_args.dbr)->value;
					state.m_elem_count = a_args.count;
					if ( a_args.count > 1 ) // Minimum Array Size is 2...
					{
						state.m_double_array = new double[a_args.count];
						double *values = (double *)
							&((struct dbr_time_double *)a_args.dbr)->value;
						for ( uint32_t i=0 ; i < a_args.count ; i++ )
						{
							state.m_double_array[i] = values[i];
						}
					}
					ctrl->updateState<struct dbr_time_double>(
						a_args.dbr, state );
					DEBUG("epicsEventHandler():"
						<< " Got Double PV Value Update" << ckPvStr);
					state_changed = true;
					break;
				default:
					ERROR("epicsEventHandler():"
						<< " Unknown Data Type Case"
						<< " a_args.type=0x"
							<< std::hex << a_args.type << std::dec
						<< " - Ignoring...!");
					break;
				}

				// Handle External PV State Change
				if ( state_changed )
				{
					DEBUG("epicsEventHandler():"
						<< " State Changed" << ckPvStr);

					// Wallclock Time...!
					struct timespec ts;

					ts.tv_sec = state.m_time.sec
						+ ADARA::EPICS_EPOCH_OFFSET;
					ts.tv_nsec = state.m_time.nsec;

					// Recording PV
					if ( !std::string("Recording").compare(
							ich->second.m_pv->m_name ) )
					{
						DEBUG("epicsEventHandler():"
							<< " RECORDING PV CHANGED");

						bool recording = ctrl->boolValueOf(
							ich->second.m_pv->m_type, state );

						DEBUG("epicsEventHandler():"
							<< "External PV Setting Recording to "
							<< recording << " at "
							<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
							<< "." << std::setfill('0') << std::setw(9)
							<< ts.tv_nsec);

						bool status;

						if ( recording )
						{
							// Do We Need to "Force" Here...???
							status = ctrl->setRecording( true, &ts );
								// Wallclock Time...!

							if ( !status )
							{
								ERROR(
									( ctrl->m_recording
										? "[RECORDING] " : "" )
									<< "epicsEventHandler():"
									<< " External PV \"Run Start\""
									<< " Command Failed!");
							}
						}

						else
						{
							// Do We Need to "Force" Here...???
							status = ctrl->setRecording( false, &ts );
								// Wallclock Time...!

							if ( !status )
							{
								ERROR(
									( ctrl->m_recording
										? "[RECORDING] " : "" )
									<< "epicsEventHandler():"
									<< " External PV \"Run Stop\""
									<< " Command Failed!");
							}
						}
					}

					// RunNumber PV
					else if ( !std::string("RunNumber").compare(
							ich->second.m_pv->m_name ) )
					{
						DEBUG("epicsEventHandler():"
							<< " RUNNUMBER PV CHANGED");

						// This One's Easy, Just Sneak External RunNumber
						// Value into SMSControl's "Next Run Number"...!
						ctrl->m_nextRunNumber = ctrl->uint32ValueOf(
							ich->second.m_pv->m_type, state );

						DEBUG("epicsEventHandler():"
							<< "External PV Setting NEXT RunNumber to "
							<< ctrl->m_nextRunNumber << " at "
							<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
							<< "." << std::setfill('0') << std::setw(9)
							<< ts.tv_nsec);
					}

					// Paused PV
					else if ( !std::string("Paused").compare(
							ich->second.m_pv->m_name ) )
					{
						DEBUG("epicsEventHandler():"
							<< " PAUSED PV CHANGED");

						bool paused = ctrl->boolValueOf(
							ich->second.m_pv->m_type, state );

						DEBUG("epicsEventHandler():"
							<< "External PV Setting Paused Mode to "
							<< paused << " at "
							<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
							<< "." << std::setfill('0') << std::setw(9)
							<< ts.tv_nsec);

						boost::shared_ptr<Markers> markers =
							ctrl->getMarkers();

						if ( paused )
						{
							markers->pause( &ts ); // Wallclock Time
						}
						else
						{
							markers->resume( &ts ); // Wallclock Time
						}

						// Also Update Regular SMS Paused PV State...
						// Note: MarkerPausedPV Update Doesn't Call
						// the "changed()" method... ;-D
						markers->updatePausedPV( paused, &ts );
							// Wallclock Time...!
					}

					// Unknown PV Name...
					else
					{
						DEBUG("epicsEventHandler():"
							<< " UNKNOWN ["
							<< ich->second.m_pv->m_name
							<< "] PV CHANGED at "
							<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
							<< "." << std::setfill('0') << std::setw(9)
							<< ts.tv_nsec
							<< " - Ignoring...");
					}
				}
			}

			// Invalid EPICS Channel/Not Found...!
			else
			{
				DEBUG("epicsEventHandler():"
					<< " Error Looking Up EPICS Data Event,"
					<< " Unknown Channel Id chid=" << a_args.chid);
			}
		}

		// Metadata event?
		else if ( epicsIsCtrlRecordType( a_args.type ) )
		{
			DEBUG("epicsEventHandler(): Meta-Data Event");

			// Valid EPICS channel?
			std::map<chid,ChanInfo>::iterator ich =
				ctrl->m_chan_info.find( a_args.chid );

			if ( ich != ctrl->m_chan_info.end() )
			{
				DEBUG("epicsEventHandler(): Meta-Data Update"
					<< " for External Primary " << ich->second.m_pv->m_name
					<< " PV " << ich->second.m_pv->m_connection);

				// Note EPICS does not define ctrl structs (or units)
				// for string types...
				// Extract units and/or enumeration values
				switch ( a_args.type )
				{
				case DBR_CTRL_SHORT:
					ich->second.m_ca_units =
						((struct dbr_ctrl_short *)a_args.dbr)->units;
					DEBUG("epicsEventHandler(): Short Units = "
						<< ich->second.m_ca_units);
					break;
				case DBR_CTRL_FLOAT:
					ich->second.m_ca_units =
						((struct dbr_ctrl_float *)a_args.dbr)->units;
					DEBUG("epicsEventHandler(): Float Units = "
						<< ich->second.m_ca_units);
					break;
				case DBR_CTRL_CHAR:
					ich->second.m_ca_units =
						((struct dbr_ctrl_char *)a_args.dbr)->units;
					DEBUG("epicsEventHandler(): Ctrl Units = "
						<< ich->second.m_ca_units);
					break;
				case DBR_CTRL_LONG:
					ich->second.m_ca_units =
						((struct dbr_ctrl_long *)a_args.dbr)->units;
					DEBUG("epicsEventHandler(): Long Units = "
						<< ich->second.m_ca_units);
					break;
				case DBR_CTRL_DOUBLE:
					ich->second.m_ca_units =
						((struct dbr_ctrl_double *)a_args.dbr)->units;
					DEBUG("epicsEventHandler(): Double Units = "
						<< ich->second.m_ca_units);
					break;
				case DBR_CTRL_ENUM:
					{
					ich->second.m_ca_enum_vals.clear();
					for ( int i = 0;
							i < ((struct dbr_ctrl_enum *)a_args.dbr)
								->no_str;
							++i )
						{
						ich->second.m_ca_enum_vals[i] =
							((struct dbr_ctrl_enum *)a_args.dbr)->strs[i];
						DEBUG("epicsEventHandler(): Enum Val #" << i
							<< " = " << ich->second.m_ca_enum_vals[i]);
						}
					break;
					}
				default:
					DEBUG("epicsEventHandler(): Unknown Case - Ignored");
					break;
				}

				// Bump state to INFO_AVAILABLE if it was pending
				if ( ich->second.m_chan_state == INFO_PENDING )
				{
					ich->second.m_chan_state = INFO_AVAILABLE;

					// Set PV Meta-Data Info... ;-D
					ich->second.m_pv->m_type =
						epicsToPVType( ich->second.m_ca_type,
							ich->second.m_ca_elem_count );
					ich->second.m_pv->m_elem_count =
						ich->second.m_ca_elem_count;
					ich->second.m_pv->m_units =
						ich->second.m_ca_units;

					ich->second.m_chan_state = READY;
				}
			}

			// Invalid EPICS Channel/Not Found...!
			else
			{
				DEBUG("epicsEventHandler():"
					<< " Error Looking Up EPICS Metadata Event,"
					<< " Unknown Channel Id chid=" << a_args.chid);
			}
		}
	}
	catch( std::exception &e )
	{
		ERROR("epicsEventHandler():" << " Exception!" << e.what());
	}
	catch(...)
	{
		ERROR("epicsEventHandler():" << " Unknown Exception!");
	}
}

void SMSControl::addPV(PVSharedPtr pv)
{
	if (m_pv_map.count(pv->getName())) {
		std::string msg("Adding duplicate PV: ");
		msg += pv->getName();
		throw std::logic_error(msg);
	}
	m_pv_map[pv->getName()] = pv;
}

bool SMSControl::setRecording( bool v, struct timespec *ts )
		// Wallclock Time...!
{
	/* We return true if we accepted the setting, and false if not.
	 * It is not an error for a caller to try to stop recording if
	 * we aren't actually recording (so return true), but it is an
	 * error to try to start recording when we already are -- return
	 * false for that case.
	 */
	if ( v == m_recording ) {
		return( !v );
 	}

	// Start a New Recording...
	if ( v ) {

		std::string why;

		// Is the RunInfo Valid...?
		if ( !m_runInfo->valid( why ) ) {
			ERROR("Failed to Start Run " << m_nextRunNumber
				<< " - RunInfo Not Valid! (" << why << ")");
			// RunInfo is NOT Valid...!
			m_summaryRunInfo = false;
			m_reasonBase = "Not Starting Run";
			m_reasonRunInfo = why;
			// Update Overall Summary and Reason
			// - "false": Don't Set Base Reason (we just set it :-)
			// - "true": Do Log Status as Error
			// - "true": Major Error
			setSummaryReason( false, true, true );
			return false;
		}
		else {
			// RunInfo is OK...! :-D
			m_summaryRunInfo = true;
			m_reasonRunInfo = why;
		}

		// Are All Required DataSources Connected...?
		if ( !checkRequiredDataSources( why ) ) {
			ERROR("Failed to Start Run " << m_nextRunNumber
				<< " - Required DataSources Not Ready! (" << why << ")");
			// Some Required DataSource(s) are NOT Connected...!
			m_summaryDataSources = false;
			m_reasonBase = "Not Starting Run";
			m_reasonDataSources = why;
			// Update Overall Summary and Reason
			// - "false": Don't Set Base Reason (we just set it :-)
			// - "true": Do Log Status as Error
			// - "true": Major Error
			setSummaryReason( false, true, true );
			return false;
		}
		else {
			// Required DataSources are OK...! :-D
			m_summaryDataSources = true;
			m_reasonDataSources = why;
		}

		// Update "Next Run Number"...
		if ( StorageManager::updateNextRun( m_nextRunNumber + 1 ) ) {
			ERROR("Failed to Start Run " << m_nextRunNumber
				<< " - Couldn't Update Next Run on Disk...!");
			// Failed to Update "Next Run Number" on Disk...!
			m_summaryOther = false;
			m_reasonBase = "Not Starting Run";
			m_reasonOther = "Unable to Increment Run Number";
			// Update Overall Summary and Reason
			// - "false": Don't Set Base Reason (we just set it :-)
			// - "true": Do Log Status as Error
			// - "true": Major Error
			setSummaryReason( false, true, true );
			return false;
		}

		// We've Updated the Run Number on disk,
		// so if we Fail Now, we need to Fail Big...
		m_currentRunNumber = m_nextRunNumber++;

		INFO("Starting Run " << m_currentRunNumber
			<< " at ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		// No More RunInfo Locking, Allow Changes Mid-Run...! ;-D
		// m_runInfo->lock();

		m_runInfo->setRunNumber( m_currentRunNumber );

		// Reset the Overall Monitor bookkeeping...
		m_allMonitors.clear();

		// Reset All DataSource Packet Statistics...
		resetPacketStats();

		// Retry *3* Times for Any Transient Failures...
		// (Then "Fail Big" and Set Summary Alarm Severity...)

		uint32_t try_count = 0;
		bool runStarted = false;

		while ( !runStarted && try_count++ < 3 ) {

			try {
				// Let our Marker Control code have a shot at
				// fixing up current state before we start recording
				// in a new container.
				m_markers->beforeNewRun( m_currentRunNumber,
					ts ); // Wallclock Time...!

				// Actually Start a New Recording...
				StorageManager::startRecording( (*ts), // Wallclock Time
					m_currentRunNumber, m_runInfo->getPropId() );
			}
			// Logic Error...! ;-O
			catch ( std::logic_error e ) {
				ERROR("Failed to Start Run " << m_currentRunNumber
					<< " - LOGIC Error! (" << e.what() << ")");
				// Run Didn't Start, Clear Run Number
				// and "Unlock" RunInfo for PV Updates...
				m_currentRunNumber = 0;
				m_runInfo->setRunNumber(0);
				// No More RunInfo Locking, Allow Changes Mid-Run...! ;-D
				// m_runInfo->unlock();
				// LOGIC Exception Starting Run...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Start Recording";
				m_reasonOther = "LOGIC Error: " + std::string( e.what() );
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - "true": Major Error
				setSummaryReason( false, true, true );
				// "Logic Error" Means Something Bad Happened, Just Bail...
				return false;
			}
			// RunTime Error... ;-b
			catch ( std::runtime_error e ) {
				ERROR("Transient RunTime Error Trying to Start Run "
					<< m_currentRunNumber
					<< " - " << e.what() << ", retry_count=" << try_count);
				// Run-Time Exception Starting Run...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Start Recording";
				m_reasonOther = "RunTime Error: "
					+ std::string( e.what() );
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - (false): Minor Error
				setSummaryReason( false, true );
				// Run Didn't Start, But Maybe Try Again... ;-b
				sleep(3);
				continue;
			}
			// Unknown Exception... ;-Q
			catch ( ... ) {
				ERROR("Transient Unknown Error Trying to Start Run "
					<< m_currentRunNumber
					<< " - retry_count=" << try_count);
				// Unknown Exception Starting Run...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Start Recording";
				m_reasonOther = "UNKNOWN Exception";
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - (false): Minor Error
				setSummaryReason( false, true );
				// Run Didn't Start, But Maybe Try Again... ;-Q
				sleep(3);
				continue;
			}

			// It Worked...! ;-D
			runStarted = true;
		}

		// It Didn't Work... ;-Q
		if ( !runStarted ) {
			ERROR("Run " << m_currentRunNumber << " Failed to Start!"
				<< " (retry_count=" << try_count << ")");
			// Run Didn't Start, Clear Run Number
			// and "Unlock" RunInfo for PV Updates...
			m_currentRunNumber = 0;
			m_runInfo->setRunNumber(0);
			// No More RunInfo Locking, Allow Changes Mid-Run...! ;-D
			// m_runInfo->unlock();
			// Update Overall Summary and Reason [Set Severity/Alarm!]
			// - "false": Don't Set Base Reason (we just set it :-)
			// - "true": Do Log Status as Error
			// - "true": Major Error
			setSummaryReason( false, true, true );
			return false;
		}

		// Run Started, Update Run Number and Recording PV Values...
		m_pvRunNumber->update(m_currentRunNumber, ts);
		m_pvRecording->update(v, ts);
	}

	// Stop the Current Recording...
	else {

		// Stopping Run, Clear Run Number
		// and "Unlock" RunInfo for PV Updates...
		INFO("Stopping Run " << m_currentRunNumber
			<< " at ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		uint32_t save_current_run_number = m_currentRunNumber;
		m_currentRunNumber = 0;
		m_runInfo->setRunNumber(0);

		// No More RunInfo Locking, Allow Changes Mid-Run...! ;-D
		// m_runInfo->unlock();

		// Retry *3* Times for Any Transient Failures...
		// (Then "Fail Big" and Set Summary Alarm Severity...)

		uint32_t try_count = 0;
		bool runStopped = false;

		while ( !runStopped && try_count++ < 3 ) {

			try {

				// Let our Marker Control code have a shot at
				// fixing up current state before we stop recording.
				m_markers->runStop( ts ); // Wallclock Time...!

				// Actually Stop Recording...
				StorageManager::stopRecording( (*ts) ); // Wallclock Time
			}
			// Logic Error...! ;-O
			catch ( std::logic_error e ) {
				ERROR("Failed to Stop Run " << save_current_run_number
					<< " - LOGIC Error! (" << e.what() << ")");
				// Run Failed to Stop...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Stop Recording";
				m_reasonOther = "LOGIC Error: " + std::string( e.what() );
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - "true": Major Error
				setSummaryReason( false, true, true );
				// "Logic Error" Means Something Bad Happened, Just Bail...
				return false;
			}
			// RunTime Error... ;-b
			catch ( std::runtime_error e ) {
				ERROR("Transient RunTime Error Trying to Stop Run "
					<< save_current_run_number
					<< " - " << e.what() << ", retry_count=" << try_count);
				// Run Failed to Stop...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Stop Recording";
				m_reasonOther = "RunTime Error: "
					+ std::string( e.what() );
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - (false): Minor Error
				setSummaryReason( false, true );
				// Run Didn't Stop, But Maybe Try Again... ;-b
				sleep(3);
				continue;
			}
			// Unknown Exception... ;-Q
			catch ( ... ) {
				ERROR("Transient Unknown Error Trying to Start Run "
					<< save_current_run_number
					<< " - retry_count=" << try_count);
				// Run Failed to Stop...!
				m_summaryOther = false;
				m_reasonBase = "Unable to Stop Recording";
				m_reasonOther = "UNKNOWN Exception";
				// Update Overall Summary and Reason
				// - "false": Don't Set Base Reason (we just set it :-)
				// - "true": Do Log Status as Error
				// - (false): Minor Error
				setSummaryReason( false, true );
				// Run Didn't Start, But Maybe Try Again... ;-Q
				sleep(3);
				continue;
			}

			// It Worked...! ;-D
			runStopped = true;
		}

		// It Didn't Work... ;-Q
		if ( !runStopped ) {
			ERROR("Run " << save_current_run_number << " Failed to Stop!"
				<< " (retry_count=" << try_count << ")");
			// Update Overall Summary and Reason [Set Severity/Alarm!]
			// - "false": Don't Set Base Reason (we just set it :-)
			// - "true": Do Log Status as Error
			// - "true": Major Error
			setSummaryReason( false, true, true );
			return false;
		}

		// Run Stopped, Update Run Number and Recording PV Values...
		m_pvRunNumber->update(0, ts);
		m_pvRecording->update(v, ts);
	}

	// Save Recording State
	m_recording = v;

	// Clear "Other" System Status and Reason, "Everything Worked"...! ;-D
	m_summaryOther = true;
	m_reasonOther = "";

	// Update Overall Summary and Reason
	// - "true": Set Base Reason from Status
	// - "false": Log Status as Info
	setSummaryReason( true, false );

	return true;
}

bool SMSControl::getRecording(void)
{
	return m_recording;
}

void SMSControl::pauseRecording( struct timespec *ts ) // Wallclock Time
{
	if ( m_currentRunNumber )
		INFO("Pausing Run " << m_currentRunNumber
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
	else
		INFO("Pausing Data Collection (Not Currently Recording)"
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

	StorageManager::pauseRecording( ts ); // Wallclock Time...!
}

void SMSControl::resumeRecording( struct timespec *ts ) // Wallclock Time
{
	if ( m_currentRunNumber )
		INFO("Resuming Run " << m_currentRunNumber
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
	else
		INFO("Resuming Data Collection (Not Currently Recording)"
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

	StorageManager::resumeRecording( ts ); // Wallclock Time...!
}

void SMSControl::externalRunControl( struct timespec *ts,
		uint32_t scanIndex, std::string command )
{
	std::stringstream ss;
	ss << " Command=[" << command << "]"
		<< " scanIndex=" << scanIndex
		<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
		<< "." << std::setfill('0') << std::setw(9)
		<< ts->tv_nsec;

	// Parse & Execute Various Commands...

	bool status;

	// Start Run
	if ( !command.compare("Start Run") )
	{
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "SMSControl::externalRunControl():"
			<< " External RunControl \"Run Start\" Command Received,"
			<< ss.str() );

		status = setRecording( true, ts ); // Wallclock Time...!

		if ( !status )
		{
			ERROR( ( m_recording ? "[RECORDING] " : "" )
				<< "SMSControl::externalRunControl():"
				<< " External RunControl \"Run Start\" Command Failed!"
				<< ss.str() );
		}
	}

	// Stop Run
	else if ( !command.compare("Stop Run") )
	{
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "SMSControl::externalRunControl():"
			<< " External RunControl \"Run Stop\" Command Received,"
			<< ss.str() );

		status = setRecording( false, ts ); // Wallclock Time...!

		if ( !status )
		{
			ERROR( ( m_recording ? "[RECORDING] " : "" )
				<< "SMSControl::externalRunControl():"
				<< " External RunControl \"Run Stop\" Command Failed!"
				<< ss.str() );
		}
	}

	// Unknown Command (or Just a System Annotation Comment... :-)
	else
	{
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "SMSControl::externalRunControl():"
			<< " Unknown External RunControl Command"
			<< " (Or System Comment) Received - Ignoring"
			<< ss.str() );
	}
}

// Update RunInfo Validity for Run Control...
// (Called by RunInfo class on PV Value Update...)
void SMSControl::updateValidRunInfo( bool isValid, std::string why,
		bool changedValid )
{
	m_summaryRunInfo = isValid;
	m_reasonRunInfo = why;

	// Update Summary/Reason...
	// - "true": Do Set Base Reason
	// - changedValid: Log Status as Error (true) or Info (false)
	// - "true": Any Errors Major
	setSummaryReason( true, changedValid, true );
}

// Update DataSource Connectivity for Run Control...
// (Called by DataSource class on Connection Status Update...)
void SMSControl::updateDataSourceConnectivity(void)
{
	std::string why;

	m_summaryDataSources = checkRequiredDataSources( why );
	m_reasonDataSources = why;

	// Update Summary/Reason...
	// - "true": Do Set Base Reason
	// - "true": Do Log Status as Error
	// - "true": Any Errors Major
	setSummaryReason( true, true, true );
}

// Check the Connection Status of Required DataSources...
bool SMSControl::checkRequiredDataSources( std::string & why )
{
	std::stringstream why_ss;

	uint32_t connected = 0;

	bool okToGo = true;

	for ( uint32_t i=0 ; i < m_dataSources.size() ; i++ )
	{
		if ( m_dataSources[i]->isRequired() )
		{
			if ( !(m_dataSources[i]->isConnected()) )
			{
				if ( okToGo ) {
					why_ss << "Required DataSources are Missing:";
				}
				else {
					why_ss << ",";
				}

				why_ss << " " << m_dataSources[i]->name()
					<< " is NOT Connected";

				okToGo = false;
			}
			else
				connected++;
		}
		else if ( m_dataSources[i]->isConnected() )
			connected++;
	}

	// Sneaky Trigger for Stacked Storage Container Cleanup...
	// - Among Other Things, This Gets Called When A Data Source Goes Down
	// If We (Now) Don't Have _Any_ Connected Data Sources,
	// Then Now Would Be A Good Time to Clean Up
	// Any Old Stacked Storage Containers... ;-D
	// (Since They Can't "Time Out" When There's No Data Flowing... :-)
	// Note: This is Also Necessary for the ADARA Test Harness to Work!
	if ( !connected )
		StorageManager::cleanContainerStack();

	// Update Connected DataSource Count...
	m_numConnectedDataSources = connected;

	if ( okToGo )
		why_ss << "All Required DataSources are Present";

	why = why_ss.str();

	return( okToGo );
}

void SMSControl::setSummaryReason( bool setBase, bool changedValid,
		bool major )
{
	// Set Overall Summary Status ("Is Error", Negative Logic...!)
	// from Individual System Component Status Values...

	m_summaryIsError =
		( !m_summaryRunInfo )
			|| ( !m_summaryDataSources )
			|| ( !m_summaryOther );

	// Update the Base Reason String, As Desired...

	if ( setBase ) {
		if ( m_summaryIsError ) {
			m_reasonBase = "System NOT Ready";
		}
		else {
			m_reasonBase = "System Ready";
		}
	}

	// Combine Individual System Component "Status Reason" Strings
	// into One Overall Summary Status Reason...

	m_reason = m_reasonBase + "; "
		+ m_reasonRunInfo + "; "
		+ m_reasonDataSources
		+ ( ( m_reasonOther.empty() )
			? "" : ( "; " + m_reasonOther ) );

	// Log Reason, Hard if the Valid Status Changed, else just "Info"...

	if ( changedValid ) {
		ERROR( m_reason );
	}
	else {
		INFO( m_reason );
	}

	// Update the Summary and Reason PV Values...

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	m_pvSummary->update( m_summaryIsError, major, &now );
	m_pvSummaryReason->update( m_reason, &now );
}

uint32_t SMSControl::registerEventSource(uint32_t srcId, uint32_t hwId)
{
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "registerEventSource(): srcId=" << srcId << " hwId=" << hwId);

	/* We're called when a data source discovers a new hardware
	 * source id and needs to allocate a bit position for completing
	 * pulses. We don't have to be terribly fast here.
	 */
	size_t i, max = m_eventSources.size();
	for (i = 0; i < max; i++) {
		// Found an Available Event Source Index...
		if ( !m_eventSources[i] ) {
			// Reset "No Registered Event Sources Flags (We Got One! :-)
			if ( m_noRegisteredEventSources ) {
				if ( m_noRegisteredEventSourcesCount ) {
					// Make Sure We Log/Notify This State Change...
					ERROR( ( m_recording ? "[RECORDING] " : "" )
						<< "registerEventSource():"
						<< " First New Event Source Registered!"
						<< " Resetting No Registered Event Source Count "
						<< m_noRegisteredEventSourcesCount << " -> 0");
					m_noRegisteredEventSourcesCount = 0;
				}
				// Gently Log the State Change if it doesn't really matter.
				else {
					DEBUG("registerEventSource():"
						<< " First New Event Source Registered,"
						<< " No Registered Event Source Count Still = "
						<< m_noRegisteredEventSourcesCount);
				}
				m_noRegisteredEventSources = false;
			}
			// Set New Event Source Bit & DataSource Reverse Lookup Index
			m_eventSources.set(i);
			m_eventSourcesIndex[i] = srcId - 1; // m_nextSrcId Starts at 1
			// Return Event Source Index as "smsId"...
			DEBUG("registerEventSource(): returning smsId=" << i);
			return i;
		}
	}

	// Oops... Out of Event Source Indices...?! Wow, Lotta Data Sources?!!
	DEBUG("registerEventSource(): Out of Event Source (smsIds)!");
	throw std::runtime_error("No more event sources available");
}

void SMSControl::unregisterEventSource(uint32_t srcId, uint32_t smsId)
{
	// Safety Check... ;-b
	if ( smsId == (uint32_t) -1 ) {
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource(): INVALID SMS ID SNUCK THRU:"
			<< " srcId=" << srcId << " smsId=" << smsId
			<< " - Skipping Unregister..." );
		return;
	}
	else {
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource():"
			<< " srcId=" << srcId << " smsId=" << smsId );
	}

	PulseMap::iterator it, last, last_minus_buffer, last_recorded;

	/* Walk the pending pulses and mark them incomplete if they are still
	 * waiting for data from this source, as it's not going to come. Keep
	 * track of the last pulse that is completed, whether by this process
	 * or via markCompleted() but left in queue due to No-EoP Buffering.
	 */
	int marked_partial = 0;
	int now_complete = 0;
	last = m_pulses.end();
	for ( it = m_pulses.begin(); it != m_pulses.end(); it++ ) {
		// Release Now-Partial Pulses...
		if ( it->second->m_pending[smsId] ) {
			it->second->m_flags |= ADARA::PARTIAL_DATA;
			it->second->m_pending.reset(smsId);
			marked_partial++;
		}
		// Note the Last Now-Completed (Partial or Not) Pulse for Handling
		if ( it->second->m_pending.none() ) {
			last = it;
			now_complete++;
		}
	}
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "unregisterEventSource():"
		<< " Internal Pulse Buffer Length = " << m_pulses.size()
		<< " marked_partial=" << marked_partial
		<< " now_complete=" << now_complete );

	if ( last != m_pulses.end() ) {

		/* Ok, we had at least one pulse completed by the recently
		 * departed source; all pulses previous to that one must be
		 * complete now as well, as the monotonically increasing
		 * pulse ids indicate that they were duplicate pulses.
		 *
		 * Addendum 7/2014: for some legacy dcomserver implementations,
		 * the neutron events and meta-data events can interleave and/or
		 * arrive "out of order", so we can optionally enforce a
		 * fixed-number-of-pulse buffering, to ensure complete pulses.
		 * (Note: this could leave some partial pulse data hanging here.)
		 */

		// Get Latest "No End-of-Pulse Buffer Size" Value from PV...
		m_noEoPPulseBufferSize = m_pvNoEoPPulseBufferSize->value();

		last_minus_buffer = last;
		uint32_t recorded = 0;
		uint32_t pcnt = 0;

		// Skip Past Any Buffering Level, Unless We're the Last to Unreg
		// - Use the Number of Registered Event Sources to Decide,
		// Including the One We are Unregistering!
		// (Any Other Remaining Sources Could Still Spew Out-of-Order...)
		if ( m_eventSources.count() > 1 ) {
			DEBUG("unregisterEventSource():"
				<< " Buffering " << m_noEoPPulseBufferSize << " Pulses,"
				<< " Last Pulse = 0x"
				<< std::hex << last_minus_buffer->first.first << std::dec);
			// Work Backward from *End of Buffer*...
			// (_Not_ the Last Complete Pulse!)
			PulseMap::reverse_iterator rit = m_pulses.rbegin();
			while ( pcnt++ < m_noEoPPulseBufferSize ) {
				// End of the Line, Not Enough Buffered Pulses to Proceed
				if ( rit != m_pulses.rend() ) {
					// Skip Over Pulse to Satisfy Buffering Requirement
					// (Slide "Last Pulse" Iterator with Reverse Iterator,
					// *If* It Catches Up... ;-)
					if ( last_minus_buffer->first == rit->first ) {
						if ( last_minus_buffer != m_pulses.begin() )
							last_minus_buffer--;
						else {
							DEBUG("unregisterEventSource():"
								<< " Ran Out of Pulses to Buffer - Done");
							/* D-Oh Before Returning, Mark Id for Re-Use */
							goto done;
						}
					}
				}
				// No Pulses to Record Yet, Buffer Not "Full" Enough...
				else {
					DEBUG("unregisterEventSource():"
						<< " No Pulses to Record, Buffer Not Full - Done");
					/* D-Oh... Before Returning, Mark This Id for Re-Use */
					goto done;
				}
				// Keep Counting...
				rit++;
			}
		}
		else {
			DEBUG("unregisterEventSource():"
				<< " Skip Buffering (" << m_noEoPPulseBufferSize << "),"
				<< " Last Event Source!");
		}

		// Record Complete/Partial Pulses Past the Buffering Threshold.
		// Note: We _Might_ Be Recording Some As-Yet-Still-Pending
		// Pulses Here, if there are some Unresolved Pulses still
		// Interlaced amidst the Pulses Being Released by the
		// Now-Unregistered Data Source...
		// But That is *OK*, Because Anything Older Than *This*
		// "Last Completed Pulse" Probably _Should_ have Already
		// been Marked Complete; the Pulses are Insertion-Sorted in
		// Pulse Time Order, so if This Pulse is Complete, then
		// All Preceding Pulses Should Also Be...!
		// (Even Any "SAWTOOTH" Pulses that arrived Out of Time Order, as
		// These should be Correctly Handled by Sufficient Buffer Size!)

		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource(): Recording Pulses"
			<< std::hex << " 0x" << m_pulses.begin()->first.first
			<< " up to 0x" << last_minus_buffer->first.first << std::dec );

		// Include "Last Complete Pulse" (With Any Buffering Adjustments)
		last_minus_buffer++;

		// Get Latest Pulse Charge and Veto Corrections Settings from PV...
		m_doPulsePchgCorrect = m_pvDoPulsePchgCorrect->value();
		m_doPulseVetoCorrect = m_pvDoPulseVetoCorrect->value();

		for ( it = m_pulses.begin(); it != last_minus_buffer; it++ ) {
			if ( m_doPulsePchgCorrect || m_doPulseVetoCorrect ) {
				PulseMap::iterator next = it;
				if (++next != m_pulses.end())
					correctPChargeVeto(it->second, next->second);
				else {
					/* Rate-limited logging of no more pulses */
					std::string log_info;
					if ( RateLimitedLogging::checkLog(
							RLLHistory_SMSControl,
							RLL_PULSE_PCHG_BUFFER_EMPTY, "none",
							2, 10, 100, log_info ) ) {
						ERROR(log_info
							<< ( m_recording ? "[RECORDING] " : "" )
							<< "unregisterEventSource:"
							<< " No More Pulses for"
							<< " Proton Charge/Veto Flags Correction! 0x"
							<< std::hex << it->first.first << std::dec);
					}
				}
			}
			recordPulse(it->second);
			last_recorded = it;
			recorded++;
			// Reset Any "Last Pulse" ID for Fast Lookup...
			if ( it->first == m_lastPid ) {
				m_lastPid = PulseIdentifier(-1,-1);
				//DEBUG("*** Freeing 'Last Pulse', Reset Last Pulse Id"
					//<< std::hex << " 0x" << it->first.first << std::dec);
			}
		}

		// Erase Any Now-Recorded Pulses
		if (recorded) {
			m_pulses.erase(m_pulses.begin(), ++last_recorded);
		}

		// Log the Size of the Remaining Internal Pulse Buffer...
		// (Rest of the List We _Didn't_ Just Process...)
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "Remaining Internal Pulse Buffer Length = "
			<< m_pulses.size()
			<< " (recorded=" << recorded << ")" );
	}

done:

	// Mark This Id for Re-Use...
	m_eventSources.reset(smsId);

	// (Check &) Reset DataSource Reverse Lookup Index...
	if ( m_eventSourcesIndex[smsId] != srcId - 1 ) {
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource:"
			<< " Mismatch in DataSource Reverse Lookup Index!"
			<< " m_eventSourcesIndex[" << smsId << "]="
			<< m_eventSourcesIndex[smsId]
			<< " != (srcId-1)=" << (srcId - 1) );
	}
	else if ( srcId == (uint32_t) -1 ) {
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource:"
			<< " Bad DataSource srcId=" << srcId
			<< " (DataSource Reverse Lookup Index"
			<< " m_eventSourcesIndex[" << smsId << "]="
			<< m_eventSourcesIndex[smsId] << ")" );
	}
	m_eventSourcesIndex[smsId] = (uint32_t) -1;

	// Check for "No Registered Event Sources" State Change...
	if ( m_eventSources.none() ) {

		// Log/Notify on State Change...
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "unregisterEventSource():"
			<< " Last Event Source Unregistered!"
			<< " Resetting No Registered Event Sources Count to 0.");
		m_noRegisteredEventSources = true;
		m_noRegisteredEventSourcesCount = 0;
	}
}

void SMSControl::popPulseBuffer(int32_t pulse_index)
{
	PulseMap::iterator it;

	if ( m_pulses.size() == 0 ) {
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "popPulseBuffer: Empty Pulse Buffer, No Pulses to Pop!"
			<< " pulse_index=" << pulse_index);
		return;
	}

	std::string isLast = "";

	// Pop "Last" Pulse...
	if ( pulse_index < 0 ) {
		if ( ((uint32_t) -pulse_index) > m_pulses.size() ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Pop Last - Pulse Index Out of Bounds!"
				<< " pulse_index=" << pulse_index
				<< " size=" << m_pulses.size());
			return;
		}
		it = m_pulses.end();
		it--;
		int32_t pindex = -1;
		while ( pindex > pulse_index && it != m_pulses.begin() ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Skipping Last Pulse pindex=" << pindex
				<< std::hex << " 0x" << it->first.first << std::dec);
			pindex--; it--;
		}
		if ( pindex > pulse_index ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Last Pulse Not Found in Buffer"
				<< " size=" << m_pulses.size());
			return;
		}
		isLast = "Last ";
	}

	// Pop Pulse of Specific Index...
	else {
		if ( ((uint32_t) pulse_index) > m_pulses.size() ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Pop Index - Pulse Index Out of Bounds!"
				<< " pulse_index=" << pulse_index
				<< " size=" << m_pulses.size());
			return;
		}
		it = m_pulses.begin();
		int32_t pindex = 1;
		while ( pindex < pulse_index && it != m_pulses.end() ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Skipping Pulse pindex=" << pindex
				<< std::hex << " 0x" << it->first.first << std::dec);
			pindex++; it++;
		}
		if ( pindex < pulse_index ) {
			DEBUG( ( m_recording ? "[RECORDING] " : "" )
				<< "popPulseBuffer: Pulse Not Found in Buffer"
				<< " size=" << m_pulses.size());
			return;
		}
	}

	// Reset Any "Last Pulse" ID for Fast Lookup...
	if ( it->first == m_lastPid ) {
		m_lastPid = PulseIdentifier(-1,-1);
		//DEBUG("*** Freeing 'Last Pulse', Reset Last Pulse Id"
			//<< std::hex << " 0x" << it->first.first << std::dec);
	}

	// Pop Given Pulse from Buffer...
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "popPulseBuffer: Popping " << isLast << "Pulse "
		<< " pulse_index=" << pulse_index
		<< std::hex << " 0x" << it->first.first << std::dec);
	m_pulses.erase(it);
}

SMSControl::PulseMap::iterator SMSControl::getPulse(
		uint64_t id, uint32_t dup)
{
	static uint32_t cnt = 0;

	PulseIdentifier pid(id, dup);
	PulseMap::iterator it;

	// Same as Last Pulse...? Save the Lookup Time... ;-D
	if ( pid == m_lastPid ) {
		return m_lastPulseIt;
	}

	// Existing Pulse...?
	it = m_pulses.find( pid );
	if ( it != m_pulses.end() ) {
		// Save Last Pulse & Iterator...
		m_lastPid = pid;
		m_lastPulseIt = it;
		return it;
	}

	// Infrequently Check Live Control PV for
	// Ignore-Interleaved-Global-SAWTOOTH Flag...
	// (Once Per Minute...)
	if ( !(++cnt % 3600) ) {
		bool tmp = m_pvIgnoreInterleavedSawtooth->value();
		if ( tmp != m_ignoreInterleavedSawtooth ) {
			m_ignoreInterleavedSawtooth = tmp;
			DEBUG("getPulse(): Setting IgnoreInterleavedSawtooth to "
				<< m_ignoreInterleavedSawtooth);
		}
	}

	// New Pulse...! If Any Pulses in list, Check for SAWTOOTH...
	// Note: Map is _Already_ Sorted by Pulse Time (PulseIdentifier),
	// so we can just grab Min and Max Time from the "ends" of the list
	// to check for any SAWTOOTH Pulses... ;-b
	if ( m_pulses.begin() != m_pulses.end() ) {

		// Get Min Pulse Time
		it = m_pulses.begin();
		uint64_t min_id = it->first.first;

		// Get Max Pulse Time
		it = m_pulses.end(); it--;
		uint64_t max_id = it->first.first;

		// Log any SAWTOOTH pulses... :-o
		if ( id < min_id ) {
			/* Rate-limited logging of Global SAWTOOTH Pulse */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_GLOBAL_SAWTOOTH_PULSE, "none",
					2, 10, 1000, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "getPulse: Global SAWTOOTH Pulse(0x"
					<< std::hex << id << ", 0x" << dup << ")"
					<< " min=0x" << min_id
					<< " max=0x" << max_id << std::dec);
			}
		}
		else if ( !m_ignoreInterleavedSawtooth
				&& id >= min_id && id < max_id ) {
			/* Rate-limited logging of Interleaved Global SAWTOOTH Pulse */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_INTERLEAVED_GLOBAL_SAWTOOTH, "none",
					2, 10, 200, log_info ) ) {
				WARN(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "getPulse: Interleaved Global SAWTOOTH Pulse(0x"
					<< std::hex << id << ", 0x" << dup << ")"
					<< " min=0x" << min_id
					<< " max=0x" << max_id << std::dec);
			}
		}
		m_lastPulseId = max_id;
	}

	// No Pulses in list, Just Compare Against "Last Pulse"...
	else {
		if ( id < m_lastPulseId ) {
			/* Rate-limited logging of Global SAWTOOTH Pulse */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_GLOBAL_SAWTOOTH_LAST, "none",
					2, 10, 1000, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "getPulse: Global SAWTOOTH Pulse(0x"
					<< std::hex << id << ", 0x" << dup << ")"
					<< " versus Last Pulse id=0x" << m_lastPulseId
					<< std::dec);
			}
		}
		m_lastPulseId = id;
	}

	// Create New Pulse...!
	PulsePtr new_pulse(new Pulse(pid, m_eventSources));

	// By Default, All New Pulses Have Yet to Have Their
	// Proton Charge or Veto Flags Corrected...
	// (Each Proton Charge/Veto Flag Usually Refers to *Previous* Pulse!)
	new_pulse->m_flags |= ADARA::PCHARGE_UNCORRECTED;
	new_pulse->m_flags |= ADARA::VETO_UNCORRECTED;

	if (dup)
		new_pulse->m_flags |= ADARA::DUPLICATE_PULSE;

	// [LESS FREQUENTLY] Check for Internal Pulse Buffer Size Overflow...
	// (Happens when One or More of Multiple Event Sources suddenly
	// Stop Sending Event Data, and we Keep Waiting, Hanging Onto Pulses
	// so they can All be Marked "Complete" Across All Data Sources... ;-)
	// - When This Happens, Regardless of the Cause, We Need to Flush
	// Some Pulses from Our Internal Pulse Buffer, so we Don't Swell Up
	// and Pop...! ;-D

	// Once Per Second...
	uint32_t freq = 60;

	// ("cnt" already incremented above... :-)
	if ( !(cnt % freq) )
	{
		// Infrequently Check Live Control PV for
		// Max Pulse Buffer Size...
		// (Once Per Minute...)
		// ("cnt" already incremented above... :-)
		if ( !(cnt % 3600) ) {
			uint32_t tmp = m_pvMaxPulseBufferSize->value();
			if ( tmp != m_maxPulseBufferSize ) {
				m_maxPulseBufferSize = tmp;
				DEBUG("getPulse(): Setting MaxPulseBufferSize to "
					<< m_maxPulseBufferSize);
			}
		}

		// Now, *Before* Inserting New Pulse, Check Max Pulse Buffer Size!
		// (Give the "New Guy" a chance to exist before we dump it... ;-D)
		if ( m_pulses.size() > m_maxPulseBufferSize ) {

			// Record & Free Just Enough Pulses as Required to Stay Under
			// the Max Internal Pulse Buffer Size...!
			// (Even after adding the next one, already in hand...!)
			uint32_t num_to_record =
				m_pulses.size() - m_maxPulseBufferSize + 1;

			// Get Latest Pulse Charge/Veto Corrections Settings from PV...
			m_doPulsePchgCorrect = m_pvDoPulsePchgCorrect->value();
			m_doPulseVetoCorrect = m_pvDoPulseVetoCorrect->value();

			// Record Complete/Partial Pulses Past the Buffering Threshold
			PulseMap::iterator last_recorded;
			uint32_t recorded = 0;
			it = m_pulses.begin();
			for ( uint32_t i=0 ;
					i < num_to_record && it != m_pulses.end() ; i++ ) {
				// Pulse will Never be Made Complete, Mark as Partial...
				if (it->second->m_pending.any()) {
					it->second->m_flags |=
						ADARA::PARTIAL_DATA;
				}
				// Correct Proton Charge...
				// (This Pulse's PCharge comes from _Next_ Pulse...!)
				if ( m_doPulsePchgCorrect || m_doPulseVetoCorrect ) {
					PulseMap::iterator next = it;
					if (++next != m_pulses.end())
						correctPChargeVeto(it->second, next->second);
					else {
						/* Rate-limited logging of no more pulses */
						std::string log_info;
						if ( RateLimitedLogging::checkLog(
								RLLHistory_SMSControl,
								RLL_PULSE_PCHG_BUFFER_EMPTY, "none",
								2, 10, 100, log_info ) ) {
							ERROR(log_info
								<< ( m_recording ? "[RECORDING] " : "" )
								<< "getPulse:"
								<< " No More Pulses for"
								<< " Proton Charge/Veto Flags Correction!"
								<< " 0x" << std::hex << it->first.first
								<< std::dec);
						}
					}
				}
				// Record Pulse to Reduce Overflow Buffer Size...
				recordPulse(it->second);
				last_recorded = it++;
				recorded++;
			}

			// Log What the Pulse Map Size Will Be _After_ Freeing
			// these Recorded Pulses...
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_PULSE_BUFFER_OVERFLOW, "none",
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "*** Internal Pulse Buffer Overflow!"
					<< " Length = " << ( m_pulses.size() - recorded )
					<< " (recorded=" << recorded << ")"
					<< std::hex
					<< " (not counting new pulse 0x" << id << ")"
					<< " - Recorded Pulses"
					<< " 0x" << m_pulses.begin()->first.first
					<< " up to 0x" << last_recorded->first.first
					<< std::dec);
			}

			// Erase Any Now-Recorded Pulses
			if (recorded) {
				m_pulses.erase(m_pulses.begin(), ++last_recorded);
			}
		}
	}

	// *NOW* Record the New Pulse... ;-D
	it = m_pulses.insert( std::make_pair( pid, new_pulse ) ).first;

	// Save Last Pulse & Iterator...
	m_lastPid = pid;
	m_lastPulseIt = it;

	// Return Iterator to New Pulse...
	return it;
}

void SMSControl::sourceUp( uint32_t UNUSED(srcId) )
{
	// New DataSource Connected!
	// - Update the Status of Any Required DataSources...
	updateDataSourceConnectivity();
}

void SMSControl::sourceDown( uint32_t srcId, bool stateChanged )
{
	// Reset the MetaData...
	m_meta->dropSourceTag( srcId );

	// DataSource Disconnected...!
	// - Update the Status of Any Required DataSources...
	if ( stateChanged )
		updateDataSourceConnectivity();
}

// Clear all the DataSource "Read Delay" flags...
void SMSControl::resetSourcesReadDelay(void)
{
	for (uint32_t i = 0; i < m_dataSources.size(); i++) {
		m_dataSources[i]->m_readDelay = false;
	}
}

// Set all the DataSource "Read Delay" flags, a Read Delay has Occurred...!
void SMSControl::setSourcesReadDelay(void)
{
	// Note: each DataSource will clear it's own flag on next Timeout...
	// (or the next read() will automatically reset every source's flag)
	for (uint32_t i = 0; i < m_dataSources.size(); i++) {
		m_dataSources[i]->m_readDelay = true;
	}

	// Dump Internal Pulse Buffer Size/Info...
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
			RLL_SET_SOURCES_READ_DELAY, "none",
			60, 10, 30, log_info ) ) {
		if ( m_pulses.begin() != m_pulses.end() ) {
			DEBUG(log_info
				<< ( m_recording ? "[RECORDING] " : "" )
				<< "Internal Pulse Buffer " << std::hex
				<< " from 0x" << m_pulses.begin()->first.first
				<< " to 0x" << m_pulses.rbegin()->first.first
				<< std::dec
				<< ", Internal Pulse Buffer Length = "
				<< m_pulses.size() );
		} else {
			DEBUG(log_info
				<< ( m_recording ? "[RECORDING] " : "" )
				<< "Internal Pulse Buffer is Empty"
				<< ", Internal Pulse Buffer Length = "
				<< m_pulses.size() );
		}
	}
}

// Clear All Packet Statistics...
// (use any DataSource, pick the "first"... :-)
void SMSControl::resetPacketStats(void)
{
	if (m_dataSources.size() > 0) {
		m_dataSources[0]->resetPacketStats();
	}
}

void SMSControl::updateMaxDataSourceTime( uint32_t srcId,
		struct timespec *ts ) // Wallclock Time...!
{
	if ( m_verbose > 1 )
	{
		DEBUG("updateMaxDataSourceTime(): srcId=" << srcId
			<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
	}

	if ( srcId >= m_dataSourcesMaxTimes.size() )
	{
		ERROR("updateMaxDataSourceTime():"
			<< " Invalid DataSource srcId=" << srcId
			<< ", Out of Range"
			<< " m_dataSourcesMaxTimes.size()="
			<< m_dataSourcesMaxTimes.size()
			<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		return;
	}

	// Set Max Time for This DataSource
	m_dataSourcesMaxTimes[ srcId ] = *ts; // Wallclock Time...!

	// Reset DataSource Max Time...
	if ( ts->tv_sec == 0 && ts->tv_nsec == 0 )
	{
		ERROR("updateMaxDataSourceTime():"
			<< " Reset DataSource Max Time srcId=" << srcId
			<< " m_dataSourcesMaxTimes[" << srcId << "]="
			<< m_dataSourcesMaxTimes[ srcId ].tv_sec
			<< "." << std::setfill('0') << std::setw(9)
			<< m_dataSourcesMaxTimes[ srcId ].tv_nsec);
	}

	else
	{
		// Convert Wallclock to EPICS Time...
		m_dataSourcesMaxTimes[ srcId ].tv_sec -= ADARA::EPICS_EPOCH_OFFSET;
	}

	// Update Overall Oldest Max DataSource Time...

	struct timespec oldest; // EPICS Time...!
	oldest.tv_sec = 0;
	oldest.tv_nsec = 0;

	struct timespec newest; // EPICS Time...!
	newest.tv_sec = 0;
	newest.tv_nsec = 0;

	for ( uint32_t i=0 ; i < m_dataSourcesMaxTimes.size() ; i++ )
	{
		if ( m_verbose > 1 )
		{
			DEBUG("updateMaxDataSourceTime():"
				<< " m_dataSourcesMaxTimes[" << i << "]="
				<< m_dataSourcesMaxTimes[i].tv_sec
				<< "." << std::setfill('0') << std::setw(9)
				<< m_dataSourcesMaxTimes[i].tv_nsec
				<< " oldest=" << oldest.tv_sec
				<< "." << std::setfill('0') << std::setw(9)
				<< oldest.tv_nsec
				<< " newest=" << newest.tv_sec
				<< "." << std::setfill('0') << std::setw(9)
				<< newest.tv_nsec);
		}

		if ( m_dataSourcesMaxTimes[i].tv_sec != 0
				|| m_dataSourcesMaxTimes[i].tv_nsec != 0 )
		{
			// Update "Oldest" Max DataSource Time...
			if ( oldest.tv_sec == 0 && oldest.tv_nsec == 0 )
			{
				if ( m_verbose > 2 )
					DEBUG("updateMaxDataSourceTime(): FIRST Oldest...");
				oldest = m_dataSourcesMaxTimes[i]; // EPICS Time...!
			}
			else if ( compareTimeStamps(
					m_dataSourcesMaxTimes[i], oldest ) < 0 )
			{
				if ( m_verbose > 2 )
					DEBUG("updateMaxDataSourceTime(): OLDER Oldest...");
				oldest = m_dataSourcesMaxTimes[i]; // EPICS Time...!
			}

			// Update "Newest" Max DataSource Time...
			if ( newest.tv_sec == 0 && newest.tv_nsec == 0 )
			{
				if ( m_verbose > 2 )
					DEBUG("updateMaxDataSourceTime(): FIRST Newest...");
				newest = m_dataSourcesMaxTimes[i]; // EPICS Time...!
			}
			else if ( compareTimeStamps(
					m_dataSourcesMaxTimes[i], newest ) > 0 )
			{
				if ( m_verbose > 2 )
					DEBUG("updateMaxDataSourceTime(): NEWER Newest...");
				newest = m_dataSourcesMaxTimes[i]; // EPICS Time...!
			}
		}
	}

	if ( m_verbose )
	{
		DEBUG("updateMaxDataSourceTime():"
			<< " oldest=" << oldest.tv_sec
			<< "." << std::setfill('0') << std::setw(9)
			<< oldest.tv_nsec
			<< " newest=" << newest.tv_sec
			<< "." << std::setfill('0') << std::setw(9)
			<< newest.tv_nsec);
	}

	// Save "Oldest" Max DataSource Time
	m_oldestMaxDataSourceTime = oldest; // EPICS Time...!

	// Save "Newest" Max DataSource Time
	m_newestMaxDataSourceTime = newest; // EPICS Time...!
}

struct timespec &SMSControl::oldestMaxDataSourceTime(void)
{
	return( m_oldestMaxDataSourceTime ); // EPICS Time...!
}

struct timespec &SMSControl::newestMaxDataSourceTime(void)
{
	return( m_newestMaxDataSourceTime ); // EPICS Time...!
}

int32_t SMSControl::registerLiveClient(std::string clientName,
		boost::shared_ptr<smsStringPV> & pvName,
		boost::shared_ptr<smsUint32PV> & pvRequestedStartTime,
		boost::shared_ptr<smsStringPV> & pvCurrentFilePath,
		boost::shared_ptr<smsConnectedPV> & pvStatus)
{
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "registerLiveClient clientName=" << clientName);

	/* We're called when a new Live Client connects to the SMS,
	 * and needs to allocate a bit index for naming status PVs.
	 * All of this is for convenience and external monitoring only.
	 * We don't have to be terribly fast here.
	 */
	size_t i, max = m_liveClients.size();
	int32_t clientId = -1; // default, result if no free Ids remain...
	for (i = 0; i < max && clientId < 0; i++) {
		if (!m_liveClients[i]) {
			m_liveClients.set(i);
			DEBUG("registerLiveClient returning clientId=" << i);
			clientId = i;
		}
	}

	// Create/Get Persistent EPICS PVs for This Live Client Instance...
	if ( clientId >= 0 ) {

		// Allocate Next Index of PVs...
		if ( (uint32_t) clientId >= m_pvLiveClientNames.size() ) {

			std::string prefix(m_pvPrefix);
			prefix += ":LiveClient:";

			std::stringstream ss;
			ss << clientId + 1; // eh, count from 1 like everything else
			prefix += ss.str();

			// Live Client Name...
			m_pvLiveClientNames.resize(clientId + 1);
			m_pvLiveClientNames[clientId] =
				boost::shared_ptr<smsStringPV>(new
					smsStringPV(prefix + ":Name"));
			addPV(m_pvLiveClientNames[clientId]);

			// Live Client Requested Start Time...
			m_pvLiveClientStartTimes.resize(clientId + 1);
			m_pvLiveClientStartTimes[clientId] =
				boost::shared_ptr<smsUint32PV>(new
					smsUint32PV(prefix + ":RequestedStartTime"));
			addPV(m_pvLiveClientStartTimes[clientId]);

			// Live Client Current File Path...
			m_pvLiveClientFilePaths.resize(clientId + 1);
			m_pvLiveClientFilePaths[clientId] =
				boost::shared_ptr<smsStringPV>(new
					smsStringPV(prefix + ":CurrentFilePath"));
			addPV(m_pvLiveClientFilePaths[clientId]);

			// Live Client Status...
			m_pvLiveClientStatuses.resize(clientId + 1);
			m_pvLiveClientStatuses[clientId] =
				boost::shared_ptr<smsConnectedPV>(new
					smsConnectedPV(prefix + ":Status"));
			addPV(m_pvLiveClientStatuses[clientId]);

			// Update the Number of Live Clients PV... (we just added one)
			// Note: don't ever decrement Number of Live Client PVs,
			// monotonically increasing... (we leave the old Live Client
			// index/PVs around for information on their demise... ;-D)
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			m_pvNumLiveClients->update(clientId + 1, &now);
		}

		// Return Proper Indexed PVs to Live Client...
		pvName = m_pvLiveClientNames[clientId];
		pvRequestedStartTime = m_pvLiveClientStartTimes[clientId];
		pvCurrentFilePath = m_pvLiveClientFilePaths[clientId];
		pvStatus = m_pvLiveClientStatuses[clientId];
	}

	else {
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "registerLiveClient Out of Live Client Ids!");
		// *Don't* throw an exception here, this is _Not_ mission critical!
		// throw std::runtime_error("No more Live Client Ids available");
	}

	return( clientId );
}

void SMSControl::unregisterLiveClient(int32_t clientId)
{
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "unregisterLiveClient: clientId=" << clientId);

	/* Mark this id for re-use. */
	if ( clientId >= 0 )
		m_liveClients.reset(clientId);

	// Note: Leave the Number of Live Client PVs, Monotonically Increasing
	// (because we leave the old Live Client index/PVs around for
	// hysterical reasons, that is to see how they died... ;-D)
}

void SMSControl::addMonitorEvent(const ADARA::RawDataPkt &pkt,
		PulsePtr &pulse, uint32_t pixel, uint32_t tof)
{
	uint32_t monId = pixel >> 16;
	monId &= 0xff;

	MonitorMap::iterator mon = pulse->m_monitors.find(monId);
	if (mon == pulse->m_monitors.end()) {

		/* One hopes that an optimizing compiler would remove
		 * the unneeded constructions and copies...
		 */
		BeamMonitor new_mon(pkt.sourceID(), pkt.tofField());
		MonitorMap::value_type val(monId, new_mon);
		mon = pulse->m_monitors.insert(val).first;

		mon->second.m_eventTof.reserve(m_monitorReserve);

		/* Track the creation of overall collections of monitors...
		 */
		MonitorMap::iterator allMon = m_allMonitors.find(monId);
		if (allMon == m_allMonitors.end()) {
			INFO( ( m_recording ? "[RECORDING] " : "" )
				<< "New Monitor id=" << monId
				<< " (pixelId=0x" << std::hex << pixel
				<< " sourceID=0x" << pkt.sourceID()
				<< " tofField=0x" << pkt.tofField() << std::dec << ")");
			m_allMonitors.insert(val);
		}
	}

	uint32_t rising = (pixel & 1) << 31;
	tof &= m_monitorTOFMask;
	tof |= rising;

	mon->second.m_eventTof.push_back(tof);
}

void SMSControl::addChopperEvent(const ADARA::RawDataPkt &pkt,
		PulsePtr &pulse, uint32_t pixel, uint32_t tof)
{
	uint32_t cid = pixel & ~0xf0000000;
	cid >>= 16;

	if (!m_choppers.count(cid)) {
		std::string ddp, num;

		num = boost::lexical_cast<std::string>(cid);

		ddp += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		ddp += "<device "
		"xmlns=\"http://public.sns.gov/schema/device.xsd\"\n"
		"    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
		"    xsi:schemaLocation=\"http://public.sns.gov/schema/device.xsd "
		"http://public.sns.gov/schema/device.xsd\">\n";

		ddp += "  <device_name>chopper";
		ddp += num;
		ddp += "_TDC</device_name>\n";
		ddp += "  <process_variables>\n";
		ddp += "    <process_variable>\n";
		ddp += "      <pv_name>chopper";
		ddp += num;
		ddp += "_TDC_falling</pv_name>\n";
		ddp += "      <pv_id>1</pv_id>\n";
		ddp += "      <pv_description>Falling Edge of TDC signal (offset from pulse)</pv_description>\n";
		ddp += "      <pv_type>unsigned integer</pv_type>\n";
		ddp += "      <pv_units>nanoseconds</pv_units>\n";
		ddp += "    </process_variable>\n";
		ddp += "    <process_variable>\n";
		ddp += "      <pv_name>chopper";
		ddp += num;
		ddp += "_TDC</pv_name>\n";
		ddp += "      <pv_id>2</pv_id>\n";
		ddp += "      <pv_description>Rising Edge of TDC signal (offset from pulse)</pv_description>\n";
		ddp += "      <pv_type>unsigned integer</pv_type>\n";
		ddp += "      <pv_units>nanoseconds</pv_units>\n";
		ddp += "    </process_variable>\n";
		ddp += "  </process_variables>\n";
		ddp += "</device>";

		/* We currently reserve device IDs 0x80000000 for SMS internal
		 * use. We put the choppers at the low end.
		 *
		 * TODO better allocation policy for IDs.
		 */
		uint32_t cid_dev = 0x80000000 | cid;
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_meta->addFastMetaDDP(now, cid_dev, ddp);

		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "New Chopper id=" << cid
			<< " (pixelId=0x" << std::hex << pixel
			<< " sourceID=0x" << pkt.sourceID() << std::dec << ")");

		m_choppers.insert(cid);
	}

	if (pulse->m_chopperEvents[cid].empty()) {
		pulse->m_chopperEvents[cid].reserve(m_chopperReserve);
	}

	/* The time-of-flight value is in bits 0-20 of the field; mask
	 * out the frame number and unused high bit. Then we convert from
	 * a 100ns unit to pure nanoseconds.
	 *
	 * We store leading vs trailing edge in the low-order bit; we need
	 * this when emitting the packets later to generate the right
	 * variable ID.
	 *
	 * TODO we currently expect the preprocessor to frame-correct
	 * these values; if this is not the case, we will need to handle
	 * that here.
	 */
	tof &= m_chopperTOFMask;
	tof *= 100;
	tof <<= 1;
	if (pixel & 1)
		tof |= 1;

	pulse->m_chopperEvents[cid].push_back(tof);
}

void SMSControl::pulseEvents( const ADARA::RawDataPkt &pkt,
		uint32_t hwId, uint32_t dup,
		bool is_mapped, bool mixed_data_packets,
		uint32_t &event_count, uint32_t &meta_count, uint32_t &err_count )
{
	static uint32_t cnt = 0;

	PulsePtr &pulse = getPulse(pkt.pulseId(), dup)->second;

	if (!pulse->m_rtdl) {
		/* Hmm, no RTDL; save the raw packet's concept of values
		 * we'll need for the banked event packets.
		 */
		pulse->m_vetoFlags = pkt.vetoFlags();
		pulse->m_cycle = pkt.cycle();
		pulse->m_ringPeriod = m_lastRingPeriod;

		/* Note: pkt.badCycle() and pkt.badVeto() are Deprecated. */
	}

	/* Find this source in the current pulse; if it doesn't exist
	 * yet, we'll need to save the intrapulse time and TOF Offset fields.
	 */
	SourceMap::iterator src = pulse->m_pulseSources.find(hwId);
	if (src == pulse->m_pulseSources.end()) {
		/* One hopes that an optimizing compiler would remove
		 * the unneeded constructions and copies...
		 */
		EventSource new_src( pkt.intraPulseTime(), pkt.tofField() );
		// Note: "Number" of States Includes State 0...
		new_src.m_numStates = m_numStatesLast;
		new_src.m_banks_arr_size =
			new_src.m_numStates * ( m_maxBank + 1 );
		new_src.m_banks_arr = new EventVector[ new_src.m_banks_arr_size ];
		SourceMap::value_type val( hwId, new_src );
		src = pulse->m_pulseSources.insert(val).first;
	}

	/* We'll say this time and time again, but we can't use the one
	 * from the RTDL packet -- that was for the previous pulse.
	 *
	 * Note: Nope, the charge in the RawDataPkt/MappedDataPkt is
	 * "Off By One" too, for Previous Pulse, just like with the RTDL...
	 * This is now corrected in correctPChargeVeto().
	 *
	 * Also Note: By capturing the charge from RawDataPkt/MappedDataPkt,
	 * there is a subtle side benefit, which is that any Pulses with
	 * No Events will go thru _Without_ any Proton Charge (set to 0.0),
	 * which is Very Convenient for Sub-60Hz Operation, where we don't
	 * _Want_ any Proton Charge for those Pulses... ;-D  "Lucky."
	 */
	if (pulse->m_rtdl) {
		// Always Pull Proton Charge from RTDL Packet,
		// as Data Packet does _Not_ Always Provide This Any More! ;-D
		pulse->m_charge = pulse->m_rtdl->pulseCharge();
	}
	else {
		// Only Log 1st Occurrence "Missing RTDL for Setting Proton Charge"
		// - i.e. If the Pulse Charge has _Not Yet_ Been Set from Data Pkt
		// or If the Data Packet Pulse Charge "Changed" (yikes)... ;-D
		if ( !m_noRTDLPulses && pulse->m_charge != pkt.pulseCharge() ) {
			// Rate-Limited Log Missing RTDL for Setting Proton Charge...
			std::stringstream ss;
			// Just Use Hardware ID for RLL Key...
			ss << hwId;
			std::string log_info;
			if ( RateLimitedLogging::checkLog(
					RLLHistory_SMSControl,
					RLL_MISSING_RTDL_PROTON_CHARGE, ss.str(),
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "pulseEvents():"
					<< " Missing RTDL for Setting Proton Charge!"
					<< " Use Data Packet Proton Charge, If Available..."
					<< " Data=" << pkt.pulseCharge()
					<< " id=0x" << std::hex << pulse->m_id.first
					<< " dup=0x" << pulse->m_id.second << std::dec);
			}
		}
		pulse->m_charge = pkt.pulseCharge();
	}

	// Infrequently Check Live Control PV for Bit/Mask Handling
	// (Once Per Minute...)
	if ( !(++cnt % 3600) ) {
		// Neutron Event State Bits
		uint32_t tmp = m_pvNeutronEventStateBits->value();
		if ( tmp != m_neutronEventStateBits ) {
			ERROR( ( m_recording ? "[RECORDING] " : "" )
				<< "pulseEvents(): Number of Neutron Event State Bits"
				<< " has been Changed from " << m_neutronEventStateBits
				<< " to " << tmp
				<< ": STATES are Now "
				<< ( (tmp > 0) ? "ACTIVATED" : "DEACTIVATED" ) );
			m_neutronEventStateBits = tmp;

			// Set Neutron Event State Mask Based on Number of Bits...
			if ( m_neutronEventStateBits > 0 ) {
				m_neutronEventStateMask =
					( ((uint32_t) -1) << (28 - m_neutronEventStateBits) )
						& 0x0FFFFFFF;
				INFO("Setting Neutron Event State Mask to 0x"
					<< std::hex << m_neutronEventStateMask << std::dec
					<< ".");
			}
		}
		// Neutron Event Sort By State
		m_neutronEventSortByState = m_pvNeutronEventSortByState->value();
		// Beam Monitor TOF Bits
		tmp = m_pvMonitorTOFBits->value();
		if ( tmp != m_monitorTOFBits ) {
			ERROR( ( m_recording ? "[RECORDING] " : "" )
				<< "pulseEvents(): Number of Beam Monitor TOF Bits"
				<< " has been Changed from " << m_monitorTOFBits
				<< " to " << tmp);
			m_monitorTOFBits = tmp;

			// Set Beam Monitor TOF Mask Based on Number of Bits...
			m_monitorTOFMask = ((uint32_t) -1) >> (32 - m_monitorTOFBits);
			INFO("Setting Beam Monitor TOF Mask to 0x"
				<< std::hex << m_monitorTOFMask << std::dec << ".");
		}
		// Chopper TOF Bits
		tmp = m_pvChopperTOFBits->value();
		if ( tmp != m_chopperTOFBits ) {
			ERROR( ( m_recording ? "[RECORDING] " : "" )
				<< "pulseEvents(): Number of Chopper TOF Bits"
				<< " has been Changed from " << m_chopperTOFBits
				<< " to " << tmp);
			m_chopperTOFBits = tmp;

			// Set Chopper TOF Mask Based on Number of Bits...
			m_chopperTOFMask = ((uint32_t) -1) >> (32 - m_chopperTOFBits);
			INFO("Setting Chopper TOF Mask to 0x"
				<< std::hex << m_chopperTOFMask << std::dec << ".");
		}
	}

	ADARA::Event translated;
	const ADARA::Event *events = pkt.events();
	uint32_t i, count = pkt.num_events();
	uint32_t phys, base_phys, logical;
	uint32_t state;
	uint32_t key;
	uint16_t bank = 0;

	bool got_neutrons = false;
	bool got_metadata = false;

	for (i=0; i < count; i++) {

		phys = events[i].pixel;

		state = 0;

		switch (phys >> 28) {

			case 4: // Monitor Event
				/* Add this event to our monitors, and go on to the
				 * next raw event -- it doesn't go in the banked
				 * events section.
				 */
				addMonitorEvent(pkt, pulse, phys, events[i].tof);
				pulse->m_numMonEvents++;
				// Don't Count This Pulse's Proton Charge - No Neutrons
				got_metadata = true;
				meta_count++;
				continue;

			case 7: // Chopper Event
				/* Add this event to our choppers, and go on to the
				 * next raw event -- it doesn't go into the banked
				 * event section.
				 */
				addChopperEvent(pkt, pulse, phys, events[i].tof);
				// Don't Count This Pulse's Proton Charge - No Neutrons
				got_metadata = true;
				meta_count++;
				continue;

			case 0: // Detector Event
				// Strip Off Any State Bits and Decode State...
				if ( m_neutronEventStateBits > 0 ) {
					state = ( phys & m_neutronEventStateMask )
						>> (28 - m_neutronEventStateBits);
					base_phys = phys & (~m_neutronEventStateMask);
					// DEBUG("pulseEvents(): PixelId 0x"
						// << std::hex << phys
						// << " Decodes to State 0x" << state
						// << " and Base PixelId 0x" << base_phys
						// << std::dec);
				}
				else {
					base_phys = phys;
				}
				// Already Mapped to Logical PixelId at Data Source...!
				if (is_mapped) {
					// PixelId Already Is Logical...!
					logical = phys;
					// Just Lookup Bank from Logical PixelId...
					if (m_pixelMap->mapEventBank(base_phys, bank)) {
						pulse->m_flags |=
							ADARA::MAPPING_ERROR;
					}
					bank += PixelMap::REAL_BANK_OFFSET;
				}
				// Not Yet Mapped...
				// Map Physical PixelId to Logical PixelId
				// and Identify Detector Bank...
				else {
					if (m_pixelMap->mapEvent(base_phys, logical, bank)) {
						pulse->m_flags |=
							ADARA::MAPPING_ERROR;
					}
					bank += PixelMap::REAL_BANK_OFFSET;
				}
				// Count This Pulse's Proton Charge - We Got Neutrons! :-D
				got_neutrons = true;
				event_count++;
				break;

			case 5: case 6: // Fast-Metadata Variable Update
				// Don't Count This Pulse's Proton Charge - No Neutrons
				got_metadata = true;
				// Only Count Meta-Data Event _Once_ in Fall-Through...!
				/* This is a fast-metadata update, see if we have a
				 * mapping for it. If not, let it fall through to the
				 * common error pixel handling.
				 */
				if (m_fastmeta->validVariable(phys, key)) {
					if (pulse->m_fastMetaEvents[key].empty()) {
						pulse->m_fastMetaEvents[key].reserve(
							m_fastMetaReserve);
					}
					pulse->m_fastMetaEvents[key].push_back(events[i]);
					meta_count++;
					continue;
				}
				else {
					// Rate-Limited Log Unknown Fast Meta-Data PixelId...
					std::stringstream ss;
					// Just Use Device ID for RLL Key...
					uint32_t devId = phys >> 16;
					ss << std::hex << "0x" << devId;
					std::string log_info;
					if ( RateLimitedLogging::checkLog(
							RLLHistory_SMSControl,
							RLL_UNKNOWN_FAST_META_PIXEL_ID, ss.str(),
							2, 10, 5000, log_info ) ) {
						ERROR(log_info
							<< ( m_recording ? "[RECORDING] " : "" )
							<< "pulseEvents():"
							<< " Unknown Fast Meta-Data"
							<< ( ( (phys >> 28) == 5 ) ?
								" Trigger" : " Analog/ADC" )
							<< " PixelId phys=0x"
							<< std::hex << phys << std::dec
							<< " (Device ID " << ss.str() << ")");
					}
					// Add Generic Fast Meta-Data Device for This PixelId
					m_fastmeta->addGenericDevice(phys, key);
					if (pulse->m_fastMetaEvents[key].empty()) {
						pulse->m_fastMetaEvents[key].reserve(
							m_fastMetaReserve);
					}
					pulse->m_fastMetaEvents[key].push_back(events[i]);
					meta_count++;
					continue;
				}
				// No mapping, Error Pixel...
				/* FALLTHROUGH */

			case 1: case 2: case 3: // Unused as yet...
				/* Unused sources, let them drop into error handling */
				// Don't Count This Pulse's Proton Charge - No Neutrons
				got_metadata = true;
				// Only Count Meta-Data Event _Once_ in Fall-Through...!
				meta_count++;
				/* FALLTHROUGH */

			default: // Error Event
				/* Error bit is set, identity map and put in the
				 * error bank
				 */
				logical = phys;
				bank = (uint16_t)
					( PixelMap::ERROR_BANK + PixelMap::REAL_BANK_OFFSET );
						// Bank Index = 0...! :-D

				pulse->m_flags |= ADARA::ERROR_PIXELS;
				err_count++;

		} // switch (phys >> 28)

		if ( state > 0 )
			pulse->m_flags |= ADARA::HAS_STATES;

		// If _Not_ Sorting Neutron Events By State,
		// Then Clamp All Events to State=0...!
		if ( !m_neutronEventSortByState )
			state = 0;

		// Check State Versus Source Max State,
		// Re-Allocate Banks Array As Needed...
		// Note: "Number" of States Includes State 0...
		if ( state + 1 > src->second.m_numStates ) {
			uint32_t new_banks_arr_size =
				( state + 1 ) * ( m_maxBank + 1 );
			DEBUG("pulseEvents(): New State Out of Bounds:"
				<< " state+1=" << ( state + 1 )
				<< " > src->m_numStates=" << src->second.m_numStates
				<< " - Re-Allocating Banks Array With"
				<< " new_banks_arr_size=" << src->second.m_banks_arr_size
				<< " -> " << new_banks_arr_size);
			EventVector *new_banks_arr = new EventVector[
				new_banks_arr_size ];
			for ( uint32_t i=0 ; i < src->second.m_banks_arr_size ; i++ ) {
				new_banks_arr[i].swap( src->second.m_banks_arr[i] );
			}
			delete[] src->second.m_banks_arr;
			src->second.m_banks_arr = new_banks_arr;
			src->second.m_banks_arr_size = new_banks_arr_size;
			// Note: "Number" of States Includes State 0...
			src->second.m_numStates = state + 1;

			// Also Check/Update Overall Last Max State for New Sources...
			// Note: "Number" of States Includes State 0...
			if ( state + 1 > m_numStatesLast ) {
				DEBUG("pulseEvents(): New Overall Max State:"
					<< " state+1=" << ( state + 1 )
					<< " > m_numStatesLast=" << m_numStatesLast
					<< " - Updating for New Sources...");
				m_numStatesLast = state + 1;
				// Reset Last Max State Reset Count...
				m_numStatesResetCount = 10;
			}
		}

		// Get the Bank Index for This Bank+State...

		uint32_t bsindex = bank + ( state * ( m_maxBank + 1 ) );

		EventVector *ev = &(src->second.m_banks_arr[ bsindex ]);

		// Increment Bank Count on Each First Bank+State Event...
		if ( ev->size() == 0 ) {
			pulse->m_numBanks++;
			src->second.m_activeBanks++;
			ev->reserve(m_bankReserve);
		}

		translated.pixel = logical;
		translated.tof = events[i].tof;

		ev->push_back(translated);

		pulse->m_numEvents++;
	}

	// If We Got Neutrons, We Will Count This Pulse's Proton Charge! :-D
	// Or, If We Got Nuthin' (Didn't Get Neutrons, but Also
	//    Didn't Get Meta-Data Events), Count It Just to Be Safe... ;-b
	// Or, If This Data Source is Marked as having "Mixed Data Packets",
	//    then All Bets are Off, Just Count the Dang Proton Charge... ;-o
	// OR, Use Authoritative Data Flags for This in the New
	//    Version 1+ RawDataPkt/MappedDataPkt Packets...!
	//       -> Data Flags Supersede Any Auto-Deduction Crapola... ;-D
	if ( got_neutrons
			|| ( !pkt.gotDataFlags()
				&& ( !got_metadata || mixed_data_packets ) )
			|| ( pkt.gotDataFlags()
				&& pkt.dataFlags() & ADARA::DataFlags::GOT_NEUTRONS ) ) {
		pulse->m_flags |= ADARA::GOT_NEUTRONS;
	}
	// Also Note the Presence of Meta-Data Events, for Completeness
	if ( got_metadata
			|| ( !pkt.gotDataFlags() && mixed_data_packets )
			|| ( pkt.gotDataFlags()
				&& pkt.dataFlags() & ADARA::DataFlags::GOT_METADATA ) ) {
		pulse->m_flags |= ADARA::GOT_METADATA;
	}
}

void SMSControl::pulseRTDL(const ADARA::RTDLPkt &pkt, uint32_t dup)
{
	PulsePtr &pulse = getPulse(pkt.pulseId(), dup)->second;

	/* We don't log about an existing RTDL packet: we'll always have
	 * one if there is more than one pre-processor on the beam line.
	 */
	if (pulse->m_rtdl)
		return;

	// If We Got Neutrons, We Will Count This Pulse's Proton Charge! :-D
	// Use Any Authoritative Data Flags for This in the New
	//    Version 1+ RTDLPkt Packets...!
	if ( pkt.gotDataFlags() ) {
		if ( pkt.dataFlags() & ADARA::DataFlags::GOT_NEUTRONS )
			pulse->m_flags |= ADARA::GOT_NEUTRONS;
		if ( pkt.dataFlags() & ADARA::DataFlags::GOT_METADATA )
			pulse->m_flags |= ADARA::GOT_METADATA;
	}

	/* Save off information about this pulse for the incoming pulse.
	 * We don't save the pulse charge here, as that is for the
	 * previous pulse. (Same is true for RawDataPkt/MappedDataPkt tho,
	 * however doing it this way is "Better"... Please see comment
	 * in pulseEvents() above... ;-)
	 *
	 * Note: the Veto Flags here are _Also_ for the previous pulse,
	 * but this will be corrected along with the Proton Charge,
	 * if possible, in correctPChargeVeto().
	 */
	pulse->m_vetoFlags = pkt.vetoFlags();
	pulse->m_cycle = pkt.cycle();
	pulse->m_ringPeriod = pkt.ringPeriod();

	m_lastRingPeriod = pkt.ringPeriod();

	/* Note: pkt.badCycle() and pkt.badVeto() are Deprecated. */

	pulse->m_rtdl = boost::make_shared<ADARA::RTDLPkt>(pkt);

	// Is pulse pending from any data sources...?
	if ( !pulse->m_pending.any() ) {

		// ***Periodically*** Log Pulse with No Registered Event Sources!!!
		// (E.g. _Not_ Every 216000 Occurrences using Rate-Limited Logging)
		// - Only Log If We're in "No Registered Event Source" State
		// Every 36000 Occurrences (Every 10 Mins)...
		if ( pulse->m_numEventSources == 0 ) {
			if ( !(m_noRegisteredEventSourcesCount++ % 36000) ) {
				ERROR( ( m_recording ? "[RECORDING] " : "" )
					<< "pulseRTDL: Pulse with No Registered Event Sources!"
					<< std::hex << " pulse=0x"
						<< pulse->m_id.first << std::dec
					<< " (m_noRegisteredEventSources="
					<< ( m_noRegisteredEventSources ? "true" : "false" )
					<< " numEventSources=" << pulse->m_numEventSources
					<< " count=" << m_noRegisteredEventSourcesCount << ")"
					<< " Marking Partial...");
			}
		}
		else {
			// Rate-Limited Log RTDL Out of Order with Raw Data/Packets...
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_RTDL_OUT_OF_ORDER_WITH_DATA, "none",
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "pulseRTDL: RTDL Out of Order with Raw Data"
					<< std::hex << " pulse=0x"
						<< pulse->m_id.first << std::dec
					<< " - Pulse Not Pending...?"
					<< " Marking Partial...");
			}
		}

		// Mark Pulse "Partial" Because there's No Event Data,
		// but then go ahead and mark it "Complete" to record it, lol...!
		pulse->m_flags |= ADARA::PARTIAL_DATA;
		markComplete(pkt.pulseId(), dup, -1);
	}
}

void SMSControl::markPartial(uint64_t pulseId, uint32_t dup)
{
	PulsePtr &pulse = getPulse(pulseId, dup)->second;

	pulse->m_flags |= ADARA::PARTIAL_DATA;
}

void SMSControl::markComplete(uint64_t pulseId, uint32_t dup,
			uint32_t smsId)
{
	static uint32_t data_source_pending_counts[ SOURCE_SET_SIZE ];
	static uint32_t queue_log_count = 0;
	static uint32_t cnt = 0;

	PulseMap::iterator it, last, last_minus_buffer, last_recorded;

	// Infrequently Check Live Control PV for Intermittent Data Threshold
	// (Once Per Minute...)
	if ( !(++cnt % 3600) ) {
		uint32_t tmp = m_pvIntermittentDataThreshold->value();
		if ( tmp != m_intermittentDataThreshold ) {
			m_intermittentDataThreshold = tmp;
			DEBUG("markComplete(): Setting IntermittentDataThreshold to "
				<< m_intermittentDataThreshold);
		}
	}

	// *Before* We Mark This Pulse Complete,
	// Check for Any "Intermittent/Bursty" Data Sources
	// and "Unregister" Them as Event Sources...! ;-D
	// (Only Check for this If there are "Enough" Pulses,
	// and More than One Active Event Source...! ;-)
	if ( m_intermittentDataThreshold > 0
			&& m_pulses.size() > m_intermittentDataThreshold
			&& m_eventSources.count() > 1 ) {
		// Count Number of Consecutive "Missing Pulses" Per DataSource...
		memset( (void *)&data_source_pending_counts, 0,
			sizeof(data_source_pending_counts) );
		size_t unset_count = -1;
		bool found = false;
		uint32_t i;
		// Check Thru Pulses in Buffer, Looking for Pending DataSources...
		for ( it = m_pulses.begin(); it != m_pulses.end(); it++ ) {
			// Check All the Pending DataSources for This Pulse...
			unset_count = it->second->m_pending.count();
			for ( i=0 ; unset_count && i < m_eventSources.size() ; i++ ) {
				// This is a Pending DataSource for This Pulse...
				if ( it->second->m_pending[i] ) {
					// Count How Many Pulses This DataSource is Pending For
					if ( ++(data_source_pending_counts[i])
							>= m_intermittentDataThreshold ) {
						ERROR( ( m_recording ? "[RECORDING] " : "" )
							<< "markComplete():"
							<< " Identified Intermittent Data Source"
							<< " srcId=" << ( m_eventSourcesIndex[i] + 1 )
							<< " smsId=" << i
							<< " (pending count = "
							<< data_source_pending_counts[i] << ")"
							<< " - Marking Event Source as Intermittent"
							<< " and Unregistering as Event Source...!" );
						if ( m_eventSourcesIndex[i] != (uint32_t) -1
								&& m_eventSourcesIndex[i]
									< m_dataSources.size() ) {
							// Need to Set DataSource *First*,
							// as unregisterEventSource() Clears
							// DataSource Reverse Lookup Index! ;-D
							m_dataSources[ m_eventSourcesIndex[i] ]->
								setIntermittent( i, true );
							unregisterEventSource(
								m_eventSourcesIndex[i] + 1, i);
						} else {
							ERROR( ( m_recording ? "[RECORDING] " : "" )
								<< "markComplete():"
								<< " Bad DataSource Reverse Lookup Index "
								<< m_eventSourcesIndex[i]
								<< " For smsId=" << i
								<< " - Can't Set DataSource"
								<< " as Intermittent!" );
						}
						// Only Capture _One_ Intermittent Data Source
						// At A Time...! ;-D (Screws Up the Pulse List...!)
						found = true;
						break;
					}
					// One Less Pending DataSource to Check for This Pulse
					unset_count--;
				}
			}
			// If Found, Iterator will be Corrupted - Break Out...! ;-D
			if ( found ) break;
		}
	}

	// _Now_ Proceed with Normal Pulse Completion Marking...
	PulseMap::iterator current = getPulse(pulseId, dup);
	PulsePtr &pulse = current->second;

	// If this is a "Real" Event Source, then Check Whole Pulse Buffer
	// (up to This Current Pulse :-) for Any "Now-Partial" Completions
	// for This Source... ;-D

	// This accounts for any Lack of Synchrony among Pulses from
	// Different Data Sources, e.g. when running Sub-60Hz we can have
	// Mutually Exclusive Pulse Sets being used for Different Event Types,
	// So we have to handle such "Interleaving"...! ;-D

	int marked_partial = 0;
	int now_complete = 0;
	last = m_pulses.end();

	if ( smsId != (uint32_t) -1 ) {

		// Just like unregisterEventSource() but different. :-D
		// (Stop _Just Before_ Current Pulse - Only One Known "Complete")
		for ( it = m_pulses.begin(); it != current; it++ ) {
			// Release Now-Partial Pulses...
			if ( it->second->m_pending[smsId] ) {
				it->second->m_flags |= ADARA::PARTIAL_DATA;
				it->second->m_pending.reset(smsId);
				marked_partial++;
			}
			// Note Last Now-Completed (Partial or Not) Pulse for Handling
			if ( it->second->m_pending.none() ) {
				last = it;
				now_complete++;
			}
		}

		// And, Of Course, Mark the Current Pulse Complete for This Source
		pulse->m_pending.reset(smsId);
	}

	// Is this Current Pulse Now Complete...?
	if ( pulse->m_pending.none() ) {
		last = current;
		now_complete++;
	}

	// Pulse Still Pending from Other Data Sources...
	else {
		// If Nothing Anywhere in the Pulse Buffer is Complete Yet,
		// then just return...
		if ( !now_complete ) {
			// Periodically Log the Size of the Internal Pulse Buffer...
			if ( !(++queue_log_count % 5000) ) {
				DEBUG( ( m_recording ? "[RECORDING] " : "" )
					<< "markComplete():"
					<< " [Pending] Internal Pulse Buffer Length = "
					<< m_pulses.size() );
			}
			return;
		}
	}

	// Periodically Log the Size/Status of the Internal Pulse Buffer...
	if ( !(++queue_log_count % 5000) ) {
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "markComplete():"
			<< " Internal Pulse Buffer Length = " << m_pulses.size()
			<< " marked_partial=" << marked_partial
			<< " now_complete=" << now_complete );
	}

	/* Some pulse has now been marked complete by all active data sources.
	 * As we expect to get monotonically increasing pulse ids, all
	 * previously incomplete pulses can be considered to be partial
	 * pulses -- one or more data sources had a duplicated pulse or
	 * otherwise did not send data for that pulse.
	 *
	 * We can now walk from the beginning of the pending pulses to
	 * the current one, pushing the data to storage.
	 *
	 * Addendum 7/2014: for some legacy dcomserver implementations,
	 * the neutron events and meta-data events can interleave and/or
	 * arrive "out of order", so we can optionally enforce a
	 * fixed-number-of-pulse buffering, to ensure complete pulses.
	 */

	// [LESS FREQUENTLY] Get Latest "No End-of-Pulse Buffer Size" PV Value
	// (Re-Use "count" from above Intermittent Data Threshold PV check...)
	// Once Every 10 Seconds...
	uint32_t freq = 600;

	// ("cnt" already incremented above... :-)
	if ( !(cnt % freq) ) {
		uint32_t tmp = m_pvNoEoPPulseBufferSize->value();
		if ( tmp != m_noEoPPulseBufferSize ) {
			m_noEoPPulseBufferSize = tmp;
			DEBUG("markComplete(): Setting NoEoPPulseBufferSize to "
				<< m_noEoPPulseBufferSize);
		}
	}

	last_minus_buffer = last;
	uint32_t recorded = 0;
	uint32_t pcnt = 0;

	// Work Backward from *End of Buffer* (_Not_ the Last Complete Pulse!)
	PulseMap::reverse_iterator rit = m_pulses.rbegin();
	while ( pcnt++ < m_noEoPPulseBufferSize ) {
		// End of the Line, Not Enough Buffered Pulses to Proceed
		if ( rit != m_pulses.rend() ) {
			// Skip Over Pulse to Satisfy Buffering Requirement
			// (Slide "Current Pulse" Iterator with Reverse Iterator,
			// *If* It Catches Up... ;-)
			if ( last_minus_buffer->first == rit->first ) {
				if ( last_minus_buffer != m_pulses.begin() )
					last_minus_buffer--;
				else {
					// DEBUG("markComplete():"
						// << " Ran Out of Pulses to Buffer - Done");
					return;
				}
			}
		}
		// No Pulses to Record Yet, Buffer Not "Full" Enough...
		else {
			// DEBUG("markComplete():"
				// << " No Pulses to Record, Buffer Not Full - Done");
			return;
		}
		// Keep Counting...
		rit++;
	}

	// Record Complete/Partial Pulses Past the Buffering Threshold.
	// Note: We _Might_ Be Recording Some As-Yet-Still-Pending
	// Pulses Here, if there are some Unresolved Pulses still
	// Interlaced amidst the Pulses Before the Current Pulse.
	// But That is *OK*, Because Anything Older Than *This*
	// "Last Completed Pulse" Probably _Should_ have Already
	// been Marked Complete; the Pulses are Insertion-Sorted in
	// Pulse Time Order, so if This Pulse is Complete, then
	// All Preceding Pulses Should Also Be...!
	// (Even Any "SAWTOOTH" Pulses that arrived Out of Time Order, as
	// These should be Correctly Handled by Sufficient Buffer Size!)

	// Include "Last Complete Pulse" (With Any Buffering Adjustments)
	last_minus_buffer++;

	// [LESS FREQUENTLY] Get Latest Pulse Charge and Veto Corrections
	// Settings from PVs...
	// (Note: count already incremented above for
	//    "No End-of-Pulse Buffer Size"...)
	// ("cnt" already incremented above... :-)
	if ( !(cnt % freq) ) {
		bool tmp = m_pvDoPulsePchgCorrect->value();
		if ( tmp != m_doPulsePchgCorrect ) {
			m_doPulsePchgCorrect = tmp;
			DEBUG("markComplete(): Setting DoPulsePchgCorrect to "
				<< m_doPulsePchgCorrect);
		}
		tmp = m_pvDoPulseVetoCorrect->value();
		if ( tmp != m_doPulseVetoCorrect ) {
			m_doPulseVetoCorrect = tmp;
			DEBUG("markComplete(): Setting DoPulseVetoCorrect to "
				<< m_doPulseVetoCorrect);
		}
	}

	for ( it = m_pulses.begin(); it != last_minus_buffer; it++ ) {

		// Previous Pulses will Never be Made Complete,
		// Already Marked as Partial as Needed Above...

		// Correct Proton Charge...
		// (This Pulse's PCharge comes from _Next_ Pulse...!)
		if ( m_doPulsePchgCorrect || m_doPulseVetoCorrect ) {
			PulseMap::iterator next = it;
			if (++next != m_pulses.end()) {
				correctPChargeVeto(it->second, next->second);
			}
			else {
				/* Rate-limited logging of no more pulses */
				std::string log_info;
				if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
						RLL_PULSE_PCHG_BUFFER_EMPTY, "none",
						2, 10, 100, log_info ) ) {
					ERROR(log_info
						<< ( m_recording ? "[RECORDING] " : "" )
						<< "markComplete:"
						<< " No More Pulses for"
						<< " Proton Charge/Veto Flags Correction! 0x"
						<< std::hex << it->first.first << std::dec);
				}
			}
		}

		// Recording Previously Complete (Buffered) Pulse
		recordPulse(it->second);
		last_recorded = it;
		recorded++;

		// Reset Any "Last Pulse" ID for Fast Lookup...
		if ( it->first == m_lastPid ) {
			m_lastPid = PulseIdentifier(-1,-1);
			//DEBUG("*** Freeing 'Last Pulse', Reset Last Pulse Id"
				//<< std::hex << " 0x" << it->first.first << std::dec);
		}
	}

	// Erase Any Now-Recorded Pulses
	if ( recorded ) {
		m_pulses.erase(m_pulses.begin(), ++last_recorded);
	}

	// Periodically Log the Resulting Size of the Internal Pulse Buffer...
	if ( !(++queue_log_count % 5000) ) {
		DEBUG( ( m_recording ? "[RECORDING] " : "" )
			<< "markComplete():"
			<< "Internal Pulse Buffer Length = " << m_pulses.size()
			<< " (recorded=" << recorded << ")" );
	}
}

void SMSControl::correctPChargeVeto(PulsePtr &pulse, PulsePtr &next_pulse)
{
	// Check if Next Pulse is About "Pulse Frequency" Away from This Pulse

	uint64_t pulse_id = pulse->m_id.first;
	uint64_t pulse_sec = pulse_id >> 32;
	uint64_t pulse_nsec = pulse_id & 0xffffffff;

	uint64_t next_pulse_id = next_pulse->m_id.first;
	uint64_t next_pulse_sec = next_pulse_id >> 32;
	uint64_t next_pulse_nsec = next_pulse_id & 0xffffffff;

	uint64_t interPulseTime =
		( ( next_pulse_sec - pulse_sec ) * NANO_PER_SECOND_LL )
			+ ( next_pulse_nsec - pulse_nsec );

	if ( m_interPulseTimeMin < interPulseTime
			&& interPulseTime < m_interPulseTimeMax )
	{
		if ( m_doPulsePchgCorrect )
		{
			// Reset the Pulse Proton Charge Uncorrected Flag
			pulse->m_flags &= ~ADARA::PCHARGE_UNCORRECTED;

			// Set the Pulse Charge from the Next Pulse...
			// (Since We Don't Set a Pulse's Charge until pulseEvents(),
			// with data from the RawDataPkt/MappedDataPkt packets,
			// check RTDL as needed to ensure we get a Valid Pulse Charge!)
			if ( next_pulse->m_charge == 0 && next_pulse->m_rtdl )
				pulse->m_charge = next_pulse->m_rtdl->pulseCharge();
			else
				pulse->m_charge = next_pulse->m_charge;
		}

		if ( m_doPulseVetoCorrect )
		{
			// Reset the Pulse Veto Flags Uncorrected Flag
			pulse->m_flags &= ~ADARA::VETO_UNCORRECTED;

			// Set the Veto Flags from the Next Pulse
			pulse->m_vetoFlags = next_pulse->m_vetoFlags;
		}

		// If RTDL Packet(s) present, Adjust RTDL Proton Charge Here Too...
		if ( pulse->m_rtdl && next_pulse->m_rtdl )
		{
			// Set the RTDL Pulse Charge from the Next Pulse's RTDL
			if ( m_doPulsePchgCorrect )
			{
				pulse->m_rtdl->setPulseCharge(
					next_pulse->m_rtdl->pulseCharge() );
			}

			// Set the RTDL Veto Flags from the Next Pulse's RTDL
			if ( m_doPulseVetoCorrect )
			{
				pulse->m_rtdl->setVetoFlags(
					next_pulse->m_rtdl->vetoFlags() );
			}
		}
	}

	// Log Pulse Proton Charge Correction Failure...!
	else
	{
		// Generate Pulse/Next Pulse RTDL Logging String
		std::stringstream ss_rtdl;
		if ( pulse->m_rtdl ) {
			ss_rtdl << " pulse->m_rtdl->pulseCharge()=";
			ss_rtdl << pulse->m_rtdl->pulseCharge();
		}
		if ( next_pulse->m_rtdl ) {
			ss_rtdl << " next_pulse->m_rtdl->pulseCharge()=";
			ss_rtdl << next_pulse->m_rtdl->pulseCharge();
		}

		/* Rate-limited logging of global sawtooth pulse */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
				RLL_PULSE_PCHG_UNCORRECTED, "none",
				2, 10, 5000, log_info ) ) {
			ERROR(log_info
				<< ( m_recording ? "[RECORDING] " : "" )
				<< "correctPChargeVeto: *** Next Pulse Out of Range -"
				<< " Uncorrected Pulse Proton Charge/Veto Flags!"
				<< std::hex
				<< " pulse=0x" << pulse->m_id.first
				<< " next_pulse=0x" << next_pulse->m_id.first
				<< std::dec
				<< " pulse_sec=" << pulse_sec
				<< " pulse_nsec=" << pulse_nsec
				<< " next_pulse_sec=" << next_pulse_sec
				<< " next_pulse_nsec=" << next_pulse_nsec
				<< " interPulseTime=" << interPulseTime
				<< " m_interPulseTimeMin=" << m_interPulseTimeMin
				<< " m_interPulseTimeMax=" << m_interPulseTimeMax
				<< " m_doPulsePchgCorrect=" << m_doPulsePchgCorrect
				<< " pulse->m_charge=" << pulse->m_charge
				<< " next_pulse->m_charge=" << next_pulse->m_charge
				<< ss_rtdl.str()
				<< " m_doPulseVetoCorrect=" << m_doPulseVetoCorrect
				<< " pulse->m_vetoFlags=" << pulse->m_vetoFlags
				<< " next_pulse->m_vetoFlags=" << next_pulse->m_vetoFlags);
		}
	}
}

void SMSControl::recordPulse(PulsePtr &pulse)
{
	static uint32_t cnt = 0;

	/* Send the RTDL packet, followed by the banked event packet */

	// XXX avoid sending the RTDL for a pulse twice (if duplicated)

	try {

		// Got RTDL... :-D
		if (pulse->m_rtdl) {
			/* Don't notify clients; we want to keep the banked
			 * event packet with the RTDL packet.
			 */
			StorageManager::addPacket(pulse->m_rtdl->packet(),
						pulse->m_rtdl->packet_length(),
						false /* ignore_pkt_timestamp */,
						true /* check_old_containers */,
						false /* notify */);
		}

		// NO RTDL for Pulse!
		else {

			// Only Check Live Control PV _Very Infrequently_...
			//    - This Option is Not Likely to Ever Change... ;-D
			//    - On HFIR, Where this is Needed, We Run at "1 HZ"...
			//       -> so this should only check the PV once per hour...
			//    - Otherwise, on SNS, this is like "once per minute"...
			if ( !(++cnt % 3600) ) {
				bool tmp = m_pvNoRTDLPulses->value();
				if ( tmp != m_noRTDLPulses ) {
					m_noRTDLPulses = tmp;
					DEBUG("recordPulse(): Setting NoRTDLPulses to "
						<< m_noRTDLPulses);
				}
			}

			// Skip Error Logging if We Don't Expect Any RTDLs Anyway...
			if ( !m_noRTDLPulses ) {
				/* Rate-limited logging of no RTDL for pulse */
				std::string log_info;
				if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
						RLL_NO_RTDL_FOR_PULSE, "none",
						2, 10, 5000, log_info ) ) {
					ERROR(log_info
						<< ( m_recording ? "[RECORDING] " : "" )
						<< "recordPulse: NO RTDL for Pulse"
						<< " id=0x" << std::hex << pulse->m_id.first
						<< " dup=0x" << pulse->m_id.second << std::dec);
				}
			}

			// Always Mark Pulse as Missing RTDL if it Doesn't Have One!
			pulse->m_flags |= ADARA::MISSING_RTDL;
		}

		// Properly Set Veto Bit in Flags, Based on Timing Master Header
		// (Don't Worry, This Just Sets an "Aggregation" Veto Bit;
		//    the Full Veto Flags are _Also_ Included Now...! ;-D)
		bool is_veto = false;
		if ( m_targetStationNumber == 1 ) {
			if ( pulse->m_vetoFlags & TARGET_1_VETO_MASK ) {
				is_veto = true;
			}
		}
		else if ( m_targetStationNumber == 2 ) {
			if ( pulse->m_vetoFlags & TARGET_2_VETO_MASK ) {
				is_veto = true;
			}
		}
		if ( is_veto ) {
			pulse->m_flags |= ADARA::PULSE_VETO;
		}

		// Build Various Packets for Pulse...

		buildMonitorPacket(pulse);

		// Choose Between Polarization State vs. "Normal" Banked Events...
		if ( pulse->m_flags & ADARA::HAS_STATES ) {
			buildBankedStatePacket(pulse);
		}
		else {
			buildBankedPacket(pulse);
		}

		buildChopperPackets(pulse);

		buildFastMetaPackets(pulse);

	} catch (std::runtime_error e) {
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "Failed to record pulse: "
			<< " id=0x" << std::hex << pulse->m_id.first
			<< " dup=0x" << pulse->m_id.second << std::dec
			<< " - " << e.what());

		/* Abuse std::logic_error here somewhat -- we want failure to
		 * write data to be fatal, but don't have a way to distinguish
		 * those from transient socket errors via std::runtime_error.
		 * We currently will die on logic_errors in those that call
		 * down to here, so use those for now.
		 *
		 * XXX find a better way to distinguish fatal vs non-fatal
		 * errors, perhaps using custom exception classes.
		 */
		throw std::logic_error("Unable to record pulse");
	}
}

void SMSControl::buildMonitorPacket(PulsePtr &pulse)
{
	m_iovec.clear();
	m_hdrs.clear();

	m_iovec.reserve(1 + pulse->m_monitors.size() * 2);

	/* IMPORTANT: m_hdrs must be correctly sized, as we use pointers
	 * into the vector in the iovec(s) we submit to
	 * StorageManager::addPacket(). No reallocation is allowed after
	 * we've reserved the proper size.
	 */
	uint32_t size = 8 + pulse->m_monitors.size() * 3;
	m_hdrs.reserve(size);

	/* Common ADARA packet header */
	size += pulse->m_numMonEvents;
	size *= sizeof(uint32_t);
	size -= sizeof(ADARA::Header);
	m_hdrs.push_back(size);
	m_hdrs.push_back( ADARA_PKT_TYPE(
		ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE,
		ADARA::PacketType::BEAM_MONITOR_EVENT_VERSION ) );
	m_hdrs.push_back(pulse->m_id.first >> 32);
	m_hdrs.push_back(pulse->m_id.first);

	/* Beam monitor event header */
	if ( pulse->m_flags & ADARA::GOT_NEUTRONS ) {
		// Count This Pulse's Proton Charge - We Got Neutrons! :-D
		// (Or at least we could have gotten some... :-)
		m_hdrs.push_back(pulse->m_charge);
	} else {
		// No Neutrons This Pulse, Don't Count This Pulse's Proton Charge
		m_hdrs.push_back((uint32_t) 0);
	}
	m_hdrs.push_back(pulseEnergy(pulse->m_ringPeriod));
	m_hdrs.push_back(pulse->m_cycle);

	uint32_t flags = ( pulse->m_flags & 0xfffff )
		+ ( ( pulse->m_vetoFlags & 0xfff ) << 20 );
	m_hdrs.push_back(flags);

	struct iovec iov;
	iov.iov_base = &m_hdrs.front();
	iov.iov_len = m_hdrs.size() * sizeof(uint32_t);
	m_iovec.push_back(iov);

	MonitorMap::iterator mIt, mEnd = pulse->m_monitors.end();
	for (mIt = pulse->m_monitors.begin(); mIt != mEnd; mIt++) {
		iov.iov_base = &m_hdrs.front() + m_hdrs.size();
		iov.iov_len = 3 * sizeof(uint32_t);
		m_iovec.push_back(iov);

		BeamMonitor &mon = mIt->second;
		uint32_t id_cnt = mIt->first << 22;
		id_cnt |= mon.m_eventTof.size();
		m_hdrs.push_back(id_cnt);
		m_hdrs.push_back(mon.m_sourceId);
		m_hdrs.push_back(mon.m_tofField);

		iov.iov_base = &mon.m_eventTof.front();
		iov.iov_len = mon.m_eventTof.size();
		iov.iov_len *= sizeof(uint32_t);
		m_iovec.push_back(iov);
	}

	StorageManager::addPacket(m_iovec);
}

void SMSControl::buildBankedPacket(PulsePtr &pulse)
{
	m_iovec.clear();
	m_hdrs.clear();

	uint32_t size = 1 + pulse->m_numBanks * 2;
	size += pulse->m_pulseSources.size() * 4;
	m_iovec.reserve(size);

	/* IMPORTANT: m_hdrs must be correctly sized, as we use pointers
	 * into the vector in the iovec(s) we submit to
	 * StorageManager::addPacket(). No reallocation is allowed after
	 * we've reserved the proper size.
	 */
	size = 8 + pulse->m_pulseSources.size() * 4;
	size += pulse->m_numBanks * 2;
	m_hdrs.reserve(size);

	/* Common ADARA packet header */
	size *= sizeof(uint32_t);
	size += pulse->m_numEvents * sizeof(ADARA::Event);
	size -= sizeof(ADARA::Header);
	m_hdrs.push_back(size);
	m_hdrs.push_back( ADARA_PKT_TYPE(
		ADARA::PacketType::BANKED_EVENT_TYPE,
		ADARA::PacketType::BANKED_EVENT_VERSION ) );
	m_hdrs.push_back(pulse->m_id.first >> 32);
	m_hdrs.push_back(pulse->m_id.first);

	/* Banked Event Header */
	if ( pulse->m_flags & ADARA::GOT_NEUTRONS ) {
		// Count This Pulse's Proton Charge - We Got Neutrons! :-D
		// (Or at least we could have gotten some... :-)
		m_hdrs.push_back(pulse->m_charge);
	} else {
		// No Neutrons This Pulse, Don't Count This Pulse's Proton Charge
		m_hdrs.push_back((uint32_t) 0);
	}
	m_hdrs.push_back(pulseEnergy(pulse->m_ringPeriod));
	m_hdrs.push_back(pulse->m_cycle);

	uint32_t flags = ( pulse->m_flags & 0xfffff )
		+ ( ( pulse->m_vetoFlags & 0xfff ) << 20 );
	m_hdrs.push_back(flags);

	struct iovec iov;
	iov.iov_base = &m_hdrs.front();
	iov.iov_len = m_hdrs.size() * sizeof(uint32_t);
	m_iovec.push_back(iov);

	SourceMap::iterator sIt, sEnd = pulse->m_pulseSources.end();
	for (sIt = pulse->m_pulseSources.begin(); sIt != sEnd; sIt++) {
		iov.iov_base = &m_hdrs.front() + m_hdrs.size();
		iov.iov_len = 4 * sizeof(uint32_t);
		m_iovec.push_back(iov);

		EventSource &src = sIt->second;
		m_hdrs.push_back(sIt->first);
		m_hdrs.push_back(src.m_intraPulseTime);
		m_hdrs.push_back(src.m_tofField);
		m_hdrs.push_back(src.m_activeBanks);

		for ( uint32_t bi=0 ; bi < src.m_banks_arr_size ; bi++ )
		{
			EventVector *ev = &(src.m_banks_arr[bi]);

			if ( ev->size() == 0 )
				continue;

			iov.iov_base = &m_hdrs.front() + m_hdrs.size();
			iov.iov_len = 2 * sizeof(uint32_t);
			m_iovec.push_back(iov);

			uint32_t bank = bi % ( m_maxBank + 1 );

			// Because we're using Unsigned Integers, we'll translate:
			//    - Error Pixels to Bank -2
			//       (PixelMap::ERROR_BANK = 0xfffe -> 0xfffffffe)
			//    - Unmapped Pixels to Bank -1
			//       (PixelMap::UNMAPPED_BANK = 0xffff -> 0xffffffff)
			// All other bank ids will get their real number.
			m_hdrs.push_back( bank - PixelMap::REAL_BANK_OFFSET );
			m_hdrs.push_back( ev->size() );

			iov.iov_base = &(ev->front());
			iov.iov_len = ev->size();
			iov.iov_len *= sizeof(ADARA::Event);
			m_iovec.push_back(iov);
		}
	}

	StorageManager::addPacket(m_iovec);

	// Check Last Max States/Reset Counter...
	// - This Pulse had *No States*, i.e. Max States == 1...
	// Note: "Number" of States Includes State 0...
	if ( m_numStatesLast > 1 && --m_numStatesResetCount <= 0 )
	{
		DEBUG("buildBankedPacket():"
			<< " Overall Max State Reset Count Expired,"
			<< " Reset Overall Max State:"
			<< "  m_numStatesLast=" << m_numStatesLast
			<< " -> 1...");
		m_numStatesLast = 1;
	}

	// Free Up Allocated Banks Array Storage...
	for (sIt = pulse->m_pulseSources.begin(); sIt != sEnd; sIt++) {
		EventSource &src = sIt->second;
		delete[] src.m_banks_arr;
	}
}

void SMSControl::buildBankedStatePacket(PulsePtr &pulse)
{
	m_iovec.clear();
	m_hdrs.clear();

	uint32_t size = 1 + pulse->m_numBanks * 3;
	size += pulse->m_pulseSources.size() * 4;
	m_iovec.reserve(size);

	/* IMPORTANT: m_hdrs must be correctly sized, as we use pointers
	 * into the vector in the iovec(s) we submit to
	 * StorageManager::addPacket(). No reallocation is allowed after
	 * we've reserved the proper size.
	 */
	size = 8 + pulse->m_pulseSources.size() * 4;
	size += pulse->m_numBanks * 3;
	m_hdrs.reserve(size);

	/* Common ADARA packet header */
	size *= sizeof(uint32_t);
	size += pulse->m_numEvents * sizeof(ADARA::Event);
	size -= sizeof(ADARA::Header);
	m_hdrs.push_back(size);
	m_hdrs.push_back( ADARA_PKT_TYPE(
		ADARA::PacketType::BANKED_EVENT_STATE_TYPE,
		ADARA::PacketType::BANKED_EVENT_STATE_VERSION ) );
	m_hdrs.push_back(pulse->m_id.first >> 32);
	m_hdrs.push_back(pulse->m_id.first);

	/* Banked State Event Header */
	if ( pulse->m_flags & ADARA::GOT_NEUTRONS ) {
		// Count This Pulse's Proton Charge - We Got Neutrons! :-D
		// (Or at least we could have gotten some... :-)
		m_hdrs.push_back(pulse->m_charge);
	} else {
		// No Neutrons This Pulse, Don't Count This Pulse's Proton Charge
		m_hdrs.push_back((uint32_t) 0);
	}
	m_hdrs.push_back(pulseEnergy(pulse->m_ringPeriod));
	m_hdrs.push_back(pulse->m_cycle);

	uint32_t flags = ( pulse->m_flags & 0xfffff )
		+ ( ( pulse->m_vetoFlags & 0xfff ) << 20 );
	m_hdrs.push_back(flags);

	struct iovec iov;
	iov.iov_base = &m_hdrs.front();
	iov.iov_len = m_hdrs.size() * sizeof(uint32_t);
	m_iovec.push_back(iov);

	uint32_t numStates = 1;

	SourceMap::iterator sIt, sEnd = pulse->m_pulseSources.end();
	for (sIt = pulse->m_pulseSources.begin(); sIt != sEnd; sIt++) {
		iov.iov_base = &m_hdrs.front() + m_hdrs.size();
		iov.iov_len = 4 * sizeof(uint32_t);
		m_iovec.push_back(iov);

		EventSource &src = sIt->second;
		m_hdrs.push_back(sIt->first);
		m_hdrs.push_back(src.m_intraPulseTime);
		m_hdrs.push_back(src.m_tofField);
		m_hdrs.push_back(src.m_activeBanks);

		for ( uint32_t bi=0 ; bi < src.m_banks_arr_size ; bi++ )
		{
			EventVector *ev = &(src.m_banks_arr[bi]);

			if ( ev->size() == 0 )
				continue;

			iov.iov_base = &m_hdrs.front() + m_hdrs.size();
			iov.iov_len = 3 * sizeof(uint32_t);
			m_iovec.push_back(iov);

			uint32_t bank = bi % ( m_maxBank + 1 );
			uint32_t state = bi / ( m_maxBank + 1 );

			// Accumulate Max State for This Pulse...
			if ( state + 1 > numStates )
				numStates = state + 1;

			// Because we're using Unsigned Integers, we'll translate:
			//    - Error Pixels to Bank -2
			//       (PixelMap::ERROR_BANK = 0xfffe -> 0xfffffffe)
			//    - Unmapped Pixels to Bank -1
			//       (PixelMap::UNMAPPED_BANK = 0xffff -> 0xffffffff)
			// All other bank ids will get their real number.
			m_hdrs.push_back( bank - PixelMap::REAL_BANK_OFFSET );
			m_hdrs.push_back( state );
			m_hdrs.push_back( ev->size() );

			iov.iov_base = &(ev->front());
			iov.iov_len = ev->size();
			iov.iov_len *= sizeof(ADARA::Event);
			m_iovec.push_back(iov);
		}
	}

	StorageManager::addPacket(m_iovec);

	// Check Last Max States/Reset Counter...
	// Note: "Number" of States Includes State 0...
	if ( m_numStatesLast > numStates && --m_numStatesResetCount <= 0 )
	{
		DEBUG("buildBankedStatePacket():"
			<< " Overall Max State Reset Count Expired,"
			<< " Reset Overall Max State:"
			<< "  m_numStatesLast=" << m_numStatesLast
			<< " -> " << numStates << "...");
		m_numStatesLast = numStates;
	}
	// Reset the Reset Counter if We're Maintaining This Number of States
	else if ( m_numStatesLast == numStates )
		m_numStatesResetCount = 10;

	// Free Up Allocated Banks Array Storage...
	for (sIt = pulse->m_pulseSources.begin(); sIt != sEnd; sIt++) {
		EventSource &src = sIt->second;
		delete[] src.m_banks_arr;
	}
}

void SMSControl::buildChopperPackets(PulsePtr &pulse)
{
	uint32_t pkt[4 + (sizeof(ADARA::Header) / sizeof(uint32_t))];

	pkt[0] = 4 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_U32_TYPE,
		ADARA::PacketType::VAR_VALUE_U32_VERSION );
	pkt[6] = ADARA::VariableStatus::OK << 16;
	pkt[6] |= ADARA::VariableSeverity::OK;

	ChopperMap::iterator cit, cend = pulse->m_chopperEvents.end();
	for (cit = pulse->m_chopperEvents.begin(); cit != cend; ++cit) {
		/* Set the device ID inside SMS's range, using the chopper's
		 * ID number.
		 */
		pkt[4] = 0x80000000 | cit->first;

		ChopperEvents::iterator eit, eend = cit->second.end();
		bool first = true;
		for (eit = cit->second.begin(); eit != eend; ++eit) {
			uint32_t val = *eit;
			uint32_t tof = val >> 1;

			// Check for Chopper Event Synchronization Issue from DSP...
			// - E.g. 1st Chopper Event for a Pulse is _Really_ the
			// _Last_ Chopper Event from the Previous Pulse...
			//    -> Event TOF should be added to Previous Pulse Time,
			//    but since we're already in _This_ Pulse, instead
			//    just "Zero Out" the TOF and add to This Pulse Time... ;-Q
			// - Identify This Case with the following criteria:
			//    -> TOF for 1st Chopper Event is *Greater* than 2nd Event
			//    -> TOF value is in the neighborhood of InterPulseTime...
			// ALSO Check for Chopper Glitch Event Issue from DSP...?
			// - E.g. Every 10 Seconds ("Magic" 600th Pulse...? ;-)
			// an Erroneous Chopper Event comes in with TOF = 0.0333333-ish
			// which falls _Between_ the "regularly scheduled" events
			// at the given Chopper frequency... ;-Q
			//    -> NOTE: Problem Related to DSP Capturing *Both* the
			//    Rising and Falling Edge Events for a Chopper...!
			//    (No More Glitches when Just Capturing Rising Edge...! :-)
			// - Identify This Case similarly, but with a Different
			// neighborhood of "2 * InterPulseTime"... ;-b

			if ( first )
			{
				first = false;

				if ( eit + 1 != eend )
				{
					uint32_t tof2 = *(eit + 1) >> 1;
					if ( tof > tof2 )
					{
						// *** Chopper Sync Issue
						// - Event Pushed to Subsequent Pulse...
						if ( m_interPulseTimeChopperMin < tof
								&& tof < m_interPulseTimeChopperMax )
						{
							/* Rate-limited logging of no RTDL for pulse */
							std::stringstream ss;
							ss << cit->first;
							std::string log_info;
							if ( RateLimitedLogging::checkLog(
									RLLHistory_SMSControl,
									RLL_CHOPPER_SYNC_ISSUE, ss.str(),
									60, 10, 100, log_info ) ) {
								ERROR(log_info
									<< ( m_recording
										? "[RECORDING] " : "" )
									<< "buildChopperPackets():"
									<< " *** Chopper " << cit->first
									<< " Event Synchronization Error!"
									<< std::hex << " (pulse=0x"
										<< pulse->m_id.first << ")"
										<< std::dec
									<< " 1st Event TOF1=" << tof
									<< " > 2nd Event TOF2=" << tof2
									<< " and TOF1 in Neighborhood of"
									<< " InterPulseTime ("
									<< m_interPulseTimeChopperMin << ", "
									<< m_interPulseTimeChopperMax << ")"
									<< " - Resetting TOF1 to Zero!");
							}
							tof = 0;
						}

						// *** Chopper Glitch Issue
						// - Errant Event in Otherwise Normal Sequence...
						else if ( m_interPulseTimeChopGlitchMin < tof
								&& tof < m_interPulseTimeChopGlitchMax )
						{
							/* Rate-limited logging of no RTDL for pulse */
							std::stringstream ss;
							ss << cit->first;
							std::string log_info;
							if ( RateLimitedLogging::checkLog(
									RLLHistory_SMSControl,
									RLL_CHOPPER_GLITCH_ISSUE, ss.str(),
									60, 10, 100, log_info ) ) {
								ERROR(log_info
									<< ( m_recording
										? "[RECORDING] " : "" )
									<< "buildChopperPackets():"
									<< " *** Chopper " << cit->first
									<< " Glitch Event Error!"
									<< std::hex << " (pulse=0x"
										<< pulse->m_id.first << ")"
										<< std::dec
									<< " 1st Event TOF1=" << tof
									<< " > 2nd Event TOF2=" << tof2
									<< " and TOF1 in Neighborhood of"
									<< " 2 * InterPulseTime ("
									<< m_interPulseTimeChopGlitchMin
									<< ", "
									<< m_interPulseTimeChopGlitchMax << ")"
									<< " - Omitting Chopper Event!");
							}
							continue;
						}
					}
				}
			}

			/* Set the variable ID and the updated value */
			pkt[5] = (val & 1) + 1;
			pkt[7] = tof;

			/* Create a different timestamp for each variable
			 * update packet by adding the TOF value to the pulse
			 * ID, handling overflow of the nanoseconds field.
			 */
			uint32_t ns = pulse->m_id.first & 0xffffffff;
			ns += tof;

			pkt[2] = pulse->m_id.first >> 32;
			if (ns >= NANO_PER_SECOND_LL) {
				ns -= NANO_PER_SECOND_LL;
				pkt[2]++;
			}
			pkt[3] = ns;

			/* We consider chopper data as fast metadata, so
			 * we don't keep track of the current value and just
			 * push it into the stream.
			 */
			StorageManager::addPacket(pkt, sizeof(pkt));
		}
	}
}

void SMSControl::buildFastMetaPackets(PulsePtr &pulse)
{
	uint64_t pulse_id = pulse->m_id.first;

	FastMetaMap::iterator fmit, fmend = pulse->m_fastMetaEvents.end();
	for (fmit = pulse->m_fastMetaEvents.begin(); fmit != fmend; ++fmit)
	{
		EventVector::iterator it, end = fmit->second.end();

		// REMOVEME
		//DEBUG("buildFastMetaPackets(): key="
			//<< std::hex << fmit->first << std::dec
			//<< " size=" << fmit->second.size());

		if ( fmit->second.size() > 3 )
		{
			m_fastmeta->sendMultUpdate(pulse_id, fmit->second);
		}

		else
		{
			for (it = fmit->second.begin(); it != end ; ++it)
			{
				m_fastmeta->sendUpdate(pulse_id, it->pixel, it->tof);
			}
		}
	}
}

uint32_t SMSControl::pulseEnergy(uint32_t ringPeriod)
{
	// Handle Bogus Ring Period...!
	// (Can't Have Zero Ring Period, Equals Infinite Velocity...! ;-)
	if ( ringPeriod == 0 ) {
		// Skip Error Logging if We Don't Expect Any RTDLs Anyway...
		// (The Ring Period will Always Be Zero, e.g. on HFIR Beamlines!)
		if ( !m_noRTDLPulses ) {
			/* Rate-limited logging of bogus ring period zero */
			std::string log_info;
			if ( RateLimitedLogging::checkLog(
					RLLHistory_SMSControl,
					RLL_BOGUS_PULSE_ENERGY_ZERO, "none",
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "pulseEnergy(): Bogus Ring Period of Zero"
					<< " - No RTDL for 1st Pulse After SMS Restart?"
					<< " Setting Pulse Energy to Zero.");
			}
		}
		// Just Return Zero Pulse Energy...
		return( 0 );
	}

	double ring_circumference = 248; // meters
	double period = ringPeriod * 1e-12; // seconds
	double v = ring_circumference / period; // m/s
	double c = 299792458; // m/s
	double beta = v / c;
	double E0 = 938.257e6; // rest energy of proton, eV

	// Another Sanity Check, "Beta" Better Be < 1.0... ;-D
	// (As Far As We Know, Can't Go Faster Than Light...? ;-D)
	if ( beta >= 1.0 ) {
		/* Rate-limited logging of bogus ring period zero */
		std::string log_info;
		if ( RateLimitedLogging::checkLog(
				RLLHistory_SMSControl,
				RLL_BOGUS_PULSE_ENERGY_BETA, "none",
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< ( m_recording ? "[RECORDING] " : "" )
				<< "pulseEnergy(): Bogus Pulse Data"
				<< " ringPeriod=" << ringPeriod
				<< " beta=" << beta
				<< " - No Faster Than Light Ring Traversal!"
				<< " Setting Pulse Energy to Zero.");
		}
		// Just Return Zero Pulse Energy...
		return( 0 );
	}

	/* Return pulse energy in eV */
	return (E0 / sqrt(1 - (beta * beta))) - E0;
}

void SMSControl::updateDescriptor(const ADARA::DeviceDescriptorPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateDescriptor(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableU32Pkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableDoublePkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableStringPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableU32ArrayPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableDoubleArrayPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::MultVariableU32Pkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::MultVariableDoublePkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::MultVariableStringPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::MultVariableU32ArrayPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::MultVariableDoubleArrayPkt &pkt,
			uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::extractLastValue( ADARA::MultVariableU32Pkt inPkt,
		ADARA::PacketSharedPtr &outPkt)
{
	m_meta->extractLastValue(inPkt, outPkt);
}

void SMSControl::extractLastValue( ADARA::MultVariableDoublePkt inPkt,
		ADARA::PacketSharedPtr &outPkt)
{
	m_meta->extractLastValue(inPkt, outPkt);
}

void SMSControl::extractLastValue( ADARA::MultVariableStringPkt inPkt,
		ADARA::PacketSharedPtr &outPkt)
{
	m_meta->extractLastValue(inPkt, outPkt);
}

void SMSControl::extractLastValue( ADARA::MultVariableU32ArrayPkt inPkt,
		ADARA::PacketSharedPtr &outPkt)
{
	m_meta->extractLastValue(inPkt, outPkt);
}

void SMSControl::extractLastValue(
		ADARA::MultVariableDoubleArrayPkt inPkt,
		ADARA::PacketSharedPtr &outPkt)
{
	m_meta->extractLastValue(inPkt, outPkt);
}

