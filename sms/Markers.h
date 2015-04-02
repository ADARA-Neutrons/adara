#ifndef __MARKERS_H
#define __MARKERS_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>

#include <stdint.h>

#include "SMSControl.h"

class MarkerPausedPV;
class MarkerTriggerPV;
class smsStringPV;
class smsUint32PV;

class Markers : boost::noncopyable {
public:
	Markers(SMSControl *sms);
	~Markers();

	void newRun(void);
	void runStop(void);
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

	boost::signals2::connection m_connection;

	uint32_t m_scanIndex;

	void startScan(void);
	void stopScan(void);
	void annotate(void);
	void addRunComment(void);
	void onPrologue(void);
	void emitPrologue(ADARA::MarkerType::Enum);
	void emitPacket(ADARA::MarkerType::Enum, bool addComment = true);
	void emitPacket(const struct timespec &, ADARA::MarkerType::Enum,
			bool addComment, bool prologue = false);
};

#endif /* __MARKERS_H */
