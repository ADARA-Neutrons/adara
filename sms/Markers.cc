
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <stdint.h>
#include <string>
#include <sstream>

#include "Markers.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.Markers"));

class MarkerPausedPV : public smsBooleanPV {
public:
	MarkerPausedPV( const std::string &name, Markers *m ) :
		smsBooleanPV(name, /* auto_save */ false,
			/* no_changed_on_update */ true),
		m_markers(m)
	{}

	// Note: We *Don't* Want to Call "changed()" at the end of "update()"
	// for this smsBooleanPV derivation...
	//    -> we need to change ("update()") the value of the Paused PV
	//    _Without_ triggering all the tricking-down consequences! ;-D
	// This "changed()" method only gets called for External "write()"...
	void changed(void)
	{
		if ( value() )
			m_markers->pause();
		else
			m_markers->resume();
	}

private:
	Markers *m_markers;
};

class MarkerTriggerPV : public smsTriggerPV {
public:
	typedef boost::function<void (void)> callback;

	MarkerTriggerPV( const std::string &name, callback cb ) :
		smsTriggerPV(name), m_cb(cb) {}

	void triggered(void) { m_cb(); }

private:
	callback m_cb;
};

class MarkerCommentPV : public smsStringPV {
public:
	typedef boost::function<void (void)> callback;

	MarkerCommentPV( const std::string &name, callback cb ) :
		smsStringPV(name), m_cb(cb) {}

	void changed(void)
	{
		INFO("MarkerCommentPV::changed()");

		// Only Call Callback if String PV Set to Non-Empty Value...
		// (Otherwise, "unset()" triggers a callback... ;-b)
		std::string comment = value();
		if ( !comment.empty() && comment.compare( "(unset)" ) )
			m_cb();

		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts);
		StorageManager::autoSavePV( m_pv_name, comment, &ts );
	}

private:
	callback m_cb;
};

Markers::Markers( SMSControl *ctrl, bool notesCommentAutoReset ) :
	m_ctrl(ctrl), m_inRun(false), m_isPaused(false),
	m_notesCommentSet(false),
	m_notesCommentAutoReset(notesCommentAutoReset),
	m_useFirstNotesComment(false),
	m_runNumber(0), m_scanIndex(0)
{
	std::string prefix(ctrl->getBeamlineId());
	prefix += ":SMS:";

	m_pausedPV.reset( new MarkerPausedPV( prefix + "Paused", this ) );
	ctrl->addPV(m_pausedPV);

	prefix += "Marker:";

	m_commentPV.reset( new smsStringPV( prefix + "Comment" ) );
	ctrl->addPV(m_commentPV);

	m_indexPV.reset( new smsUint32PV( prefix + "ScanIndex" ) );
	ctrl->addPV(m_indexPV);

	m_scanStartPV.reset( new MarkerTriggerPV( prefix + "StartScan",
			boost::bind( &Markers::startScan, this ) ) );
	ctrl->addPV(m_scanStartPV);

	m_scanStopPV.reset( new MarkerTriggerPV( prefix + "StopScan",
			boost::bind( &Markers::stopScan, this ) ) );
	ctrl->addPV(m_scanStopPV);

	m_annotatePV.reset( new MarkerTriggerPV( prefix + "Annotate",
			boost::bind( &Markers::annotate, this ) ) );
	ctrl->addPV(m_annotatePV);

	m_runCommentPV.reset( new MarkerTriggerPV( prefix + "RunComment",
			boost::bind( &Markers::addRunComment, this ) ) );
	ctrl->addPV(m_runCommentPV);

	m_scanCommentPV.reset( new MarkerCommentPV( prefix + "ScanComment",
			boost::bind( &Markers::addScanComment, this ) ) );
	ctrl->addPV(m_scanCommentPV);

	m_notesCommentPV.reset( new MarkerCommentPV( prefix + "NotesComment",
			boost::bind( &Markers::addNotesComment, this ) ) );
	ctrl->addPV(m_notesCommentPV);

	m_notesCommentAutoResetPV.reset( new smsBooleanPV(
			prefix + "NotesCommentAutoReset" ) );
	ctrl->addPV(m_notesCommentAutoResetPV);

	m_annotationCommentPV.reset(
		new MarkerCommentPV( prefix + "AnnotationComment",
			boost::bind( &Markers::addAnnotationComment, this ) ) );
	ctrl->addPV(m_annotationCommentPV);

	m_connection = StorageManager::onPrologue(
				boost::bind( &Markers::onPrologue, this ) );

	// Initialize Notes Comment Auto Reset PV...
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	m_notesCommentAutoResetPV->update(m_notesCommentAutoReset, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;

	if ( StorageManager::getAutoSavePV( m_scanCommentPV->getName(),
			value, ts ) ) {
		m_scanCommentPV->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_notesCommentPV->getName(),
			value, ts ) ) {
		m_notesCommentPV->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_annotationCommentPV->getName(),
			value, ts ) ) {
		m_annotationCommentPV->update(value, &ts);
	}
}

Markers::~Markers()
{
	m_connection.disconnect();
}

// NOTE: This method gets called to "Clean Up" _Before_ a New Run Starts!
// (*None* of these packets here will go into the New Run...! ;-)
void Markers::beforeNewRun( uint32_t runNumber )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );

	// Save Run Number for Run Stop Comment...
	m_runNumber = runNumber;

	/* Reset the scan index, comment, and paused flag at the start of
	 * each new run. We'll emit markers if we are paused or in a scan so
	 * that any live clients can follow our state.
	 * -> Do the Un-Pause/Resume _First_ so any Scan Stop Marker will be
	 * seen for sure in a regular Un-Paused stream data file...
	 */
	if ( m_pausedPV->value() ) {
		// Set Paused State _Before_ Resume,
		// for Next Prologue to Use Queued...
		m_isPaused = false;
		// Clean Up Container before Actual Start!
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->resumeRecording();
		// Spew "We've Resumed" Packet
		std::string comment = "Warning: Run Resumed at New Run Start!";
		emitPacket( now, ADARA::MarkerType::RESUME, comment );
		// _This_ update() _Doesn't_ trigger changed()...!
		m_pausedPV->update(false, &now);
		// _Also_ Queue This Resume Comment for _After_ Run Start...
		// (just use generic Annotation Comment... ;-)
		resumeQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + comment ) );
	}

	if ( m_scanIndex ) {
		std::string comment = "Warning: Scan Stopped at New Run Start!";
		emitPacket( now, ADARA::MarkerType::SCAN_STOP, comment );
		// _Also_ Queue This Scan Stop/Comment for _After_ Run Start...
		std::stringstream ss_scan;
		ss_scan << m_scanIndex << "|";
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				ss_scan.str() + "[PRE-RUN] " + comment ) );
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
	}

	// Don't Unset Comment PV(s) on Run Start, _Only_ on Run Stop...
	// Comments can be entered in the "Before Run" interim...! ;-D

	// Queue Run Start Comment... (Get New Timestamp, for Proper Ordering!)
	clock_gettime( CLOCK_REALTIME, &now );
	std::stringstream ss;
	ss << "Run " << m_runNumber << " Started.";
	annotationCommentQueue.push_back(
		std::pair<struct timespec, std::string>( now, ss.str() ) );

	// If Run Notes Comment Persists Since the Last Run Ended,
	// And No Other Run Notes have been Queued as Yet, Then Re-Use It Now!
	// (Will Get Run Notes Queued, Set/Dumped on First Run Prologue...)
	if ( m_notesCommentPV->valid() && !notesCommentQueue.size() ) {
		addNotesComment();
	}

	// Run is About to Start Now...
	m_inRun = true;
}

void Markers::runStop(void)
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );

	/* Reset the scan index, comment, and paused flag at the end of
	 * each run, if it was stopped _Without_ first unpausing/stopping
	 * the scan... We'll emit markers if we were paused or in a scan so
	 * that any live clients can follow our state.
	 * Resume the pause _before_ stopping the scan, if that makes sense. :)
	 * -> so any Scan Stop Marker will be seen for sure in a regular
	 * Un-Paused stream data file...
	 */
	if ( m_pausedPV->value() ) {
		// Set Paused State _Before_ Resume,
		// for Next Prologue to Use Queued...
		m_isPaused = false;
		// Clean Up Container before Full Stop!
		// Thwart Impending "Scan Start Continuation" Packet
		// as we Stop Run!
		uint32_t save_scanIndex = m_scanIndex;
		m_scanIndex = 0;
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->resumeRecording();
		// Restore Any Hanging Scan Index for Completion of Stop...
		m_scanIndex = save_scanIndex;
		// DO DUMP of Queued Markers/Comments *First* Here, Before Warning!
		// Dump Latest of Any Interim Run Notes Comment (Log Intervening)
		dumpRunNotesComment();
		// Dump Any Pre-Run Scan Comments Now...
		dumpQueuedComments();
		// Spew "We've Resumed" Packet
		emitPacket( now, ADARA::MarkerType::RESUME,
			"Warning: Run Resumed at Run Stop!" );
		// _This_ update() _Doesn't_ trigger changed()...!
		m_pausedPV->update(false, &now);
	}

	// (Possibly Redundant _Second_ Queued Dump Attempt, Ok if Empty Now.)
	// Dump Latest of Any Interim Run Notes Comment (Log Any Intervening)
	dumpRunNotesComment();
	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments();

	if ( m_scanIndex ) {
		emitPacket( now, ADARA::MarkerType::SCAN_STOP,
			"Warning: Scan Stopped at Run Stop!" );
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
	}

	// Add Run Stop Comment...
	std::stringstream ss;
	ss << "Run " << m_runNumber << " Stopped.";
	emitPacket( now, ADARA::MarkerType::GENERIC, ss.str() );

	// Clear All Comment PVs on Run Stop...
	m_commentPV->unset(); // DEPRECATED
	m_scanCommentPV->unset();
	m_annotationCommentPV->unset();

	// Optionally Auto-Reset the Run Notes Between Runs
	// (Don't Always Reset, Often Convenient to Re-Use... ;-)
	m_notesCommentAutoReset = m_notesCommentAutoResetPV->value();
	if ( m_notesCommentAutoReset ) {
		m_notesCommentPV->unset();
	}

	// No Longer in a Run Now...
	m_inRun = false;

	// Reset Run Notes Set Flag, Allow Set Again for Next Run...
	m_notesCommentSet = false;
}

void Markers::pause(void)
{
	std::stringstream ss;
	ss << "Run ";
	if ( m_inRun ) ss << m_runNumber << " ";
	ss << "Paused.";
	DEBUG(ss.str());
	emitPacket( ADARA::MarkerType::PAUSE, ss.str() );
	// Set Paused State _Before_ Pause, for Next Prologue to Skip Queued
	m_isPaused = true;
	// NOTE: Triggers New File/Prologue with _Current_ State...!
	m_ctrl->pauseRecording();

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		pauseQueue.push_back( std::pair<struct timespec, std::string>( now,
			"[PRE-RUN] " + ss.str() ) );
	}
}

void Markers::resume(void)
{
	std::stringstream ss;
	ss << "Run ";
	if ( m_inRun ) ss << m_runNumber << " ";
	ss << "Resumed.";
	DEBUG(ss.str());
	// Set Paused State _Before_ Resume, for Next Prologue to Use Queued
	m_isPaused = false;
	// NOTE: Triggers New File/Prologue with _Current_ State...!
	m_ctrl->resumeRecording();
	emitPacket( ADARA::MarkerType::RESUME, ss.str() );

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		resumeQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + ss.str() ) );
	}
}

void Markers::startScan(void)
{
	m_scanIndex = m_indexPV->value();
	std::stringstream ss;
	if ( !m_inRun )
		ss << "[PRE-RUN] ";
	else if ( m_isPaused )
		ss << "[PAUSED Run " << m_runNumber << "] ";
	else
		ss << "[Run " << m_runNumber << "] ";
	ss << "Scan #" << m_scanIndex << " Started.";
	DEBUG( ss.str() );
	// NOTE: *Don't* Include "Scan Comment" Here,
	// Already Added on Scan Comment PV Set...
	emitPacket( ADARA::MarkerType::SCAN_START, ss.str() );

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		std::stringstream ss_scan;
		ss_scan << m_scanIndex << "|";
		scanStartQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				ss_scan.str() + ss.str() ) );
	}
}

void Markers::stopScan(void)
{
	std::stringstream ss;
	if ( !m_inRun )
		ss << "[PRE-RUN] ";
	else if ( m_isPaused )
		ss << "[PAUSED Run " << m_runNumber << "] ";
	else
		ss << "[Run " << m_runNumber << "] ";
	ss << "Scan #" << m_scanIndex << " Stopped.";
	DEBUG( ss.str() );
	emitPacket( ADARA::MarkerType::SCAN_STOP, ss.str() );

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		std::stringstream ss_scan;
		ss_scan << m_scanIndex << "|";
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				ss_scan.str() + ss.str() ) );
	}

	m_scanIndex = 0;
}

// DEPRECATED
void Markers::annotate(void)
{
	std::stringstream ss;

	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";
		ss << " General Comment - " << m_commentPV->value();
		DEBUG("[DEPRECATED] annotate() " << ss.str());

		emitPacket( ADARA::MarkerType::GENERIC, "", m_commentPV );

		// Annotation Comments are one-shot,
		// Reset once the packet is inserted.
		m_commentPV->unset();
	}

	// Otherwise, Queue Up Annotation Comments Strings for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused )
			ss << "[PAUSED Run " << m_runNumber << "] ";

		if ( m_commentPV->valid() ) {
			ss << m_commentPV->value();
		}
		else {
			ss << "(No Comment)";
		}

		DEBUG("[DEPRECATED] annotate() General Comment - " << ss.str());

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now, ss.str() ) );
	}
}

// DEPRECATED
void Markers::addRunComment(void)
{
	std::stringstream ss;

	// In a Run, Add Run Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";

		// This Is It! :-D
		if ( !m_notesCommentSet )
		{
			ss << " Overall Run Notes - " << m_commentPV->value();
			DEBUG("[DEPRECATED] addRunComment() " << ss.str());

			emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT,
				"", m_commentPV );

			m_notesCommentSet = true;
		}

		// Extraneous Extra Run Notes, Emit as Generic Comment...
		else
		{
			ss << " [DISCARDED RUN NOTES] - " << m_commentPV->value();
			ERROR("[DEPRECATED] addRunComment() " << ss.str());

			emitPacket( ADARA::MarkerType::GENERIC,
				"[DISCARDED RUN NOTES] ", m_commentPV );
		}

		// Run Notes Comments are One-Shot,
		// Reset once the packet is inserted.
		m_commentPV->unset();
	}

	// Otherwise, Save Run Notes Comment String for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D
		// -> insert for Logging Only...! ;-D
		// -> Set "Use First Run Notes" Flag Tho...! :-D
		if ( !m_inRun ) {
			m_useFirstNotesComment = false;
			ss << "[PRE-RUN] ";
		}
		else if ( m_isPaused ) {
			m_useFirstNotesComment = true;
			ss << "[PAUSED Run " << m_runNumber << "] ";
		}

		std::string comment;
		if ( m_commentPV->valid() ) {
			comment = m_commentPV->value();
		}
		else {
			comment = "(No Comment)";
		}
		ss << comment;

		DEBUG("[DEPRECATED] addRunComment() Save Run Notes for Next Run - "
			<< ss.str());

		notesCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now, comment ) );
	}
}

void Markers::addScanComment(void)
{
	// Already in the Middle of a Scan...
	// - Prepend "Scan:" Prefix to Comment...
	std::stringstream ss;
	if ( m_scanIndex ) {
		ss << "Scan " << m_scanIndex << ": ";
	}

	// Before the Scan...
	// - Prepend "Pre-Scan:" Prefix to Comment...
	else {
		ss << "Pre-Scan: ";
	}

	// In a Run, Add Scan Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		std::stringstream ss2;
		ss2 << "Run " << m_runNumber << ":";
		ss2 << " Scan Comment - " << ss.str() << m_scanCommentPV->value();
		DEBUG("addScanComment() " << ss2.str());

		emitPacket( ADARA::MarkerType::GENERIC,
			ss.str(), m_scanCommentPV );
	}

	// Otherwise, Queue Up Scan Comments Strings for Next Scan...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		std::stringstream ss_scan;
		ss_scan << m_scanIndex << "|";

		if ( !m_inRun )
			ss_scan << "[PRE-RUN] ";
		else if ( m_isPaused )
			ss_scan << "[PAUSED Run " << m_runNumber << "] ";

		if ( m_scanCommentPV->valid() ) {
			ss << m_scanCommentPV->value();
		}
		else {
			ss << "(No Comment)";
		}

		DEBUG("addScanComment()"
			<< " Queue Scan Comment for Next Scan scanIndex="
			<< ss_scan.str() << ss.str());

		scanCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				ss_scan.str() + ss.str() ) );
	}
}

void Markers::addNotesComment(void)
{
	std::stringstream ss;

	// In a Run, Add Run Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";

		// This Is It! :-D
		if ( !m_notesCommentSet )
		{
			ss << " Overall Run Notes - " << m_notesCommentPV->value();
			DEBUG("addNotesComment() " << ss.str());

			emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT,
				"", m_notesCommentPV );

			m_notesCommentSet = true;
		}

		// Extraneous Extra Run Notes, Emit as Generic Comment...
		else
		{
			ss << " [DISCARDED RUN NOTES] - " << m_notesCommentPV->value();
			ERROR("addNotesComment() " << ss.str());

			emitPacket( ADARA::MarkerType::GENERIC,
				"[DISCARDED RUN NOTES] ", m_notesCommentPV );
		}
	}

	// Otherwise, Save Run Notes Comment String for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D
		// -> insert for Logging Only...! ;-D
		// -> Set "Use First Run Notes" Flag Tho...! :-D
		if ( !m_inRun ) {
			m_useFirstNotesComment = false;
			ss << "[PRE-RUN] ";
		}
		else if ( m_isPaused ) {
			m_useFirstNotesComment = true;
			ss << "[PAUSED Run " << m_runNumber << "] ";
		}

		std::string comment;
		if ( m_notesCommentPV->valid() ) {
			comment = m_notesCommentPV->value();
		}
		else {
			comment = "(No Comment)";
		}
		ss << comment;

		DEBUG("addNotesComment() Save Run Notes for Next Run - "
			<< ss.str());

		notesCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now, comment ) );
	}
}

void Markers::addAnnotationComment(void)
{
	std::stringstream ss;

	// In a Run, Add Annotation Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";
		ss << " General Comment - " << m_annotationCommentPV->value();
		DEBUG("addAnnotationComment() " << ss.str());

		emitPacket( ADARA::MarkerType::GENERIC,
			"", m_annotationCommentPV );
	}

	// Otherwise, Queue Up Annotation Comments Strings for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused )
			ss << "[PAUSED Run " << m_runNumber << "] ";

		if ( m_annotationCommentPV->valid() ) {
			ss << m_annotationCommentPV->value();
		}
		else {
			ss << "(No Comment)";
		}

		DEBUG("addAnnotationComment() General Comment - " << ss.str());

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now, ss.str() ) );
	}
}

void Markers::dumpRunNotesComment(void)
{
	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !m_inRun || m_isPaused )
		return;

	// Only Dump *One* Set of Official Run Notes for Any Given Run...
	if ( !m_notesCommentSet )
	{
		// Dump First Run Notes Comment Entered, If Any...
		if ( m_useFirstNotesComment )
		{
			MarkerQueue::iterator first_notes_it =
				notesCommentQueue.begin();

			if ( first_notes_it != notesCommentQueue.end() )
			{
				DEBUG("dumpRunNotesComment()"
					<< " Run " << m_runNumber << ":"
					<< " Found First Run Notes Comment - "
					<< first_notes_it->first.tv_sec
					<< "." << first_notes_it->first.tv_nsec
					<< ": " << first_notes_it->second);

				emitPacket( first_notes_it->first,
					ADARA::MarkerType::OVERALL_RUN_COMMENT,
					first_notes_it->second );

				notesCommentQueue.erase( first_notes_it );

				m_notesCommentSet = true;
			}
		}

		// Dump Last Run Notes Comment Entered, If Any...
		else
		{
			MarkerQueue::reverse_iterator last_notes_it =
				notesCommentQueue.rbegin();

			if ( last_notes_it != notesCommentQueue.rend() )
			{
				DEBUG("dumpRunNotesComment()"
					<< " Run " << m_runNumber << ":"
					<< " Found Last Run Notes Comment - "
					<< last_notes_it->first.tv_sec
					<< "." << last_notes_it->first.tv_nsec
					<< ": " << last_notes_it->second);

				emitPacket( last_notes_it->first,
					ADARA::MarkerType::OVERALL_RUN_COMMENT,
					last_notes_it->second );

				notesCommentQueue.pop_back();

				m_notesCommentSet = true;
			}
		}
	}

	// Now Log Any Intervening Run Notes & Discard...
	// (...Done in dumpQueuedComments() to Interleave with Other Queues...)
}

void Markers::dumpQueuedComments(void)
{
	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !m_inRun || m_isPaused )
		return;

	MarkerQueue::iterator pause_it = pauseQueue.begin();
	MarkerQueue::iterator resume_it = resumeQueue.begin();
	MarkerQueue::iterator scan_start_it = scanStartQueue.begin();
	MarkerQueue::iterator scan_stop_it = scanStopQueue.begin();
	MarkerQueue::iterator scan_comment_it = scanCommentQueue.begin();
	MarkerQueue::iterator notes_it = notesCommentQueue.begin();
	MarkerQueue::iterator annotation_it = annotationCommentQueue.begin();

	// Dump Queued Markers in Proper Time Order
	while ( pause_it != pauseQueue.end()
			|| resume_it != resumeQueue.end()
			|| scan_start_it != scanStartQueue.end()
			|| scan_stop_it != scanStopQueue.end()
			|| scan_comment_it != scanCommentQueue.end()
			|| notes_it != notesCommentQueue.end()
			|| annotation_it != annotationCommentQueue.end() )
	{
		MarkerQueue::iterator next_it;
		ADARA::MarkerType::Enum markerType = ADARA::MarkerType::GENERIC;
		std::string prefix = "";
		std::string desc = "";
		uint64_t t, next = (uint64_t) -1;
		bool is_error = false;
		bool is_scan = false;

		if ( pause_it != pauseQueue.end()
				&& (t=timespec_to_nsec( pause_it->first )) < next ) {
			next_it = pause_it;
			markerType = ADARA::MarkerType::PAUSE;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = false;
			next = t;
		}

		if ( resume_it != resumeQueue.end()
				&& (t=timespec_to_nsec( resume_it->first )) < next ) {
			next_it = resume_it;
			markerType = ADARA::MarkerType::RESUME;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = false;
			next = t;
		}

		if ( scan_start_it != scanStartQueue.end()
				&& (t=timespec_to_nsec( scan_start_it->first )) < next ) {
			next_it = scan_start_it;
			markerType = ADARA::MarkerType::SCAN_START;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = true;
			next = t;
		}

		if ( scan_stop_it != scanStopQueue.end()
				&& (t=timespec_to_nsec( scan_stop_it->first )) < next ) {
			next_it = scan_stop_it;
			markerType = ADARA::MarkerType::SCAN_STOP;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = true;
			next = t;
		}

		if ( scan_comment_it != scanCommentQueue.end()
				&& (t=timespec_to_nsec( scan_comment_it->first ))
					< next ) {
			next_it = scan_comment_it;
			markerType = ADARA::MarkerType::GENERIC;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = true;
			next = t;
		}

		if ( notes_it != notesCommentQueue.end()
				&& (t=timespec_to_nsec( notes_it->first )) < next ) {
			next_it = notes_it;
			markerType = ADARA::MarkerType::GENERIC;
			prefix = "[DISCARDED RUN NOTES] ";
			desc = "Discarding Intervening Pre-Run Notes";
			is_error = true;
			is_scan = false;
			next = t;
		}

		if ( annotation_it != annotationCommentQueue.end()
				&& (t=timespec_to_nsec( annotation_it->first )) < next ) {
			next_it = annotation_it;
			markerType = ADARA::MarkerType::GENERIC;
			prefix = "";
			desc = "Dump Next Comment/Marker";
			is_error = false;
			is_scan = false;
			next = t;
		}

		// Handle Scan Index Decoding from Saved Comment String... ;-Q
		uint32_t save_scanIndex = (uint32_t) -1;
		std::string comment = next_it->second;
		std::stringstream ss_scan;
		if ( is_scan )
		{
			save_scanIndex = m_scanIndex;
			std::size_t found;
			if ( (found = next_it->second.find("|")) != std::string::npos )
			{
				m_scanIndex = boost::lexical_cast<uint32_t>(
					next_it->second.substr( 0, found ) );
				comment = next_it->second.substr( found + 1 );
			}
			ss_scan << "scanIndex=" << m_scanIndex << " ";
		}

		// Dump Next Comment/Marker...
		std::stringstream ss;
		ss << "dumpQueuedComments()"
			<< " Run " << m_runNumber << ": "
			<< desc << " MarkerType=" << markerType << " - "
			<< next_it->first.tv_sec << "." << next_it->first.tv_nsec
			<< ": " << ss_scan.str() << prefix << comment;

		if ( is_error )
			ERROR( ss.str() );
		else
			DEBUG( ss.str() );

		emitPacket( next_it->first, markerType, prefix + comment );

		// Handle Scan Index Restore... ;-b
		if ( is_scan )
			m_scanIndex = save_scanIndex;

		// Increment Given Iterator...
		if ( next_it == pause_it )
			pause_it++;
		else if ( next_it == resume_it )
			resume_it++;
		else if ( next_it == scan_start_it )
			scan_start_it++;
		else if ( next_it == scan_stop_it )
			scan_stop_it++;
		else if ( next_it == scan_comment_it )
			scan_comment_it++;
		else if ( next_it == notes_it )
			notes_it++;
		else if ( next_it == annotation_it )
			annotation_it++;
	}

	// Clear Out Queued Pauses
	pauseQueue.clear();

	// Clear Out Queued Resumes
	resumeQueue.clear();

	// Clear Out Queued Scan Starts
	scanStartQueue.clear();

	// Clear Out Queued Scan Stops
	scanStopQueue.clear();

	// Clear Out Queued Scan Comments
	scanCommentQueue.clear();

	// Clear Out Queued Run Notes Comments
	notesCommentQueue.clear();

	// Clear Out Queued Annotation Comments
	annotationCommentQueue.clear();
}

void Markers::onPrologue(void)
{
	// Dump Latest of Any Interim Run Notes Comment (Log Any Intervening)
	dumpRunNotesComment();

	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments();

	if ( m_scanIndex )
	{
		std::stringstream ss;
		ss << "[NEW RUN FILE CONTINUATION] ";
		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused )
			ss << "[PAUSED Run " << m_runNumber << "] ";
		else
			ss << "[Run " << m_runNumber << "] ";
		ss << "Scan #" << m_scanIndex << " Started.";
		DEBUG("onPrologue() " << ss.str());
		emitPrologue( ADARA::MarkerType::SCAN_START, ss.str() );
	}

	if ( m_isPaused )
	{
		std::stringstream ss;
		ss << "[NEW RUN FILE CONTINUATION] ";
		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		ss << "Run ";
		if ( m_inRun ) ss << m_runNumber << " ";
		ss << "Paused.";
		DEBUG("onPrologue() " << ss.str());
		emitPrologue( ADARA::MarkerType::PAUSE, ss.str() );
	}
}

void Markers::emitPrologue( ADARA::MarkerType::Enum markerType,
		std::string prefix )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, prefix, StringPVSharedPtr(), true );
}

void Markers::emitPacket( ADARA::MarkerType::Enum markerType,
		std::string prefix, StringPVSharedPtr commentPV )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, prefix, commentPV );
}

void Markers::emitPacket( const struct timespec &ts,
		ADARA::MarkerType::Enum markerType,
		std::string prefix, StringPVSharedPtr commentPV, bool prologue )
{
	uint32_t pkt[ 2 + sizeof(ADARA::Header) / sizeof(uint32_t) ];
	std::string comment = "";
	uint32_t pad = 0;
	struct iovec iov;
	IoVector iovec;

	pkt[0] = 2 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::STREAM_ANNOTATION_TYPE,
		ADARA::PacketType::STREAM_ANNOTATION_VERSION );
	pkt[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	pkt[3] = ts.tv_nsec;

	pkt[4] = (uint32_t) markerType << 16;
	if ( markerType == ADARA::MarkerType::OVERALL_RUN_COMMENT )
		pkt[5] = 0;
	else
		pkt[5] = m_scanIndex;

	/* Set the hint flag for clients, but only for scan start/stops */
	// XXX think this through, maybe get rid of hint
	switch ( markerType ) {
		case ADARA::MarkerType::SCAN_START:
		case ADARA::MarkerType::SCAN_STOP:
			pkt[4] |= 0x80000000;
			break;
		default:
			/* We default to no reset */
			break;
	}

	iov.iov_base = pkt;
	iov.iov_len = sizeof(pkt);
	iovec.push_back(iov);

	/* Append the optional comment to the packet */
	if ( commentPV ) {
		if ( commentPV->valid() ) {
			comment = prefix + commentPV->value();
		}
		else {
			comment = prefix + "(No Comment)";
		}
	}
	else if ( !prefix.empty() ) {
		comment = prefix;
	}

	/* Append the optional comment to the packet */
	if ( !comment.empty() ) {
	
		/* We only allow so much content... */
		if ( comment.size() > 65535 )
			comment.resize(65535);

		pkt[0] += ( comment.size() + 3 ) & ~3;
		pkt[4] |= comment.size();

		iov.iov_base = const_cast<char *>(comment.c_str());
		iov.iov_len = comment.size();
		iovec.push_back(iov);

		if ( comment.size() % 4 ) {
			iov.iov_base = &pad;
			iov.iov_len = 4 - ( comment.size() % 4 );
			iovec.push_back(iov);
		}
	}

	if ( prologue )
		StorageManager::addPrologue(iovec);
	else
		StorageManager::addPacket(iovec);
}

