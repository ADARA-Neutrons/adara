#ifndef __QUICK_COUNTER_H
#define __QUICK_COUNTER_H

#include <stdint.h>
#include <string>

#include "SMSControl.h"
#include "FastMeta.h"

class QuickCounter {
public:

	QuickCounter(struct FastMeta::Variable *var, uint32_t key);

	void startCounting(void);
	void stopCounting(void);

private:
	SMSControl *m_ctrl;

	struct FastMeta::Variable *m_var;
	uint32_t m_key;
};

#endif /* __QUICK_COUNTER_H */
