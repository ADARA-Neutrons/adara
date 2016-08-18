#ifndef __MARKERS_H
#define __MARKERS_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>

#include <stdint.h>

#include "SMSControl.h"

class MarkerPausedPV;
class MarkerTriggerPV;
class MarkerCommentPV;
class smsStringPV;
class smsUint32PV;

class Markers : boost::noncopyable {
public:
	typedef boost::shared_ptr<smsStringPV> StringPVSharedPtr;

	typedef std::vector< std::pair<struct timespec, std::string> >
		MarkerQueue;

	Markers( SMSControl *ctrl );
	~Markers();

	void beforeNewRun( uint32_t runNumber );
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

	boost::shared_ptr<MarkerCommentPV> m_scanCommentPV;
	boost::shared_ptr<MarkerCommentPV> m_notesCommentPV;
	boost::shared_ptr<MarkerCommentPV> m_annotationCommentPV;

	boost::signals2::connection m_connection;

	SMSControl *m_ctrl;

	bool m_inRun;
	bool m_isPaused;

	uint32_t m_runNumber;

	uint32_t m_scanIndex;

	MarkerQueue scanStopQueue;
	MarkerQueue scanStartQueue;
	MarkerQueue scanCommentQueue;
	MarkerQueue notesCommentQueue;
	MarkerQueue annotationCommentQueue;

	void startScan(void);
	void stopScan(void);
	void annotate(void);
	void addRunComment(void);
	void addScanComment(void);
	void addNotesComment(void);
	void addAnnotationComment(void);

	void dumpLastRunNotes(void);
	void dumpQueuedComments(void);

	void onPrologue(void);

	void emitPrologue( ADARA::MarkerType::Enum );

	void emitPacket( ADARA::MarkerType::Enum,
		std::string prefix = "",
		StringPVSharedPtr commentPV = StringPVSharedPtr() );

	void emitPacket( const struct timespec &,
		ADARA::MarkerType::Enum,
		std::string prefix = "",
		StringPVSharedPtr commentPV = StringPVSharedPtr(),
		bool prologue = false );
};

#endif /* __MARKERS_H */
