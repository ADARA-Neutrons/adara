
#include <boost/function.hpp>
#include <stdint.h>
#include <string>

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
	m_ctrl(ctrl), m_scanIndex(0)
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

void Markers::newRun(void)
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );

	/* Reset the scan index, comment, and paused flag at the start of
	 * each new run. We'll emit markers if we are paused or in a scan so
	 * that any live clients can follow our state.
	 */
	if ( m_scanIndex ) {
		emitPacket( now, ADARA::MarkerType::SCAN_STOP );
		// update() doesn't trigger changed()!
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
	}

	if ( m_pausedPV->value() ) {
		// Clean Up Container before Full Stop!
		m_ctrl->resumeRecording();
		// Spew "We've Resumed" Packet
		emitPacket( now, ADARA::MarkerType::RESUME );
		// update() doesn't trigger changed()!
		m_pausedPV->update(false, &now);
	}

	// Don't Unset Comment PV(s) on Run Start, _Only_ on Run Stop...
	// Comments can be entered in the "Before Run" interim...! ;-D
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
	 */
	if ( m_pausedPV->value() ) {
		// Clean Up Container before Full Stop!
		m_ctrl->resumeRecording();
		// Spew "We've Resumed" Packet
		emitPacket( now, ADARA::MarkerType::RESUME );
		// update() doesn't trigger changed()!
		m_pausedPV->update(false, &now);
	}

	if ( m_scanIndex ) {
		emitPacket( now, ADARA::MarkerType::SCAN_STOP );
		// update() doesn't trigger changed()!
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
	}

	m_commentPV->unset();
	m_notesCommentPV->unset();
	m_annotationCommentPV->unset();
}

void Markers::pause(void)
{
	DEBUG("Paused!");
	emitPacket( ADARA::MarkerType::PAUSE, m_commentPV );
	m_ctrl->pauseRecording();
	// Pause Comments are one-shot, and reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::resume(void)
{
	DEBUG("Resumed!");
	m_ctrl->resumeRecording();
	emitPacket( ADARA::MarkerType::RESUME, m_commentPV );
	// Resume Comments are one-shot, and reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::startScan(void)
{
	m_scanIndex = m_indexPV->value();
	DEBUG("Start Scan " << m_scanIndex);
	emitPacket( ADARA::MarkerType::SCAN_START, m_commentPV );
	// Scan Start Comments are one-shot, reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::stopScan(void)
{
	DEBUG("Stop Scan " << m_scanIndex);
	emitPacket( ADARA::MarkerType::SCAN_STOP, m_commentPV );
	m_scanIndex = 0;
	// Scan Stop Comments are one-shot, reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::annotate(void)
{
	emitPacket( ADARA::MarkerType::GENERIC, m_commentPV );
	// Annotation Comments are one-shot, reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::addRunComment(void)
{
	emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT, m_commentPV );
	// Run Comments are one-shot, and reset once the packet is inserted.
	m_commentPV->unset();
}

void Markers::addNotesComment(void)
{
	emitPacket( ADARA::MarkerType::OVERALL_RUN_COMMENT, m_notesCommentPV );
}

void Markers::addAnnotationComment(void)
{
	emitPacket( ADARA::MarkerType::GENERIC, m_annotationCommentPV );
}

void Markers::onPrologue(void)
{
	if ( m_scanIndex )
		emitPrologue( ADARA::MarkerType::SCAN_START );
	if ( m_pausedPV->value() )
		emitPrologue( ADARA::MarkerType::PAUSE );
}

void Markers::emitPrologue( ADARA::MarkerType::Enum markerType )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, StringPVSharedPtr(), true );
}

void Markers::emitPacket( ADARA::MarkerType::Enum markerType,
		StringPVSharedPtr commentPV )
{
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now );
	emitPacket( now, markerType, commentPV );
}

void Markers::emitPacket( const struct timespec &ts,
		ADARA::MarkerType::Enum markerType,
		StringPVSharedPtr commentPV, bool prologue )
{
	uint32_t pkt[ 2 + sizeof(ADARA::Header) / sizeof(uint32_t) ];
	std::string comment;
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

			comment = commentPV->value();

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
	}

	if ( prologue )
		StorageManager::addPrologue(iovec);
	else
		StorageManager::addPacket(iovec);
}

