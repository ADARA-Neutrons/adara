
#include "Logging.h"

LOGGER("SMS.QuickCounter");

#include <string>

#include <stdint.h>
#include <time.h>

#include "ADARA.h"
#include "FastMeta.h"
#include "QuickCounter.h"
#include "StorageManager.h"
#include "ADARAUtils.h"

QuickCounter::QuickCounter(boost::shared_ptr<MetaDataMgr> mgr,
			struct FastMeta::Variable *var,
			uint32_t key, uint32_t stat_devId,
			double done_timeout, bool auto_reset):
		m_done_timeout(done_timeout),
		m_meta(mgr), m_stat_devId(stat_devId), m_var(var), m_key(key),
		m_auto_reset(auto_reset)
{
	LOGGER_INIT();

	if ( var == NULL )
	{
		ERROR("QuickCounter(): NULL Fast Meta-Data Variable..."
			<< " Bailing...!!");
		return;
	}

	DEBUG("QuickCounter(): New Fast Meta-Data Counter Device"
		<< " [" << m_var->m_name << "]"
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
		<< " Done Timeout = " << m_done_timeout
		<< " Auto Reset = " << m_auto_reset);

	m_counting = QC_NOT_COUNTING;

	// Initialize Statistics...

	reset_stats();

	// Create Counter PVs...

	m_ctrl = SMSControl::getInstance();

	std::string prefix(m_ctrl->getPVPrefix());
	prefix += ":" + m_var->m_name;

	m_pvCounting = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":IsCounting",
			0, INT32_MAX, /* AutoSave */ false));

	m_pvDoneTimeout = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":DoneTimeout",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ true));

	m_pvAutoReset = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":AutoReset",
			/* AutoSave */ true));

	m_pvElapsedTime = boost::shared_ptr<smsFloat64PV>(new
		smsFloat64PV(prefix + ":ElapsedTime",
			0.0, FLOAT64_MAX, FLOAT64_EPSILON,
			/* AutoSave */ false));

	m_pvDetectorCountsAll = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":DetectorAll",
			0, INT32_MAX, /* AutoSave */ false));

	m_pvMonitorCountsAll = boost::shared_ptr<smsUint32PV>(new
		smsUint32PV(prefix + ":MonitorAll",
			0, INT32_MAX, /* AutoSave */ false));

	m_ctrl->addPV(m_pvCounting);
	m_ctrl->addPV(m_pvDoneTimeout);
	m_ctrl->addPV(m_pvAutoReset);
	m_ctrl->addPV(m_pvElapsedTime);
	m_ctrl->addPV(m_pvDetectorCountsAll);
	m_ctrl->addPV(m_pvMonitorCountsAll);

	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	m_pvCounting->update(m_counting, &now);
	m_pvDoneTimeout->update(m_done_timeout, &now);
	m_pvAutoReset->update(m_auto_reset, &now);
	m_pvElapsedTime->update(m_elapsed_time, &now);
	m_pvDetectorCountsAll->update(m_detector_counts_all, &now);
	m_pvMonitorCountsAll->update(m_monitor_counts_all, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	double dvalue;
	bool bvalue;

	if ( StorageManager::getAutoSavePV( m_pvDoneTimeout->getName(),
			dvalue, ts ) ) {
		m_done_timeout = dvalue;
		m_pvDoneTimeout->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_pvAutoReset->getName(),
			bvalue, ts ) ) {
		m_auto_reset = bvalue;
		m_pvAutoReset->update(bvalue, &ts);
	}

	// Register This Quick Counter with SMSControl...
	m_counter_id = m_ctrl->registerQuickCounter(m_var->m_name);
}

void QuickCounter::reset_stats(void)
{
	m_start_time.tv_sec = 0;
	m_start_time.tv_nsec = 0;

	m_stop_time.tv_sec = 0;
	m_stop_time.tv_nsec = 0;

	m_done_time.tv_sec = 0;
	m_done_time.tv_nsec = 0;

	m_elapsed_time = 0.0;

	m_detector_counts_all = 0;
	m_monitor_counts_all = 0;

	m_update_det_count_pvs_cnt = 0;
	m_update_mon_count_pvs_cnt = 0;
}

void QuickCounter::update_pvs(struct timespec *ts)
{
	static uint32_t cnt = 0;

	// Live PVs Always Get "Now" Update Times...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	m_pvCounting->update(m_counting, &now);
	m_pvElapsedTime->update(m_elapsed_time, &now);
	m_pvDetectorCountsAll->update(m_detector_counts_all, &now);
	m_pvMonitorCountsAll->update(m_monitor_counts_all, &now);

	// Update Done Timeout from PV... (Periodically...)
	if ( !(++cnt % 100) )
	{
		double new_done_timeout = m_pvDoneTimeout->value();
		if ( !approximatelyEqual( m_done_timeout, new_done_timeout,
				0.0000001 ) ) {
			ERROR("update_pvs(): Updating Done Timeout"
				<< " for Fast Meta-Data Counter Device"
				<< " [" << m_var->m_name << "]"
				<< " from " << m_done_timeout
				<< " to " << new_done_timeout);
			m_done_timeout = new_done_timeout;
		}

		bool new_auto_reset = m_pvAutoReset->value();
		if ( m_auto_reset != new_auto_reset ) {
			ERROR("update_pvs(): Updating Auto Reset"
				<< " for Fast Meta-Data Counter Device"
				<< " [" << m_var->m_name << "]"
				<< " from " << m_auto_reset
				<< " to " << new_auto_reset);
			m_auto_reset = new_auto_reset;
		}
	}

	// Send Fast Meta-Data PV Updates for Stats Device...
	// Use Timestamp from Data Stream (Handy for Test Harness...! ;-D)
	sendUpdates(ts);
}

void QuickCounter::sendUpdates(struct timespec *ts)
{
	// PV Index for Fast Meta-Data Quick Counter Device... ;-D
	// TODO: Hard-Coded PV Indices For Now... Should Define Constants...
	uint32_t varId;

	// Is Counting State PV
	varId = 1;
	sendUpdateUint32(ts, varId, m_counting);

	// Elapsed Time PV
	varId = 2;
	sendUpdateFloat64(ts, varId, m_elapsed_time);

	// Detector Counts All PV
	varId = 3;
	sendUpdateUint32(ts, varId, m_detector_counts_all);

	// Monitor Counts All PV
	varId = 4;
	sendUpdateUint32(ts, varId, m_monitor_counts_all);
}

void QuickCounter::sendUpdateUint32(struct timespec *ts,
		uint32_t varId, uint32_t val)
{
	uint32_t pkt[4 + (sizeof(ADARA::Header) / sizeof(uint32_t))];

	pkt[0] = 4 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_U32_TYPE,
		ADARA::PacketType::VAR_VALUE_U32_VERSION );

	pkt[2] = ts->tv_sec;   // EPICS Time...
	pkt[3] = ts->tv_nsec;

	pkt[4] = m_stat_devId;
	pkt[5] = varId;
	pkt[6] = ADARA::VariableStatus::OK << 16;
	pkt[6] |= ADARA::VariableSeverity::OK;
	pkt[7] = val;

	/* For now, assume QuickCounter Statistics PVs are _Not_ Persistent;
	 * these stats will change frequently, and frankly should probably
	 * be reset on Run Start anyway, etc.
	 */
	StorageManager::addPacket(pkt, sizeof(pkt));
}

void QuickCounter::sendUpdateFloat64(struct timespec *ts,
		uint32_t varId, double dval)
{
	uint32_t pkt[3 + (sizeof(ADARA::Header) / sizeof(uint32_t))
			+ (sizeof(double) / sizeof(uint32_t))];

	pkt[0] = sizeof(double) + ( 3 * sizeof(uint32_t) );
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE,
		ADARA::PacketType::VAR_VALUE_DOUBLE_VERSION );

	pkt[2] = ts->tv_sec;   // EPICS Time...
	pkt[3] = ts->tv_nsec;

	pkt[4] = m_stat_devId;
	pkt[5] = varId;
	pkt[6] = ADARA::VariableStatus::OK << 16;
	pkt[6] |= ADARA::VariableSeverity::OK;

	double *ptr = (double *) &(pkt[7]);
	*ptr = dval;

	/* For now, assume QuickCounter Statistics PVs are _Not_ Persistent;
	 * these stats will change frequently, and frankly should probably
	 * be reset on Run Start anyway, etc.
	 */
	StorageManager::addPacket(pkt, sizeof(pkt));
}

void QuickCounter::startCounting(uint64_t pulse_id, uint32_t tof)
{
	// UPDATE THE Internal IsCounting State from the PV NOW...!!
	// (Since we expect the external QuickScan Control to do a
	// Reset of the IsCounting to 0 before each new COunt Period... ;-D)
	// TODO This should probably be wired in _Directly_ maybe,
	// from the PV changed() method...? Unless that allows
	// undue havoc to the Internal State Machine...? ;-)
	uint32_t new_counting = m_pvCounting->value();
	if ( m_counting != new_counting ) {
		ERROR("startCounting(): Updating IsCounting State from PV"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " from " << m_counting
			<< " to " << new_counting);
		m_counting = new_counting;
	}

	/* Create a timestamp for each Counting Marker Trigger by
	 * adding the TOF value to the pulse ID, handling overflow of the
	 * nanoseconds field. TOF is originally in units of 100ns.
	 *
	 * Note that we strip any cycle field from the TOF.
	 */
	struct timespec ts;
	tof &= ((1U << 21) - 1);
	tof *= 100;
	ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
	ts.tv_nsec = tof + (pulse_id & 0xffffffff);

	// If Redundant Start When Already Counting,
	// Just Reset Statistics and Proceed...
	if ( m_counting == QC_COUNTING )
	{
		ERROR("startCounting(): Redundant Counter Start!"
			<< " Resetting Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);
	}

	// Got New Start Trigger While Still Waiting...!
	// Ignore New Start and Finish Previous Count...?! ;-Q
	else if ( m_counting == QC_WAITING )
	{
		ERROR("startCounting():"
			<< " *** START BEFORE PREVIOUS COUNT COMPLETION!!"
			<< " IGNORING New Start Trigger"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);

		return;
	}

	// Got New Start Trigger Before Done Counting Reset...!!
	// This is Bad, But Ignore New Start and Just Keep Waiting...?! ;-Q
	else if ( m_counting == QC_DONE_COUNTING )
	{
		// If "Auto Reset" is Set, Then Go Ahead and Start New Count... ;-D
		if ( m_auto_reset )
		{
			ERROR("startCounting():"
				<< " Start Without Clearing Done Counting State"
				<< " - But Auto Reset is True, So START NEW COUNTS Anyway"
				<< " for Fast Meta-Data Counter Device"
				<< " [" << m_var->m_name << "]"
				<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
				<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
				<< " TOF " << tof
				<< " (" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0) << ")"
				<< " m_counting=" << m_counting);

			m_counting = QC_COUNTING;

			DEBUG("startCounting(): Setting m_counting to " << m_counting);
		}

		else
		{
			ERROR("startCounting():"
				<< " *** START WITHOUT CLEARING DONE COUNTING STATE!!"
				<< " IGNORING New Start Trigger"
				<< " for Fast Meta-Data Counter Device"
				<< " [" << m_var->m_name << "]"
				<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
				<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
				<< " TOF " << tof
				<< " (" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0) << ")"
				<< " m_counting=" << m_counting);

			return;
		}
	}

	// Start Counting Normally...
	else
	{
		DEBUG("startCounting(): Start Accumulating Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);

		m_counting = QC_COUNTING;

		DEBUG("startCounting(): Setting m_counting to " << m_counting);
	}

	reset_stats();

	m_start_time.tv_sec = ts.tv_sec;
	m_start_time.tv_nsec = ts.tv_nsec;

	DEBUG("startCounting(): EPICS Time"
		<< " sec=" << m_start_time.tv_sec
		<< " ns=" << m_start_time.tv_nsec);

	update_pvs( &ts );

	// Register Detector All Counter with SMSControl...
	m_detector_counts_all_id = m_ctrl->registerDetectorAllCounter(
		this, m_var->m_name, &m_start_time);

	// Register Monitor All Counter with SMSControl...
	m_monitor_counts_all_id = m_ctrl->registerMonitorAllCounter(
		this, m_var->m_name, &m_start_time);
}

void QuickCounter::stopCounting(uint64_t pulse_id, uint32_t tof)
{
	/* Create a timestamp for each Counting Marker Trigger by
	 * adding the TOF value to the pulse ID, handling overflow of the
	 * nanoseconds field. TOF is originally in units of 100ns.
	 *
	 * Note that we strip any cycle field from the TOF.
	 */
	struct timespec ts;
	tof &= ((1U << 21) - 1);
	tof *= 100;
	ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
	ts.tv_nsec = tof + (pulse_id & 0xffffffff);

	// If Erroneous Stop When Not Counting,
	// Log Error and Ignore Statistics...
	if ( m_counting != QC_COUNTING )
	{
		DEBUG("stopCounting(): Counter Stop When Not Counting!"
			<< " Ignoring Any Accumulated Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);

		reset_stats();

		update_pvs( &ts );

		return;
	}
	else
	{
		DEBUG("stopCounting(): Stop Accumulating Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);
	}

	m_stop_time.tv_sec = ts.tv_sec;
	m_stop_time.tv_nsec = ts.tv_nsec;

	DEBUG("stopCounting(): EPICS Time"
		<< " sec=" << m_stop_time.tv_sec
		<< " ns=" << m_stop_time.tv_nsec);
	
	// Compute Elapsed Time in Seconds (Double)
	m_elapsed_time = calcDiffSeconds( m_stop_time, m_start_time );

	DEBUG("stopCounting():"
		<< " Elapsed=" << m_elapsed_time);

	// Compute Done Timeout Time...

	struct timespec delay;
	delay.tv_sec = (uint32_t) m_done_timeout;
	delay.tv_nsec = (uint32_t) ( ( m_done_timeout
			- ((double) delay.tv_sec) )
		* NANO_PER_SECOND_D );

	m_done_time.tv_sec = m_stop_time.tv_sec + delay.tv_sec;
	m_done_time.tv_nsec = m_stop_time.tv_nsec + delay.tv_nsec;

	while ( m_done_time.tv_nsec >= NANO_PER_SECOND_LL ) {
		m_done_time.tv_nsec -= NANO_PER_SECOND_LL;
		m_done_time.tv_sec++;
	}

	DEBUG("stopCounting(): EPICS Done Timeout Time"
		<< " sec=" << m_done_time.tv_sec
		<< " ns=" << m_done_time.tv_nsec);

	// Stop Counting...
	// (I.e. Wait for Counting Completion for Detector/Monitor Events...)

	m_counting = QC_WAITING;

	DEBUG("stopCounting(): Setting m_counting to " << m_counting);

	// Update PVs...

	update_pvs( &ts );
}

void QuickCounter::doneCounting(uint64_t pulse_id)
{
	// Extract Timestamp from Pulse ID...
	struct timespec ts;
	ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
	ts.tv_nsec = pulse_id & 0xffffffff;

	// If Erroneous Done When Not Waiting,
	// Log Error and Ignore...
	if ( m_counting != QC_WAITING )
	{
		DEBUG("doneCounting(): Counter Done When Not Waiting!"
			<< " Ignoring Done Timeout"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);

		return;
	}

	ERROR("doneCounting():"
		<< " Done Waiting for Counts"
		<< " for Fast Meta-Data Counter Device"
		<< " [" << m_var->m_name << "]"
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
		<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
		<< " (" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< ts.tv_nsec << std::setw(0) << ")"
		<< " m_counting=" << m_counting);

	// Done Counting...

	m_counting = QC_DONE_COUNTING;

	DEBUG("doneCounting(): Setting m_counting to " << m_counting);

	// Update PVs...

	update_pvs( &ts );

	// Un-Register Detector All Counter with SMSControl...
	m_ctrl->unregisterDetectorAllCounter(m_var->m_name,
		m_detector_counts_all_id);
	m_detector_counts_all_id = -1;

	// Un-Register Monitor All Counter with SMSControl...
	m_ctrl->unregisterMonitorAllCounter(m_var->m_name,
		m_monitor_counts_all_id);
	m_monitor_counts_all_id = -1;
}

void QuickCounter::addDetectorAllCounts(uint64_t pulse_id, uint32_t counts)
{
	// If Erroneous Counts When Not Counting,
	// Log Error and Ignore Counts...
	if ( m_counting != QC_COUNTING && m_counting != QC_WAITING )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		ERROR("addDetectorAllCounts():"
			<< " Ignoring Detector Counts When Not Counting or Waiting"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " Counts " << counts
			<< " Detector Counts All " << m_detector_counts_all
			<< " m_counting=" << m_counting);

		return;
	}

	// Add Latest Detector Counts...
	m_detector_counts_all += counts;

	// TODO REMOVEME - Pull Out Timestamp Extract to Uncomment... ;-D
	// DEBUG("addDetectorAllCounts():"
	//	<< " Got Detector Counts"
	//	<< " for Fast Meta-Data Counter Device"
	//	<< " [" << m_var->m_name << "]"
	//	<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
	//	<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
	//	<< " (" << ts.tv_sec << "."
	//		<< std::setfill('0') << std::setw(9)
	//		<< ts.tv_nsec << std::setw(0) << ")"
	//	<< " Counts " << counts
	//	<< " Detector Counts All -> " << m_detector_counts_all
	//	<< " m_counting=" << m_counting);

	// Periodically Update Count PVs...
	// (Post-Increment, So We Always Get "1st Counts"... ;-D)
	if ( !(m_update_det_count_pvs_cnt++ % 100) )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		DEBUG("addDetectorAllCounts():"
			<< " Updating Detector Counts"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " Counts " << counts
			<< " Detector Counts All -> " << m_detector_counts_all
			<< " m_counting=" << m_counting);
	
		update_pvs( &ts );
	}
}

void QuickCounter::addMonitorAllCounts(uint64_t pulse_id, uint32_t counts)
{
	// If Erroneous Counts When Not Counting,
	// Log Error and Ignore Counts...
	if ( m_counting != QC_COUNTING && m_counting != QC_WAITING )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		ERROR("addMonitorAllCounts():"
			<< " Ignoring Monitor Counts When Not Counting or Waiting"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " Counts " << counts
			<< " Monitor Counts All " << m_monitor_counts_all
			<< " m_counting=" << m_counting);

		return;
	}

	// Add Latest Monitor Counts...
	m_monitor_counts_all += counts;

	// TODO REMOVEME - Pull Out Timestamp Extract to Uncomment... ;-D
	// DEBUG("addMonitorAllCounts():"
	// 	<< " Got Monitor Counts"
	// 	<< " for Fast Meta-Data Counter Device"
	// 	<< " [" << m_var->m_name << "]"
	// 	<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
	//	<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
	//	<< " (" << ts.tv_sec << "."
	//		<< std::setfill('0') << std::setw(9)
	//		<< ts.tv_nsec << std::setw(0) << ")"
	// 	<< " Counts " << counts
	// 	<< " Monitor Counts All -> " << m_monitor_counts_all
	// 	<< " m_counting=" << m_counting);

	// Periodically Update Count PVs...
	// (Post-Increment, So We Always Get "1st Counts"... ;-D)
	if ( !(m_update_mon_count_pvs_cnt++ % 100) )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		DEBUG("addMonitorAllCounts():"
			<< " Updating Monitor Counts"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " at Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " Counts " << counts
			<< " Monitor Counts All -> " << m_monitor_counts_all
			<< " m_counting=" << m_counting);
	
		update_pvs( &ts );
	}
}

