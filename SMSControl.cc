#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "StorageManager.h"

SMSControl::SMSControl(const std::string &beamline) :
	m_nextRunNumber(1), m_recording(false)
{
	std::string prefix(beamline);
	prefix += ":SMS";

	m_pvRecording = boost::shared_ptr<smsRecordingPV>(new
						smsRecordingPV(prefix, this));
	m_pvRunNumber = boost::shared_ptr<smsRunNumberPV>(new
						smsRunNumberPV(prefix));

	addPV(m_pvRecording);
	addPV(m_pvRunNumber);

	/* TODO get old run number information from StorageManager */
}

SMSControl::~SMSControl()
{
}

void SMSControl::show(unsigned level) const
{
	caServer::show(level);
}

pvExistReturn SMSControl::pvExistTest(const casCtx &ctx, const caNetAddr &,
			   const char *pv_name)
{
	/* This is the new version, but just call to the deprecated one
	 * since we don't currently deal with access control on a per-net
	 * basis.
	 */
	return pvExistTest(ctx, pv_name);
}

pvExistReturn SMSControl::pvExistTest(const casCtx &ctx, const char *pv_name)
{
	if (m_pv_map.find(pv_name) != m_pv_map.end())
		return pverExistsHere;
	return pverDoesNotExistHere;
}

pvAttachReturn SMSControl::pvAttach(const casCtx &ctx, const char *pv_name)
{
	std::map<std::string, boost::shared_ptr<casPV> >::iterator iter;

	iter = m_pv_map.find(pv_name);
	if (iter == m_pv_map.end())
		return pverDoesNotExistHere;

	return *(iter->second);
}

void SMSControl::addPV(boost::shared_ptr<casPV> pv)
{
	m_pv_map[pv->getName()] = pv;
}

bool SMSControl::setRecording(bool v)
{
	struct timespec now;

	/* If we didn't change state, only give an error if someone
	 * tried to start a new recording.
	 */
	if (v == m_recording)
		return !v;

	clock_gettime(CLOCK_REALTIME, &now);
	if (v) {
		/* Starting a new recording */
		/* TODO persist the run number */
		StorageManager::startRecording(m_nextRunNumber);
		m_pvRunNumber->update(m_nextRunNumber++, &now);
		m_pvRecording->update(v, &now);
	} else {
		/* Stop the current recording */
		StorageManager::stopRecording();
		m_pvRunNumber->update(0, &now);
		m_pvRecording->update(v, &now);
	}

	m_recording = v;
	return true;
}
