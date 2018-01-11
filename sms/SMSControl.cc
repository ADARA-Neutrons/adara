
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
#include "Logging.h"
#include "utils.h"

#include "snsTiming.h"

#include <string>
#include <sstream>
#include <map>
#include <time.h>
#include <math.h>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <stdint.h>

#include <cadef.h>

static LoggerPtr logger(Logger::getLogger("SMS.Control"));

RateLimitedLogging::History RLLHistory_SMSControl;

// Rate-Limited Logging IDs...
#define RLL_GLOBAL_SAWTOOTH_PULSE        0
#define RLL_INTERLEAVED_GLOBAL_SAWTOOTH  1
#define RLL_GLOBAL_SAWTOOTH_LAST         2
#define RLL_PULSE_BUFFER_OVERFLOW        3
#define RLL_SET_SOURCES_READ_DELAY       4
#define RLL_RTDL_OUT_OF_ORDER_WITH_DATA  5
#define RLL_PULSE_PCHG_UNCORRECTED       6
#define RLL_PULSE_PCHG_BUFFER_EMPTY      7
#define RLL_NO_RTDL_FOR_PULSE            8
#define RLL_CHOPPER_SYNC_ISSUE           9
#define RLL_CHOPPER_GLITCH_ISSUE        10

uint32_t SMSControl::m_targetStationNumber;

std::string SMSControl::m_version;
std::string SMSControl::m_facility;
std::string SMSControl::m_beamlineId;
std::string SMSControl::m_beamlineShortName;
std::string SMSControl::m_beamlineLongName;
std::string SMSControl::m_geometryPath;
std::string SMSControl::m_pixelMapPath;

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

bool SMSControl::m_allowNonOneToOnePixelMapping;

bool SMSControl::m_notesCommentAutoReset;

class PopPulseBufferPV : public smsInt32PV {
public:
	PopPulseBufferPV(const std::string &name) :
		smsInt32PV(name) {}

private:
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

class CleanShutdownPV : public smsTriggerPV {
public:
	CleanShutdownPV(const std::string &name) :
		smsTriggerPV(name) {}

private:
	void triggered(void)
	{
		DEBUG("CleanShutdownPV " << m_pv_name << " Triggered."
			<< " Cleanly Shutting Down SMS Daemon.");
		exit(0);
	}
};

SMSControl *SMSControl::m_singleton = NULL;

static uint32_t pulseEnergy(uint32_t ringPeriod)
{
	double ring_circumference = 248; // meters
	double period = ringPeriod * 1e-12; // seconds
	double v = ring_circumference / period; // m/s
	double c = 299792458; // m/s
	double beta = v / c;
	double E0 = 938.257e6; // rest energy of proton, eV

	/* Return pulse energy in eV */
	return (E0 / sqrt(1 - (beta * beta))) - E0;
}

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

	m_allowNonOneToOnePixelMapping =
			conf.get<bool>("sms.allow_non_one_to_one_pixel_mappings",
				false);
	INFO("Setting Allow Non-One-to-One Pixel Mapping to "
		<< m_allowNonOneToOnePixelMapping << ".");

	m_notesCommentAutoReset =
			conf.get<bool>("sms.run_notes_auto_reset", true);
	INFO("Setting Run Notes Auto Reset to "
		<< m_notesCommentAutoReset << ".");

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
	bool mixed_data_packets;
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
	mixed_data_packets = info.get<bool>("mixed_data_packets", false);
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

	// Should probably let someone know if we're Mixing Data Packets
	// with Both Neutron Events and Meta-Data Events... ;-D
	if (mixed_data_packets) {
		DEBUG("Mixed Data Packets Flag Set to True for " << name
			<< " - No Auto-Deduce Sub-60Hz Pulse for Proton Charge Zero!");
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
							 mixed_data_packets,
							 chunk_size,
							 rtdlNoDataThresh,
							 save_input_stream));
	m_dataSources.push_back(src);

	// Update Number of Data Sources PV...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvNumDataSources->update(m_nextSrcId, &now); // count from 1!

	/* We save source ID 0 for internal use. */
	m_nextSrcId++;
	/* TODO check against the max number of sources? */
}

SMSControl::SMSControl() :
	m_currentRunNumber(0), m_recording(false), m_nextSrcId(1),
	m_noRegisteredEventSources(true), m_noRegisteredEventSourcesCount(0),
	m_lastPulseId(0), m_lastRingPeriod(0),
	m_monitorReserve(1024), m_bankReserve(4096),
	m_chopperReserve(128), m_fastMetaReserve(16),
	m_meta(new MetaDataMgr), m_fastmeta(new FastMeta(m_meta))
{
	// Initialize Control PVs...
	std::string prefix(m_beamlineId);
	prefix += ":SMS";

	m_pvVersion = boost::shared_ptr<smsStringPV>(new
						smsStringPV(prefix + ":Version"));

	m_pvRecording = boost::shared_ptr<smsRecordingPV>(new
						smsRecordingPV(prefix, this));
	m_pvRunNumber = boost::shared_ptr<smsRunNumberPV>(new
						smsRunNumberPV(prefix));

	m_markers = boost::shared_ptr<Markers>(new
						Markers(this, m_notesCommentAutoReset));

	m_pvSummary = boost::shared_ptr<smsErrorPV>(new
						smsErrorPV(prefix + ":Summary"));

	m_pvSummaryReason = boost::shared_ptr<smsStringPV>(new
						smsStringPV(prefix + ":SummaryReason"));

	m_pvNoEoPPulseBufferSize = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":Control:"
							+ "NoEoPPulseBufferSize"));

	m_pvMaxPulseBufferSize = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":Control:"
							+ "MaxPulseBufferSize"));

	m_pvPopPulseBuffer = boost::shared_ptr<PopPulseBufferPV>(new
						PopPulseBufferPV(prefix + ":Control:"
							+ "PopPulseBuffer"));

	m_pvNoRTDLPulses = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(prefix + ":Control:"
							+ "NoRTDLPulses"));

	m_pvDoPulsePchgCorrect = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(prefix + ":Control:"
							+ "DoPulsePchgCorrect"));

	m_pvDoPulseVetoCorrect = boost::shared_ptr<smsBooleanPV>(new
						smsBooleanPV(prefix + ":Control:"
							+ "DoPulseVetoCorrect"));

	m_pvNumDataSources = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":Control:"
							+ "NumDataSources"));

	m_pvNumLiveClients = boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":Control:"
							+ "NumLiveClients"));

	// The Kill Switch. ["NEVER USE THIS!" Lol... (Except for Valgrind) :-]
	m_pvCleanShutdown = boost::shared_ptr<CleanShutdownPV>(new
						CleanShutdownPV(prefix + ":CleanShutdown"));

	addPV(m_pvVersion);
	addPV(m_pvRecording);
	addPV(m_pvRunNumber);
	addPV(m_pvSummary);
	addPV(m_pvSummaryReason);
	addPV(m_pvNoEoPPulseBufferSize);
	addPV(m_pvMaxPulseBufferSize);
	addPV(m_pvPopPulseBuffer);
	addPV(m_pvNoRTDLPulses);
	addPV(m_pvDoPulsePchgCorrect);
	addPV(m_pvDoPulseVetoCorrect);
	addPV(m_pvNumDataSources);
	addPV(m_pvNumLiveClients);
	addPV(m_pvCleanShutdown);

	// Initialize Config/Info PVs...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	// Initialize Version PV to Usual SMS Daemon Version String...
	m_pvVersion->update(m_version, &now);

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

	// Initialize Fast "Last Pulse" Lookup...
	PulseIdentifier m_lastPid(-1,-1);

	// Initialize the Live Client Index List PV...
	m_pvNumLiveClients->update(0, &now);

	m_nextRunNumber = StorageManager::getNextRun();
	if (!m_nextRunNumber)
		throw std::runtime_error("Unable to Get Next Run Number");

	m_beamlineInfo.reset(new BeamlineInfo(m_targetStationNumber,
			m_beamlineId, m_beamlineShortName, m_beamlineLongName));
	m_runInfo.reset(new RunInfo(m_facility, m_beamlineId, this,
		m_sendSampleInRunInfo));
	m_geometry.reset(new Geometry(m_geometryPath));
	m_pixelMap.reset(new PixelMap(m_pixelMapPath,
		m_allowNonOneToOnePixelMapping));

	m_maxBanks = m_pixelMap->numBanks() + PixelMap::REAL_BANK_OFFSET;

	// Notify the IPTS-ITEMS IOC that "We're Alive" and request that it
	// Re-Send the IPTS Proposal and ITEMS Sample Information PVs...

	int ca_status;
	if ( !( (ca_status =
			ca_context_create( ca_disable_preemptive_callback ))
				& CA_M_SUCCESS ) )
	{
		ERROR("IPTS-ITEMS Resend - "
			<< "Channel Access Error in "
			<< "ca_context_create( ca_disable_preemptive_callback ): "
			<< ca_message(ca_status) );
		return;
	}
	// Log as "Error" so we'll get notified if this is working... ;-D
	ERROR("IPTS-ITEMS Resend - "
		<< "Channel Access Context Successfully Created");

	std::string resendPVName = m_beamlineId + ":CS:IPTS_ITEMS:SMS:Resend";
	chid resend_chid;
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

SMSControl::~SMSControl()
{
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

void SMSControl::addPV(PVSharedPtr pv)
{
	if (m_pv_map.count(pv->getName())) {
		std::string msg("Adding duplicate PV: ");
		msg += pv->getName();
		throw std::logic_error(msg);
	}
	m_pv_map[pv->getName()] = pv;
}

bool SMSControl::setRecording( bool v )
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
		INFO("Starting Run " << m_currentRunNumber);
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
				m_markers->beforeNewRun( m_currentRunNumber );

				// Actually Start a New Recording...
				StorageManager::startRecording( m_currentRunNumber,
					m_runInfo->getPropId() );
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
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvRunNumber->update(m_currentRunNumber, &now);
		m_pvRecording->update(v, &now);
	}

	// Stop the Current Recording...
	else {

		// Stopping Run, Clear Run Number
		// and "Unlock" RunInfo for PV Updates...
		INFO("Stopping Run " << m_currentRunNumber);
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
				m_markers->runStop();

				// Actually Stop Recording...
				StorageManager::stopRecording();
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
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvRunNumber->update(0, &now);
		m_pvRecording->update(v, &now);
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

void SMSControl::pauseRecording(void)
{
	if ( m_currentRunNumber )
		INFO("Pausing Run " << m_currentRunNumber);
	else
		INFO("Pausing Data Collection (Not Currently Recording)");

	StorageManager::pauseRecording();
}

void SMSControl::resumeRecording(void)
{
	if ( m_currentRunNumber )
		INFO("Resuming Run " << m_currentRunNumber);
	else
		INFO("Resuming Data Collection (Not Currently Recording)");

	StorageManager::resumeRecording();
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
		}
	}

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

	m_pvSummary->update( m_summaryIsError, &now, major );
	m_pvSummaryReason->update( m_reason, &now );
}

uint32_t SMSControl::registerEventSource(uint32_t hwId)
{
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "registerEventSource(): hwId=" << hwId);

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
					ERROR("registerEventSource():"
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
			// Set New Event Source Bit & Return Index...
			m_eventSources.set(i);
			DEBUG("registerEventSource(): returning smsId=" << i);
			return i;
		}
	}

	// Oops... Out of Event Source Indices...?! Wow, Lotta Data Sources?!!
	DEBUG("registerEventSource(): Out of Event Source (smsIds)!");
	throw std::runtime_error("No more event sources available");
}

void SMSControl::unregisterEventSource(uint32_t smsId)
{
	DEBUG( ( m_recording ? "[RECORDING] " : "" )
		<< "unregisterEventSource(): smsId=" << smsId );

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
			it->second->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
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
		// (Even Any "Sawtooth" Pulses that arrived Out of Time Order, as
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
					/* Rate-limited logging of global sawtooth pulse */
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

	// Check for "No Registered Event Sources" State Change...
	if ( m_eventSources.none() ) {
		// Log/Notify on State Change...
		ERROR("unregisterEventSource():"
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

	// New Pulse...! If Any Pulses in list, Check for SAWTOOTH...
	// Note: Map is _Already_ Sorted by Pulse Time (PulseIdentifier),
	// so we can just grab Min and Max Time from the "ends" of the list
	// to check for any SAWTOOTH Pulses... ;-b
	if (m_pulses.begin() != m_pulses.end()) {

		// Get Min Pulse Time
		it = m_pulses.begin();
		uint64_t min_id = it->first.first;

		// Get Max Pulse Time
		it = m_pulses.end(); it--;
		uint64_t max_id = it->first.first;

		// Log any SAWTOOTH pulses... :-o
		if (id < min_id) {
			/* Rate-limited logging of Global SAWTOOTH Pulse */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_GLOBAL_SAWTOOTH_PULSE, "none",
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_recording ? "[RECORDING] " : "" )
					<< "getPulse: Global SAWTOOTH Pulse(0x"
					<< std::hex << id << ", 0x" << dup << ")"
					<< " min=0x" << min_id
					<< " max=0x" << max_id << std::dec);
			}
		}
		else if (id >= min_id && id < max_id) {
			/* Rate-limited logging of Global SAWTOOTH Pulse */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
					RLL_INTERLEAVED_GLOBAL_SAWTOOTH, "none",
					2, 10, 100, log_info ) ) {
				ERROR(log_info
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
					2, 10, 100, log_info ) ) {
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
	new_pulse->m_flags |= ADARA::BankedEventPkt::PCHARGE_UNCORRECTED;
	new_pulse->m_flags |= ADARA::BankedEventPkt::VETO_UNCORRECTED;

	if (dup)
		new_pulse->m_flags |= ADARA::BankedEventPkt::DUPLICATE_PULSE;

	// [LESS FREQUENTLY] Check for Internal Pulse Buffer Size Overflow...
	// (Happens when One or More of Multiple Event Sources suddenly
	// Stop Sending Event Data, and we Keep Waiting, Hanging Onto Pulses
	// so they can All be Marked "Complete" Across All Data Sources... ;-)
	// - When This Happens, Regardless of the Cause, We Need to Flush
	// Some Pulses from Our Internal Pulse Buffer, so we Don't Swell Up
	// and Pop...! ;-D

	static uint32_t cnt = 0;

	// Once Per Second...
	uint32_t freq = 60;

	if ( !(++cnt % freq) )
	{
		// Update Max Pulse Buffer Size from PV...
		m_maxPulseBufferSize = m_pvMaxPulseBufferSize->value();

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
						ADARA::BankedEventPkt::PARTIAL_DATA;
				}
				// Correct Proton Charge...
				// (This Pulse's PCharge comes from _Next_ Pulse...!)
				if ( m_doPulsePchgCorrect || m_doPulseVetoCorrect ) {
					PulseMap::iterator next = it;
					if (++next != m_pulses.end())
						correctPChargeVeto(it->second, next->second);
					else {
						/* Rate-limited logging of global sawtooth pulse */
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
	it = m_pulses.insert( make_pair( pid, new_pulse ) ).first;

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

			std::string prefix(m_beamlineId);
			prefix += ":SMS";
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
	tof &= ((1U << 21) - 1);
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
	tof &= (1U << 21) - 1;
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
		EventSource new_src(pkt.intraPulseTime(), pkt.tofField(),
				m_maxBanks);
		SourceMap::value_type val(hwId, new_src);
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
	pulse->m_charge = pkt.pulseCharge();

	ADARA::Event translated;
	const ADARA::Event *events = pkt.events();
	uint32_t i, count = pkt.num_events();
	uint32_t phys, logical;
	uint16_t bank = 0;

	bool got_neutrons = false;
	bool got_metadata = false;

	for (i=0; i < count; i++) {

		phys = events[i].pixel;

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
				// Already Mapped to Logical PixelId at Data Source...!
				if (is_mapped) {
					// PixelId Already Is Logical...!
					logical = phys;
					// Just Lookup Bank from Logical PixelId...
					if (m_pixelMap->mapEventBank(logical, bank)) {
						pulse->m_flags |=
							ADARA::BankedEventPkt::MAPPING_ERROR;
					}
					bank += PixelMap::REAL_BANK_OFFSET;
				}
				// Not Yet Mapped...
				// Map Physical PixelId to Logical PixelId
				// and Identify Detector Bank...
				else {
					if (m_pixelMap->mapEvent(phys, logical, bank)) {
						pulse->m_flags |=
							ADARA::BankedEventPkt::MAPPING_ERROR;
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
				if (m_fastmeta->validVariable(phys)) {
					if (pulse->m_fastMetaEvents.empty()) {
						pulse->m_fastMetaEvents.reserve(m_fastMetaReserve);
					}
					pulse->m_fastMetaEvents.push_back(events[i]);
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

				pulse->m_flags |= ADARA::BankedEventPkt::ERROR_PIXELS;
				err_count++;

		} // switch (phys >> 28)

		if (src->second.m_banks[bank].empty()) {
			/* pulse->m_numBanks will double count banks if
			 * their events arrive via two different HW sources.
			 * This is good, as we'll need the room in the packet
			 * for a bank section header in each source section.
			 */
			pulse->m_numBanks++;
			src->second.m_activeBanks++;

			src->second.m_banks[bank].reserve(m_bankReserve);
		}

		translated.pixel = logical;
		translated.tof = events[i].tof;
		src->second.m_banks[bank].push_back(translated);
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
		pulse->m_flags |= ADARA::BankedEventPkt::GOT_NEUTRONS;
	}
	// Also Note the Presence of Meta-Data Events, for Completeness
	if ( got_metadata
			|| ( !pkt.gotDataFlags() && mixed_data_packets )
			|| ( pkt.gotDataFlags()
				&& pkt.dataFlags() & ADARA::DataFlags::GOT_METADATA ) ) {
		pulse->m_flags |= ADARA::BankedEventPkt::GOT_METADATA;
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
			pulse->m_flags |= ADARA::BankedEventPkt::GOT_NEUTRONS;
		if ( pkt.dataFlags() & ADARA::DataFlags::GOT_METADATA )
			pulse->m_flags |= ADARA::BankedEventPkt::GOT_METADATA;
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
					<< " - Pulse Not Pending...?"
					<< " Marking Partial...");
			}
		}

		// Mark Pulse "Partial" Because there's No Event Data,
		// but then go ahead and mark it "Complete" to record it, lol...!
		pulse->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
		markComplete(pkt.pulseId(), dup, -1);
	}
}

void SMSControl::markPartial(uint64_t pulseId, uint32_t dup)
{
	PulsePtr &pulse = getPulse(pulseId, dup)->second;

	pulse->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
}

void SMSControl::markComplete(uint64_t pulseId, uint32_t dup,
			uint32_t smsId)
{
	static uint32_t queue_log_count = 0;

	PulseMap::iterator it, last, last_minus_buffer, last_recorded;
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
				it->second->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
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

	// [LESS FREQUENTLY] Get Latest "No End-of-Pulse Buffer Size" Value
	// from PV...
	static uint32_t cnt = 0;

	// Once Every 10 Seconds...
	uint32_t freq = 600;

	if ( !(++cnt % freq) ) {
		m_noEoPPulseBufferSize = m_pvNoEoPPulseBufferSize->value();
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
	// (Even Any "Sawtooth" Pulses that arrived Out of Time Order, as
	// These should be Correctly Handled by Sufficient Buffer Size!)

	// Include "Last Complete Pulse" (With Any Buffering Adjustments)
	last_minus_buffer++;

	// [LESS FREQUENTLY] Get Latest Pulse Charge and Veto Corrections
	// Settings from PVs...
	// (Note: count already incremented above for
	//    "No End-of-Pulse Buffer Size"...)
	if ( !(cnt % freq) ) {
		m_doPulsePchgCorrect = m_pvDoPulsePchgCorrect->value();
		m_doPulseVetoCorrect = m_pvDoPulseVetoCorrect->value();
	}

	for ( it = m_pulses.begin(); it != last_minus_buffer; it++ ) {

		// Previous Pulses will Never be Made Complete,
		// Already Marked as Partial as Needed Above...

		// Correct Proton Charge...
		// (This Pulse's PCharge comes from _Next_ Pulse...!)
		if ( m_doPulsePchgCorrect || m_doPulseVetoCorrect ) {
			PulseMap::iterator next = it;
			if (++next != m_pulses.end())
				correctPChargeVeto(it->second, next->second);
			else {
				/* Rate-limited logging of global sawtooth pulse */
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
			pulse->m_flags &= ~ADARA::BankedEventPkt::PCHARGE_UNCORRECTED;

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
			pulse->m_flags &= ~ADARA::BankedEventPkt::VETO_UNCORRECTED;

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
				2, 10, 100, log_info ) ) {
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
						false);
		}

		// NO RTDL for Pulse!
		else {

			// Only Check Live Control PV _Very Infrequently_...
			//    - This Option is Not Likely to Ever Change... ;-D
			//    - On HFIR, Where this is Needed, We Run at "1 HZ"...
			//       -> so this should only check the PV once per hour...
			//    - Otherwise, on SNS, this is like "once per minute"...
			if ( !(++cnt % 3600) ) {
				m_noRTDLPulses = m_pvNoRTDLPulses->value();
			}

			// Skip Error Logging if We Don't Expect Any RTDLs Anyway...
			if ( !m_noRTDLPulses ) {
				/* Rate-limited logging of no RTDL for pulse */
				std::string log_info;
				if ( RateLimitedLogging::checkLog( RLLHistory_SMSControl,
						RLL_NO_RTDL_FOR_PULSE, "none",
						2, 10, 100, log_info ) ) {
					ERROR(log_info
						<< ( m_recording ? "[RECORDING] " : "" )
						<< "recordPulse: NO RTDL for Pulse"
						<< " id=0x" << std::hex << pulse->m_id.first
						<< " dup=0x" << pulse->m_id.second << std::dec);
				}
			}

			// Always Mark Pulse as Missing RTDL if it Doesn't Have One!
			pulse->m_flags |= ADARA::BankedEventPkt::MISSING_RTDL;
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
			pulse->m_flags |= ADARA::BankedEventPkt::PULSE_VETO;
		}

		// Build Various Packets for Pulse...
		buildMonitorPacket(pulse);
		buildBankedPacket(pulse);
		buildChopperPackets(pulse);
		buildFastMetaPackets(pulse);
	} catch (std::runtime_error e) {
		ERROR( ( m_recording ? "[RECORDING] " : "" )
			<< "Failed to record pulse: " << e.what());

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
	if ( pulse->m_flags & ADARA::BankedEventPkt::GOT_NEUTRONS ) {
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

	/* Banked event header */
	if ( pulse->m_flags & ADARA::BankedEventPkt::GOT_NEUTRONS ) {
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

		for (uint32_t i = 0; i < src.m_banks.size(); i++) {

			if (src.m_banks[i].empty())
				continue;

			iov.iov_base = &m_hdrs.front() + m_hdrs.size();
			iov.iov_len = 2 * sizeof(uint32_t);
			m_iovec.push_back(iov);

			// Because we're using Unsigned Integers, we'll translate:
			//    - Error Pixels to Bank -2
			//       (PixelMap::ERROR_BANK = 0xfffe -> 0xfffffffe)
			//    - Unmapped Pixels to Bank -1
			//       (PixelMap::UNMAPPED_BANK = 0xffff -> 0xffffffff)
			// All other bank ids will get their real number.
			m_hdrs.push_back(i - PixelMap::REAL_BANK_OFFSET);
			m_hdrs.push_back(src.m_banks[i].size());

			iov.iov_base = &src.m_banks[i].front();
			iov.iov_len = src.m_banks[i].size();
			iov.iov_len *= sizeof(ADARA::Event);
			m_iovec.push_back(iov);
		}
	}

	StorageManager::addPacket(m_iovec);
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
			if (ns >= (1000U * 1000 * 1000)) {
				ns -= 1000U * 1000 * 1000;
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
	EventVector::iterator it, end = pulse->m_fastMetaEvents.end();

	for (it = pulse->m_fastMetaEvents.begin(); it != end; ++it)
		m_fastmeta->sendUpdate(pulse_id, it->pixel, it->tof);
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

