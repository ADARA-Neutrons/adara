
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.Markers"));

#include <string>
#include <sstream>

#include <stdint.h>

#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>

#include "Markers.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

class MarkerPausedPV : public smsBooleanPV {
public:
	MarkerPausedPV( const std::string &name, Markers *m ) :
		smsBooleanPV(name, /* AutoSave */ false,
			/* No Changed on Update */ true),
		m_markers(m)
	{}

	// Note: We *Don't* Want to Call "changed()" at the end of "update()"
	// for this smsBooleanPV derivation...
	//    -> we need to change ("update()") the value of the Paused PV
	//    _Without_ triggering all the trickling-down consequences! ;-D
	// This "changed()" method only gets called for External "write()"...
	void changed(void)
	{
		struct timespec ts;
		m_value->getTimeStamp( &ts ); // Wallclock Time...!
		if ( value() )
			m_markers->pause( &ts );
		else
			m_markers->resume( &ts );
	}

private:
	Markers *m_markers;
};

class MarkerTriggerPV : public smsTriggerPV {
public:
	typedef boost::function<void ( struct timespec *ts,
			Markers::PassThru passthru,
			uint32_t pt_scanIndex, std::string pt_comment )>
		callback;

	MarkerTriggerPV( const std::string &name, callback cb ) :
		smsTriggerPV(name), m_cb(cb) {}

	void triggered( struct timespec *ts ) // Wallclock Time...!
	{
		m_cb( ts, Markers::IGNORE, -1, "" );
	}

private:
	callback m_cb;
};

class MarkerCommentPV : public smsStringPV {
public:
	typedef boost::function<void ( struct timespec *ts,
			Markers::PassThru passthru,
			uint32_t pt_scanIndex, std::string pt_comment )>
		callback;

	MarkerCommentPV( const std::string &name, callback cb ) :
		smsStringPV(name), m_cb(cb) {}

	void changed(void)
	{
		INFO("MarkerCommentPV::changed()");

		std::string comment = value();
		struct timespec ts;
		m_value->getTimeStamp( &ts ); // Wallclock Time...!

		// Only Call Callback if String PV Set to Non-Empty Value...
		// (Otherwise, "unset()" triggers a callback... ;-b)
		if ( !comment.empty() && comment.compare( "(unset)" ) )
			m_cb( &ts, Markers::IGNORE, -1, "" );

		// AutoSave PV Value Change...
		StorageManager::autoSavePV( m_pv_name, comment, &ts );
	}

private:
	callback m_cb;
};

Markers::Markers( SMSControl *ctrl, bool notesCommentAutoReset ) :
	m_ctrl(ctrl), m_inRun(false), m_isPaused(false), m_isPausedPT(false),
	m_lastInRun(false), m_lastIsPaused(false),
	m_notesCommentSet(false),
	m_notesCommentAutoReset(notesCommentAutoReset),
	m_useFirstNotesComment(false),
	m_runNumber(0), m_lastRunNumber(0), m_scanIndex(0), m_lastScanIndex(0)
{
	std::string prefix(ctrl->getPVPrefix());

	m_pausedPV.reset( new MarkerPausedPV( prefix + ":Paused", this ) );
	ctrl->addPV(m_pausedPV);

	prefix += ":Marker";

	m_commentPV.reset( new smsStringPV( prefix + ":Comment" ) );
	ctrl->addPV(m_commentPV);

	m_indexPV.reset( new smsUint32PV( prefix + ":ScanIndex" ) );
	ctrl->addPV(m_indexPV);

	m_scanStartPV.reset( new MarkerTriggerPV( prefix + ":StartScan",
			boost::bind( &Markers::startScan,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_scanStartPV);

	m_scanStopPV.reset( new MarkerTriggerPV( prefix + ":StopScan",
			boost::bind( &Markers::stopScan,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_scanStopPV);

	m_annotatePV.reset( new MarkerTriggerPV( prefix + ":Annotate",
			boost::bind( &Markers::annotate,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_annotatePV);

	m_runCommentPV.reset( new MarkerTriggerPV( prefix + ":RunComment",
			boost::bind( &Markers::addRunComment,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_runCommentPV);

	m_scanCommentPV.reset( new MarkerCommentPV( prefix + ":ScanComment",
			boost::bind( &Markers::addScanComment,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_scanCommentPV);

	m_notesCommentPV.reset( new MarkerCommentPV( prefix + ":NotesComment",
			boost::bind( &Markers::addNotesComment,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_notesCommentPV);

	m_notesCommentAutoResetPV.reset( new smsBooleanPV(
			prefix + ":NotesCommentAutoReset", /* AutoSave */ true ) );
	ctrl->addPV(m_notesCommentAutoResetPV);

	m_annotationCommentPV.reset(
		new MarkerCommentPV( prefix + ":AnnotationComment",
			boost::bind( &Markers::addAnnotationComment,
				this, _1, _2, _3, _4 ) ) );
	ctrl->addPV(m_annotationCommentPV);

	m_connection = StorageManager::onPrologue(
				boost::bind( &Markers::onPrologue, this, _1 ) );

	// Initialize Notes Comment Auto Reset PV...
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	m_notesCommentAutoResetPV->update(m_notesCommentAutoReset, &now);

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	bool bvalue;

	if ( StorageManager::getAutoSavePV( m_scanCommentPV->getName(),
			value, ts ) ) {
		m_scanCommentPV->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV( m_notesCommentPV->getName(),
			value, ts ) ) {
		m_notesCommentPV->update(value, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_notesCommentAutoResetPV->getName(), bvalue, ts ) ) {
		m_notesCommentAutoReset = bvalue;
		m_notesCommentAutoResetPV->update(bvalue, &ts);
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
void Markers::beforeNewRun( uint32_t runNumber,
		struct timespec *ts ) // Wallclock Time...!
{
	// Save Run Number for Run Stop Comment...
	m_lastRunNumber = m_runNumber;
	m_runNumber = runNumber;

	/* Reset the scan index, comment, and paused flag at the start of
	 * each new run. We'll emit markers if we are paused or in a scan so
	 * that any live clients can follow our state.
	 * -> Do the Un-Pause/Resume _First_ so any Scan Stop Marker will be
	 * seen for sure in a regular Un-Paused stream data file...
	 */

	if ( m_isPaused || m_pausedPV->value() ) {
		// Set Paused State _Before_ Resume,
		// for Next Prologue to Use Queued...
		m_lastIsPaused = true;
		m_isPaused = false;
		// Clean Up Container before Actual Start!
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->resumeRecording( ts ); // Wallclock Time...!
		// Spew "We've Resumed" Packet
		std::string comment = "Warning: Run Resumed at New Run Start!";
		emitPacket( *ts, ADARA::MarkerType::RESUME, m_scanIndex,
			"", comment );
		// _This_ update() _Doesn't_ trigger changed()...!
		if ( m_pausedPV->value() )
			m_pausedPV->update(false, ts); // Wallclock Time...!
		// _Also_ Queue This Resume Comment for _After_ Run Start...
		// (just use generic Annotation Comment... ;-)
		resumeQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				"[PRE-RUN] " + comment ) );
	}
	// Still Need to Reset "Last Paused State"...
	else {
		m_lastIsPaused = false;
	}

	if ( m_scanIndex ) {
		std::string comment = "Warning: Scan Stopped at New Run Start!";
		emitPacket( *ts, ADARA::MarkerType::SCAN_STOP, m_scanIndex,
			"", comment );
		// _Also_ Queue This Scan Stop/Comment for _After_ Run Start...
		std::stringstream ss_scan;
		ss_scan << m_scanIndex << "|";
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				ss_scan.str() + "[PRE-RUN] " + comment ) );
		m_indexPV->update(0, ts); // Wallclock Time...!
		m_lastScanIndex = m_scanIndex;
		m_scanIndex = 0;
	}
	// Still Need to Reset "Last Scan Index"...
	else {
		m_lastScanIndex = 0;
	}

	// Don't Unset Comment PV(s) on Run Start, _Only_ on Run Stop...
	// Comments can be entered in the "Before Run" interim...! ;-D

	// Queue Run Start Comment... (Bump Timestamp, for Proper Ordering!)
	struct timespec next = *ts;
	next.tv_nsec++;
	if ( next.tv_nsec >= NANO_PER_SECOND_LL ) {
		next.tv_nsec -= NANO_PER_SECOND_LL;
		next.tv_sec++;
	}
	std::stringstream ss;
	ss << "Run " << m_runNumber << " Started.";
	annotationCommentQueue.push_back(
		std::pair<struct timespec, std::string>( next, ss.str() ) );

	// If Run Notes Comment Persists Since the Last Run Ended,
	// And No Other Run Notes have been Queued as Yet, Then Re-Use It Now!
	// (Will Get Run Notes Queued, Set/Dumped on First Run Prologue...)
	if ( m_notesCommentPV->valid() && !notesCommentQueue.size() ) {
		struct timespec ts;
		m_notesCommentPV->timestamp( ts ); // Wallclock Time...!
		addNotesComment( &ts );
	}

	// Run is About to Start Now...
	m_lastInRun = m_inRun;
	m_inRun = true;
}

void Markers::runStop( struct timespec *ts ) // Wallclock Time...!
{
	/* Reset the scan index, comment, and paused flag at the end of
	 * each run, if it was stopped _Without_ first unpausing/stopping
	 * the scan... We'll emit markers if we were paused or in a scan so
	 * that any live clients can follow our state.
	 * Resume the pause _before_ stopping the scan, if that makes sense. :)
	 * -> so any Scan Stop Marker will be seen for sure in a regular
	 * Un-Paused stream data file...
	 */

	if ( m_isPaused || m_pausedPV->value() ) {
		// Set Paused State _Before_ Resume,
		// for Next Prologue to Use Queued...
		m_lastIsPaused = true;
		m_isPaused = false;
		// Clean Up Container before Full Stop!
		// Thwart Impending "Scan Start Continuation" Packet
		// as we Stop Run!
		uint32_t save_scanIndex = m_scanIndex;
		m_scanIndex = 0;
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->resumeRecording( ts ); // Wallclock Time...!
		// Restore Any Hanging Scan Index for Completion of Stop...
		m_scanIndex = save_scanIndex;
		// DO DUMP of Queued Markers/Comments *First* Here, Before Warning!
		// Dump Latest of Any Interim Run Notes Comment (Log Intervening)
		dumpRunNotesComment();
		// Dump Any Pre-Run Scan Comments Now...
		dumpQueuedComments();
		// Spew "We've Resumed" Packet
		emitPacket( *ts, ADARA::MarkerType::RESUME, m_scanIndex,
			"", "Warning: Run Resumed at Run Stop!" );
		// _This_ update() _Doesn't_ trigger changed()...!
		if ( m_pausedPV->value() )
			m_pausedPV->update( false, ts ); // Wallclock Time...!
	}
	// Still Need to Reset "Last Paused State"...
	else {
		m_lastIsPaused = false;
	}

	// (Possibly Redundant _Second_ Queued Dump Attempt, Ok if Empty Now.)
	// Dump Latest of Any Interim Run Notes Comment (Log Any Intervening)
	dumpRunNotesComment();
	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments();

	if ( m_scanIndex ) {
		emitPacket( *ts, ADARA::MarkerType::SCAN_STOP, m_scanIndex,
			"", "Warning: Scan Stopped at Run Stop!" );
		m_indexPV->update( 0, ts ); // Wallclock Time...!
		m_lastScanIndex = m_scanIndex;
		m_scanIndex = 0;
	}
	// Still Need to Reset "Last Scan Index"...
	else {
		m_lastScanIndex = 0;
	}

	// Add Run Stop Comment...
	std::stringstream ss;
	ss << "Run " << m_runNumber << " Stopped.";
	emitPacket( *ts, ADARA::MarkerType::GENERIC, m_scanIndex,
		"", ss.str() );

	// Clear All Comment PVs on Run Stop...
	m_commentPV->unset( false, ts ); // DEPRECATED, Wallclock Time...!
	m_scanCommentPV->unset( false, ts ); // Wallclock Time...!
	m_annotationCommentPV->unset( false, ts ); // Wallclock Time...!

	// Optionally Auto-Reset the Run Notes Between Runs
	// (Don't Always Reset, Often Convenient to Re-Use... ;-)
	m_notesCommentAutoReset = m_notesCommentAutoResetPV->value();
	if ( m_notesCommentAutoReset ) {
		m_notesCommentPV->unset( false, ts ); // Wallclock Time...!
	}

	// No Longer in a Run Now...
	m_lastInRun = m_inRun;
	m_inRun = false;

	// Reset Run Notes Set Flag, Allow Set Again for Next Run...
	m_notesCommentSet = false;
}

void Markers::pause( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = "";
		scanIndex = m_scanIndex;
	}

	std::stringstream ss;
	if ( m_inRun ) {
		ss << "Run " << m_runNumber << " ";
	} else {
		ss << "Data Collection ";
	}
	ss << "Paused.";
	DEBUG(ss.str()
		<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
		<< "." << std::setfill('0') << std::setw(9)
		<< ts->tv_nsec);

	emitPacket( *ts, ADARA::MarkerType::PAUSE, scanIndex,
		ss.str(), comment );

	if ( passthru != PASSTHRU )
	{
		// Save Last Pause/Resume Mode Run State Even If Not At All Changed
		m_lastRunNumber = m_runNumber;
		m_lastIsPaused = m_isPaused;
		m_lastScanIndex = scanIndex;
		m_lastInRun = m_inRun;
		// Set Paused State _Before_ Pause for Next Prologue to Skip Queued
		m_isPaused = true;
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->pauseRecording( ts ); // Wallclock Time...!
	}
	else
	{
		m_isPausedPT = true;
	}

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		pauseQueue.push_back( std::pair<struct timespec, std::string>( *ts,
			"[PRE-RUN] " + ss.str() + comment ) );
	}
}

void Markers::resume( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = "";
		scanIndex = m_scanIndex;
	}

	std::stringstream ss;
	if ( m_inRun ) {
		ss << "Run " << m_runNumber << " ";
	} else {
		ss << "Data Collection ";
	}
	ss << "Resumed.";
	DEBUG(ss.str()
		<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
		<< "." << std::setfill('0') << std::setw(9)
		<< ts->tv_nsec);

	if ( passthru != PASSTHRU )
	{
		// Save Last Pause/Resume Mode Run State Even If Not At All Changed
		m_lastRunNumber = m_runNumber;
		m_lastIsPaused = m_isPaused;
		m_lastScanIndex = scanIndex;
		m_lastInRun = m_inRun;
		// Set Paused State _Before_ Resume for Next Prologue to Use Queued
		m_isPaused = false;
		// NOTE: Triggers New File/Prologue with _Current_ State...!
		m_ctrl->resumeRecording( ts ); // Wallclock Time...!
	}
	else
	{
		m_isPausedPT = false;
	}

	emitPacket( *ts, ADARA::MarkerType::RESUME, scanIndex,
		ss.str(), comment );

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		resumeQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				"[PRE-RUN] " + ss.str() + comment ) );
	}
}

// Public Method for Setting Local MarkerPausePV Class Instance... ;-D
void Markers::updatePausedPV( bool paused, struct timespec *ts )
{
	m_pausedPV->update( paused, ts ); // Wallclock Time...!
}

void Markers::startScan( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		// NOTE: *Don't* Include "Scan Comment" Here,
		// Already Added on Scan Comment PV Set...
		comment = "";
		m_scanIndex = m_indexPV->value();
		m_lastScanIndex = m_scanIndex; // Mirror Explicit Scan Index Change
		scanIndex = m_scanIndex;
	}

	// Note: There's No Need to Save Last Pause/Resume Mode Run State Here,
	// As Scan Start/Stop and Scan Index _Do Not_ Affect Pause/Resume Mode
	// Or Affect the Captured Prologues...!

	std::stringstream ss;
	if ( !m_inRun )
		ss << "[PRE-RUN] ";
	else if ( m_isPaused || m_isPausedPT )
		ss << "[PAUSED Run " << m_runNumber << "] ";
	else
		ss << "[Run " << m_runNumber << "] ";
	ss << "Scan #" << scanIndex << " Started.";
	DEBUG( ss.str()
		<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
		<< "." << std::setfill('0') << std::setw(9)
		<< ts->tv_nsec );

	emitPacket( *ts, ADARA::MarkerType::SCAN_START, scanIndex,
		ss.str(), comment );

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		std::stringstream ss_scan;
		ss_scan << scanIndex << "|";
		scanStartQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				ss_scan.str() + ss.str() ) );
	}
}

void Markers::stopScan( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		// NOTE: *Don't* Include "Scan Comment" Here,
		// Already Added on Scan Comment PV Set...
		comment = "";
		scanIndex = m_scanIndex;
		m_scanIndex = 0;
		m_lastScanIndex = m_scanIndex; // Mirror Explicit Scan Index Change
	}

	// Note: There's No Need to Save Last Pause/Resume Mode Run State Here,
	// As Scan Start/Stop and Scan Index _Do Not_ Affect Pause/Resume Mode
	// Or Affect the Captured Prologues...!

	std::stringstream ss;
	if ( !m_inRun )
		ss << "[PRE-RUN] ";
	else if ( m_isPaused || m_isPausedPT )
		ss << "[PAUSED Run " << m_runNumber << "] ";
	else
		ss << "[Run " << m_runNumber << "] ";
	ss << "Scan #" << scanIndex << " Stopped.";
	DEBUG( ss.str()
		<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
		<< "." << std::setfill('0') << std::setw(9)
		<< ts->tv_nsec );

	emitPacket( *ts, ADARA::MarkerType::SCAN_STOP, scanIndex,
		ss.str(), comment );

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		std::stringstream ss_scan;
		ss_scan << scanIndex << "|";
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				ss_scan.str() + ss.str() ) );
	}
}

// DEPRECATED
void Markers::annotate( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = ( (m_commentPV->valid())
			? (m_commentPV->value()) : ("(No Comment)") );

		// Annotation Comments are one-shot,
		// Reset once the packet is inserted.
		m_commentPV->unset( false, ts );

		scanIndex = m_scanIndex;
	}

	std::stringstream ss;

	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";
		ss << " General Comment - " << comment;
		DEBUG("[DEPRECATED] annotate() " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		emitPacket( *ts, ADARA::MarkerType::GENERIC,
			scanIndex, "", comment );
	}

	// Otherwise, Queue Up Annotation Comment Strings for Next Run...
	else
	{
		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused || m_isPausedPT )
			ss << "[PAUSED Run " << m_runNumber << "] ";

		ss << comment;

		DEBUG("[DEPRECATED] annotate() General Comment - " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts, ss.str() ) );
	}
}

// DEPRECATED
void Markers::addRunComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = ( (m_commentPV->valid())
			? (m_commentPV->value()) : ("(No Comment)") );

		// Run Notes Comments are One-Shot,
		// (Unless Run Notes Updates is Enabled!)
		// Reset once the packet is inserted.
		m_commentPV->unset( false, ts );

		scanIndex = m_scanIndex;
	}

	std::stringstream ss;

	// In a Run, Add Run Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";

		// This Is It! :-D
		// Either Set the Run Notes Exactly Once,
		// Or Else Allow Any Number of Run Notes Updates During the Run...
		if ( !m_notesCommentSet || m_ctrl->getRunNotesUpdatesEnabled() )
		{
			ss << " Overall Run Notes - " << comment;
			DEBUG("[DEPRECATED] addRunComment() " << ss.str()
				<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec
				<< " (m_notesCommentSet=" << m_notesCommentSet << ")");

			emitPacket( *ts, ADARA::MarkerType::OVERALL_RUN_COMMENT,
				scanIndex, "", comment );

			m_notesCommentSet = true;
		}

		// Extraneous Extra Run Notes, Emit as Generic Comment...
		else
		{
			ss << " [DISCARDED RUN NOTES] - " << m_commentPV->value();
			ERROR("[DEPRECATED] addRunComment() " << ss.str()
				<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);

			emitPacket( *ts, ADARA::MarkerType::GENERIC,
				scanIndex, "[DISCARDED RUN NOTES] ", comment );
		}
	}

	// Otherwise, Save Run Notes Comment String for Next Run...
	else
	{
		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D
		// -> insert for Logging Only...! ;-D
		// -> Set "Use First Run Notes" Flag Tho...! :-D
		if ( !m_inRun ) {
			m_useFirstNotesComment = false;
			ss << "[PRE-RUN] ";
		}
		else if ( m_isPaused || m_isPausedPT ) {
			m_useFirstNotesComment = true;
			ss << "[PAUSED Run " << m_runNumber << "] ";
		}

		ss << comment;

		DEBUG("[DEPRECATED] addRunComment() Save Run Notes "
			<< " for Next Run (or When Unpaused) - "
			<< ss.str() << " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		notesCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts, comment ) );
	}
}

void Markers::addScanComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = ( (m_scanCommentPV->valid())
			? (m_scanCommentPV->value()) : ("(No Comment)") );
		scanIndex = m_scanIndex;
	}

	// Already in the Middle of a Scan...
	// - Prepend "Scan:" Prefix to Comment...
	std::stringstream ss;
	if ( scanIndex ) {
		ss << "Scan " << scanIndex << ": ";
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
		ss2 << " Scan Comment - " << ss.str() << comment;
		DEBUG("addScanComment() " << ss2.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		emitPacket( *ts, ADARA::MarkerType::GENERIC,
			scanIndex, ss.str(), comment );
	}

	// Otherwise, Queue Up Scan Comment Strings for Next Scan...
	else
	{
		std::stringstream ss_scan;
		ss_scan << scanIndex << "|";

		if ( !m_inRun )
			ss_scan << "[PRE-RUN] ";
		else if ( m_isPaused || m_isPausedPT )
			ss_scan << "[PAUSED Run " << m_runNumber << "] ";

		ss << comment;

		DEBUG("addScanComment()"
			<< " Queue Scan Comment for Next Scan scanIndex="
			<< ss_scan.str() << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		scanCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts,
				ss_scan.str() + ss.str() ) );
	}
}

void Markers::addNotesComment( struct timespec *ts, // Wallclock Time...!
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = ( (m_notesCommentPV->valid())
			? (m_notesCommentPV->value()) : ("(No Comment)") );
		scanIndex = m_scanIndex;
	}

	std::stringstream ss;

	// In a Run, Add Run Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";

		// This Is It! :-D
		// Either Set the Run Notes Exactly Once,
		// Or Else Allow Any Number of Run Notes Updates During the Run...
		if ( !m_notesCommentSet || m_ctrl->getRunNotesUpdatesEnabled() )
		{
			ss << " Overall Run Notes - " << comment;
			DEBUG("addNotesComment() " << ss.str()
				<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec
				<< " (m_notesCommentSet=" << m_notesCommentSet << ")");

			emitPacket( *ts, ADARA::MarkerType::OVERALL_RUN_COMMENT,
				scanIndex, "", comment );

			m_notesCommentSet = true;
		}

		// Extraneous Extra Run Notes, Emit as Generic Comment...
		else
		{
			ss << " [DISCARDED RUN NOTES] - " << comment;
			ERROR("addNotesComment() " << ss.str()
				<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);

			emitPacket( *ts, ADARA::MarkerType::GENERIC,
				scanIndex, "[DISCARDED RUN NOTES] ", comment );
		}
	}

	// Otherwise, Save Run Notes Comment String for Next Run...
	else
	{
		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D
		// -> insert for Logging Only...! ;-D
		// -> Set "Use First Run Notes" Flag Tho...! :-D
		if ( !m_inRun ) {
			m_useFirstNotesComment = false;
			ss << "[PRE-RUN] ";
		}
		else if ( m_isPaused || m_isPausedPT ) {
			m_useFirstNotesComment = true;
			ss << "[PAUSED Run " << m_runNumber << "] ";
		}

		ss << comment;

		DEBUG("addNotesComment() Save Run Notes"
			<< " for Next Run (or When Unpaused) - "
			<< ss.str() << " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		notesCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts, comment ) );
	}
}

void Markers::addAnnotationComment( struct timespec *ts, // Wallclock Time
		PassThru passthru, uint32_t pt_scanIndex, std::string pt_comment )
{
	std::string comment;
	uint32_t scanIndex;

	if ( passthru != IGNORE ) {
		comment = pt_comment;
		scanIndex = pt_scanIndex;
	}
	else {
		comment = ( (m_annotationCommentPV->valid())
			? (m_annotationCommentPV->value()) : ("(No Comment)") );
		scanIndex = m_scanIndex;
	}

	std::stringstream ss;

	// In a Run, Add Annotation Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";
		ss << " General Comment - " << comment;
		DEBUG("addAnnotationComment() " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		emitPacket( *ts, ADARA::MarkerType::GENERIC,
			scanIndex, "", comment );
	}

	// Otherwise, Queue Up Annotation Comment Strings for Next Run...
	else
	{
		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused || m_isPausedPT )
			ss << "[PAUSED Run " << m_runNumber << "] ";

		ss << comment;

		DEBUG("addAnnotationComment() General Comment - " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts, ss.str() ) );
	}
}

void Markers::addSystemComment( struct timespec *ts, // Wallclock Time...!
		uint32_t scanIndex, std::string comment )
{
	std::stringstream ss;

	// In a Run, Add System Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		ss << "Run " << m_runNumber << ":";
		ss << " System Comment - " << comment;
		DEBUG("addSystemComment() " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		emitPacket( *ts, ADARA::MarkerType::SYSTEM,
			scanIndex, "", comment );
	}

	// Otherwise, Queue Up System Comment Strings for Next Run...
	else
	{
		if ( !m_inRun )
			ss << "[PRE-RUN] ";
		else if ( m_isPaused || m_isPausedPT )
			ss << "[PAUSED Run " << m_runNumber << "] ";

		ss << comment;

		DEBUG("addSystemComment() System Comment - " << ss.str()
			<< " at " << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);

		systemCommentQueue.push_back(
			std::pair<struct timespec, std::string>( *ts, ss.str() ) );
	}
}

void Markers::dumpRunNotesComment( bool prologue )
{
	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !m_inRun || m_isPaused )
		return;

	// Only Dump *One* Set of Official Run Notes for Any Given Run...
	// Or Else Allow Any Number of Run Notes Updates During the Run... :-D
	if ( !m_notesCommentSet || m_ctrl->getRunNotesUpdatesEnabled() )
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
					<< " Found First Run Notes Comment at "
					<< first_notes_it->first.tv_sec
						- ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< first_notes_it->first.tv_nsec << std::setw(0)
					<< ": " << first_notes_it->second
					<< " (m_notesCommentSet=" << m_notesCommentSet << ")");

				emitPacket( first_notes_it->first,
					ADARA::MarkerType::OVERALL_RUN_COMMENT, m_scanIndex,
					"", first_notes_it->second, prologue );

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
					<< " Found Last Run Notes Comment at "
					<< last_notes_it->first.tv_sec
						- ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< last_notes_it->first.tv_nsec << std::setw(0)
					<< ": " << last_notes_it->second
					<< " (m_notesCommentSet=" << m_notesCommentSet << ")");

				emitPacket( last_notes_it->first,
					ADARA::MarkerType::OVERALL_RUN_COMMENT, m_scanIndex,
					"", last_notes_it->second, prologue );

				notesCommentQueue.pop_back();

				m_notesCommentSet = true;
			}
		}
	}

	// Now Log Any Intervening Run Notes & Discard...
	// (...Done in dumpQueuedComments() to Interleave with Other Queues...)
}

void Markers::dumpQueuedComments( bool prologue, bool capture_last )
{
	uint32_t runNumber;
	uint32_t scanIndex;
	uint32_t isPaused;
	uint32_t inRun;

	// Snag Proper State Depending on Whether This is a Real Dump,
	// Or Simply a "Captured" Last Prologue Header...
	if ( capture_last )
	{
		runNumber = m_lastRunNumber;
		scanIndex = m_lastScanIndex;
		isPaused = m_lastIsPaused;
		inRun = m_lastInRun;
	}
	else
	{
		runNumber = m_runNumber;
		scanIndex = m_scanIndex;
		isPaused = m_isPaused;
		inRun = m_inRun;
	}

	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !inRun || isPaused )
		return;

	MarkerQueue::iterator pause_it = pauseQueue.begin();
	MarkerQueue::iterator resume_it = resumeQueue.begin();
	MarkerQueue::iterator scan_start_it = scanStartQueue.begin();
	MarkerQueue::iterator scan_stop_it = scanStopQueue.begin();
	MarkerQueue::iterator scan_comment_it = scanCommentQueue.begin();
	MarkerQueue::iterator notes_it = notesCommentQueue.begin();
	MarkerQueue::iterator annotation_it = annotationCommentQueue.begin();
	MarkerQueue::iterator system_it = systemCommentQueue.begin();

	// Dump Queued Markers in Proper Time Order
	while ( pause_it != pauseQueue.end()
			|| resume_it != resumeQueue.end()
			|| scan_start_it != scanStartQueue.end()
			|| scan_stop_it != scanStopQueue.end()
			|| scan_comment_it != scanCommentQueue.end()
			|| notes_it != notesCommentQueue.end()
			|| annotation_it != annotationCommentQueue.end()
			|| system_it != systemCommentQueue.end() )
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
			// Note: These Comments Here are _Always_ those Extraneous
			// Intervening Run Notes Entered Either Before a Run or
			// While Paused. The "Chosen" Run Notes Comment, From the
			// Start or End of This Queue, Has Already Been Extracted
			// Via the Call to dumpRunNotesComment(). ;-D
			// Anyway, Any Residual Run Notes Comments Queued Here
			// Should Just Go Into the Generic Run Annotation Comments. :-D
			markerType = ADARA::MarkerType::GENERIC;
			prefix = "[DISCARDED RUN NOTES] ";
			desc = "Discarding Intervening Pre-Run or Paused Notes";
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

		if ( system_it != systemCommentQueue.end()
				&& (t=timespec_to_nsec( system_it->first )) < next ) {
			next_it = system_it;
			markerType = ADARA::MarkerType::SYSTEM;
			prefix = "";
			desc = "Dump Next System Comment/Marker";
			is_error = false;
			is_scan = false;
			next = t;
		}

		// Handle Scan Index Decoding from Saved Comment String... ;-Q
		std::string comment = next_it->second;
		uint32_t use_scanIndex = scanIndex; // Use as Default if Not a Scan
		std::stringstream ss_scan;
		if ( is_scan )
		{
			std::size_t found;
			if ( (found = comment.find("|")) != std::string::npos )
			{
				use_scanIndex = boost::lexical_cast<uint32_t>(
					comment.substr( 0, found ) );
				comment = comment.substr( found + 1 );
			}
			ss_scan << "scanIndex=" << use_scanIndex << " ";
		}

		// Dump Next Comment/Marker...
		std::stringstream ss;
		ss << "dumpQueuedComments()"
			<< " Run " << runNumber << ": "
			<< desc << " MarkerType=" << markerType << " at "
			<< next_it->first.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< next_it->first.tv_nsec << std::setw(0)
			<< ": " << ss_scan.str() << prefix << comment;

		if ( is_error )
			ERROR( ss.str() );
		else
			DEBUG( ss.str() );

		emitPacket( next_it->first, markerType, use_scanIndex,
			prefix, comment, prologue );

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
		else if ( next_it == system_it )
			system_it++;
	}

	// Only Clear Out Queued Markers If _Not_ Capturing Last Prologue...!
	if ( !capture_last )
	{
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

		// Clear Out Queued System Comments
		systemCommentQueue.clear();
	}
}

void Markers::onPrologue( bool capture_last )
{
	uint32_t runNumber;
	uint32_t scanIndex;
	uint32_t isPaused;
	uint32_t inRun;

	// Snag Proper State Depending on Whether This is a Real Dump,
	// Or Simply a "Captured" Last Prologue Header...
	if ( capture_last )
	{
		runNumber = m_lastRunNumber;
		scanIndex = m_lastScanIndex;
		isPaused = m_lastIsPaused;
		inRun = m_lastInRun;
	}
	else
	{
		runNumber = m_runNumber;
		scanIndex = m_scanIndex;
		isPaused = m_isPaused;
		inRun = m_inRun;
	}

	// Skip Irreversible Run Notes State Change
	// If Only Capturing Last Prologue Header...
	if ( !capture_last )
	{
		// Dump Latest of Any Interim Run Notes Comment
		// (Log Any Intervening)
		dumpRunNotesComment( true );
	}

	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments( true, capture_last );

	if ( scanIndex )
	{
		std::stringstream ss;
		ss << "[NEW RUN FILE CONTINUATION] ";
		if ( !inRun )
			ss << "[PRE-RUN] ";
		else if ( isPaused || m_isPausedPT )
			ss << "[PAUSED Run " << runNumber << "] ";
		else
			ss << "[Run " << runNumber << "] ";
		ss << "Scan #" << scanIndex << " Started.";
		DEBUG("onPrologue() " << ss.str());
		emitPrologue( ADARA::MarkerType::SCAN_START, ss.str() );
	}

	if ( isPaused )
	{
		std::stringstream ss;
		ss << "[NEW RUN FILE CONTINUATION] ";
		if ( !inRun ) {
			ss << "[PRE-RUN] Data Collection ";
		} else {
			ss << "Run " << runNumber << " ";
		}
		ss << "Paused.";
		DEBUG("onPrologue() " << ss.str());
		emitPrologue( ADARA::MarkerType::PAUSE, ss.str() );
	}
}

void Markers::emitPrologue( ADARA::MarkerType::Enum markerType,
		std::string comment )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, m_scanIndex, "", comment, true );
}

void Markers::emitPacket( const struct timespec &ts, // Wallclock Time...!
		ADARA::MarkerType::Enum markerType, uint32_t scanIndex,
		std::string prefix, std::string comment, bool prologue )
{
	uint32_t pkt[ 2 + sizeof(ADARA::Header) / sizeof(uint32_t) ];
	std::string full_comment = "";
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
		pkt[5] = scanIndex;

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

	full_comment = prefix + comment;

	if ( !full_comment.empty() ) {
	
		/* We only allow so much content... */
		if ( full_comment.size() > 65535 )
			full_comment.resize(65535);

		pkt[0] += ( full_comment.size() + 3 ) & ~3;
		pkt[4] |= full_comment.size();

		iov.iov_base = const_cast<char *>(full_comment.c_str());
		iov.iov_len = full_comment.size();
		iovec.push_back(iov);

		if ( full_comment.size() % 4 ) {
			iov.iov_base = &pad;
			iov.iov_len = 4 - ( full_comment.size() % 4 );
			iovec.push_back(iov);
		}
	}

	if ( prologue )
		StorageManager::addPrologue(iovec);
	else
		StorageManager::addPacket(iovec,
			false /* ignore_pkt_timestamp */,
			false /* check_old_containers */);
}

