#include <boost/function.hpp>
#include <string>

#include "Markers.h"
#include "StorageManager.h"

class MarkerPausedPV : public smsBooleanPV {
public:
	MarkerPausedPV(const std::string &name, Markers *m) :
		smsBooleanPV(name), m_markers(m) {}

private:
	Markers *m_markers;

	void changed(void)
	{
		if (value())
			m_markers->pause();
		else
			m_markers->resume();
	}
};

class MarkerTriggerPV : public smsTriggerPV {
public:
        typedef boost::function<void (void)> callback;

	MarkerTriggerPV(const std::string &name, callback cb) :
		smsTriggerPV(name), m_cb(cb) {}

private:
	callback m_cb;

	void triggered(void) { m_cb(); }
};

Markers::Markers(const std::string &beamline, SMSControl *sms)
{
	std::string prefix(beamline);
	prefix += ":SMS:";

	m_pausedPV.reset(new MarkerPausedPV(prefix + "Paused", this));
	sms->addPV(m_pausedPV);

	prefix += "Marker:";
	m_commentPV.reset(new smsStringPV(prefix + "Comment"));
	sms->addPV(m_commentPV);

	m_indexPV.reset(new smsUint32PV(prefix + "ScanIndex"));
	sms->addPV(m_indexPV);

	m_scanStartPV.reset(new MarkerTriggerPV(prefix + "StartScan",
			    boost::bind(&Markers::startScan, this)));
	sms->addPV(m_scanStartPV);

	m_scanStopPV.reset(new MarkerTriggerPV(prefix + "StopScan",
			    boost::bind(&Markers::stopScan, this)));
	sms->addPV(m_scanStopPV);

	m_annotatePV.reset(new MarkerTriggerPV(prefix + "Annotate",
			    boost::bind(&Markers::annotate, this)));
	sms->addPV(m_annotatePV);

	m_runCommentPV.reset(new MarkerTriggerPV(prefix + "RunComment",
			    boost::bind(&Markers::addRunComment, this)));
	sms->addPV(m_runCommentPV);

	m_connection = StorageManager::onPrologue(
				boost::bind(&Markers::onPrologue, this));
}

Markers::~Markers()
{
	m_connection.disconnect();
}

void Markers::newRun(void)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	/* Reset the scan index, comment, and paused flag at the start of
	 * each new run. We'll emit markers if we are paused or in a scan so
	 * that any live clients can follow our state.
	 */
	if (m_scanIndex) {
		emitPacket(now, ADARA::MarkerType::SCAN_STOP, false);
		m_indexPV->update(0, &now);
		m_scanIndex = 0;
	}

	if (m_pausedPV->value()) {
		emitPacket(now, ADARA::MarkerType::RESUME, false);
		m_pausedPV->update(false, &now);
	}

	m_commentPV->unset();
}

void Markers::pause(void)
{
	emitPacket(ADARA::MarkerType::PAUSE);
}

void Markers::resume(void)
{
	emitPacket(ADARA::MarkerType::RESUME);
}

void Markers::startScan(void)
{
	m_scanIndex = m_indexPV->value();
	emitPacket(ADARA::MarkerType::SCAN_START);
}

void Markers::stopScan(void)
{
	emitPacket(ADARA::MarkerType::SCAN_STOP);
	m_scanIndex = 0;
}

void Markers::annotate(void)
{
	emitPacket(ADARA::MarkerType::GENERIC);
}

void Markers::addRunComment(void)
{
	emitPacket(ADARA::MarkerType::OVERALL_RUN_COMMENT);
}

void Markers::onPrologue(void)
{
	if (m_scanIndex)
		emitPrologue(ADARA::MarkerType::SCAN_START);
	if (m_pausedPV->value())
		emitPrologue(ADARA::MarkerType::PAUSE);
}

void Markers::emitPrologue(ADARA::MarkerType::Enum markerType)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	emitPacket(now, markerType, false, true);
}

void Markers::emitPacket(ADARA::MarkerType::Enum markerType, bool addComment)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	emitPacket(now, markerType, addComment);

	/* Comments are one-shot, and reset once the packet is inserted.
	 * Don't unset it if we aren't adding the comment -- this protects
	 * against a race between setting up an annotation packet and
	 * rolling over to a new file in the container.
	 */
	if (addComment)
		m_commentPV->unset();
}

void Markers::emitPacket(const struct timespec &ts,
			 ADARA::MarkerType::Enum markerType,
			 bool addComment, bool prologue)
{
	uint32_t pkt[2 + sizeof(ADARA::Header) / sizeof(uint32_t)];
	std::string comment;
	uint32_t pad = 0;
	struct iovec iov;
	IoVector iovec;

	pkt[0] = 2 * sizeof(uint32_t);
	pkt[1] = ADARA::PacketType::STREAM_ANNOTATION_V0;
	pkt[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	pkt[3] = ts.tv_nsec;

	pkt[4] = (uint32_t) markerType << 16;
	if (markerType == ADARA::MarkerType::OVERALL_RUN_COMMENT)
		pkt[5] = 0;
	else
		pkt[5] = m_scanIndex;

	/* Set the hint flag for clients, but only for scan start/stops */
	// XXX think this through, maybe get rid of hint
	switch (markerType) {
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
	if (addComment && m_commentPV->valid()) {
		comment = m_commentPV->value();

		/* We only allow so much content... */
		if (comment.size() > 65535)
			comment.resize(65535);

		pkt[0] += (comment.size() + 3) & ~3;
		pkt[4] |= comment.size();

		iov.iov_base = const_cast<char *>(comment.c_str());
		iov.iov_len = comment.size();
		iovec.push_back(iov);

		if (comment.size() % 4) {
			iov.iov_base = &pad;
			iov.iov_len = 4 - (comment.size() % 4);
			iovec.push_back(iov);
		}
	}

	if (prologue)
		StorageManager::addPrologue(iovec);
	else
		StorageManager::addPacket(iovec);
}
