
#include "Logging.h"

LOGGER("QuickCounter");

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
			uint32_t key, uint32_t stat_devId):
		m_meta(mgr), m_stat_devId(stat_devId), m_var(var), m_key(key)
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
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec);

	m_counting = false;

	// Initialize Statistics...

	reset_stats();

	// Create Counter PVs...

	m_ctrl = SMSControl::getInstance();

	std::string prefix(m_ctrl->getPVPrefix());
	prefix += ":" + m_var->m_name;

	m_pvCounting = boost::shared_ptr<smsBooleanPV>(new
		smsBooleanPV(prefix + ":IsCounting",
			/* AutoSave */ false));

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
	m_ctrl->addPV(m_pvElapsedTime);
	m_ctrl->addPV(m_pvDetectorCountsAll);
	m_ctrl->addPV(m_pvMonitorCountsAll);

	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	m_pvCounting->update(m_counting, &now);
	m_pvElapsedTime->update(m_elapsed_time, &now);
	m_pvDetectorCountsAll->update(m_detector_counts_all, &now);
	m_pvMonitorCountsAll->update(m_monitor_counts_all, &now);

	// Register This Quick Counter with SMSControl...
	m_counter_id = m_ctrl->registerQuickCounter(m_var->m_name);
}

void QuickCounter::reset_stats(void)
{
	m_start_time.tv_sec = 0;
	m_start_time.tv_nsec = 0;

	m_stop_time.tv_sec = 0;
	m_stop_time.tv_nsec = 0;

	m_elapsed_time = 0.0;

	m_detector_counts_all = 0;
	m_monitor_counts_all = 0;

	m_update_det_count_pvs_cnt = 0;
	m_update_mon_count_pvs_cnt = 0;
}

void QuickCounter::update_pvs(struct timespec *ts)
{
	// Live PVs Always Get "Now" Update Times...
	struct timespec now;
	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	m_pvCounting->update(m_counting, &now);
	m_pvElapsedTime->update(m_elapsed_time, &now);
	m_pvDetectorCountsAll->update(m_detector_counts_all, &now);
	m_pvMonitorCountsAll->update(m_monitor_counts_all, &now);

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
	if ( m_counting )
	{
		ERROR("startCounting(): Redundant Counter Start!"
			<< " Resetting Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);
	}
	else
	{
		DEBUG("startCounting(): Start Accumulating Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " TOF " << tof
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " m_counting=" << m_counting);

		m_counting = true;

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
	if ( !m_counting )
	{
		DEBUG("stopCounting(): Counter Stop When Not Counting!"
			<< " Ignoring Any Accumulated Statistics"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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

	// Stop Counting...

	m_counting = false;

	DEBUG("stopCounting(): Setting m_counting to " << m_counting);

	// Update PVs...

	update_pvs( &ts );

	// Un-Register Detector All Counter with SMSControl...
	m_ctrl->unregisterDetectorAllCounter(m_var->m_name,
		m_detector_counts_all_id);

	// Un-Register Monitor All Counter with SMSControl...
	m_ctrl->unregisterMonitorAllCounter(m_var->m_name,
		m_monitor_counts_all_id);
}

void QuickCounter::addDetectorAllCounts(uint64_t pulse_id, uint32_t counts)
{
	// If Erroneous Stop When Not Counting,
	// Log Error and Ignore Statistics...
	if ( !m_counting )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		ERROR("addDetectorAllCounts():"
			<< " Ignoring Detector Counts When Not Counting"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
	//	<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
	// If Erroneous Stop When Not Counting,
	// Log Error and Ignore Statistics...
	if ( !m_counting )
	{
		// Extract Timestamp from Pulse ID...
		struct timespec ts;
		ts.tv_sec = pulse_id >> 32;  // EPICS Time...!
		ts.tv_nsec = pulse_id & 0xffffffff;

		ERROR("addMonitorAllCounts():"
			<< " Ignoring Monitor Counts When Not Counting"
			<< " for Fast Meta-Data Counter Device"
			<< " [" << m_var->m_name << "]"
			<< " PixelId Key " << std::hex << "0x" << m_key << std::dec
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
	//	<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
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
			<< " Pulse Time 0x" << std::hex << pulse_id << std::dec
			<< " (" << ts.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< ts.tv_nsec << std::setw(0) << ")"
			<< " Counts " << counts
			<< " Monitor Counts All -> " << m_monitor_counts_all
			<< " m_counting=" << m_counting);
	
		update_pvs( &ts );
	}
}

