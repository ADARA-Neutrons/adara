#ifndef __MARKERS_H
#define __MARKERS_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signal.hpp>

#include "SMSControl.h"
#include "SMSControlPV.h"

class MarkerPausedPV;
class MarkerTriggerPV;

class Markers : boost::noncopyable {
public:
	Markers(const std::string &beamline, SMSControl *sms);
	~Markers();

	void newRun(void);
	void pause(void);
	void resume(void);

private:
	boost::shared_ptr<MarkerPausedPV> m_pausedPV;
	boost::shared_ptr<smsStringPV> m_commentPV;
	boost::shared_ptr<smsUint32PV> m_indexPV;
	boost::shared_ptr<MarkerTriggerPV> m_scanStartPV;
	boost::shared_ptr<MarkerTriggerPV> m_scanStopPV;
	boost::shared_ptr<MarkerTriggerPV> m_annotatePV;
	boost::shared_ptr<MarkerTriggerPV> m_runCommentPV;

	boost::signals::connection m_connection;

	void startScan(void);
	void stopScan(void);
	void annotate(void);
	void addRunComment(void);
	void onPrologue(void);
	void emitPacket(ADARA::MarkerType::Enum);
	void emitPacket(const struct timespec &, ADARA::MarkerType::Enum);
};

#endif /* __MARKERS_H */
