#ifndef __QUICK_COUNTER_H
#define __QUICK_COUNTER_H

#include <boost/smart_ptr.hpp>

#include <string>

#include <stdint.h>
#include <time.h>

#include "SMSControl.h"
#include "SMSControlPV.h"
#include "FastMeta.h"

class smsBooleanPV;
class smsUint32PV;
class smsFloat64PV;

// QuickCounter "IsCounting" State Flags
#define QC_NOT_COUNTING		(0)
#define QC_COUNTING			(1)
#define QC_WAITING			(2)
#define QC_DONE_COUNTING	(3)

class QuickCounter {
public:

	QuickCounter(boost::shared_ptr<MetaDataMgr> mgr,
			struct FastMeta::Variable *var, uint32_t key,
			uint32_t stat_devId, double done_timeout, bool auto_reset);

	void reset_stats(void);

	void update_pvs(struct timespec *ts);

	void sendUpdates(struct timespec *now);

	void sendUpdateUint32(struct timespec *now,
			uint32_t varId, uint32_t val);
	void sendUpdateFloat64(struct timespec *now,
			uint32_t varId, double dval);

	void startCounting(uint64_t pulse_id, uint32_t tof);
	void stopCounting(uint64_t pulse_id, uint32_t tof);

	void doneCounting(uint64_t pulse_id);

	void addDetectorAllCounts(uint64_t pulse_id, uint32_t counts);
	void addMonitorAllCounts(uint64_t pulse_id, uint32_t counts);

	uint32_t m_counting;

	double m_done_timeout;

	uint64_t m_start_pulse_id;
	uint32_t m_start_tof;

	struct timespec m_start_time;

	uint64_t m_stop_pulse_id;
	uint32_t m_stop_tof;

	struct timespec m_stop_time;

	struct timespec m_done_time;

private:
	SMSControl *m_ctrl;

	boost::shared_ptr<MetaDataMgr> m_meta;

	int32_t m_counter_id;

	uint32_t m_stat_devId;

	struct FastMeta::Variable *m_var;
	uint32_t m_key;

	boost::shared_ptr<smsUint32PV> m_pvCounting;

	boost::shared_ptr<smsFloat64PV> m_pvDoneTimeout;

	boost::shared_ptr<smsBooleanPV> m_pvAutoReset;
	bool m_auto_reset;

	boost::shared_ptr<smsFloat64PV> m_pvElapsedTime;
	double m_elapsed_time;

	int32_t m_detector_counts_all_id;
	boost::shared_ptr<smsUint32PV> m_pvDetectorCountsAll;
	uint32_t m_detector_counts_all;

	int32_t m_monitor_counts_all_id;
	boost::shared_ptr<smsUint32PV> m_pvMonitorCountsAll;
	uint32_t m_monitor_counts_all;

	uint32_t m_update_det_count_pvs_cnt;
	uint32_t m_update_mon_count_pvs_cnt;
};

#endif /* __QUICK_COUNTER_H */
