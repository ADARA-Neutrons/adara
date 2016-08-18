
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
		smsBooleanPV(name), m_markers(m) {}

private:
	Markers *m_markers;

	void changed(void)
	{
		if ( value() )
			m_markers->pause();
		else
			m_markers->resume();
	}
};

class MarkerTriggerPV : public smsTriggerPV {
public:
	typedef boost::function<void (void)> callback;

	MarkerTriggerPV( const std::string &name, callback cb ) :
		smsTriggerPV(name), m_cb(cb) {}

private:
	callback m_cb;

	void triggered(void) { m_cb(); }
};

class MarkerCommentPV : public smsStringPV {
public:
	typedef boost::function<void (void)> callback;

	MarkerCommentPV( const std::string &name, callback cb ) :
		smsStringPV(name), m_cb(cb) {}

private:
	callback m_cb;

	void changed(void)
	{
		// Only Call Callback if String PV Set to Non-Empty Value...
		// (Otherwise, "unset()" triggers a callback... ;-b)
		std::string comment = value();
		if ( !comment.empty() && comment.compare( "(unset)" ) )
			m_cb();
	}
};

Markers::Markers( SMSControl *ctrl ) :
	m_ctrl(ctrl), m_inRun(false), m_isPaused(false),
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

	m_annotationCommentPV.reset(
		new MarkerCommentPV( prefix + "AnnotationComment",
			boost::bind( &Markers::addAnnotationComment, this ) ) );
	ctrl->addPV(m_annotationCommentPV);

	m_connection = StorageManager::onPrologue(
				boost::bind( &Markers::onPrologue, this ) );
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
		// Clean Up Container before Actual Start!
		m_ctrl->resumeRecording();
		// Spew "We've Resumed" Packet
		std::string comment = "Warning: Run Resumed at New Run Start!";
		emitPacket( now, ADARA::MarkerType::RESUME, comment );
		// update() doesn't trigger changed()!
		m_pausedPV->update(false, &now);
		m_isPaused = false;
		// _Also_ Queue This Resume Comment for _After_ Run Start...
		// (just use generic Annotation Comment... ;-)
		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + comment ) );
	}

	if ( m_scanIndex ) {
		std::string comment = "Warning: Scan Stopped at New Run Start!";
		emitPacket( now, ADARA::MarkerType::SCAN_STOP, comment );
		// update() doesn't trigger changed()!
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
		// _Also_ Queue This Scan Stop/Comment for _After_ Run Start...
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + comment ) );
	}

	// Don't Unset Comment PV(s) on Run Start, _Only_ on Run Stop...
	// Comments can be entered in the "Before Run" interim...! ;-D

	// Queue Run Start Comment... (Get New Timestamp, for Proper Ordering!)
	clock_gettime( CLOCK_REALTIME, &now );
	std::stringstream ss;
	ss << "Run " << m_runNumber << " Started.";
	annotationCommentQueue.push_back(
		std::pair<struct timespec, std::string>( now, ss.str() ) );

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
		// Clean Up Container before Full Stop!
		m_ctrl->resumeRecording();
		// Spew "We've Resumed" Packet
		emitPacket( now, ADARA::MarkerType::RESUME,
			"Warning: Run Resumed at Run Stop!" );
		// update() doesn't trigger changed()!
		m_pausedPV->update(false, &now);
		m_isPaused = false;
	}

	// Dump Latest of Any Interim Run Notes Comment (Log Any Intervening)
	dumpLastRunNotes();

	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments();

	if ( m_scanIndex ) {
		emitPacket( now, ADARA::MarkerType::SCAN_STOP,
			"Warning: Scan Stopped at Run Stop!" );
		// update() doesn't trigger changed()!
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
	m_notesCommentPV->unset();
	m_annotationCommentPV->unset();

	// No Longer in a Run Now...
	m_inRun = false;
}

void Markers::pause(void)
{
	m_isPaused = true;
	std::string comment = "Run Paused.";
	DEBUG(comment);
	emitPacket( ADARA::MarkerType::PAUSE, comment );
	m_ctrl->pauseRecording();

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + comment ) );
	}
}

void Markers::resume(void)
{
	m_isPaused = false;
	std::string comment = "Run Resumed.";
	DEBUG(comment);
	m_ctrl->resumeRecording();
	emitPacket( ADARA::MarkerType::RESUME, comment );

	// If Not in Run, then _Also_ Queue Message for Later...
	if ( !m_inRun )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				"[PRE-RUN] " + comment ) );
	}
}

void Markers::startScan(void)
{
	m_scanIndex = m_indexPV->value();
	std::stringstream ss;
	ss << "Scan #" << m_scanIndex << " Started.";
	DEBUG( ss.str() );
	// NOTE: *Don't* Include "Scan Comment" Here, Already Added on Set...
	emitPacket( ADARA::MarkerType::SCAN_START, ss.str() );

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		std::string label = "";
		if ( !m_inRun )
			label = "[PRE-RUN] ";
		else if ( m_isPaused )
			label = "[PAUSED] ";
		scanStartQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				label + ss.str() ) );
	}
}

void Markers::stopScan(void)
{
	std::stringstream ss;
	ss << "Scan #" << m_scanIndex << " Stopped.";
	DEBUG( ss.str() );
	emitPacket( ADARA::MarkerType::SCAN_STOP, ss.str() );
	m_scanIndex = 0;

	// If Not in Run, or if Paused, then _Also_ Queue Message for Later...
	if ( !m_inRun || m_isPaused )
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );
		std::string label = "";
		if ( !m_inRun )
			label = "[PRE-RUN] ";
		else if ( m_isPaused )
			label = "[PAUSED] ";
		scanStopQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				label + ss.str() ) );
	}
}

// DEPRECATED
void Markers::annotate(void)
{
	if ( m_inRun && !m_isPaused )
	{
		emitPacket( ADARA::MarkerType::GENERIC, "", m_commentPV );

		// Annotation Comments are one-shot,
		// reset once the packet is inserted.
		m_commentPV->unset();
	}

	// Otherwise, Queue Up Annotation Comments Strings for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		std::string label = "";
		if ( !m_inRun )
			label = "[PRE-RUN] ";
		else if ( m_isPaused )
			label = "[PAUSED] ";

		std::string comment;
		if ( m_commentPV->valid() ) {
			comment = m_commentPV->value();
		}
		else {
			comment = "(No Comment)";
		}

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				label + comment ) );
	}
}

// DEPRECATED
void Markers::addRunComment(void)
{
	if ( m_inRun && !m_isPaused )
	{
		emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT,
			"", m_commentPV );

		// Run Comments are one-shot, and reset once the packet is inserted.
		m_commentPV->unset();
	}

	// Otherwise, Save Last Run Comment String for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D

		std::string comment;
		if ( m_commentPV->valid() ) {
			comment = m_commentPV->value();
		}
		else {
			comment = "(No Comment)";
		}

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
		emitPacket( ADARA::MarkerType::GENERIC, ss.str(), m_scanCommentPV );
	}

	// Otherwise, Queue Up Scan Comments Strings for Next Scan...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		std::string label = "";
		if ( !m_inRun )
			label = "[PRE-RUN] ";
		else if ( m_isPaused )
			label = "[PAUSED] ";

		if ( m_scanCommentPV->valid() ) {
			ss << m_scanCommentPV->value();
		}
		else {
			ss << "(No Comment)";
		}

		scanCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				label + ss.str() ) );
	}
}

void Markers::addNotesComment(void)
{
	// In a Run, Add Run Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT,
			"", m_notesCommentPV );
	}

	// Otherwise, Save Last Run Comment String for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		// No "[PRE-RUN]" or "[PAUSED]" Labels for Run Notes...! ;-D

		std::string comment;
		if ( m_notesCommentPV->valid() ) {
			comment = m_notesCommentPV->value();
		}
		else {
			comment = "(No Comment)";
		}

		notesCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now, comment ) );
	}
}

void Markers::addAnnotationComment(void)
{
	// In a Run, Add Annotation Comment Now...
	if ( m_inRun && !m_isPaused )
	{
		emitPacket( ADARA::MarkerType::GENERIC, "", m_annotationCommentPV );
	}

	// Otherwise, Queue Up Annotation Comments Strings for Next Run...
	else
	{
		struct timespec now;
		clock_gettime( CLOCK_REALTIME, &now );

		std::string label = "";
		if ( !m_inRun )
			label = "[PRE-RUN] ";
		else if ( m_isPaused )
			label = "[PAUSED] ";

		std::string comment;
		if ( m_annotationCommentPV->valid() ) {
			comment = m_annotationCommentPV->value();
		}
		else {
			comment = "(No Comment)";
		}

		annotationCommentQueue.push_back(
			std::pair<struct timespec, std::string>( now,
				label + comment ) );
	}
}

void Markers::dumpLastRunNotes(void)
{
	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !m_inRun || m_isPaused )
		return;

	// Dump Last Run Notes, If Any...

	MarkerQueue::reverse_iterator last_notes_it =
		notesCommentQueue.rbegin();

	if ( last_notes_it != notesCommentQueue.rend() )
	{
		DEBUG("dumpLastRunNotes() Found Last Run Note - "
			<< last_notes_it->first.tv_sec
			<< "." << last_notes_it->first.tv_nsec
			<< ":" << last_notes_it->second);

		emitPacket( last_notes_it->first,
			ADARA::MarkerType::OVERALL_RUN_COMMENT, last_notes_it->second );

		notesCommentQueue.pop_back();
	}

	// Log Any Intervening Run Notes & Discard...

	MarkerQueue::iterator notes_it = notesCommentQueue.begin();

	while ( notes_it != notesCommentQueue.end() )
	{
		ERROR("dumpLastRunNotes(): Discarding Intervening Pre-Run Notes - "
			<< notes_it->first.tv_sec << "." << notes_it->first.tv_nsec
			<< ":" << notes_it->second);

		// Add Discarded Run Comment to Annotation Comments...
		std::string comment = "[DISCARDED RUN NOTES] " + notes_it->second;
		emitPacket( notes_it->first, ADARA::MarkerType::GENERIC, comment );

		notes_it++;
	}

	// Clear Out Queued Run Notes
	notesCommentQueue.clear();
}

void Markers::dumpQueuedComments(void)
{
	// Keep Saving Things Until a Run Actually Starts (& Un-Pauses!)
	if ( !m_inRun || m_isPaused )
		return;

	MarkerQueue::iterator scan_start_it = scanStartQueue.begin();
	MarkerQueue::iterator scan_stop_it = scanStopQueue.begin();
	MarkerQueue::iterator scan_comment_it = scanCommentQueue.begin();
	MarkerQueue::iterator annotation_it = annotationCommentQueue.begin();

	// Dump Queued Markers in Proper Time Order
	while ( scan_start_it != scanStartQueue.end()
			|| scan_stop_it != scanStopQueue.end()
			|| scan_comment_it != scanCommentQueue.end()
			|| annotation_it != annotationCommentQueue.end() )
	{
		MarkerQueue::iterator next_it;
		ADARA::MarkerType::Enum markerType = ADARA::MarkerType::GENERIC;
		uint64_t t, next = (uint64_t) -1;

		if ( scan_start_it != scanStartQueue.end()
				&& (t=timespec_to_nsec( scan_start_it->first )) < next ) {
			next_it = scan_start_it;
			markerType = ADARA::MarkerType::SCAN_START;
			next = t;
		}

		if ( scan_stop_it != scanStopQueue.end()
				&& (t=timespec_to_nsec( scan_stop_it->first )) < next ) {
			next_it = scan_stop_it;
			markerType = ADARA::MarkerType::SCAN_STOP;
			next = t;
		}

		if ( scan_comment_it != scanCommentQueue.end()
				&& (t=timespec_to_nsec( scan_comment_it->first )) < next ) {
			next_it = scan_comment_it;
			markerType = ADARA::MarkerType::GENERIC;
			next = t;
		}

		if ( annotation_it != annotationCommentQueue.end()
				&& (t=timespec_to_nsec( annotation_it->first )) < next ) {
			next_it = annotation_it;
			markerType = ADARA::MarkerType::GENERIC;
			next = t;
		}

		// Dump Next Comment/Marker...
		DEBUG("dumpQueuedComments() Dump Next Comment/Marker,"
			<< "MarkerType=" << markerType << " - "
			<< next_it->first.tv_sec << "." << next_it->first.tv_nsec
			<< ":" << next_it->second);

		emitPacket( next_it->first, markerType, next_it->second );

		// Increment Given Iterator...
		if ( next_it == scan_start_it )
			scan_start_it++;
		else if ( next_it == scan_stop_it )
			scan_stop_it++;
		else if ( next_it == scan_comment_it )
			scan_comment_it++;
		else if ( next_it == annotation_it )
			annotation_it++;
	}

	// Clear Out Queued Scan Starts
	scanStartQueue.clear();

	// Clear Out Queued Scan Stops
	scanStopQueue.clear();

	// Clear Out Queued Scan Comments
	scanCommentQueue.clear();

	// Clear Out Queued Annotation Comments
	annotationCommentQueue.clear();
}

void Markers::onPrologue(void)
{
	if ( m_scanIndex )
		emitPrologue( ADARA::MarkerType::SCAN_START );
	if ( m_pausedPV->value() )
		emitPrologue( ADARA::MarkerType::PAUSE );

	// Dump Latest of Any Interim Run Notes Comment (Log Any Intervening)
	dumpLastRunNotes();

	// Dump Any Pre-Run Scan Comments Now...
	dumpQueuedComments();
}

void Markers::emitPrologue( ADARA::MarkerType::Enum markerType )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, "", StringPVSharedPtr(), true );
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

