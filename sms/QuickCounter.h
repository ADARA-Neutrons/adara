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

private:
	SMSControl *m_ctrl;

	struct FastMeta::Variable *m_var;
	uint32_t m_key;

	boost::shared_ptr<smsBooleanPV> m_pvCounting;
	bool m_counting;

	struct timespec m_start_time;
	struct timespec m_stop_time;

	boost::shared_ptr<smsFloat64PV> m_pvElapsedTime;
	double m_elapsed_time;
};

#endif /* __QUICK_COUNTER_H */
