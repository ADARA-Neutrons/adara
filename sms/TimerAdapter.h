#ifndef __TIMER_ADAPTER_H
#define __TIMER_ADAPTER_H

#include <fdManager.h>
#include <epicsTimer.h>

template<class T> class TimerAdapter : public epicsTimerNotify {
public:
        explicit TimerAdapter(T *obj, bool (T::*f)(void) = &T::timerExpired) :
			m_timer(fileDescriptorManager.createTimer()),
			m_obj(obj), m_f(f), m_delay(0.0) { }
	virtual ~TimerAdapter() { m_timer.destroy(); }

	void start(double delaySeconds) {
		m_delay = delaySeconds;
		m_timer.start(*this, delaySeconds);
	}
	void cancel(void) { m_timer.cancel(); m_delay = 0.0; }

private:
	epicsTimer &m_timer;
	T *m_obj;
	bool (T::*m_f)(void);
	double m_delay;

	expireStatus expire(const epicsTime &currentTime) {
		if ((m_obj->*m_f)())
			return expireStatus(restart, m_delay);
		else
			return expireStatus(noRestart);
	};
};

#endif /* __TIMER_ADAPTER_H */

