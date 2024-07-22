#ifndef __QUICK_COUNTER_H
#define __QUICK_COUNTER_H

#include <string>

#include <stdint.h>
#include <time.h>

#include "SMSControl.h"
#include "SMSControlPV.h"
#include "FastMeta.h"

class smsBooleanPV;
class smsUint32PV;
class smsFloat64PV;

class QuickCounter {
public:

	QuickCounter(struct FastMeta::Variable *var, uint32_t key);

	void reset_stats(void);

	void update_pvs(void);

	void startCounting(uint64_t pulse_id, uint32_t tof);
	void stopCounting(uint64_t pulse_id, uint32_t tof);

	void addDetectorAllCounts(uint32_t counts);
	void addMonitorAllCounts(uint32_t counts);

private:
	SMSControl *m_ctrl;

	int32_t m_counter_id;

	struct FastMeta::Variable *m_var;
	uint32_t m_key;

	boost::shared_ptr<smsBooleanPV> m_pvCounting;
	bool m_counting;

	struct timespec m_start_time;
	struct timespec m_stop_time;

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
