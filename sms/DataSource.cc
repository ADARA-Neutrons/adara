
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.DataSource"));

#include <stdexcept>
#include <sstream>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "StorageManager.h"
#include "Markers.h"
#include "DataSource.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "utils.h"

RateLimitedLogging::History RLLHistory_DataSource;

// Rate-Limited Logging IDs...
#define RLL_DROPPED_PACKETS           0
#define RLL_LOCAL_DUPLICATE_PULSE     1
#define RLL_LOCAL_SAWTOOTH_PULSE      2
#define RLL_RAWDATA_PULSE_IN_PAST     3
#define RLL_RAWDATA_PULSE_IN_FUTURE   4
#define RLL_MISSING_PULSE_SEQUENCE    5
#define RLL_LOCAL_PULSE_SEQUENCE      6
#define RLL_LOCAL_SOURCE_SEQUENCE     7
#define RLL_WONT_CONN                 8
#define RLL_TRYING_CONN               9
#define RLL_CONN_REFUSED             10
#define RLL_CONN_REQUEST_ERROR       11
#define RLL_CONN_FAILED              12
#define RLL_PARSE_MAX_READ_CHUNK     13
#define RLL_READ_EXCEPTION           14
#define RLL_READ_DELAY               15
#define RLL_PULSEID_ZERO             16
#define RLL_UNKNOWN_PACKET           17
#define RLL_OVERSIZE_PACKET          18
#define RLL_LOCAL_DUPLICATE_RTDL     19
#define RLL_LOCAL_SAWTOOTH_RTDL      20
#define RLL_LOCAL_RTDL_SEQUENCE      21
#define RLL_RTDL_PULSE_IN_PAST       22
#define RLL_RTDL_PULSE_IN_FUTURE     23
#define RLL_ANNOTATION               24
#define RLL_HEARTBEAT                25

// Pulse Time Sanity Check Constants
#define FACILITY_START_TIME 512715600 // EPICS Sat Apr  1 00:00:00 EST 2006
#define SECS_PER_WEEK 604800 // 60 * 60 * 24 * 7

class DataSourceRequiredPV : public smsBooleanPV {
public:
	DataSourceRequiredPV( const std::string &name,
			DataSource *dataSource, bool auto_save = false ) :
		smsBooleanPV(name, auto_save), m_dataSource(dataSource),
		m_auto_save(auto_save)
	{ }

	void changed(void)
	{
		bool is_required = value();

		std::string isRequired = ( is_required )
			? "*Required*" : "*Not Required*";

		ERROR("DataSourceRequiredPV: Data Source " << m_dataSource->name()
			<< " is being Marked as " << isRequired
			<< " for Data Collection!");

		m_dataSource->setRequired( is_required );

		if ( m_auto_save && !m_first_set )
		{
			// AutoSave PV Value Change...
			struct timespec ts;
			m_value->getTimeStamp(&ts);
			// Use String Representation of Boolean for AutoSave File...
			std::string bvalstr = ( is_required ) ? "true" : "false";
			StorageManager::autoSavePV( m_pv_name, bvalstr, &ts );
		}
	}

private:
	DataSource *m_dataSource;

	bool m_auto_save;
};


/* Pulse invariants -- these should not change between raw event
 * packets for a given pulse, so we can use them to help detect
 * duplicate pulse IDs
 */
struct PulseInvariants {
	uint64_t	m_pulseId;
	ADARA::PulseFlavor::Enum m_flavor;
	uint32_t	m_intraPulseTime;
	uint32_t	m_pulseCharge;
	uint16_t	m_cycle;
	uint16_t	m_vetoFlags;
	uint8_t		m_timingStatus;
	bool		m_pulseGood;	// not part of invariants, bookkeeping
	uint32_t	m_dupCount;		// not part of invariants, bookkeeping
};

bool operator==( const PulseInvariants &p1, const PulseInvariants &p2 )
{
	return( p1.m_pulseId == p2.m_pulseId
		&& p1.m_flavor == p2.m_flavor
		&& p1.m_pulseCharge == p2.m_pulseCharge
		&& p1.m_vetoFlags == p2.m_vetoFlags
		&& p1.m_cycle == p2.m_cycle
		&& p1.m_timingStatus == p2.m_timingStatus
		&& p1.m_intraPulseTime == p2.m_intraPulseTime );
	// Note: m_pulseGood and m_dupCount are _Not_ part of invariants,
	// they're just tagalongs for bookkeeping... Don't compare... :-D
}

typedef std::list< std::pair<PulseInvariants, uint32_t> > PulseSeqList;


class HWSource {
public:
	HWSource( const std::string &name, int32_t hwIndex,
			uint32_t hwId, uint32_t smsId, uint32_t maxPulseSeqList,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceHwId,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceSmsId,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceEventBandwidthSecond,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceEventBandwidthMinute,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceEventBandwidthTenMin,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceMetaBandwidthSecond,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceMetaBandwidthMinute,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceMetaBandwidthTenMin,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceErrBandwidthSecond,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceErrBandwidthMinute,
			boost::shared_ptr<smsUint32PV> &
				pvHWSourceErrBandwidthTenMin,
			DataSource *dataSource ) :
		m_hwIndex(hwIndex),
		m_pvHWSourceHwId(pvHWSourceHwId),
		m_pvHWSourceSmsId(pvHWSourceSmsId),
		m_pvHWSourceEventBandwidthSecond(pvHWSourceEventBandwidthSecond),
		m_pvHWSourceEventBandwidthMinute(pvHWSourceEventBandwidthMinute),
		m_pvHWSourceEventBandwidthTenMin(pvHWSourceEventBandwidthTenMin),
		m_pvHWSourceMetaBandwidthSecond(pvHWSourceMetaBandwidthSecond),
		m_pvHWSourceMetaBandwidthMinute(pvHWSourceMetaBandwidthMinute),
		m_pvHWSourceMetaBandwidthTenMin(pvHWSourceMetaBandwidthTenMin),
		m_pvHWSourceErrBandwidthSecond(pvHWSourceErrBandwidthSecond),
		m_pvHWSourceErrBandwidthMinute(pvHWSourceErrBandwidthMinute),
		m_pvHWSourceErrBandwidthTenMin(pvHWSourceErrBandwidthTenMin),
		m_dataSource(dataSource),
		m_name(name), m_hwId(hwId), m_smsId(smsId),
		m_maxPulseSeqList(maxPulseSeqList),
		m_intermittent(false), m_recoverPktCount(0),
		m_lastPulse(0)
	{
		// Snag an SMSControl Instance Handle _Exactly Once_...! ;-o
		m_ctrl = SMSControl::getInstance();

		// Initialize "RTDL Packets with No Data Packets" Count...
		m_rtdlNoDataCount = 0;

		// Initialize Event Bandwidth Statistics
		m_event_count_second = 0;
		m_event_count_minute = 0;
		m_event_count_tenmin = 0;
		m_meta_count_second = 0;
		m_meta_count_minute = 0;
		m_meta_count_tenmin = 0;
		m_err_count_second = 0;
		m_err_count_minute = 0;
		m_err_count_tenmin = 0;

		// Source Sequence: Increases Per Source Packet, No "Reset"...
		// - Initialize to 0xffffffff, a Value the 16-bit Source Sequence
		// will Never Take, so we can _Ignore_ the First Sequence Value
		// Until we "Prime the Pump"... ;-D
		m_sourceSeq = (uint32_t) -1;

		// Make Sure Max Pulse Sequence List Size is Reasonable...
		// (It better at least be 1...?)
		if ( m_maxPulseSeqList < 1 ) {
			uint32_t tmpMaxPulseSeqList = 1;
			ERROR("HWSource::HWSource():"
				<< " Invalid Setting for"
				<< " Max Pulse Sequence List (Buffer) Size"
				<< " for " << m_name
				<< std::hex << " src=0x" << m_hwId << std::dec
				<< " Correcting from " << m_maxPulseSeqList
				<< " to " << tmpMaxPulseSeqList);
			m_maxPulseSeqList = tmpMaxPulseSeqList;
		}

		// Pulse Sequence List, Track Per HWSource/Source ID, Per Pulse...
		// Increases Per Event Packet (Resets Per Pulse)
		m_pulseSeqList.clear();

		// Initialize HWSource Bandwidth PVs...
		if ( m_hwIndex >= 0 ) {
			struct timespec now;
			clock_gettime(CLOCK_REALTIME_COARSE, &now);
			m_pvHWSourceHwId->update(m_hwId, &now);
			m_pvHWSourceSmsId->update(m_smsId, &now);
			m_pvHWSourceEventBandwidthSecond->update(
				m_event_count_second, &now);
			m_pvHWSourceEventBandwidthMinute->update(
				m_event_count_minute, &now);
			m_pvHWSourceEventBandwidthTenMin->update(
				m_event_count_tenmin, &now);
			m_pvHWSourceMetaBandwidthSecond->update(
				m_meta_count_second, &now);
			m_pvHWSourceMetaBandwidthMinute->update(
				m_meta_count_minute, &now);
			m_pvHWSourceMetaBandwidthTenMin->update(
				m_meta_count_tenmin, &now);
			m_pvHWSourceErrBandwidthSecond->update(
				m_err_count_second, &now);
			m_pvHWSourceErrBandwidthMinute->update(
				m_err_count_minute, &now);
			m_pvHWSourceErrBandwidthTenMin->update(
				m_err_count_tenmin, &now);
		}
	}

	uint32_t hwId(void) const { return m_hwId; }
	uint32_t smsId(void) const { return m_smsId; }

	void setSmsId( uint32_t smsId )
	{
		m_smsId = smsId;

		// Update HWSource "smsId" PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME_COARSE, &now);
		m_pvHWSourceSmsId->update(m_smsId, &now);
	}

	uint32_t intermittent(void) const { return m_intermittent; }

	void setIntermittent( bool intermittent )
	{
		m_intermittent = intermittent;
	}

	uint32_t incrRecoverPktCount(void) { return (++m_recoverPktCount); }

	void resetRecoverPktCount(void) { m_recoverPktCount = 0; }

	PulseSeqList	m_pulseSeqList;		// Increases Per Event Packet
										// (Resets Per Pulse)

	PulseSeqList::iterator findPulseSequence( PulseInvariants &pulse,
			uint32_t &dupCount )
	{
		PulseSeqList::iterator found = m_pulseSeqList.end();

		PulseSeqList::iterator psit;

		dupCount = 0;

		for ( psit=m_pulseSeqList.begin() ;
				psit != m_pulseSeqList.end() ; ++psit )
		{
			// Count Any "Duplicate" Pulses (Same Time, Diff Invariants)
			if ( psit->first.m_pulseId == pulse.m_pulseId )
				dupCount++;

			// Matching Pulse Invariants Found!
			if ( psit->first == pulse )
			{
				found = psit;
				// Don't Break Out of Loop Yet Here,
				// Need to Obtain Full "Duplicate" Pulse Count...! ;-D
			}
		}

		// Use Any Original Duplicate Count from Initial Pulse Creation...
		if ( found != m_pulseSeqList.end() )
		{
			dupCount = pulse.m_dupCount;
		}

		// Log Any New (Not Found) Duplicate Pulses in event packets...
		if ( dupCount && found == m_pulseSeqList.end() ) {
			/* Rate-limited logging of duplicate pulses */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_DUPLICATE_PULSE, m_name,
					2, 10, 5000, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "findPulseSequence(): New Pulse (RawDataPkt):"
					<< " Local Duplicate pulse from "
					<< m_name
					<< std::hex << " src=0x" << m_hwId
					<< " pulseId=0x" << pulse.m_pulseId << std::dec);
				dumpPulseInvariants( "New Duplicate Pulse", pulse );
				PulseSeqList::iterator dupsit;
				for ( dupsit=m_pulseSeqList.begin() ;
						dupsit != m_pulseSeqList.end() ; ++dupsit )
				{
					// Log Any "Duplicate" Pulses
					// (Same Time, Diff Invariants)
					if ( dupsit->first.m_pulseId == pulse.m_pulseId )
					{
						dumpPulseInvariants( "Matching Duplicate Pulse",
							dupsit->first );
					}
				}
			}
		}

		return( found );
	}

	PulseSeqList::iterator getPulse( const ADARA::RawDataPkt &pkt,
			bool &isNewPulse )
	{
		static uint32_t cnt = 0;

		// Extract Pulse Invariants Meta-Data from Raw Data Packet...
		PulseInvariants pulse;
		pulse.m_pulseId = pkt.pulseId();
		pulse.m_flavor = pkt.flavor();
		pulse.m_intraPulseTime = pkt.intraPulseTime();
		pulse.m_pulseCharge = pkt.pulseCharge();
		pulse.m_cycle = pkt.cycle();
		pulse.m_vetoFlags = pkt.vetoFlags();
		pulse.m_timingStatus = pkt.timingStatus();
		pulse.m_pulseGood = false;
		pulse.m_dupCount = (uint32_t) -1;

		uint32_t dupCount;

		// See if This Pulse Already Exists in the Pulse Sequence List
		PulseSeqList::iterator psit = findPulseSequence( pulse, dupCount );

		isNewPulse = false;

		// Pulse Not Found in List, Add It...
		if ( psit == m_pulseSeqList.end() )
		{
			// Assign Any "Duplicate Pulse" Count to Pulse...
			pulse.m_dupCount = dupCount;

			// Create New Pulse Sequence Entry, Push to Front of List...
			std::pair<PulseInvariants, uint32_t> pulse_src( pulse, 0 );
			m_pulseSeqList.push_front( pulse_src );
			isNewPulse = true;

			// Find the Most Recent/"Last" PulseId (Time)
			// for Strict SAWTOOTH Checking...
			// - Just Use Running Max PulseId Time, Actually
			// More Accurate than the "Last Pulse We Saw"... ;-D
			// (Unless of course a Bogus Future Pulse Time comes in... ;-b)
			if ( pulse.m_pulseId > m_lastPulse )
			{
				m_lastPulse = pulse.m_pulseId;
			}

			// Periodically Check for Latest Max Pulse Sequence List Size
			// (About Once Per Minute...)
			if ( !(++cnt % 3333) ) {
				uint32_t tmpMaxPulseSeqList =
					m_dataSource->getMaxPulseSeqList();
				if ( tmpMaxPulseSeqList != m_maxPulseSeqList )
				{
					if ( tmpMaxPulseSeqList < 1 ) {
						ERROR("HWSource::HWSource():"
							<< " Invalid Requested Setting for"
							<< " Max Pulse Sequence List (Buffer) Size"
							<< " for " << m_name
							<< std::hex << " src=0x" << m_hwId << std::dec
							<< " Correcting from " << tmpMaxPulseSeqList
							<< " to " << 1);
						tmpMaxPulseSeqList = 1;
					} else {
						ERROR("HWSource::getPulse():"
							<< " Updating Value of"
							<< " Max Pulse Sequence List (Buffer) Size"
							<< " for " << m_name
							<< std::hex << " src=0x" << m_hwId << std::dec
							<< " from " << m_maxPulseSeqList
							<< " to " << tmpMaxPulseSeqList);
					}
					m_maxPulseSeqList = tmpMaxPulseSeqList;
				}
			}

			// Keep Pulse Sequence List Size Small/"Fixed"... ;-D
			// Once Maximum Pulse Sequence List Size is Reached,
			// Pop One Pulse Off the End for Every New Pulse in Front!
			while ( m_pulseSeqList.size() > m_maxPulseSeqList )
			{
				PulseSeqList::iterator old_pulse_it =
					--(m_pulseSeqList.end());

				// Check for Bogus Future Pulse Time Corruption
				// of our Most Recent/Last/Max Pulse Time... ;-b
				if ( old_pulse_it->first.m_pulseId == m_lastPulse ) {
					ERROR("HWSource::getPulse():"
						<< " Hmmm... Oldest Pulse in Pulse Sequence List"
						<< " has Last Pulse Time...?!"
						<< " m_lastPulse=0x"
						<< std::hex << m_lastPulse << std::dec
						<< " Possible Corruption of Last Pulse Time"
						<< " from Bogus Future Pulse Time..."
						<< std::hex << " src=0x" << m_hwId << std::dec
						<< " (" << m_name << ")");
					m_lastPulse = 0;
					PulseSeqList::iterator psit_max =
						m_pulseSeqList.begin();
					while ( psit_max != old_pulse_it ) {
						if ( psit_max->first.m_pulseId > m_lastPulse )
							m_lastPulse = psit_max->first.m_pulseId;
						psit_max++;
					}
					ERROR("HWSource::getPulse():"
						<< " Resetting Last Pulse Time to "
						<< " m_lastPulse=0x"
						<< std::hex << m_lastPulse
						<< " src=0x" << m_hwId << std::dec
						<< " (" << m_name << ")");
					// Reset DataSource Max Time Too! ;-D
					m_dataSource->resetMaxTime();
				}

				// Finish/Release Oldest Pulse...
				// (Only Marks Complete *Once*, If Still "PulseGood"...)
				endPulse( old_pulse_it, false );

				// Remove Last Pulse from Sequence List...
				m_pulseSeqList.pop_back();
			}

			// Now Grab Iterator for Our Newly Pushed First Pulse Entry...
			psit = m_pulseSeqList.begin();
		}

		// Return What We Found or Added...
		return( psit );
	}

	void endPulse( PulseSeqList::iterator &psit, bool eop = true )
	{
		// Hmmm... Missing Pulse Sequence for
		// This HWSource/Source ID and Pulse...?!
		if ( psit == m_pulseSeqList.end() )
		{
			/* Rate-limited logging of Missing Pulse Sequence! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_MISSING_PULSE_SEQUENCE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "endPulse():"
					<< " Hmmm... Missing Pulse Sequence...?!"
					<< " Internal Bookkeeping Error!"
					<< std::hex << " src=0x" << m_hwId << std::dec
					<< " (" << m_name << ")");
			}
			return;
		}

		PulseInvariants &pulse = psit->first;

		// This is _Not_ An Official End-of-Pulse, Mark Pulse as Partial...
		if ( !eop )
		{
#if 0
			/* We currently get this on every other pulse for
			 * 30 Hz operation; disable the message until
			 * we get a better fix.
			 */
			/* Rate-limited logging of dropped packets */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_DROPPED_PACKETS, m_name, 2, 10, 100, log_info ) )
			{
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Dropped packet from " << m_name << " src 0x"
					<< std::hex << m_hwId << std::dec);
			}
#endif
			// Only Process "Good" Pulses (That Haven't Yet Been Processed)
			if ( pulse.m_pulseGood )
				m_ctrl->markPartial( pulse.m_pulseId, pulse.m_dupCount );
		}

		// This Is the Official End-of-Pulse,
		// So Mark Any Preceding/"Incomplete" Pulses as Partial
		// and Dispatch Them On Their Way...
		// (_Before_ We Complete This Current Pulse, As They're Older! ;-D)
		else
		{
			// NOTE: Mark & Dispatch Pulses in *Reverse* Order,
			// i.e. from the End, as in "First In, First Out"...! ;-D
			PulseSeqList::iterator old_psit = m_pulseSeqList.end();
			if ( old_psit != m_pulseSeqList.begin() ) --old_psit;

			// Only Process Up to Current Pulse...
			for ( ;
					old_psit != m_pulseSeqList.begin()
						&& old_psit != psit ;
					--old_psit )
			{
				PulseInvariants &old_pulse = old_psit->first;

				// Only Process "Good" Pulses
				// (That Haven't Yet Been Processed)
				if ( old_pulse.m_pulseGood )
				{
					// Mark Any Preceding/"Incomplete" Pulses as Partial...
					m_ctrl->markPartial(
						old_pulse.m_pulseId, old_pulse.m_dupCount );

					// Dispatch Pulse On Its Way...
					m_ctrl->markComplete(
						old_pulse.m_pulseId, old_pulse.m_dupCount,
						m_smsId );

					// Never Mark the Same Pulse "Complete" Twice...! ;-D
					// (We Keep Pulses Around in the Pulse Sequence List
					// Even After They're "Complete", for More Accurate
					// "Duplicate" Pulse Bookkeeping... ;-D)
					old_pulse.m_pulseGood = false;
				}
			}
		}

		// Only Process "Good" Pulses (That Haven't Yet Been Processed)
		if ( pulse.m_pulseGood )
		{
			// Dispatch This Pulse On Its Way...
			m_ctrl->markComplete( pulse.m_pulseId, pulse.m_dupCount,
				m_smsId );

			// Never Mark the Same Pulse "Complete" Twice...! ;-D
			// (We Keep Pulses Around in the Pulse Sequence List
			// Even After They're "Complete", for More Accurate
			// "Duplicate" Pulse Bookkeeping... ;-D)
			pulse.m_pulseGood = false;
		}
	}

	bool newPulse( const ADARA::RawDataPkt &pkt,
			PulseSeqList::iterator &psit )
	{
		uint64_t pulseId = pkt.pulseId();

		PulseInvariants &pulse = psit->first;

		// Assume Pulse is "Good" Unless Proven Otherwise... ;-D
		pulse.m_pulseGood = true;

		// Check for SAWTOOTH Pulse Times in event packets...
		if ( !(m_dataSource->ignoreLocalSAWTOOTH())
				&& pulseId < m_lastPulse ) {
			/* Rate-limited logging of local sawtooth pulses */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_SAWTOOTH_PULSE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "newPulse(RawDataPkt): Local SAWTOOTH RawData"
					<< " from " << m_name
					<< std::hex << " src=0x" << m_hwId
					<< " m_lastPulse=0x" << m_lastPulse
					<< " pulseId=0x" << pulseId << std::dec
					<< " cycle=" << pkt.cycle()
					<< " vetoFlags=" << pkt.vetoFlags());
			}
		}

		// strip off pulse nanoseconds...
		time_t sec = pulseId >> 32;
		// check for "totally bogus" pulses, in distant past/future... ;-b
		struct timespec now;
		clock_gettime(CLOCK_REALTIME_COARSE, &now);
		time_t future = 
			now.tv_sec - ADARA::EPICS_EPOCH_OFFSET + SECS_PER_WEEK;
		// before SNS time began... ;-D
		if ( sec < FACILITY_START_TIME )
		{
			/* Rate-limited logging of Bogus Pulses from Distant Past! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_RAWDATA_PULSE_IN_PAST, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "*** Dropping Bogus RawData Pulse Time"
					<< " from Distant Past (Before Facility Start Time)!"
					<< std::hex << " src=0x" << m_hwId
					<< " pulseId=0x" << pulseId << std::dec
					<< " (" << sec << " < " << FACILITY_START_TIME << ")"
					<< " (" << m_name << ")");
			}
			pulse.m_pulseGood = false;
		}
		// more than a week into the future...! :-o
		else if ( sec > future )
		{
			/* Rate-limited logging of Bogus Pulses from Distant Future! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_RAWDATA_PULSE_IN_FUTURE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "*** Dropping Bogus RawData Pulse Time"
					<< " from Distant Future (Over One Week from Now)!"
					<< std::hex << " src=0x" << m_hwId
					<< " pulseId=0x" << pulseId << std::dec
					<< " (" << sec << " > " << future << ")"
					<< " (" << m_name << ")");
			}
			pulse.m_pulseGood = false;
		}

		return( pulse.m_pulseGood );
	}

	void dumpPulseInvariants( std::string label,
			const PulseInvariants &pulse )
	{
		WARN("dumpPulseInvariants(): " << label
			<< std::hex << " src=0x" << m_hwId
			<< " pulseId=0x" << pulse.m_pulseId << std::dec
			<< " dupCount=" << pulse.m_dupCount
			<< " flavor=" << pulse.m_flavor
			<< " pulseCharge=" << pulse.m_pulseCharge
			<< " vetoFlags=" << pulse.m_vetoFlags
			<< " cycle=" << pulse.m_cycle
			<< " timingStatus=" << (uint32_t) pulse.m_timingStatus
			<< " intraPulseTime=" << pulse.m_intraPulseTime
			<< " pulseGood=" << pulse.m_pulseGood);
	}

	// Increases Per Event Packet (Resets Per Pulse)
	bool checkPulseSeq( const ADARA::RawDataPkt &pkt,
			PulseSeqList::iterator &psit )
	{
		uint32_t pulseSeq = -1;

		// Hmmm... Missing Pulse Sequence for
		// This HWSource/Source ID and Pulse...?!
		if ( psit == m_pulseSeqList.end() )
		{
			/* Rate-limited logging of Missing Pulse Sequence! */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_MISSING_PULSE_SEQUENCE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "checkPulseSeq():"
					<< " Hmmm... Missing Pulse Sequence...?!"
					<< " Internal Bookkeeping Error!"
					<< std::hex << " src=0x" << m_hwId
					<< " pulseId=0x" << pkt.pulseId() << std::dec
					<< " (" << m_name << ")");
			}
		}

		// Found Pulse Sequence Entry, Grab Current Sequence Counter...
		else
		{
			pulseSeq = psit->second;

			// Increment Pulse Sequence Counter
			psit->second = ( psit->second + 1 ) % pkt.maxPulseSeq();
		}

		// DEBUG("checkPulseSeq():"
			// << std::hex
			// << " src=0x" << m_hwId
			// << " pkt.pulseId()=0x" << pkt.pulseId()
			// << " pkt.pulseSeq()=0x" << pkt.pulseSeq()
			// << " pulseSeq=0x" << pulseSeq
			// << std::dec
			// << " m_pulseSeqList.size()=" << m_pulseSeqList.size());

		bool ok = ( pkt.pulseSeq() == pulseSeq );
		if ( !ok ) {
			// Rate-limited logging of Pulse sequence out-of-order?
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_LOCAL_PULSE_SEQUENCE, m_name,
					2, 10, 100, log_info ) ) {
				ERROR(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "checkPulseSeq(): "
					<< "Local Pulse Sequence Out-of-Order:"
					<< " pkt.pulseSeq()=" << pkt.pulseSeq()
					<< " !=" << " pulseSeq=" << pulseSeq
					<< std::hex << " src=0x" << m_hwId
					<< " pkt.pulseId()=0x" << pkt.pulseId()
					<< " pkt.cycle()=0x" << pkt.cycle()
					<< ( (pkt.cycle() == 0x3FF)
						? " [MISSING RTDL from DSP?]" : "" )
					<< " hwId=0x" << m_hwId << std::dec);
			}
		}

		return ok;
	}

	// Increases Per Source Packet
	bool checkSourceSeq(const ADARA::RawDataPkt &pkt) {
		bool ok = (pkt.sourceSeq() == m_sourceSeq);
		if ( !ok ) {
			// Don't Count/Log the _First_ Source Sequence as an Error
			// - We Don't Yet Know Where the Data Source Sequence is At...!
			if ( m_sourceSeq == ((uint32_t) -1) ) {
				DEBUG("checkSourceSeq() Priming the Pump:"
					<< " m_sourceSeq=" << m_sourceSeq
					<< " -> pkt.sourceSeq()=" << pkt.sourceSeq()
					<< std::hex << " src=0x" << m_hwId
					<< " pkt.pulseId()=0x" << pkt.pulseId()
					<< " hwId=0x" << m_hwId << std::dec);
				ok = true;
			}
			else {
				// Rate-limited logging of Source sequence out-of-order?
				std::string log_info;
				if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
						RLL_LOCAL_SOURCE_SEQUENCE, m_name,
						2, 10, 100, log_info ) ) {
					ERROR(log_info
						<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
						<< "checkSourceSeq() "
						<< "Local Source Sequence Out-of-Order: "
						<< pkt.sourceSeq() << " != " << m_sourceSeq
						<< std::hex << " src=0x" << m_hwId
						<< " pkt.pulseId()=0x" << pkt.pulseId()
						<< " pkt.cycle()=0x" << pkt.cycle()
						<< ( (pkt.cycle() == 0x3FF)
							? " [MISSING RTDL from DSP?]" : "" )
						<< " hwId=0x" << m_hwId << std::dec);
				}
			}
			// Try to Ride the Wave... (or "Prime the Pump" on Connect...!)
			m_sourceSeq = ( pkt.sourceSeq() + 1 ) % pkt.maxSourceSeq();
		}
		else {
			m_sourceSeq = ( m_sourceSeq + 1 ) % pkt.maxSourceSeq();
		}
		return ok;
	}

	// "RTDL Packets with No Data Packets" Count
	uint32_t	m_rtdlNoDataCount;

	// HWSource PV Index from Parent DataSource Class
	int32_t		m_hwIndex;

	// Event Bandwidth Statistics
	uint32_t	m_event_count_second;
	uint32_t	m_event_count_minute;
	uint32_t	m_event_count_tenmin;
	uint32_t	m_meta_count_second;
	uint32_t	m_meta_count_minute;
	uint32_t	m_meta_count_tenmin;
	uint32_t	m_err_count_second;
	uint32_t	m_err_count_minute;
	uint32_t	m_err_count_tenmin;

	boost::shared_ptr<smsUint32PV> m_pvHWSourceHwId;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceSmsId;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceEventBandwidthTenMin;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceMetaBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceMetaBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceMetaBandwidthTenMin;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceErrBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceErrBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvHWSourceErrBandwidthTenMin;

private:

	DataSource *m_dataSource;

	SMSControl *m_ctrl;

	const std::string &m_name;

	uint32_t		m_hwId;
	uint32_t		m_smsId;

	uint32_t		m_maxPulseSeqList;

	bool			m_intermittent;

	uint32_t		m_recoverPktCount;

	uint64_t		m_lastPulse;

	uint32_t		m_sourceSeq;		// Increases Per Source Packet
};


DataSource::DataSource( const std::string &name,
			bool enabled, bool is_required,
			const std::string &uri, uint32_t id,
			double connect_retry, double connect_timeout,
			double data_timeout, uint32_t data_timeout_retry,
			bool ignore_eop, bool ignore_local_sawtooth,
			Markers::PassThru ignore_annotation_pkts,
			bool mixed_data_packets,
			bool check_source_sequence, bool check_pulse_sequence,
			uint32_t max_pulse_seq_list,
			unsigned int read_chunk,
			uint32_t rtdlNoDataThresh, bool save_input_stream ) :
	m_name(uri), m_basename(name), m_uri(uri),
	m_fdreg(NULL), m_timer(NULL), m_addrinfo(NULL),
	m_state(DISABLED), m_smsSourceId(id), m_fd(-1),
	m_connect_retry(connect_retry), m_connect_timeout(connect_timeout),
	m_data_timeout(data_timeout), m_data_timeout_retry(data_timeout_retry),
	m_data_timeout_retry_count(0),
	m_ignore_eop(ignore_eop),
	m_ignore_local_sawtooth(ignore_local_sawtooth),
	m_ignore_annotation_pkts(ignore_annotation_pkts),
	m_mixed_data_packets(mixed_data_packets),
	m_check_source_sequence(check_source_sequence),
	m_check_pulse_sequence(check_pulse_sequence),
	m_max_pulse_seq_list(max_pulse_seq_list),
	m_max_read_chunk(read_chunk), m_rtdlNoDataThresh(rtdlNoDataThresh),
	m_save_input_stream(save_input_stream)
{
	// Snag an SMSControl Instance Handle _Exactly Once_...! ;-o
	m_ctrl = SMSControl::getInstance();

	// Initialize Pulse/Event Bandwidth Statistics

	resetBandwidthStatistics();

	// Create Run-Time Status and Configuration PV Prefix
	//    - Full Set of PVs Per Data Source Index...

	std::string prefix(m_ctrl->getPVPrefix());
	prefix += ":DataSource:";

	std::stringstream ss;
	ss << m_smsSourceId;
	prefix += ss.str();

	// Parse Basic Data Source Info...

	m_name += " (";
	m_name += m_basename;
	m_name += ")";

	// Get "Now" Timestamp for Subsequent PV Updates...
	// (Need it early for "Enabled" PV... ;-D)

	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	// Check for "Enabled" AutoSave _Now_ Before Proceeding...
	//    - "m_enabled" gets used immediately in setRequired(), but we
	//    wait until the very end of the Constructor to Update the PV...!

	struct timespec ts_enabled;
	bool bvalue;

	m_pvEnabled = boost::shared_ptr<smsEnabledPV>(new
		smsEnabledPV(prefix + ":Enabled", this, /* AutoSave */ true));

	m_enabled = enabled;

	if ( StorageManager::getAutoSavePV( m_pvEnabled->getName(),
			bvalue, ts_enabled ) ) {
		DEBUG("DataSource(): Updating Enabled State from AutoSave"
			<< " - m_enabled=" << m_enabled << " -> " << bvalue);
		m_enabled = bvalue;
	}
	else {
		ts_enabled = now;
	}

	// Check for "Required" AutoSave _Now_ Before Proceeding...
	//    - "m_required" gets used immediately in setRequired()...!

	struct timespec ts_required;

	m_pvRequired = boost::shared_ptr<DataSourceRequiredPV>(new
		DataSourceRequiredPV(prefix + ":Required", this,
			/* AutoSave */ true));

	m_required = is_required;

	if ( StorageManager::getAutoSavePV( m_pvRequired->getName(),
			bvalue, ts_required ) ) {
		DEBUG("DataSource(): Updating Required State from AutoSave"
			<< " - m_required=" << m_required << " -> " << bvalue);
		m_required = bvalue;
	}
	else {
		ts_required = now;
	}

	setRequired( m_required, true );

	parseURI(m_uri);

	m_pvName = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":Name"));

	m_pvBaseName = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":BaseName", /* AutoSave */ true));

	m_pvDataURI = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":DataURI", /* AutoSave */ true));

	m_pvConnected = boost::shared_ptr<smsConnectedPV>(new
		smsConnectedPV(prefix + ":Connected"));

	m_pvConnectRetryTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectRetryTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvConnectTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ConnectTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvDataTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":DataTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvDataTimeoutRetry = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":DataTimeoutRetry", 0, INT32_MAX,
			/* AutoSave */ true));

	m_pvIgnoreEoP = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":IgnoreEoP", /* AutoSave */ true));

	m_pvIgnoreLocalSAWTOOTH = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":IgnoreLocalSAWTOOTH",
			/* AutoSave */ true));

	m_pvIgnoreAnnotationPkts = boost::shared_ptr<smsPassThruPV>(new
		smsPassThruPV(prefix + ":IgnoreAnnotationPkts",
			/* AutoSave */ true));

	m_pvMixedDataPackets = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":MixedDataPackets", /* AutoSave */ true));

	m_pvCheckSourceSequence = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":CheckSourceSequence",
		/* AutoSave */ true));

	m_pvCheckPulseSequence = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":CheckPulseSequence",
		/* AutoSave */ true));

	m_pvMaxPulseSeqList = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MaxPulseSeqList", 0, INT32_MAX,
			/* AutoSave */ true));

	m_pvMaxReadChunk = boost::shared_ptr<smsStringPV>(new
		smsStringPV(prefix + ":MaxReadChunk", /* AutoSave */ true));

	m_pvRTDLNoDataThresh = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":RTDLNoDataThresh", 0, INT32_MAX,
			/* AutoSave */ true));

	m_pvSaveInputStream = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":SaveInputStream", /* AutoSave */ true));

	m_pvPulseBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthSecond"));

	m_pvPulseBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthMinute"));

	m_pvPulseBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":PulseBandwidthTenMin"));

	m_pvEventBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthSecond"));

	m_pvEventBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthMinute"));

	m_pvEventBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":EventBandwidthTenMin"));

	m_pvMetaBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MetaBandwidthSecond"));

	m_pvMetaBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MetaBandwidthMinute"));

	m_pvMetaBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MetaBandwidthTenMin"));

	m_pvErrBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":ErrBandwidthSecond"));

	m_pvErrBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":ErrBandwidthMinute"));

	m_pvErrBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":ErrBandwidthTenMin"));

	m_pvNumHWSources = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":NumHWSources"));

	m_ctrl->addPV(m_pvName);
	m_ctrl->addPV(m_pvBaseName);
	m_ctrl->addPV(m_pvDataURI);
	m_ctrl->addPV(m_pvEnabled);
	m_ctrl->addPV(m_pvRequired);
	m_ctrl->addPV(m_pvConnected);
	m_ctrl->addPV(m_pvConnectRetryTimeout);
	m_ctrl->addPV(m_pvConnectTimeout);
	m_ctrl->addPV(m_pvDataTimeout);
	m_ctrl->addPV(m_pvDataTimeoutRetry);
	m_ctrl->addPV(m_pvIgnoreEoP);
	m_ctrl->addPV(m_pvIgnoreLocalSAWTOOTH);
	m_ctrl->addPV(m_pvIgnoreAnnotationPkts);
	m_ctrl->addPV(m_pvMixedDataPackets);
	m_ctrl->addPV(m_pvCheckSourceSequence);
	m_ctrl->addPV(m_pvCheckPulseSequence);
	m_ctrl->addPV(m_pvMaxPulseSeqList);
	m_ctrl->addPV(m_pvMaxReadChunk);
	m_ctrl->addPV(m_pvRTDLNoDataThresh);
	m_ctrl->addPV(m_pvSaveInputStream);

	m_ctrl->addPV(m_pvPulseBandwidthSecond);
	m_ctrl->addPV(m_pvPulseBandwidthMinute);
	m_ctrl->addPV(m_pvPulseBandwidthTenMin);
	m_ctrl->addPV(m_pvEventBandwidthSecond);
	m_ctrl->addPV(m_pvEventBandwidthMinute);
	m_ctrl->addPV(m_pvEventBandwidthTenMin);
	m_ctrl->addPV(m_pvMetaBandwidthSecond);
	m_ctrl->addPV(m_pvMetaBandwidthMinute);
	m_ctrl->addPV(m_pvMetaBandwidthTenMin);
	m_ctrl->addPV(m_pvErrBandwidthSecond);
	m_ctrl->addPV(m_pvErrBandwidthMinute);
	m_ctrl->addPV(m_pvErrBandwidthTenMin);

	m_ctrl->addPV(m_pvNumHWSources);

	// Initialize Data Source PVs...
	// (All except "Enabled"!  Save that for later... :-)

	m_pvName->update(m_name, &now);
	m_pvBaseName->update(m_basename, &now);
	m_pvDataURI->update(m_uri, &now);
	m_pvRequired->update(m_required, &ts_required); // AutoSave TimeStamp
	m_pvConnected->disconnected();
	m_pvConnectRetryTimeout->update(m_connect_retry, &now);
	m_pvConnectTimeout->update(m_connect_timeout, &now);
	m_pvDataTimeout->update(m_data_timeout, &now);
	m_pvDataTimeoutRetry->update(m_data_timeout_retry, &now);
	m_pvIgnoreEoP->update(m_ignore_eop, &now);
	m_pvIgnoreLocalSAWTOOTH->update(m_ignore_local_sawtooth, &now);
	m_pvIgnoreAnnotationPkts->update(m_ignore_annotation_pkts, &now);
	m_pvMixedDataPackets->update(m_mixed_data_packets, &now);
	m_pvCheckSourceSequence->update(m_check_source_sequence, &now);
	m_pvCheckPulseSequence->update(m_check_pulse_sequence, &now);
	m_pvMaxPulseSeqList->update(m_max_pulse_seq_list, &now);
	m_pvRTDLNoDataThresh->update(m_rtdlNoDataThresh, &now);
	m_pvSaveInputStream->update(m_save_input_stream, &now);

	m_pvPulseBandwidthSecond->update(m_pulse_count_second, &now);
	m_pvPulseBandwidthMinute->update(m_pulse_count_minute, &now);
	m_pvPulseBandwidthTenMin->update(m_pulse_count_tenmin, &now);
	m_pvEventBandwidthSecond->update(m_event_count_second, &now);
	m_pvEventBandwidthMinute->update(m_event_count_minute, &now);
	m_pvEventBandwidthTenMin->update(m_event_count_tenmin, &now);
	m_pvMetaBandwidthSecond->update(m_meta_count_second, &now);
	m_pvMetaBandwidthMinute->update(m_meta_count_minute, &now);
	m_pvMetaBandwidthTenMin->update(m_meta_count_tenmin, &now);
	m_pvErrBandwidthSecond->update(m_err_count_second, &now);
	m_pvErrBandwidthMinute->update(m_err_count_minute, &now);
	m_pvErrBandwidthTenMin->update(m_err_count_tenmin, &now);

	m_pvNumHWSources->update(0, &now);

	// Initialize Max Read Chunk PV (construct string)...
	std::stringstream ssMRC;
	ssMRC << m_max_read_chunk;
	m_pvMaxReadChunk->update(ssMRC.str(), &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	uint32_t uvalue;
	double dvalue;

	// DataSource BaseName and URI...

	bool do_name = false;

	if ( StorageManager::getAutoSavePV( m_pvBaseName->getName(),
			value, ts ) ) {
		m_basename = value;
		m_pvBaseName->update(value, &ts);
		do_name = true;
	}

	if ( StorageManager::getAutoSavePV( m_pvDataURI->getName(),
			value, ts ) ) {
		m_uri = value;
		m_pvDataURI->update(value, &ts);
		do_name = true;
	}

	// Regenerate DataSource Name...
	if ( do_name ) {
		m_name = m_uri;
		m_name += " (";
		m_name += m_basename;
		m_name += ")";
		// Parse New URI...
		parseURI(m_uri);
		// Update DataSource Name PV...
		m_pvName->update(m_name, &ts);
	}

	// DataSource Timeouts...

	if ( StorageManager::getAutoSavePV( m_pvConnectRetryTimeout->getName(),
			dvalue, ts ) ) {
		m_connect_retry = dvalue;
		m_pvConnectRetryTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvConnectTimeout->getName(),
			dvalue, ts ) ) {
		m_connect_timeout = dvalue;
		m_pvConnectTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvDataTimeout->getName(),
			dvalue, ts ) ) {
		m_data_timeout = dvalue;
		m_pvDataTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvDataTimeoutRetry->getName(),
			uvalue, ts ) ) {
		m_data_timeout_retry = uvalue;
		m_pvDataTimeoutRetry->update(uvalue, &ts);
	}

	// Misc DataSource Control Settings...

	if ( StorageManager::getAutoSavePV( m_pvIgnoreEoP->getName(),
			bvalue, ts ) ) {
		m_ignore_eop = bvalue;
		m_pvIgnoreEoP->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvIgnoreLocalSAWTOOTH->getName(),
			bvalue, ts ) ) {
		m_ignore_local_sawtooth = bvalue;
		m_pvIgnoreLocalSAWTOOTH->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_pvIgnoreAnnotationPkts->getName(),
			uvalue, ts ) ) {
		m_ignore_annotation_pkts = (Markers::PassThru) uvalue;
		m_pvIgnoreAnnotationPkts->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvMixedDataPackets->getName(),
			bvalue, ts ) ) {
		m_mixed_data_packets = bvalue;
		m_pvMixedDataPackets->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvCheckSourceSequence->getName(),
			bvalue, ts ) ) {
		m_check_source_sequence = bvalue;
		m_pvCheckSourceSequence->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvCheckPulseSequence->getName(),
			bvalue, ts ) ) {
		m_check_pulse_sequence = bvalue;
		m_pvCheckPulseSequence->update(bvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvMaxPulseSeqList->getName(),
			uvalue, ts ) ) {
		m_max_pulse_seq_list = uvalue;
		m_pvMaxPulseSeqList->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvRTDLNoDataThresh->getName(),
			uvalue, ts ) ) {
		m_rtdlNoDataThresh = uvalue;
		m_pvRTDLNoDataThresh->update(uvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvSaveInputStream->getName(),
			bvalue, ts ) ) {
		m_save_input_stream = bvalue;
		m_pvSaveInputStream->update(bvalue, &ts);
	}

	// Restore/Parse/Check Max Read Chunk from AutoSave... ;-D

	if ( StorageManager::getAutoSavePV( m_pvMaxReadChunk->getName(),
			value, ts ) ) {
		unsigned int tmp_max_read_chunk;
		bool parse_ok = true;
		try {
			tmp_max_read_chunk = parse_size(value);
		} catch (std::runtime_error e) {
			std::string msg("Unable to parse read size for source '");
			msg += m_name;
			msg += "': ";
			msg += e.what();
			ERROR("Invalid AutoSave Value for Max Read Size: "
				<< msg << " - Leave As Is"
				<< " m_max_read_chunk=" << m_max_read_chunk);
			parse_ok = false;
		}
		if ( parse_ok ) {
			m_max_read_chunk = tmp_max_read_chunk;
			// Log the change...
			std::stringstream ssMRC;
			ssMRC << "AutoSave:";
			ssMRC << " Setting Max Read Chunk Size for " << m_name;
			ssMRC << " to " << m_max_read_chunk;
			ssMRC << " (" << value << ")";
			DEBUG(ssMRC.str());
			m_pvMaxReadChunk->update(value, &ts);
		}
	}

	// Set Up Data Source Connection Timer...
	try {
		m_timer = new TimerAdapter<DataSource>(this);
	}
	catch (std::exception &e) {
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
		std::string msg( m_ctrl->getRecording() ? "[RECORDING] " : "" );
		msg += "Unable to Create TimerAdapter for Data Source ";
		msg += m_name;
		msg += ": Bailing! ";
		msg += e.what();
		ERROR("DataSource(): " << msg);
		throw std::runtime_error(msg);
	}
	catch (...) {
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
		std::string msg( m_ctrl->getRecording() ? "[RECORDING] " : "" );
		msg += "Unable to Create TimerAdapter for Data Source ";
		msg += m_name;
		msg += ": Bailing! ";
		msg += "Unknown Exception.";
		ERROR("DataSource(): " << msg);
		throw std::runtime_error(msg);
	}

	resetMaxTime();

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;
	m_dupRTDL = 0;

	m_last_pkt_type = -1;
	m_last_pkt_len = -1;
	m_last_pkt_sec = -1;
	m_last_pkt_nsec = -1;

	m_rtdl_pkt_counts = -1;
	m_data_pkt_counts = -1;

	m_readDelay = false;

	// Register for Signal to Dump Device Descriptor & Starting Values
	// into Saved Input Streams...
	m_connection = StorageManager::onSavePrologue(
		boost::bind(&DataSource::onSavePrologue, this, _1),
		m_smsSourceId );

	// "Enabled" PV Update Triggers "startConnect()" when Enabled... :-D
	// (Possibly Using TimeStamp from AutoSaved Value... ;-D)
	m_pvEnabled->update(m_enabled, &ts_enabled);
}

DataSource::~DataSource()
{
	if ( m_addrinfo != NULL ) {
		freeaddrinfo(m_addrinfo);
		m_addrinfo = NULL;
	}
	delete m_timer;
	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd >= 0) {
		if ( m_ctrl->verbose() > 0 )
			DEBUG("Close m_fd=" << m_fd);
		close(m_fd);
		m_fd = -1;
	}
	m_connection.disconnect();
}

void DataSource::parseURI(std::string uri)
{
	std::string node;
	std::string service("31416");
	struct addrinfo hints;
	size_t pos = uri.find_first_of(':');
	int rc;

	// Extract Desired Service, if found...
	// Set Node to Any Remainder.
	if (pos != std::string::npos) {
		node = uri.substr(0, pos);
		if (pos != uri.length())
			service = uri.substr(pos + 1);
	} else
		node = uri;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_CANONNAME | AI_V4MAPPED;

	// Free Any Previous Address Info...
	if ( m_addrinfo != NULL ) {
		freeaddrinfo(m_addrinfo);
		m_addrinfo = NULL;
	}

	rc = getaddrinfo(node.c_str(), service.c_str(), &hints, &m_addrinfo);
	if (rc) {
		// *Don't* Throw Exception Here, Just Log Loudly & Thwart Connect!
		std::string msg("Unable to lookup data source ");
		msg += m_name;
		msg += ": ";
		msg += gai_strerror(rc);
		ERROR("parseURI(): " << msg << " - *** Won't Attempt Connect!");
		if ( m_addrinfo != NULL ) {
			freeaddrinfo(m_addrinfo);
			m_addrinfo = NULL;
		}
	}
}

void DataSource::setRequired(bool is_required, bool force)
{
	// Only Do Stuff if the Value Changed (or on First Setting ("force"))
	if ( is_required != m_required || force )
	{
		INFO("setRequired(): Setting Required Flag to"
			<< " [" << is_required << "]" << " force=" << force);

		m_required = is_required;

		if ( m_required && !m_enabled )
		{
			std::string isRequired = ( m_required )
				? "*Required*" : "*Not Required*";

			ERROR("*** Note: DISABLED Data Source " << m_name
				<< " Marked as " << isRequired
				<< " for Data Collection!");
		}

		// Tell Control We've Changed Our Required Status...
		m_ctrl->updateDataSourceConnectivity();
	}
	else {
		INFO("setRequired(): No Change to Required Flag"
			<< " [" << m_required << "]" << " force=" << force
			<< " - Ignoring...");
	}
}

void DataSource::unregisterHWSources(bool isSourceDown, bool stateChanged,
		std::string why)
{
	/* Complete any outstanding pulses, and inform the manager
	 * of our change of status
	 */
	HWSrcMap::iterator it;

	struct timespec now;
	clock_gettime( CLOCK_REALTIME_COARSE, &now );

	// Meh, This Log Message can be Annoying for Persistently Down Sources
	// - Only Log at This Level if the State Actually Changed...
	if ( stateChanged )
	{
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "unregisterHWSources(): State Change:"
			<< " Data Source " << m_name << " is " << why << ","
			<< " Source is" << ( isSourceDown ? "" : " Not" ) << " Down" );
	}

	for ( it=m_hwSources.begin(); it != m_hwSources.end(); it++ )
	{
		// Reset HWSource PV Index Bit, Clear Out SMS Id PV Value...
		if ( it->second->m_hwIndex >= 0 ) {
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "unregisterHWSources():"
				<< " Resetting HW Index=" << it->second->m_hwIndex
				<< " for " << why << " Data Source " << m_name
				<< " smsId=" << it->second->smsId() );
			it->second->m_pvHWSourceSmsId->update( -1, &now );
			m_hwIndices.reset( it->second->m_hwIndex );
		}

		// Only End Current Pulse if Event Source Still Active...!
		if ( it->second->smsId() != (uint32_t) -1 ) {
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "unregisterHWSources():"
				<< " Ending Active Pulses"
				<< " for " << why << " Data Source " << m_name
				<< " smsId=" << it->second->smsId()
				<< " m_pulseSeqList.size()="
					<< it->second->m_pulseSeqList.size() );
			// NOTE: Mark & Dispatch Pulses in *Reverse* Order,
			// i.e. from the End, as in "First In, First Out"...! ;-D
			PulseSeqList::iterator psit = it->second->m_pulseSeqList.end();
			if ( psit != it->second->m_pulseSeqList.begin() ) --psit;
			for ( ; psit != it->second->m_pulseSeqList.end() ; )
			{
				PulseInvariants &pulse = psit->first;
				INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "unregisterHWSources():"
					<< " End Pulse"
					<< std::hex << " 0x" << psit->first.m_pulseId
						<< std::dec
					<< " pulseGood=" << pulse.m_pulseGood
					<< " (" << m_name << ")" );
				it->second->endPulse( psit, false );
				// Make Sure We Process the First Pulse in List Too...! ;-D
				if ( psit != it->second->m_pulseSeqList.begin() ) --psit;
				else break;
			}
			it->second->m_pulseSeqList.clear();
		}

		// Only Unregister Event Source if Still Active...!
		// NOTE: endPulse() above calls SMSControl::markComplete(),
		// which could identify this as an Intermittent Data Source,
		// in which case it Already Unregistered This Event Source...! ;-Q
		if ( it->second->smsId() != (uint32_t) -1 ) {
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "unregisterHWSources():"
				<< " Unregistering Event Source"
				<< " for " << why << " Data Source " << m_name
				<< " smsId=" << it->second->smsId() );
			m_ctrl->unregisterEventSource( m_smsSourceId,
				it->second->smsId() );
		}
		else {
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Skipping Unregistering of Inactive Event Source"
				<< " (hwId=" << it->second->hwId() << ","
				<< " smsId=" << it->second->smsId() << ")"
				<< " for " << why << " Data Source " << m_name );
		}
	}

	m_hwSources.clear();

	// Update Number of HWSources PV...
	m_pvNumHWSources->update( 0, &now, true /* no_log */ );

	if ( isSourceDown )
		m_ctrl->sourceDown( m_smsSourceId, stateChanged );
}

void DataSource::dumpLastReadStats(std::string who)
{
	INFO(who << ": " << m_name
		<< " Last Packet:"
		<< " type=0x" << std::hex << m_last_pkt_type << std::dec
		<< " sec=" << m_last_pkt_sec
		<< " nsec=" << m_last_pkt_nsec
		<< " len=" << m_last_pkt_len
		<< " last_start_read_time="
		<< Parser::last_start_read_time.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< Parser::last_start_read_time.tv_nsec << std::setw(0)
		<< " last_bytes_read=" << Parser::last_bytes_read
		<< " last_read_errno=" << Parser::last_read_errno
		<< " (" << strerror( Parser::last_read_errno ) << ")"
		<< " last_end_read_time="
		<< Parser::last_end_read_time.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< Parser::last_end_read_time.tv_nsec << std::setw(0)
		<< " last_pkts_parsed=" << Parser::last_pkts_parsed
		<< " last_total_bytes=" << Parser::last_total_bytes
		<< " last_total_packets=" << Parser::last_total_packets
		<< " last_read_count=" << Parser::last_read_count
		<< " last_loop_count=" << Parser::last_loop_count
		<< " last_elapsed=" << Parser::last_elapsed
		<< " last_read_elapsed=" << Parser::last_read_elapsed
		<< " last_read_elapsed_total=" << Parser::last_read_elapsed_total
		<< " last_parse_elapsed=" << Parser::last_parse_elapsed
		<< " last_parse_elapsed_total=" << Parser::last_parse_elapsed_total
		<< " ;"
		<< " last_last_start_read_time="
		<< Parser::last_last_start_read_time.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< Parser::last_last_start_read_time.tv_nsec << std::setw(0)
		<< " last_last_bytes_read=" << Parser::last_last_bytes_read
		<< " last_last_read_errno=" << Parser::last_last_read_errno
		<< " (" << strerror( Parser::last_last_read_errno ) << ")"
		<< " last_last_end_read_time="
		<< Parser::last_last_end_read_time.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< Parser::last_last_end_read_time.tv_nsec << std::setw(0)
		<< " last_last_pkts_parsed=" << Parser::last_last_pkts_parsed
		<< " last_last_total_bytes=" << Parser::last_last_total_bytes
		<< " last_last_total_packets=" << Parser::last_last_total_packets
		<< " last_last_read_count=" << Parser::last_last_read_count
		<< " last_last_loop_count=" << Parser::last_last_loop_count
		<< " last_last_elapsed=" << Parser::last_last_elapsed
		<< " last_last_read_elapsed=" << Parser::last_last_read_elapsed
		<< " last_last_read_elapsed_total="
			<< Parser::last_last_read_elapsed_total
		<< " last_last_parse_elapsed=" << Parser::last_last_parse_elapsed
		<< " last_last_parse_elapsed_total="
			<< Parser::last_last_parse_elapsed_total);
}

void DataSource::connectionFailed(bool dumpStats, bool dumpDiscarded,
		State new_state)
{
	m_timer->cancel();

	// Dump "Last Read()" Statistics...
	if (dumpStats)
		dumpLastReadStats("connectionFailed()");

	// Dump "Discarded Packet" Statistics...
	if (dumpDiscarded) {
		// Dump Discarded Packet Statistics...
		std::string log_info;
		Parser::getDiscardedPacketsLogString(log_info);
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< log_info );
		// Reset Discarded Packet Statistics...
		Parser::resetDiscardedPacketsStats();
	}

	// Update/Dump Any Pulse/Event Bandwidth Statistics...
	//    - Follow "dumpDiscarded" Flag for Logging Control...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);
	updateBandwidthSecond( now, dumpDiscarded );
	updateBandwidthMinute( now, dumpDiscarded );
	updateBandwidthTenMin( now, dumpDiscarded );

	if (m_fdreg) {
		delete m_fdreg;
		m_fdreg = NULL;
	}
	if (m_fd >= 0) {
		if ( m_ctrl->verbose() > 0 )
			DEBUG("Close m_fd=" << m_fd);
		close(m_fd);
		m_fd = -1;
	}

	bool stateChanged = ( m_state == ACTIVE );

	m_state = new_state;

	/* Complete any outstanding pulse, and inform the manager of our
	 * failure
	 */
	unregisterHWSources(true, stateChanged, "Disconnected");

	resetMaxTime();

	if ( stateChanged ) {
		m_ctrl->updateMaxDataSourceTime( m_smsSourceId,
			&m_maxTime ); // Wallclock Time...! (0.0 from Reset)
	}

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;

	if (m_state != DISABLED) {
		// Update Connect Retry Timeout from PV...
		m_connect_retry = m_pvConnectRetryTimeout->value();
		m_timer->start(m_connect_retry);
	}
}

bool DataSource::timerExpired(void)
{
	// DEBUG("timerExpired() entry");

	switch ( m_state ) {

		case DISABLED:
		{
			WARN("Ignoring Timeout for Disabled Data Source " << m_name);
			if ( m_readDelay )
				m_readDelay = false; // reset flag set by SMSControl...
			break;
		}

		case IDLE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Connect Retry Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Connect Retry Timeout from PV...
				m_connect_retry = m_pvConnectRetryTimeout->value();
				m_timer->start(m_connect_retry);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				startConnect();
			}
			break;
		}

		case CONNECTING:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Connect Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Connect Timeout from PV...
				m_connect_timeout = m_pvConnectTimeout->value();
				m_timer->start(m_connect_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				// Leave m_pvConnected in its current state, latch failures
				WARN( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Connection request timed out to " << m_name );
				connectionFailed(false, false, IDLE);
			}
			break;
		}

		case ACTIVE:
		{
			if ( m_readDelay ) {
				WARN("Ignoring Data Timeout (Read Delayed)"
					<< " for " << m_name << ", Resetting Timer.");
				// Update Data Timeout from PV...
				m_data_timeout = m_pvDataTimeout->value();
				m_data_timeout_retry = m_pvDataTimeoutRetry->value();
				m_data_timeout_retry_count = 0;
				m_timer->start(m_data_timeout);
				m_readDelay = false; // reset flag set by SMSControl...
			} else {
				// Retry Count has Expired...?
				if ( ++m_data_timeout_retry_count > m_data_timeout_retry )
				{
					ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
						<< "Timed out waiting for data from " << m_name
						<< " (Retry Count " << m_data_timeout_retry_count
						<< " > Number of Retries " << m_data_timeout_retry
						<< "...)");
					m_pvConnected->failed();
					connectionFailed(true, true, IDLE);
				}
				// Still Have Some Retries Left, Log It!!
				else
				{
					ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
						<< "WARNING: "
						<< "Timed out waiting for data from " << m_name
						<< " - Retry Count " << m_data_timeout_retry_count
						<< " <= Number of Retries " << m_data_timeout_retry
						<< ", Stay Connected, Continuing...");
					m_timer->start(m_data_timeout);
				}
			}
			break;
		}
	}

	// DEBUG("timerExpired() exit");

	return false;
}

void DataSource::fdReady(void)
{
	// DEBUG("fdReady() entry m_state=" << m_state);

	switch (m_state) {
		case DISABLED:
			WARN("Ignoring Data Ready for Disabled Data Source "
				<< m_name);
			break;
		case IDLE:
			ERROR("fdReady(): Invalid Idle State! Bailing...");
			throw std::logic_error("Invalid state");
		case CONNECTING:
			connectComplete();
			break;
		case ACTIVE:
			dataReady();
			break;
	}

	// DEBUG("fdReady() exit");
}

void DataSource::startConnect(void)
{
	std::string log_info;
	int flags, rc;

	/* Clear out any old state from the ADARA parser. */
	reset();

	// Update Data URI from PV...
	std::string basename = m_pvBaseName->value();
	std::string uri = m_pvDataURI->value();
	if ( m_basename.compare( basename ) || m_uri.compare( uri ) ) {
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Setting New Data Source Info from PVs:"
			<< " BaseName=" << basename
			<< " URI=" << uri );
		m_basename = basename;
		m_uri = uri;
		// Regenerate DataSource Name...
		m_name = m_uri;
		m_name += " (";
		m_name += m_basename;
		m_name += ")";
		// Parse New URI...
		parseURI(m_uri);
		// Update DataSource Name PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME_COARSE, &now);
		m_pvName->update(m_name, &now);
	}

	// Did the Address Lookup Succeed...?
	if ( m_addrinfo == NULL ) {
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_WONT_CONN, m_name,
				60, 3, 10, log_info ) ) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "startConnect():"
				<< " Invalid Address Info/Lookup for Data Source "
				<< m_name << " - *** Won't Attempt to Connect...!");
		}
		goto error;
	}

	// Ready to Connect...
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_TRYING_CONN, m_name,
			60, 3, 10, log_info ) ) {
		INFO(log_info
			<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Trying connection to " << m_name);
	}
	m_pvConnected->trying_to_connect();

	m_fd = socket(m_addrinfo->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Error creating socket for " << m_name );
		m_fd = -1;   // just to be sure... ;-b
		goto error;
	}
	if ( m_ctrl->verbose() > 0 ) {
		DEBUG("New Socket for " << m_name << " m_fd=" << m_fd);
	}

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Error getting socket flags for " << m_name );
		goto error_fd;
	}
	if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK)) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Error setting socket flags for " << m_name );
		goto error_fd;
	}

	rc = connect(m_fd, m_addrinfo->ai_addr, m_addrinfo->ai_addrlen);
	if (rc < 0)
		rc = errno;

	switch (rc) {

		case ECONNREFUSED:
			/* Rate-limited logging of refused connection */
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_CONN_REFUSED, m_name,
					60, 3, 10, log_info ) ) {
				WARN(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Connection refused by " << m_name);
			}
			goto error_fd;

		case EAGAIN:
		case EINTR:
		case EINPROGRESS:
			m_state = CONNECTING;
			break;

		case 0:
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Connection established to " << m_name );
			m_state = ACTIVE;
			m_pvConnected->connected();
			resetBandwidthStatistics();
			m_ctrl->sourceUp(m_smsSourceId);
			break;

		default:
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_CONN_REQUEST_ERROR, m_name,
					60, 3, 10, log_info ) ) {
				WARN(log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Unknown connection request error for " << m_name
					<< ": " << strerror(rc) << " (Ignoring!)");
			}
	}

	try {
		/* We won't notice that the connect completed until we get
		 * the first packet from the source unless we look for the
		 * connection becoming writable.
		 */
		fdRegType type = (m_state == CONNECTING) ? fdrWrite : fdrRead;
		m_fdreg = new ReadyAdapter(m_fd, type,
				boost::bind(&DataSource::fdReady, this),
				m_ctrl->verbose());
	} catch (std::exception &e) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Exception in startConnect()"
			<< " Creating ReadyAdapter for " << m_name
			<< " fdReady(): " << e.what());
		m_fdreg = NULL; // just to be sure... ;-b
		goto error_fd;
	} catch (...) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Unknown Exception in startConnect()"
			<< " Creating ReadyAdapter for " << m_name
			<< " fdReady()");
		m_fdreg = NULL; // just to be sure... ;-b
		goto error_fd;
	}

	// Update Connect Timeout from PV...
	m_connect_timeout = m_pvConnectTimeout->value();
	m_timer->start(m_connect_timeout);
	return;

error_fd:

	if (m_fd >= 0) {
		if ( m_ctrl->verbose() > 0 )
			DEBUG("Close m_fd=" << m_fd);
		close(m_fd);
		m_fd = -1;
	}

error:

	m_pvConnected->failed();
	connectionFailed(false, false, IDLE);
}

void DataSource::connectComplete(void)
{
	socklen_t elen = sizeof(int);
	int e, rc;

	// Check File Descriptor...
	if (m_fd < 0) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "connectComplete(): Invalid File Descriptor for "
			<< m_name << " m_fd=" << m_fd);
		// We might as well Disconnect and try again later...
		// Leave m_pvConnected in its current state, latch failures
		connectionFailed(false, false, IDLE);
		return;
	}

	rc = getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &e, &elen);
	if (!rc && !e) {

		if (m_fdreg) {
			delete m_fdreg;
			m_fdreg = NULL;
		}

		// Catch Bad Alloc Exception...
		try {
			m_fdreg = new ReadyAdapter(m_fd, fdrRead,
					boost::bind(&DataSource::fdReady, this),
					m_ctrl->verbose());
		} catch (std::exception &e) {
			ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Exception in connectComplete()"
				<< " Creating ReadyAdapter for " << m_name
				<< " fdReady(): " << e.what());
			m_fdreg = NULL; // just to be sure... ;-b
			// If we can't get notified to read from this Data Source,
			// we might as well Disconnect and try again later...
			// Leave m_pvConnected in its current state, latch failures
			connectionFailed(false, false, IDLE);
			return;
		} catch (...) {
			ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Unknown Exception in connectComplete()"
				<< " Creating ReadyAdapter for " << m_name
				<< " fdReady()");
			m_fdreg = NULL; // just to be sure... ;-b
			// If we can't get notified to read from this Data Source,
			// we might as well Disconnect and try again later...
			// Leave m_pvConnected in its current state, latch failures
			connectionFailed(false, false, IDLE);
			return;
		}

		m_timer->cancel();

		// Update Data Timeout from PV...
		m_data_timeout = m_pvDataTimeout->value();
		m_data_timeout_retry = m_pvDataTimeoutRetry->value();
		m_data_timeout_retry_count = 0;
		m_timer->start(m_data_timeout);

		m_state = ACTIVE;

		m_pvConnected->connected();

		resetBandwidthStatistics();

		m_ctrl->sourceUp(m_smsSourceId);

		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Connection established to " << m_name );
		return;
	}

	if (rc < 0)
		e = errno;

	if (e == EAGAIN || e == EINTR || e == EINPROGRESS) {
		/* Odd, but harmless; just keep waiting */
		return;
	}

	/* Rate-limited logging of connection issue */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_CONN_FAILED, m_name, 60, 3, 10, log_info ) ) {
		WARN(log_info
			<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Connection request to " << m_name
			<< " failed: " << strerror(e));
	}
	// Leave m_pvConnected in its current state, latch failures
	connectionFailed(false, false, IDLE);
}

void DataSource::dataReady(void)
{
	std::string log_info;

	m_timer->cancel();

	// Update Our Data Timeout/Max Read Chunk Internals Less Frequently,
	// It Takes Some Time...!
	// Only allow updates every few minutes (depending on bandwidth... ;-)
	static uint32_t cnt = 0;
	uint32_t freq = 999;

	// Update Data Timeout from PV... (Periodically...)
	if ( !(++cnt % freq) ) {
		double new_data_timeout = m_pvDataTimeout->value();
		if ( !approximatelyEqual( m_data_timeout, new_data_timeout,
				0.0000001 ) ) {
			ERROR("dataReady(): Updating Data Timeout for " << m_name
				<< " from " << m_data_timeout
				<< " to " << new_data_timeout);
			m_data_timeout = new_data_timeout;
		}
		uint32_t new_data_timeout_retry = m_pvDataTimeoutRetry->value();
		if ( m_data_timeout_retry != new_data_timeout_retry ) {
			ERROR("dataReady(): Updating Data Timeout Retry for " << m_name
				<< " from " << m_data_timeout_retry
				<< " to " << new_data_timeout_retry);
			m_data_timeout_retry = new_data_timeout_retry;
		}
	}
	m_data_timeout_retry_count = 0;
	m_timer->start(m_data_timeout);

	struct timespec readStart;
	clock_gettime(CLOCK_REALTIME_COARSE, &readStart);

 	// reset read delayed flag, starting a new read now...
	m_ctrl->resetSourcesReadDelay();

	bool readOk = true;

	m_rtdl_pkt_counts = 0;
	m_data_pkt_counts = 0;

	// Update Max Read Chunk from PV... (Periodically...)
	// (Note: count already incremented above for Data Timeout...!)
	if ( !(cnt % freq) ) {
		std::string val = m_pvMaxReadChunk->value();
		unsigned int tmp_max_read_chunk;
		try {
			tmp_max_read_chunk = parse_size(val);
		} catch (std::runtime_error e) {
			std::string msg("Unable to parse read size for source '");
			msg += m_name;
			msg += "': ";
			msg += e.what();
			// *Don't* Throw std::runtime_error(msg), Just Log Instead...
			/* Rate-limited log of failure */
			std::string rll_log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_PARSE_MAX_READ_CHUNK, m_name,
					60, 3, 100, rll_log_info ) ) {
				ERROR(rll_log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "dataReady(): " << msg << " - Revert to Original"
					<< " m_max_read_chunk=" << m_max_read_chunk);
			}
			// String parse failed, revert to original value...
			tmp_max_read_chunk = m_max_read_chunk;
		}
		if ( tmp_max_read_chunk != m_max_read_chunk ) {
			m_max_read_chunk = tmp_max_read_chunk;
			// Log the change...
			std::stringstream ssMRC;
			ssMRC << ( m_ctrl->getRecording() ? "[RECORDING] " : "" );
			ssMRC << "Setting Max Read Chunk Size for " << m_name;
			ssMRC << " to " << m_max_read_chunk;
			ssMRC << " (" << val << ")";
			ERROR(ssMRC.str());
		}
	}

	// Check File Descriptor...
	if (m_fd < 0) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "dataReady(): Invalid File Descriptor for "
			<< m_name << " m_fd=" << m_fd);
		m_pvConnected->failed();
		connectionFailed(true, true, IDLE);
		return;
	}

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		if (!read(m_fd, log_info, 4000, m_max_read_chunk)) {
			INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Connection closed with " << m_name
				<< " log_info=(" << log_info << ")" );
			m_pvConnected->disconnected();
			connectionFailed(true, true, IDLE);
			readOk = false;
		}
	} catch (std::runtime_error e) {
		/* Rate-limited log of failure */
		std::string rll_log_info;
		bool dumpStats = false;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_READ_EXCEPTION, m_name, 60, 5, 10, rll_log_info ) ) {
			ERROR(rll_log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Exception reading from " << m_name
				<< ": " << e.what());
			dumpStats = true;
		}
		m_pvConnected->failed();
		connectionFailed(dumpStats, true, IDLE);
		readOk = false;
	}

	if ( readOk )
	{
		struct timespec readEnd;
		clock_gettime(CLOCK_REALTIME_COARSE, &readEnd);

		double elapsed = calcDiffSeconds( readEnd, readStart );

 		// set read delayed flag...!
		if ( elapsed > 2.0 )
		{
			/* Rate-limited logging of read delay threshold */
			std::string rll_log_info;
			bool dumpStats = false;
			if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
					RLL_READ_DELAY, m_name, 60, 20, 5, rll_log_info ) ) {
				ERROR(rll_log_info
					<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "dataReady(): Read Delay Threshold Exceeded"
					<< " elapsed=" << elapsed << " (" << m_name << ")"
					<< " log_info=(" << log_info << ")"
					<< " m_rtdl_pkt_counts=" << m_rtdl_pkt_counts
					<< " m_data_pkt_counts=" << m_data_pkt_counts);
				dumpStats = true;
			}
			m_ctrl->setSourcesReadDelay();
			if ( dumpStats ) dumpLastReadStats("dataReady() (Read Delay)");
			// We were "Away" for a while... Trigger Internals PV Update!
			cnt = freq - 1;
		}
	}
}

void DataSource::enabled(void)
{
	DEBUG("*** Data Source " << m_name << " Enabled!");

	// Change Internal State to Default "Idle", We're Enabled Now...
	m_state = IDLE;

	startConnect();
}

void DataSource::disabled(void)
{
	DEBUG("*** Data Source " << m_name << " Disabled!");

	// Mark Connection State as "Disconnected"...
	m_pvConnected->disconnected();

	// Close Down Socket, Change Internal State to DISABLED...
	// (so we won't do anything... ;-)
	connectionFailed(true, true, DISABLED);
}

bool DataSource::rxPacket(const ADARA::Packet &pkt)
{
	// Meter Live Control PV Updates and "Discarded Packet" statistics...
	static uint32_t cnt = 0;

	// Optionally Save Input Stream to Storage Container File...
	// (Check the Live Control PV Periodically, but _Not_ Every Packet!)
	// (And While We're At It, Also Check "Ignore Local SAWTOOTH" PV... :-)
	if ( !( ++cnt % 3333 ) ) {
		bool tmpSaveInputStream = m_pvSaveInputStream->value();
		if ( tmpSaveInputStream != m_save_input_stream )
		{
			ERROR("rxPacket():"
				<< " Updating Value of"
				<< " Save Input Stream"
				<< " for " << m_name
				<< " from " << m_save_input_stream
				<< " to " << tmpSaveInputStream);
			m_save_input_stream = tmpSaveInputStream;
		}
		bool tmpIgnoreLocalSAWTOOTH = m_pvIgnoreLocalSAWTOOTH->value();
		if ( tmpIgnoreLocalSAWTOOTH != m_ignore_local_sawtooth )
		{
			ERROR("rxPacket():"
				<< " Updating Value of"
				<< " Ignore Local SAWTOOTH"
				<< " for " << m_name
				<< " from " << m_ignore_local_sawtooth
				<< " to " << tmpIgnoreLocalSAWTOOTH);
			m_ignore_local_sawtooth = tmpIgnoreLocalSAWTOOTH;
		}
	}

	if ( m_save_input_stream ) {
		StorageManager::savePacket( pkt.packet(), pkt.packet_length(),
			m_smsSourceId, pkt.base_type() );
	}

	// Once in a blue moon, dump "Discarded Packet" statistics... ;-D
	// (Note: count already incremented above for Save Input Stream...!)
	if ( !( cnt % 99999 ) ) {
		std::string log_info;
		Parser::getDiscardedPacketsLogString(log_info);
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "rxPacket(): " << log_info );
	}

	// Save "Last Packet" Info for Debugging...
	m_last_pkt_type = pkt.type();
	m_last_pkt_len = pkt.payload_length();
	m_last_pkt_sec = pkt.timestamp().tv_sec; // Wallclock Time...!
	m_last_pkt_nsec = pkt.timestamp().tv_nsec;

	// INFO("rxPacket() type=0x" << std::hex << m_last_pkt_type << std::dec
		// << " sec=" << m_last_pkt_sec
		// << " nsec=" << m_last_pkt_nsec
		// << " len=" << m_last_pkt_len );

	ADARA::PacketType::Type bt = pkt.base_type();

	switch ( bt ) {

		case ADARA::PacketType::HEARTBEAT_TYPE:
			/* We actually *do* care about these packets after all;
			 * we need to Unregister Any DataSource that sends us one...!
			 * (or else we'll buffer everything else, swell up & pop! ;-)
			 */
			// Also, If the DataSource has Gone Event-Idle (Heartbeat)
			// Then We Need to "Count" the Heartbeat Packet Timestamp
			// As the Possible Max DataSource Time (for Container Cleanup)
			if ( compareTimeStamps( // Wallclock Time...!
					pkt.timestamp(), m_maxTime ) > 0 ) {
				m_maxTime = pkt.timestamp(); // Wallclock Time...!
				m_ctrl->updateMaxDataSourceTime( m_smsSourceId,
					&m_maxTime ); // Wallclock Time...!
			}
			return Parser::rxPacket(pkt);

		case ADARA::PacketType::SYNC_TYPE:
		case ADARA::PacketType::DATA_DONE_TYPE:
			/* We don't care about these packets, just drop them */
			/* (We still have to call their rxPacket() method
			 * to increment the Discarded Packet counts tho...! ;-D) */
			return Parser::rxPacket(pkt);

		case ADARA::PacketType::RAW_EVENT_TYPE:
		case ADARA::PacketType::MAPPED_EVENT_TYPE:
		case ADARA::PacketType::RTDL_TYPE:
		case ADARA::PacketType::SOURCE_LIST_TYPE:
		case ADARA::PacketType::DEVICE_DESC_TYPE:
		case ADARA::PacketType::VAR_VALUE_U32_TYPE:
		case ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE:
		case ADARA::PacketType::VAR_VALUE_STRING_TYPE:
		case ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE:
		case ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE:
		case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE:
		case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
			/* We use a 0 pulse id to indicate that we don't have an
			 * active pulse, and nothing should ever send one to us.
			 */
			if ( !pkt.pulseId() ) {
				/* Rate-limited logging of pulse id 0 */
				std::string log_info;
				if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
						RLL_PULSEID_ZERO, m_name,
						2, 10, 100, log_info ) ) {
					ERROR(log_info
						<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
						<< "Received Pulse Id of 0.0 from " << m_name);
				}
				return false;
			}
			/* Capture Max Packet Time for Stacked Container Expiration */
			if ( bt != ADARA::PacketType::SOURCE_LIST_TYPE
					&& bt != ADARA::PacketType::DEVICE_DESC_TYPE
					&& bt != ADARA::PacketType::STREAM_ANNOTATION_TYPE )
			{
				if ( compareTimeStamps( // Wallclock Time...!
						pkt.timestamp(), m_maxTime ) > 0 ) {
					m_maxTime = pkt.timestamp(); // Wallclock Time...!
					m_ctrl->updateMaxDataSourceTime( m_smsSourceId,
						&m_maxTime ); // Wallclock Time...!
				}
			}
			return Parser::rxPacket(pkt);

		default:
			/* We don't expect to see any other packet types here. */
			return rxUnknownPkt(pkt);
	}
}

bool DataSource::rxUnknownPkt(const ADARA::Packet &pkt)
{
	/* Rate-limited logging of unknown packet types */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_UNKNOWN_PACKET, m_name, 2, 10, 100, log_info ) ) {
		ERROR(log_info
			<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Unknown packet type 0x"
			<< std::hex << pkt.type() << std::dec
			<< " from " << m_name);
	}
	// It's Ok If We Get Something We Don't Recognize, Probably "New"...
	// - Don't Return "true" Here and Trigger a Disconnect, Just Log & Go!
	return false;
}

bool DataSource::rxOversizePkt( const ADARA::PacketHeader *hdr, 
		const uint8_t *UNUSED(chunk),
		unsigned int UNUSED(chunk_offset),
		unsigned int chunk_len )
{
	/* Rate-limited logging of oversized packets */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_OVERSIZE_PACKET, m_name, 2, 10, 100, log_info ) ) {
		// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
		if (hdr) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Oversized packet"
				<< " at " << hdr->timestamp().tv_sec // Wallclock Time...!
					- ADARA::EPICS_EPOCH_OFFSET // EPICS Time...!
				<< "." << std::setfill('0') << std::setw(9)
				<< hdr->timestamp().tv_nsec << std::setw(0)
				<< " of type 0x" << std::hex << hdr->type() << std::dec
				<< " payload_length=" << hdr->payload_length()
				<< " from " << m_name);
		} else {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "Oversized packet"
				<< " chunk_len=" << chunk_len
				<< " from " << m_name);
		}
	}
	return true;
}

void DataSource::resetPacketStats(void)
{
	// Dump Total Discarded Packet Statistics before Reset...
	std::string log_info;
	Parser::getDiscardedPacketsLogString(log_info);
	INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
		<< "resetPacketStats(): Totals Before Reset - " << log_info );

	// Reset Discarded Packet Statistics...
	Parser::resetDiscardedPacketsStats();
}

boost::shared_ptr<HWSource> DataSource::getHWSource( uint32_t hwId )
{
	HWSrcMap::iterator it;

	it = m_hwSources.find(hwId);

	if (it == m_hwSources.end()) {

		uint32_t smsId = m_ctrl->registerEventSource(m_smsSourceId, hwId);

		// Get Next Available HW Index for PVs...
		size_t i, max = m_hwIndices.size();
		int32_t hwIndex = -1; // default, result if no free Ids remain...
		for (i = 0; i < max && hwIndex < 0; i++) {
			if (!m_hwIndices[i]) {
				m_hwIndices.set(i);
				DEBUG( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "getHWSource() assigning PV hwIndex=" << i
					<< " to HWSource hwId=" << hwId
					<< " smsId=" << smsId );
				hwIndex = i;
			}
		}

		boost::shared_ptr<smsUint32PV> pvHwId;
		boost::shared_ptr<smsUint32PV> pvSmsId;
		boost::shared_ptr<smsUint32PV> pvEventBwSecond;
		boost::shared_ptr<smsUint32PV> pvEventBwMinute;
		boost::shared_ptr<smsUint32PV> pvEventBwTenMin;
		boost::shared_ptr<smsUint32PV> pvMetaBwSecond;
		boost::shared_ptr<smsUint32PV> pvMetaBwMinute;
		boost::shared_ptr<smsUint32PV> pvMetaBwTenMin;
		boost::shared_ptr<smsUint32PV> pvErrBwSecond;
		boost::shared_ptr<smsUint32PV> pvErrBwMinute;
		boost::shared_ptr<smsUint32PV> pvErrBwTenMin;

		// Create/Get Persistent EPICS PVs for This HWSource Instance...
		if ( hwIndex >= 0 ) {

			// Make New HWSource PVs, as needed...
			if ( (uint32_t) hwIndex >= m_pvHWSourceHwIds.size() ) {

				std::string prefix(m_ctrl->getPVPrefix());
				prefix += ":DataSource:";

				std::stringstream ss;
				ss << m_smsSourceId;
				prefix += ss.str();

				prefix += ":HWSource:";

				std::stringstream ss2;
				ss2 << hwIndex + 1; // count from 1 like everything else
				prefix += ss2.str();

				// HWSource Hw Id...
				m_pvHWSourceHwIds.resize(hwIndex + 1);
				m_pvHWSourceHwIds[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":HwId"));
				m_ctrl->addPV(m_pvHWSourceHwIds[hwIndex]);

				// HWSource SMS Id...
				m_pvHWSourceSmsIds.resize(hwIndex + 1);
				m_pvHWSourceSmsIds[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":SmsId"));
				m_ctrl->addPV(m_pvHWSourceSmsIds[hwIndex]);

				// HWSource Event Bandwidth Second...
				m_pvHWSourceEventBandwidthSecond.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthSecond[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthSecond"));
				m_ctrl->addPV(m_pvHWSourceEventBandwidthSecond[hwIndex]);

				// HWSource Event Bandwidth Minute...
				m_pvHWSourceEventBandwidthMinute.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthMinute[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthMinute"));
				m_ctrl->addPV(m_pvHWSourceEventBandwidthMinute[hwIndex]);

				// HWSource Event Bandwidth Ten Minutes...
				m_pvHWSourceEventBandwidthTenMin.resize(hwIndex + 1);
				m_pvHWSourceEventBandwidthTenMin[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":EventBandwidthTenMin"));
				m_ctrl->addPV(m_pvHWSourceEventBandwidthTenMin[hwIndex]);

				// HWSource Meta Bandwidth Second...
				m_pvHWSourceMetaBandwidthSecond.resize(hwIndex + 1);
				m_pvHWSourceMetaBandwidthSecond[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":MetaBandwidthSecond"));
				m_ctrl->addPV(m_pvHWSourceMetaBandwidthSecond[hwIndex]);

				// HWSource Meta Bandwidth Minute...
				m_pvHWSourceMetaBandwidthMinute.resize(hwIndex + 1);
				m_pvHWSourceMetaBandwidthMinute[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":MetaBandwidthMinute"));
				m_ctrl->addPV(m_pvHWSourceMetaBandwidthMinute[hwIndex]);

				// HWSource Meta Bandwidth Ten Minutes...
				m_pvHWSourceMetaBandwidthTenMin.resize(hwIndex + 1);
				m_pvHWSourceMetaBandwidthTenMin[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":MetaBandwidthTenMin"));
				m_ctrl->addPV(m_pvHWSourceMetaBandwidthTenMin[hwIndex]);

				// HWSource Err Bandwidth Second...
				m_pvHWSourceErrBandwidthSecond.resize(hwIndex + 1);
				m_pvHWSourceErrBandwidthSecond[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":ErrBandwidthSecond"));
				m_ctrl->addPV(m_pvHWSourceErrBandwidthSecond[hwIndex]);

				// HWSource Err Bandwidth Minute...
				m_pvHWSourceErrBandwidthMinute.resize(hwIndex + 1);
				m_pvHWSourceErrBandwidthMinute[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":ErrBandwidthMinute"));
				m_ctrl->addPV(m_pvHWSourceErrBandwidthMinute[hwIndex]);

				// HWSource Err Bandwidth Ten Minutes...
				m_pvHWSourceErrBandwidthTenMin.resize(hwIndex + 1);
				m_pvHWSourceErrBandwidthTenMin[hwIndex] =
					boost::shared_ptr<smsUint32PV>(new
						smsUint32PV(prefix + ":ErrBandwidthTenMin"));
				m_ctrl->addPV(m_pvHWSourceErrBandwidthTenMin[hwIndex]);
			}

			pvHwId = m_pvHWSourceHwIds[hwIndex];
			pvSmsId = m_pvHWSourceSmsIds[hwIndex];
			pvEventBwSecond = m_pvHWSourceEventBandwidthSecond[hwIndex];
			pvEventBwMinute = m_pvHWSourceEventBandwidthMinute[hwIndex];
			pvEventBwTenMin = m_pvHWSourceEventBandwidthTenMin[hwIndex];
			pvMetaBwSecond = m_pvHWSourceMetaBandwidthSecond[hwIndex];
			pvMetaBwMinute = m_pvHWSourceMetaBandwidthMinute[hwIndex];
			pvMetaBwTenMin = m_pvHWSourceMetaBandwidthTenMin[hwIndex];
			pvErrBwSecond = m_pvHWSourceErrBandwidthSecond[hwIndex];
			pvErrBwMinute = m_pvHWSourceErrBandwidthMinute[hwIndex];
			pvErrBwTenMin = m_pvHWSourceErrBandwidthTenMin[hwIndex];
		}
		else {
			DEBUG( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "getHWSource(): Out of PV Indices"
				<< " for HWSource hwId=" << hwId
				<< " smsId=" << smsId << "!" );
			// *Don't* throw an exception here, _Not_ mission critical!
		}

		// Create New HWSource... (Pass In Id and Bandwidth PVs...)
		boost::shared_ptr<HWSource> src( new HWSource(
			m_name, hwIndex, hwId, smsId, m_max_pulse_seq_list,
			pvHwId, pvSmsId,
			pvEventBwSecond, pvEventBwMinute, pvEventBwTenMin,
			pvMetaBwSecond, pvMetaBwMinute, pvMetaBwTenMin,
			pvErrBwSecond, pvErrBwMinute, pvErrBwTenMin,
			this ) );

		// Add to HWSource List for This DataSource...
		it = m_hwSources.insert(HWSrcMap::value_type(hwId, src)).first;

		// Update Number of HWSources PV...
		struct timespec now;
		clock_gettime(CLOCK_REALTIME_COARSE, &now);
		m_pvNumHWSources->update(m_hwSources.size(), &now,
			true /* no_log */);
	}

	return( it->second );
}

void DataSource::setIntermittent( uint32_t smsId, bool intermittent )
{
	ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
		<< "setIntermittent(): Setting HwSource Intermittent State"
		<< " smsId=" << smsId
		<< " intermittent=" << intermittent );

	HWSrcMap::iterator it;

	for (it = m_hwSources.begin(); it != m_hwSources.end(); it++) {
		if ( it->second->smsId() == smsId )
			break;
	}

	if (it == m_hwSources.end()) {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "setIntermittent(): HwSource Not Found for"
			<< " smsId=" << smsId
			<< " (intermittent=" << intermittent << ")!" );
	}
	else {
		ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "setIntermittent(): Found HwSource for"
			<< " smsId=" << smsId
			<< " -> Setting intermittent=" << intermittent
			<< " for hwId=" << it->second->hwId() );
		if ( intermittent ) {
			PulseSeqList::iterator psit;
			for ( psit=it->second->m_pulseSeqList.begin() ;
					psit != it->second->m_pulseSeqList.end() ; ++psit )
			{
				// Don't Re-markComplete()!
				psit->first.m_pulseGood = false;
				// But Keep Pulse Around for Recovery Later...
				// (No Call to endPulse( psit, false )...)
			}
			it->second->setSmsId( (uint32_t) -1 );
		}
		else {
			ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "setIntermittent():"
				<< " Re-Registering srcId=" << m_smsSourceId
				<< " hwId=" << it->second->hwId()
				<< " as an Event Source Again...");
			uint32_t smsId = m_ctrl->registerEventSource( m_smsSourceId,
				it->second->hwId() );
			it->second->setSmsId( smsId );
		}
		it->second->setIntermittent( intermittent );
	}
}

bool DataSource::rxPacket(const ADARA::RawDataPkt &pkt)
{
	return( handleDataPkt(&pkt, false) );
}

bool DataSource::rxPacket(const ADARA::MappedDataPkt &pkt)
{
	return( handleDataPkt(dynamic_cast<const ADARA::RawDataPkt *>(&pkt),
		true) );
}

bool DataSource::handleDataPkt(const ADARA::RawDataPkt *pkt,
		bool is_mapped)
{
	// Infrequently Update Internal Config from Live Control PV Values...
	static uint32_t cnt = 0;
	++cnt;

	boost::shared_ptr<HWSource> hw_src = getHWSource( pkt->sourceID() );

	bool isNewPulse;
	PulseSeqList::iterator psit = hw_src->getPulse( *pkt, isNewPulse );

	PulseInvariants &pulse = psit->first;

	// [INFREQUENTLY] Update "Check Packet Source/Pulse Sequence" Options
	// for This Data Source, from Live Config PV Value...
	// - We don't expect this to change very often, if ever,
	// so only check rarely, like every 3 minutes... ;-D
	// (Note: count already incremented above for overall method...!)
	if ( !(cnt % 9999) ) {
		bool tmpCheckSourceSequence = m_pvCheckSourceSequence->value();
		if ( tmpCheckSourceSequence != m_check_source_sequence )
		{
			ERROR("handleDataPkt():"
				<< " Updating Value of"
				<< " Check Source Sequence Numbers"
				<< " for " << m_name
				<< " from " << m_check_source_sequence
				<< " to " << tmpCheckSourceSequence);
			m_check_source_sequence = tmpCheckSourceSequence;
		}
		bool tmpCheckPulseSequence = m_pvCheckPulseSequence->value();
		if ( tmpCheckPulseSequence != m_check_pulse_sequence )
		{
			ERROR("handleDataPkt():"
				<< " Updating Value of"
				<< " Check Pulse Sequence Numbers"
				<< " for " << m_name
				<< " from " << m_check_pulse_sequence
				<< " to " << tmpCheckPulseSequence);
			m_check_pulse_sequence = tmpCheckPulseSequence;
		}
	}

	// Check for Valid Source Sequence from This Source...
	// (Increases Per Source Packet)
	bool sourceSeqOk = true;
	if ( m_check_source_sequence ) {
		sourceSeqOk = hw_src->checkSourceSeq(*pkt);
	}

	// Check for Intermittent HWSource...
	if ( hw_src->intermittent() ) {
		uint32_t recoverCount = 0;
		// See If Any Data Here, Or Just End of Last Long-Lost Pulse...?
		if ( !isNewPulse && pkt->num_events() == 0 ) {
			DEBUG( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "handleDataPkt(): Intermittent Data Source Activity"
				<< std::hex << " pulseId=0x" << pkt->pulseId() << std::dec
				<< " Long-Lost Pulse Returns for srcId=" << m_smsSourceId
				<< " hwId=" << hw_src->hwId()
				<< " - No Events: Ignore This Pulse, Wait for Data..." );
			pulse.m_pulseGood = false; // Don't Re-markComplete()!
			return false;
		}
		// "Open Loop Hysteresis" Recovery from Intermittent Data Source
		// (Just counts "Threshold" Data Packets, _Not_ A Good Indicator!)
		// -> TODO: Implement Better Intermittent Source Bookkeeping
		// in SMSControl (at some expense to performance...?)
		else if ( (recoverCount = hw_src->incrRecoverPktCount())
				< m_ctrl->getIntermittentDataThreshold() ) {
			DEBUG( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "handleDataPkt(): Data Packet Received for"
				<< " Intermittent Data Source"
				<< std::hex << " pulseId=0x" << pkt->pulseId() << std::dec
				<< " srcId=" << m_smsSourceId
				<< " hwId=" << hw_src->hwId()
				<< " recoverCount=" << recoverCount
				<< " < intermittentDataThresh="
				<< m_ctrl->getIntermittentDataThreshold()
				<< " - Process This Pulse"
				<< " But Leave Data Source as Intermittent..." );
		}
		else {
			// We've Reached the Intermittent Data Threshold Again,
			// Need to Re-Register This HwId as an Event Source!
			ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "handleDataPkt(): Intermittent Data Source Recovered!"
				<< std::hex << " pulseId=0x" << pkt->pulseId() << std::dec
				<< " Re-Registering srcId=" << m_smsSourceId
				<< " hwId=" << hw_src->hwId()
				<< " as an Event Source Again..." );
			uint32_t smsId = m_ctrl->registerEventSource( m_smsSourceId,
				hw_src->hwId() );
			hw_src->setSmsId( smsId );
			hw_src->setIntermittent( false );
			hw_src->resetRecoverPktCount();
		}
	}

	// Reset "RTDL Packets with No Data Packets" Count...
	hw_src->m_rtdlNoDataCount = 0;

	/* Check that the fields are consistent with the pulse we are
	 * currently processing. If not, then we've started a new pulse.
	 * The HWSource class will take care of missing end-of-pulse
	 * markers and duplicate pulse ids.
	 */
	bool good_pulse = true;
	if ( isNewPulse )
		good_pulse = hw_src->newPulse( *pkt, psit );
	else
		good_pulse = pulse.m_pulseGood;

	// Event Type Counts for Live Bandwidth Statistics...
	uint32_t event_count = 0, meta_count = 0, err_count = 0;

	// Pulse is "Good", Process Events...
	if ( good_pulse )
	{
		// [VERY INFREQUENTLY] Update "Mixed Data Packets" Option
		// for This Data Source, from Live Config PV Value...
		// - We don't expect this to change very often, if ever,
		// so only check very rarely, like every 10 minutes... ;-D
		// (Note: count already incremented above for overall method...!)
		if ( !(cnt % 99999) ) {
			bool tmpMixedDataPackets = m_pvMixedDataPackets->value();
			if ( tmpMixedDataPackets != m_mixed_data_packets )
			{
				ERROR("handleDataPkt():"
					<< " Updating Value of"
					<< " Mixed Data Packets"
					<< " for " << m_name
					<< " from " << m_mixed_data_packets
					<< " to " << tmpMixedDataPackets);
				m_mixed_data_packets = tmpMixedDataPackets;
			}
		}

		m_ctrl->pulseEvents( *pkt, hw_src->hwId(), pulse.m_dupCount,
			is_mapped, m_mixed_data_packets,
			event_count, meta_count, err_count );

		if ( ( m_check_pulse_sequence
				&& !(hw_src->checkPulseSeq( *pkt, psit )) )
					|| !sourceSeqOk ) {
			m_ctrl->markPartial( pkt->pulseId(), pulse.m_dupCount );
		}
	}

	// Pulse is "Bad", Count All Events as Errors...
	else {
		err_count = pkt->num_events();
	}

	// Sometimes we just can't rely on end-of-pulse being set correctly ;-b
	// - Infrequently Update from Live Control PV Value, once per minute?
	// (Note: count already incremented above for overall method...!)
	if ( !(cnt % 9999) ) {
		bool tmpIgnoreEoP = m_pvIgnoreEoP->value();
		if ( tmpIgnoreEoP != m_ignore_eop )
		{
			ERROR("handleDataPkt():"
				<< " Updating Value of"
				<< " Ignore Data Packet End-of-Pulse"
				<< " for " << m_name
				<< " from " << m_ignore_eop
				<< " to " << tmpIgnoreEoP);
			m_ignore_eop = tmpIgnoreEoP;
		}
	}

	if ( !m_ignore_eop && pkt->endOfPulse() )
		hw_src->endPulse( psit );

	// Count Events in Various Statistics...

	// Data Count Per "Read"...
	m_data_pkt_counts++;

	// Get Current Time for Bandwidth Statistics
	struct timespec now;
	clock_gettime( CLOCK_REALTIME_COARSE, &now );

	// Event Count Per Second (Updated Every 3 Seconds Or So...)
	if ( m_last_second_time.tv_sec + 3 <= now.tv_sec ) {
		// Update Bandwidth Count Per Second PVs...
		// Log Bandwidth Per Second Every 20 Seconds...
		updateBandwidthSecond( now, !( now.tv_sec % 20 ) );
		// Reset "Last Second" Time...
		m_last_second_time = now;
	}
	m_event_count_second += event_count;
	m_meta_count_second += meta_count;
	m_err_count_second += err_count;
	hw_src->m_event_count_second += event_count;
	hw_src->m_meta_count_second += meta_count;
	hw_src->m_err_count_second += err_count;

	// Event Count Per Minute
	uint32_t min = now.tv_sec / 60;
	if ( m_last_minute != min ) {
		// Update Bandwidth Count Per Minute PVs...
		updateBandwidthMinute( now, true );
		// Reset "Last Minute" Time...
		m_last_minute_time = now;
		// Reset "Last Minute"...
		m_last_minute = min;
	}
	m_event_count_minute += event_count;
	m_meta_count_minute += meta_count;
	m_err_count_minute += err_count;
	hw_src->m_event_count_minute += event_count;
	hw_src->m_meta_count_minute += meta_count;
	hw_src->m_err_count_minute += err_count;

	// Event Count Per Ten Minutes
	uint32_t tenmin = now.tv_sec / 600;
	if ( m_last_tenmin != tenmin ) {
		// Update Bandwidth Count Per Ten Minutes PVs...
		updateBandwidthTenMin( now, true );
		// Reset "Last Ten Minutes" Time...
		m_last_tenmin_time = now;
		// Reset "Last Ten Minutes"...
		m_last_tenmin = tenmin;
	}
	m_event_count_tenmin += event_count;
	m_meta_count_tenmin += meta_count;
	m_err_count_tenmin += err_count;
	hw_src->m_event_count_tenmin += event_count;
	hw_src->m_meta_count_tenmin += meta_count;
	hw_src->m_err_count_tenmin += err_count;

	return false;
}

bool DataSource::rxPacket(const ADARA::RTDLPkt &pkt)
{
	/* RTDL packets come in as the pulse occurs, but the events for
	 * them may not show up for some time. Just forward them to
	 * SMSControl.
	 */

	bool drop_pulse = false;

	// Do Duplicate Pulse Checking on a Per-DataSource Basis...
	// NOTE: This is a Bit "Loose" Relative to the DataSource::HWSource
	// Duplicate Pulse Checking, which Queues Up the Last So Many Pulses
	// to Ensure an Accurate "Duplicate" Count (Used as a Pulse "Index"!).
	// TODO: If this Ever Becomes More of a Problem, We Should Really
	// Do the Same Thing for RTDLs that we do for Raw/Mapped Data Packets,
	// which is Buffer Some Number of RTDLs to Catch Interleaved Dups! ;-D
	// (But Hopefully We Should Always have Non-Duplicate Pulses Anyway,
	// Right...? ;-D)
	if (pkt.pulseId() == m_lastRTDLPulseId) {
		m_dupRTDL++;
		/* Rate-limited logging of duplicate RTDLs */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_DUPLICATE_RTDL, m_name,
					2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "rxPacket(RTDLPkt): Local Duplicate RTDL"
				<< " from " << m_name
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags()
				<< " m_dupRTDL=" << m_dupRTDL);
		}
	}
	else m_dupRTDL = 0;

	// also check for "Local" SAWTOOTH, from within given DataSource stream
	if ( !m_ignore_local_sawtooth && pkt.pulseId() < m_lastRTDLPulseId ) {
		/* Rate-limited logging of local sawtooth RTDLs */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_SAWTOOTH_RTDL, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "rxPacket(RTDLPkt): Local SAWTOOTH RTDL from " << m_name
				<< std::hex << " m_lastRTDLPulseId=0x" << m_lastRTDLPulseId
				<< " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags());
		}
	}

	// done with this last pulseid...
	m_lastRTDLPulseId = pkt.pulseId();

	// strip off pulse nanoseconds...
	time_t sec = pkt.pulseId() >> 32;

	// check for "totally bogus" pulse times, in distant past/future... ;-b
	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);
	time_t future = 
		now.tv_sec - ADARA::EPICS_EPOCH_OFFSET + SECS_PER_WEEK;

	// before SNS time began... ;-D
	if ( sec < FACILITY_START_TIME )
	{
		/* Rate-limited logging of Bogus RTDLs from the Distant Past! */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_RTDL_PULSE_IN_PAST, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "*** Dropping Bogus RTDL Pulse Time"
				<< " from Distant Past (Before Facility Start Time)!"
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " (" << sec << " < " << FACILITY_START_TIME << ")"
				<< " (" << m_name << ")");
		}
		drop_pulse = true;
	}

	// more than a week into the future...! :-o
	else if ( sec > future )
	{
		/* Rate-limited logging of Bogus RTDLs from the Distant Future! */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_RTDL_PULSE_IN_FUTURE, m_name,
				2, 10, 100, log_info ) ) {
			ERROR(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "*** Dropping Bogus RTDL Pulse Time"
				<< " from Distant Future (Over One Week from Now)!"
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " (" << sec << " > " << future << ")"
				<< " (" << m_name << ")");
		}
		drop_pulse = true;
	}

	// just for yuks, check the cycle sequence
	if (m_lastRTDLCycle && pkt.cycle() != ((m_lastRTDLCycle + 1) % 600)) {
		/* Rate-limited logging of RTDL cycle out of sequence */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
				RLL_LOCAL_RTDL_SEQUENCE, m_name,
				2, 10, 100, log_info ) ) {
			WARN(log_info
				<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "rxPacket(RTDLPkt): Local RTDL Cycle Out of Sequence"
				<< " from " << m_name
				<< " m_lastRTDLCycle=" << m_lastRTDLCycle
				<< std::hex << " pulseId=0x" << pkt.pulseId() << std::dec
				<< " cycle=" << pkt.cycle()
				<< " vetoFlags=" << pkt.vetoFlags());
		}
	}
	m_lastRTDLCycle = pkt.cycle();

	// Ok, Process Pulse Now (Maybe... :-)
	if ( !drop_pulse ) {
		m_ctrl->pulseRTDL(pkt, m_dupRTDL);
	}

	// [LESS FREQUENTLY] Check for Run-Away Data Sources...
	// (Lots of RTDLs filling up our internal Pulse Buffer,
	// with No RawDataPkts to release them... ;-b)

	static uint32_t cnt = 0;

	// Once Per Second...
	uint32_t freq = 60;

	if ( !(++cnt % freq) )
	{
		// Update RTDL "No Data" Threshold from PV...
		m_rtdlNoDataThresh = m_pvRTDLNoDataThresh->value();

		HWSrcMap::iterator it = m_hwSources.begin();

		bool changed = false;

		while ( it != m_hwSources.end() ) {

			if ( ( it->second->m_rtdlNoDataCount += freq )
					> m_rtdlNoDataThresh ) {

				ERROR( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Run-Away Data Source " << m_name
					<< " pulseId=0x"
						<< std::hex << pkt.pulseId() << std::dec
					<< ", " << it->second->m_rtdlNoDataCount
					<< " (> " << m_rtdlNoDataThresh << ")"
					<< " RTDL Pulses without a Corresponding RawDataPkt!"
					<< " Unregistering Event Source "
						<< it->second->smsId());

				// Reset HWSource PV Index Bit, Clear Out SMS Id PV Value.
				if ( it->second->m_hwIndex >= 0 ) {
					it->second->m_pvHWSourceSmsId->update(-1, &now);
					m_hwIndices.reset( it->second->m_hwIndex );
				}

				// Only Unregister Event Source if Still Active...!
				if ( it->second->smsId() != (uint32_t) -1 ) {
					m_ctrl->unregisterEventSource(m_smsSourceId,
						it->second->smsId());
				}

				// Remove Hardware Source from Map...
				// (Note: iterator increments to next element,
				// but returns current element for deletion... :-)
				m_hwSources.erase(it++);

				changed = true;
			}
			else {
				++it;
			}
		}

		// Update Number of HWSources PV...
		if ( changed ) {
			m_pvNumHWSources->update(m_hwSources.size(), &now,
				true /* no_log */);
		}
	}

	// Count Pulse in Various Statistics...

	// RTDL Count Per "Read"...
	m_rtdl_pkt_counts++;

	// Pulse Count Per Second (Updated Every 3 Seconds Or So...)
	if ( m_last_second_time.tv_sec + 3 <= now.tv_sec ) {
		// Update Bandwidth Count Per Second PVs...
		// Log Bandwidth Per Second Every 20 Seconds...
		updateBandwidthSecond( now, !( now.tv_sec % 20 ) );
		// Reset "Last Second" Time...
		m_last_second_time = now;
	}
	m_pulse_count_second++;

	// Pulse Count Per Minute
	uint32_t min = now.tv_sec / 60;
	if ( m_last_minute != min ) {
		// Update Bandwidth Count Per Minute PVs...
		updateBandwidthMinute( now, true );
		// Reset "Last Minute" Time...
		m_last_minute_time = now;
		// Reset "Last Minute"...
		m_last_minute = min;
	}
	m_pulse_count_minute++;

	// Pulse Count Per Ten Minutes
	uint32_t tenmin = now.tv_sec / 600;
	if ( m_last_tenmin != tenmin ) {
		// Update Bandwidth Count Per Ten Minutes PVs...
		updateBandwidthTenMin( now, true );
		// Reset "Last Ten Minutes" Time...
		m_last_tenmin_time = now;
		// Reset "Last Ten Minutes"...
		m_last_tenmin = tenmin;
	}
	m_pulse_count_tenmin++;

	return false;
}

void DataSource::resetBandwidthStatistics(void)
{
	// Reset Bandwidth Times & Counts (Triggers Initial Logging)

	// Per Second...
	m_pulse_count_second = 0;
	m_event_count_second = 0;
	m_meta_count_second = 0;
	m_err_count_second = 0;

	// Per Minute...
	m_pulse_count_minute = 0;
	m_event_count_minute = 0;
	m_meta_count_minute = 0;
	m_err_count_minute = 0;

	// Per Ten Minutes...
	m_pulse_count_tenmin = 0;
	m_event_count_tenmin = 0;
	m_meta_count_tenmin = 0;
	m_err_count_tenmin = 0;

	// Get Current Time for "Elapsed" Bandwidth Calculations...
	struct timespec now;
	clock_gettime( CLOCK_REALTIME_COARSE, &now );
	// Properly Initialize a Starting Time for BW Seconds...
	m_last_second_time = now;
	// Properly Initialize a Starting Time for BW Minutes...
	m_last_minute_time = now;
	// Properly Initialize a Starting Time for BW Ten Minutes...
	m_last_tenmin_time = now;
	// Time Trackers...
	m_last_minute = -1;
	m_last_tenmin = -1;
}

void DataSource::updateBandwidthSecond( struct timespec &now, bool do_log )
{
	double elapsed = calcDiffSeconds( now, m_last_second_time );

	uint32_t pulse_rate = (uint32_t)(
		((double) m_pulse_count_second) / elapsed );
	uint32_t event_rate = (uint32_t)(
		((double) m_event_count_second) / elapsed );
	uint32_t meta_rate = (uint32_t)(
		((double) m_meta_count_second) / elapsed );
	uint32_t err_rate = (uint32_t)(
		((double) m_err_count_second) / elapsed );

	// Log the Second-Based Bandwidth Statistics Updates...
	if ( do_log ) {
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Bandwidth Per Second for " << m_name << ":"
			<< " Pulses=" << pulse_rate
			<< " Events=" << event_rate
			<< " Meta=" << meta_rate
			<< " Err=" << err_rate );
	}

	// Update Bandwidth Count Per Second PVs...
	m_pvPulseBandwidthSecond->update( pulse_rate, &now,
		true /* no_log */ );
	m_pvEventBandwidthSecond->update( event_rate, &now,
		true /* no_log */ );
	m_pvMetaBandwidthSecond->update( meta_rate, &now,
		true /* no_log */ );
	m_pvErrBandwidthSecond->update( err_rate, &now,
		true /* no_log */ );

	// Reset Counters for Next Second...
	m_pulse_count_second = 0;
	m_event_count_second = 0;
	m_meta_count_second = 0;
	m_err_count_second = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			event_rate = (uint32_t)(
				((double) it->second->m_event_count_second) / elapsed );
			meta_rate = (uint32_t)(
				((double) it->second->m_meta_count_second) / elapsed );
			err_rate = (uint32_t)(
				((double) it->second->m_err_count_second) / elapsed );
			if ( do_log && it->second->m_event_count_second > 0 ) {
				INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Bandwidth Per Second for " << m_name << ":"
					<< " HWSource HwId=" << it->second->hwId()
					<< " Events=" << event_rate
					<< " Meta=" << meta_rate
					<< " Err=" << err_rate );
			}
			it->second->m_pvHWSourceEventBandwidthSecond->update(
				event_rate, &now, true /* no_log */ );
			it->second->m_pvHWSourceMetaBandwidthSecond->update(
				meta_rate, &now, true /* no_log */ );
			it->second->m_pvHWSourceErrBandwidthSecond->update(
				err_rate, &now, true /* no_log */ );
		}
		it->second->m_event_count_second = 0;
		it->second->m_meta_count_second = 0;
		it->second->m_err_count_second = 0;
	}
}

void DataSource::updateBandwidthMinute( struct timespec &now, bool do_log )
{
	double elapsed = calcDiffSeconds( now, m_last_minute_time );

	// Log the Minute-Based Bandwidth Statistics Updates...
	if ( do_log ) {
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Bandwidth Per Minute for " << m_name << ":"
			<< " Pulses=" << m_pulse_count_minute
			<< " (" << ( m_pulse_count_minute / elapsed ) << "/sec)"
			<< " Events=" << m_event_count_minute
			<< " (" << ( m_event_count_minute / elapsed ) << "/sec)"
			<< " Meta=" << m_meta_count_minute
			<< " (" << ( m_meta_count_minute / elapsed ) << "/sec)"
			<< " Err=" << m_err_count_minute
			<< " (" << ( m_err_count_minute / elapsed ) << "/sec)" );
	}

	// Update Bandwidth Count Per Minute PVs...
	m_pvPulseBandwidthMinute->update(m_pulse_count_minute, &now,
		true /* no_log */ );
	m_pvEventBandwidthMinute->update(m_event_count_minute, &now,
		true /* no_log */ );
	m_pvMetaBandwidthMinute->update(m_meta_count_minute, &now,
		true /* no_log */ );
	m_pvErrBandwidthMinute->update(m_err_count_minute, &now,
		true /* no_log */ );

	// Reset Counters for Next Minute...
	m_pulse_count_minute = 0;
	m_event_count_minute = 0;
	m_meta_count_minute = 0;
	m_err_count_minute = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			if ( do_log && it->second->m_event_count_minute > 0 ) {
				INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Bandwidth Per Minute for " << m_name << ":"
					<< " HWSource HwId=" << it->second->hwId()
					<< " Events=" << it->second->m_event_count_minute
					<< " ("
						<< ( it->second->m_event_count_minute / elapsed )
						<< "/sec)"
					<< " Meta=" << it->second->m_meta_count_minute
					<< " ("
						<< ( it->second->m_meta_count_minute / elapsed )
						<< "/sec)"
					<< " Err=" << it->second->m_err_count_minute
					<< " ("
						<< ( it->second->m_err_count_minute / elapsed )
						<< "/sec)" );
			}
			it->second->m_pvHWSourceEventBandwidthMinute->update(
				it->second->m_event_count_minute, &now,
				true /* no_log */ );
			it->second->m_pvHWSourceMetaBandwidthMinute->update(
				it->second->m_meta_count_minute, &now,
				true /* no_log */ );
			it->second->m_pvHWSourceErrBandwidthMinute->update(
				it->second->m_err_count_minute, &now,
				true /* no_log */ );
		}
		it->second->m_event_count_minute = 0;
		it->second->m_meta_count_minute = 0;
		it->second->m_err_count_minute = 0;
	}
}

void DataSource::updateBandwidthTenMin( struct timespec &now, bool do_log )
{
	double elapsed = calcDiffSeconds( now, m_last_tenmin_time );

	// Log the Ten-Minute-Based Bandwidth Statistics Updates...
	if ( do_log ) {
		INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Bandwidth Per Ten Minutes for " << m_name << ":"
			<< " Pulses=" << m_pulse_count_tenmin
			<< " (" << ( m_pulse_count_tenmin / elapsed ) << "/sec)"
			<< " Events=" << m_event_count_tenmin
			<< " (" << ( m_event_count_tenmin / elapsed ) << "/sec)"
			<< " Meta=" << m_meta_count_tenmin
			<< " (" << ( m_meta_count_tenmin / elapsed ) << "/sec)"
			<< " Err=" << m_err_count_tenmin
			<< " (" << ( m_err_count_tenmin / elapsed ) << "/sec)" );
	}

	// Update Bandwidth Count Per Ten Minutes PVs...
	m_pvPulseBandwidthTenMin->update(m_pulse_count_tenmin, &now,
		true /* no_log */ );
	m_pvEventBandwidthTenMin->update(m_event_count_tenmin, &now,
		true /* no_log */ );
	m_pvMetaBandwidthTenMin->update(m_meta_count_tenmin, &now,
		true /* no_log */ );
	m_pvErrBandwidthTenMin->update(m_err_count_tenmin, &now,
		true /* no_log */ );

	// Reset Counters for Next Ten Minutes...
	m_pulse_count_tenmin = 0;
	m_event_count_tenmin = 0;
	m_meta_count_tenmin = 0;
	m_err_count_tenmin = 0;

	// Handle ALL HWSource Bandwidth Statistics/Reset Counters...
	for ( HWSrcMap::iterator it = m_hwSources.begin();
			it != m_hwSources.end() ; it++ ) {
		if ( it->second->m_hwIndex >= 0 ) {
			if ( do_log && it->second->m_event_count_tenmin > 0 ) {
				INFO( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Bandwidth Per Ten Minutes for " << m_name << ":"
					<< " HWSource HwId=" << it->second->hwId()
					<< " Events=" << it->second->m_event_count_tenmin
					<< " ("
						<< ( it->second->m_event_count_tenmin / elapsed )
						<< "/sec)"
					<< " Meta=" << it->second->m_meta_count_tenmin
					<< " ("
						<< ( it->second->m_meta_count_tenmin / elapsed )
						<< "/sec)"
					<< " Err=" << it->second->m_err_count_tenmin
					<< " ("
						<< ( it->second->m_err_count_tenmin / elapsed )
						<< "/sec)" );
			}
			it->second->m_pvHWSourceEventBandwidthTenMin->update(
				it->second->m_event_count_tenmin, &now,
				true /* no_log */ );
			it->second->m_pvHWSourceMetaBandwidthTenMin->update(
				it->second->m_meta_count_tenmin, &now,
				true /* no_log */ );
			it->second->m_pvHWSourceErrBandwidthTenMin->update(
				it->second->m_err_count_tenmin, &now,
				true /* no_log */ );
		}
		it->second->m_event_count_tenmin = 0;
		it->second->m_meta_count_tenmin = 0;
		it->second->m_err_count_tenmin = 0;
	}
}

bool DataSource::rxPacket(const ADARA::SourceListPkt &pkt)
{
	const uint32_t *ids = pkt.ids();

	/* TODO note undeclared ids when we get raw event packets */
	for (uint32_t i = 0; i < pkt.num_ids(); i++) {
		getHWSource(ids[i]);
	}
	return false;
}

bool DataSource::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Device Descriptor Packet for Each Device ID...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Remove Any Former Device Descriptor Packet for This Device ID...
	if ( dit != m_devices.end() ) {
		m_devices.erase(dit);
	}
	// If Not An Empty Descriptor, then Save Descriptor Packet...
	if ( !pkt.description().empty() ) {
		boost::shared_ptr<ADARA::DeviceDescriptorPkt> ddp;
		ddp = boost::make_shared<ADARA::DeviceDescriptorPkt>(pkt);
		m_devices[ pkt.devId() ].m_descriptorPkt = ddp;
		m_devices[ pkt.devId() ].m_devId = pkt.devId();
	}

	m_ctrl->updateDescriptor(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Variable Value Packet...
		boost::shared_ptr<ADARA::VariableU32Pkt> vvp;
		vvp = boost::make_shared<ADARA::VariableU32Pkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Variable Value Packet...
		boost::shared_ptr<ADARA::VariableDoublePkt> vvp;
		vvp = boost::make_shared<ADARA::VariableDoublePkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableStringPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Variable Value Packet...
		boost::shared_ptr<ADARA::VariableStringPkt> vvp;
		vvp = boost::make_shared<ADARA::VariableStringPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableU32ArrayPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Variable Value Packet...
		boost::shared_ptr<ADARA::VariableU32ArrayPkt> vvp;
		vvp = boost::make_shared<ADARA::VariableU32ArrayPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::VariableDoubleArrayPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Variable Value Packet...
		boost::shared_ptr<ADARA::VariableDoubleArrayPkt> vvp;
		vvp = boost::make_shared<ADARA::VariableDoubleArrayPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::MultVariableU32Pkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Multiple Variable Value Packet...
		boost::shared_ptr<ADARA::MultVariableU32Pkt> vvp;
		vvp = boost::make_shared<ADARA::MultVariableU32Pkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::MultVariableDoublePkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Multiple Variable Value Packet...
		boost::shared_ptr<ADARA::MultVariableDoublePkt> vvp;
		vvp = boost::make_shared<ADARA::MultVariableDoublePkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::MultVariableStringPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Multiple Variable Value Packet...
		boost::shared_ptr<ADARA::MultVariableStringPkt> vvp;
		vvp = boost::make_shared<ADARA::MultVariableStringPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::MultVariableU32ArrayPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Multiple Variable Value Packet...
		boost::shared_ptr<ADARA::MultVariableU32ArrayPkt> vvp;
		vvp = boost::make_shared<ADARA::MultVariableU32ArrayPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::MultVariableDoubleArrayPkt &pkt)
{
	// * For Saved Input Stream Prologue *
	// Save the Latest Variable Value Update Packet for Each PV/Device...
	DeviceMap::iterator dit = m_devices.find( pkt.devId() );
	// Make Sure We Already Saw the Device Descriptor Packet for This PV!
	if ( dit != m_devices.end() ) {
		// Look for Existing Variable Value Update Packet for this Device
		VariablePktMap &varPktMap = dit->second.m_variablePkts;
		VariablePktMap::iterator vit = varPktMap.find( pkt.varId() );
		// Remove Any Former Variable Value Packet for This Device PV...
		if ( vit != varPktMap.end() ) {
			varPktMap.erase(vit);
		}
		// Save Multiple Variable Value Packet...
		boost::shared_ptr<ADARA::MultVariableDoubleArrayPkt> vvp;
		vvp = boost::make_shared<ADARA::MultVariableDoubleArrayPkt>(pkt);
		varPktMap[ pkt.varId() ] = vvp;
	}

	m_ctrl->updateValue(pkt, m_smsSourceId);
	return false;
}

bool DataSource::rxPacket(const ADARA::AnnotationPkt &pkt)
{
	// Meter Live Control PV Updates...
	static uint32_t cnt = 0;

	/* Rate-limited logging of Annotation packets */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_ANNOTATION, m_name, 3, 10, 100, log_info ) ) {
		std::stringstream ss;
		ss << " - MarkerType ";
		switch ( pkt.marker_type() ) {
			case ADARA::MarkerType::GENERIC:
				ss << "GENERIC"; break;
			case ADARA::MarkerType::SCAN_START:
				ss << "SCAN_START"; break;
			case ADARA::MarkerType::SCAN_STOP:
				ss << "SCAN_STOP"; break;
			case ADARA::MarkerType::PAUSE:
				ss << "PAUSE"; break;
			case ADARA::MarkerType::RESUME:
				ss << "RESUME"; break;
			case ADARA::MarkerType::OVERALL_RUN_COMMENT:
				ss << "OVERALL_RUN_COMMENT"; break;
			case ADARA::MarkerType::SYSTEM:
				ss << "SYSTEM"; break;
		}
		ss << " (" << pkt.marker_type() << ")";
		ss << ", ScanIndex=" << pkt.scanIndex();
		ss << ", Comment=[" << pkt.comment() << "]";
		INFO(log_info
			<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "External Annotation Packet Received from " << m_name
			<< ss.str()
			<< " at " << pkt.timestamp().tv_sec // Wallclock Time...!
				- ADARA::EPICS_EPOCH_OFFSET // EPICS Time...!
			<< "." << std::setfill('0') << std::setw(9)
			<< pkt.timestamp().tv_nsec);
	}

	// (Check the Live Control PV Periodically, but _Not_ Every Packet!)
	if ( !( ++cnt % 9 ) ) {
		Markers::PassThru tmpIgnoreAnnotationPkts =
			(Markers::PassThru) m_pvIgnoreAnnotationPkts->value();
		if ( tmpIgnoreAnnotationPkts != m_ignore_annotation_pkts )
		{
			ERROR("rxPacket( AnnotationPkt ):"
				<< " Updating Value of"
				<< " Ignore Annotation Packets"
				<< " for " << m_name
				<< " from " << m_ignore_annotation_pkts
				<< " to " << tmpIgnoreAnnotationPkts);
			m_ignore_annotation_pkts = tmpIgnoreAnnotationPkts;
		}
	}

	// Optionally Interpret External or Replayed ADARA Marker/Annotations
	if ( m_ignore_annotation_pkts != Markers::IGNORE )
	{
		// Pass-Thru Invoke Appropriate Markers Handler Method...
		boost::shared_ptr<Markers> markers = m_ctrl->getMarkers();
		struct timespec ts = pkt.timestamp(); // Wallclock Time...!
		switch ( pkt.marker_type() ) {
			case ADARA::MarkerType::GENERIC:
				markers->addAnnotationComment( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), pkt.comment() );
				break;
			case ADARA::MarkerType::SCAN_START:
				markers->startScan( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), "" );
					// Don't Pass Comment, Let Scan Start Re-Generate It...
				break;
			case ADARA::MarkerType::SCAN_STOP:
				markers->stopScan( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), "" );
					// Don't Pass Comment, Let Scan Stop Re-Generate It...
				break;
			case ADARA::MarkerType::PAUSE:
				markers->pause( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), "" );
					// Don't Pass Comment, Let Pause Re-Generate It...
				break;
			case ADARA::MarkerType::RESUME:
				markers->resume( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), "" );
					// Don't Pass Comment, Let Resume Re-Generate It...
				break;
			case ADARA::MarkerType::OVERALL_RUN_COMMENT:
				markers->addNotesComment( &ts, // Wallclock Time...!
					m_ignore_annotation_pkts,
					pkt.scanIndex(), pkt.comment() );
				break;
			case ADARA::MarkerType::SYSTEM:
				// Always Pass Thru System Comments...
				markers->addSystemComment( &ts, // Wallclock Time...!
					pkt.scanIndex(), pkt.comment() );
				// Execute Any External System Commands... (Start/Stop Run)
				// (Need to Do This in _Either_ PassThru or Execute Mode,
				// I.e. to Get Proper Starting TimeStamp for Run Replay!)
				DEBUG( ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "DataSource::rxPacket(AnnotationPkt):"
					<< " External RunControl Packet Received"
					<< " from " << m_name
					<< " - Execute:"
					<< " Command=[" << pkt.comment() << "]"
					<< " scanIndex=" << pkt.scanIndex()
					<< " at " << pkt.timestamp().tv_sec // Wallclock Time!
						- ADARA::EPICS_EPOCH_OFFSET // EPICS Time...!
					<< "." << std::setfill('0') << std::setw(9)
					<< pkt.timestamp().tv_nsec );
				m_ctrl->externalRunControl( &ts, // Wallclock Time...!
					pkt.scanIndex(), pkt.comment() );
				break;
		}
	}

	return false;
}

bool DataSource::rxPacket(const ADARA::HeartbeatPkt &pkt)
{
	/* Rate-limited logging of heartbeat packets */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_DataSource,
			RLL_HEARTBEAT, m_name, 60, 3, 10, log_info ) ) {
		INFO(log_info
			<< ( m_ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Heartbeat Packet for " << m_name);
	}

	// Update/Dump Any Pulse/Event Bandwidth Statistics...
	//    - Only Do Logging If We _Just Now_ Went Idle ("1st Heartbeat"),
	//    I.e. We Still had Some HWSources when this Heartbeat Arrived...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);
	bool do_log = ( m_hwSources.size() > 0 );
	updateBandwidthSecond( now, do_log );
	updateBandwidthMinute( now, do_log );
	updateBandwidthTenMin( now, do_log );

	// Check to Make Sure Whether We Should Expire Any
	// Containers (or Embedded PauseModes...)
	struct timespec ts = pkt.timestamp(); // Wallclock Time...!
	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET; // EPICS Time...!
	StorageManager::checkContainerTimeout( "HeartbeatPkt", ts );

	// In case this DataSource was formerly registered and sending events,
	// we need to *Unregister All Registered SourceIds* when we receive a
	// Heartbeat packet, so we won't hold back any other DataSources and
	// swell up to buffer-explode...! ;-O

	/* Complete any outstanding pulses, and inform the manager of our
	 * now-idle state (not down, just idle... :-)
	 */
	unregisterHWSources(false, false, "Now-Idle");

	m_lastRTDLPulseId = 0;
	m_lastRTDLCycle = 0;

	return false;
}

void DataSource::onSavePrologue( bool UNUSED(capture_last) )
{
	// Make a Little Stream Annotation Packet for This Saved Input Stream

	uint32_t pkt[ 2 + sizeof(ADARA::Header) / sizeof(uint32_t) ];
	uint32_t pad = 0;
	struct iovec iov;
	IoVector iovec;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	pkt[0] = 2 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::STREAM_ANNOTATION_TYPE,
		ADARA::PacketType::STREAM_ANNOTATION_VERSION );
	pkt[2] = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET; // EPICS Time...!
	pkt[3] = now.tv_nsec;

	pkt[4] = (uint32_t) ADARA::MarkerType::SYSTEM << 16;
	pkt[5] = 0;

	iov.iov_base = pkt;
	iov.iov_len = sizeof(pkt);
	iovec.push_back(iov);

	std::stringstream ss;
	ss << "SMS Saved Input Stream"
		<< " for Data Source ID " << m_smsSourceId
		<< " [" << m_name << "]";
	
	pkt[0] += ( ss.str().size() + 3 ) & ~3;
	pkt[4] |= ss.str().size();

	iov.iov_base = const_cast<char *>(ss.str().c_str());
	iov.iov_len = ss.str().size();
	iovec.push_back(iov);

	if ( ss.str().size() % 4 ) {
		iov.iov_base = &pad;
		iov.iov_len = 4 - ( ss.str().size() % 4 );
		iovec.push_back(iov);
	}

	StorageManager::addSavePrologue( iovec, m_smsSourceId );

	// Dump All Known Device Descriptors and Last Variable Value Updates

	DeviceMap::iterator dit, dend = m_devices.end();

	for ( dit = m_devices.begin(); dit != dend; ++dit ) {

		DeviceVariables &dev = dit->second;
		ADARA::Packet *dev_pkt = dev.m_descriptorPkt.get();

		StorageManager::addSavePrologue(
			dev_pkt->packet(), dev_pkt->packet_length(), m_smsSourceId );

		VariablePktMap &varPktMap = dev.m_variablePkts;
		VariablePktMap::iterator vit, vend = varPktMap.end();

		for ( vit = varPktMap.begin(); vit != vend; ++vit ) {

			ADARA::Packet *var_pkt = vit->second.get();

			// Handle Multiple Variable Value Packets!
			// (Strip Off "Last" Variable Value and Create
			// New Single Variable Value Packet for Save Prologue...)

			ADARA::PacketSharedPtr newPkt;
			bool is_mult = false;

			if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE )
			{
				ADARA::MultVariableU32Pkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				m_ctrl->extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE )
			{
				ADARA::MultVariableDoublePkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				m_ctrl->extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE )
			{
				ADARA::MultVariableStringPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				m_ctrl->extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE )
			{
				ADARA::MultVariableU32ArrayPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				m_ctrl->extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE)
			{
				ADARA::MultVariableDoubleArrayPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				m_ctrl->extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}

			if ( is_mult )
			{
				// Replace Multiple Variable Value Packet for
				// This Device PV with New Single Variable Value Packet
				vit->second.swap( newPkt );

				// Use New Packet in Save Prologue...
				var_pkt = vit->second.get();
			}

			StorageManager::addSavePrologue(
				var_pkt->packet(), var_pkt->packet_length(),
				m_smsSourceId );
		}
	}
}

