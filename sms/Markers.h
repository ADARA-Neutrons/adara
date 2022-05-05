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
class smsBooleanPV;

class Markers : boost::noncopyable {
public:
	typedef boost::shared_ptr<smsStringPV> StringPVSharedPtr;

	typedef std::vector< std::pair<struct timespec, std::string> >
		MarkerQueue;

	enum PassThru { IGNORE, PASSTHRU, EXECUTE };

	Markers( SMSControl *ctrl, bool notesCommentAutoReset );
	~Markers();

	void beforeNewRun( uint32_t runNumber, struct timespec *ts );
	void runStop( struct timespec *ts );

	void addAnnotationComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void startScan( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void stopScan( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void addScanComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void pause( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void resume( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void addNotesComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
	void addSystemComment( struct timespec *ts, // Wallclock Time...!
		uint32_t scanIndex = -1, std::string comment = "" );

	void annotate( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
		// DEPRECATED
	void addRunComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru = IGNORE,
		uint32_t pt_scanIndex = -1, std::string pt_comment = "" );
		// DEPRECATED

	void updatePausedPV( bool paused, struct timespec *ts );

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
	boost::shared_ptr<smsBooleanPV> m_notesCommentAutoResetPV;
	boost::shared_ptr<MarkerCommentPV> m_annotationCommentPV;

	boost::signals2::connection m_connection;

	SMSControl *m_ctrl;

	bool m_inRun;
	bool m_isPaused;
	bool m_isPausedPT;

	bool m_lastInRun;
	bool m_lastIsPaused;

	bool m_notesCommentSet;
	bool m_notesCommentAutoReset;
	bool m_useFirstNotesComment;

	uint32_t m_runNumber;

	uint32_t m_lastRunNumber;

	uint32_t m_scanIndex;

	uint32_t m_lastScanIndex;

	MarkerQueue pauseQueue;
	MarkerQueue resumeQueue;
	MarkerQueue scanStopQueue;
	MarkerQueue scanStartQueue;
	MarkerQueue scanCommentQueue;
	MarkerQueue notesCommentQueue;
	MarkerQueue annotationCommentQueue;
	MarkerQueue systemCommentQueue;

	void dumpRunNotesComment( bool prologue = false );
	void dumpQueuedComments( bool prologue = false,
		bool capture_last = false );

	void onPrologue( bool capture_last );

	void emitPrologue( ADARA::MarkerType::Enum, std::string comment = "" );

	void emitPacket( const struct timespec &, // Wallclock Time...!
		ADARA::MarkerType::Enum,
		uint32_t scanIndex,
		std::string prefix = "",
		std::string comment = "",
		bool prologue = false );
};

#endif /* __MARKERS_H */
