#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "StorageManager.h"
#include "DataSource.h"
#include "RunInfo.h"
#include "Geometry.h"
#include "PixelMap.h"
#include "BeamlineInfo.h"
#include "MetaDataMgr.h"

#include "Logging.h"

#include <math.h>

static LoggerPtr logger(Logger::getLogger("SMS.Control"));

SMSControl *SMSControl::m_singleton = NULL;

static uint32_t pulseEnergy(uint32_t ringPeriod)
{
	double ring_circumference = 248; // meters
	double period = ringPeriod * 1e-12; // seconds
	double v = ring_circumference / period; // m/s
	double c = 299792458; // m/s
	double beta = v / c;
	double E0 = 938.257e6; // rest energy of proton, eV

	/* Return pulse energy in eV */
	return (E0 / sqrt(1 - (beta * beta))) - E0;
}

SMSControl::SMSControl(const std::string &beamlineId,
		       const std::string &beamlineShortName,
		       const std::string &beamlineLongName) :
	m_currentRunNumber(0), m_recording(false), m_nextSrcId(1),
	m_lastRingPeriod(0), m_bankReserve(4096), m_meta(new MetaDataMgr)
{
	if (m_singleton)
		throw std::runtime_error("SMSControl is a singleton");

	std::string prefix(beamlineId);
	prefix += ":SMS";

	m_pvRecording = boost::shared_ptr<smsRecordingPV>(new
						smsRecordingPV(prefix, this));
	m_pvRunNumber = boost::shared_ptr<smsRunNumberPV>(new
						smsRunNumberPV(prefix));

	addPV(m_pvRecording);
	addPV(m_pvRunNumber);

	m_nextRunNumber = StorageManager::getNextRun();
	if (!m_nextRunNumber)
		throw std::runtime_error("Unable to get next run number");

	m_beamlineInfo.reset(new BeamlineInfo(beamlineId, beamlineShortName,
					      beamlineLongName));
	m_runInfo.reset(new RunInfo(beamlineId, this));
	m_geometry.reset(new Geometry("/SNSlocal/sms/conf/geometry.xml"));
	m_pixelMap.reset(new PixelMap("/SNSlocal/sms/conf/pixelmap"));

	m_maxBanks = m_pixelMap->numBanks() + Pulse::REAL_BANK_OFFSET;

	m_singleton = this;
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

void SMSControl::addPV(PVSharedPtr pv)
{
	if (m_pv_map.count(pv->getName())) {
		std::string msg("Adding duplicate PV: ");
		msg += pv->getName();
		throw std::logic_error(msg);
	}
	m_pv_map[pv->getName()] = pv;
}

bool SMSControl::setRecording(bool v)
{
	struct timespec now;

	/* We return true if we accepted the setting, and false if not.
	 * It is not an error for a caller to try to stop recording if
	 * we aren't actually recording (so return true), but it is an
	 * error to try to start recording when we already are -- return
	 * false for that case.
	 *
	 * TODO don't allow recording to start unless we have all required
	 * fields from RunInfo.
	 */
	if (v == m_recording)
		return !v;

	clock_gettime(CLOCK_REALTIME, &now);
	if (v) {
		/* Starting a new recording */
		if (StorageManager::updateNextRun(m_nextRunNumber + 1)) {
			ERROR("Unable to increment run number, not starting");
			return false;
		}

		/* We've updated the run on disk, so if we fail now, we need
		 * to fail big.
		 */
		m_currentRunNumber = m_nextRunNumber++;
		INFO("Starting run " << m_currentRunNumber);
		m_runInfo->lock();
		m_runInfo->setRunNumber(m_currentRunNumber);
		StorageManager::startRecording(m_currentRunNumber);
		m_pvRunNumber->update(m_currentRunNumber, &now);
		m_pvRecording->update(v, &now);
	} else {
		/* Stop the current recording */
		INFO("Stopping run " << m_currentRunNumber);
		m_runInfo->setRunNumber(0);
		m_runInfo->unlock();
		StorageManager::stopRecording();
		m_pvRunNumber->update(0, &now);
		m_pvRecording->update(v, &now);
	}

	m_recording = v;
	return true;
}

void SMSControl::addSource(const std::string &uri)
{
	/* TODO will eventually need to configure fast metadata conversion,
	 * and if we expect to see pulse data from this source
	 */
	boost::shared_ptr<DataSource> src(new DataSource(uri, m_nextSrcId));
	m_sources.push_back(src);

	/* We save source ID 0 for internal use. */
	m_nextSrcId++;
	/* TODO check against the max number of sources? */
}

uint32_t SMSControl::registerEventSource(uint32_t hwId)
{
	/* We're called when a data source discovers a new hardware
	 * source id and needs to allocate a bit position for completing
	 * pulses. We don't have to be terribly fast here.
	 */
	size_t i, max = m_eventSources.size();
	for (i = 0; i < max; i++) {
		if (!m_eventSources[i]) {
			m_eventSources.set(i);
			return i;
		}
	}

	throw std::runtime_error("No more event sources available");
}

void SMSControl::unregisterEventSource(uint32_t smsId)
{
	PulseMap::iterator it, last;;

	/* Walk the pending pulses and mark them incomplete if they are still
	 * waiting for data form this source, as it's not going to come. Keep
	 * track of the last pulse that is completed by this process.
	 */
	last = m_pulses.end();
	for (it = m_pulses.begin(); it != m_pulses.end(); it++) {
		if (it->second->m_pending[smsId]) {
			it->second->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
			it->second->m_pending.reset(smsId);
			if (it->second->m_pending.none())
				last = it;
		}
	}

	if (last != m_pulses.end()) {
		/* Ok, we had at least one pulse completed by the recently
		 * departed source; all pulses previous to that one must be
		 * complete now as well, as the monotonically increasing
		 * pulse ids indicate that they were duplicate pulses.
		 */
		for (it = m_pulses.begin(); it != last; it++)
			recordPulse(it->second);
		recordPulse(last->second);
		m_pulses.erase(m_pulses.begin(), ++last);
	}

	/* Mark this id for re-use. */
	m_eventSources.reset(smsId);
}

SMSControl::PulseMap::iterator SMSControl::getPulse(uint64_t id, uint32_t dup)
{
	PulseIdentifier pid(id, dup);
	PulseMap::iterator it;

	it = m_pulses.find(pid);
	if (it != m_pulses.end())
		return it;

	PulsePtr new_pulse(new Pulse(pid, m_eventSources));

	if (dup)
		new_pulse->m_flags |= ADARA::BankedEventPkt::DUPLICATE_PULSE;

	return m_pulses.insert(make_pair(pid, new_pulse)).first;
}

void SMSControl::sourceUp(uint32_t id)
{
	// XXX still needed?
	m_activeSources.set(id);
}

void SMSControl::sourceDown(uint32_t id)
{
	// XXX only really needed to reset the metadata
	m_activeSources.reset(id);
	m_meta->dropTag(id);
}

void SMSControl::addMonitorEvent(const ADARA::RawDataPkt &pkt, PulsePtr &pulse,
				 uint32_t pixel, uint32_t tof)
{
	uint32_t trailing = (pixel & 1) << 31;
	tof |= trailing;

	pixel >>= 16;
	pixel &= 0xff;

	MonitorMap::iterator mon = pulse->m_monitors.find(pixel);
	if (mon == pulse->m_monitors.end()) {
		/* One hopes that an optimizing compiler would remove
		 * the unneeded constructions and copies...
		 */
		BeamMonitor new_mon(pkt.sourceID(), pkt.tofField());
		MonitorMap::value_type val(pixel, new_mon);
		mon = pulse->m_monitors.insert(val).first;
	}

	mon->second.m_eventTof.push_back(tof);
}

void SMSControl::pulseEvents(const ADARA::RawDataPkt &pkt, uint32_t hwId,
			     uint32_t dup)
{
	PulsePtr &pulse = getPulse(pkt.pulseId(), dup)->second;

	if (!pulse->m_rtdl) {
		/* Hmm, no RTDL; save the raw packet's concept of values
		 * we'll need for the banked event packets.
		 */
		pulse->m_cycle = pkt.cycle();
		pulse->m_ringPeriod = m_lastRingPeriod;

		/* TODO handle pkt.badCycle() and pkt.badVeto(), etc. ?? */
	}

	/* Find this source in the current pulse; if it doesn't exist
	 * yet, we'll need to save the intrapulse time and TOF Offset fields.
	 */
	SourceMap::iterator src = pulse->m_sources.find(hwId);
	if (src == pulse->m_sources.end()) {
		/* One hopes that an optimizing compiler would remove
		 * the unneeded constructions and copies...
		 */
		EventSource new_src(pkt.intraPulseTime(), pkt.tofField(),
				    m_maxBanks);
		SourceMap::value_type val(hwId, new_src);
		src = pulse->m_sources.insert(val).first;
	}

	/* We'll save this time and time again, but we can't use the one
	 * from the RTDL packet -- that was for the previous pulse.
	 * XXX validate that assertion when we have beam again
	 */
	pulse->m_charge = pkt.pulseCharge();

	const ADARA::Event *events = pkt.events();
	uint32_t i, count = pkt.num_events();
	uint32_t phys, logical;
	uint16_t bank;
	ADARA::Event translated;

	for (i = 0; i < count; i++) {
		phys = events[i].pixel;

		switch (phys >> 28) {
		case 4:
			/* Add this event to our monitors, and go on to the
			 * next raw event -- it doesn't go in the banked
			 * events section.
			 */
			addMonitorEvent(pkt, pulse, phys, events[i].tof);
			pulse->m_numMonEvents++;
			continue;
		case 0:
			if (m_pixelMap->mapEvent(phys, logical, bank))
				pulse->m_flags |= ADARA::BankedEventPkt::MAPPING_ERROR;
			bank += Pulse::REAL_BANK_OFFSET;
			break;
		case 7:
			/* TODO handle chopper events */
		case 5: case 6:
			/* TODO handle fast metadata */
		case 1: case 2: case 3:
			/* Unused sources, let them drop into error handling */
		default:
			/* Error bit is set, identity map and put in the
			 * error bank
			 */
			logical = phys;
			bank = 0;

			pulse->m_flags |= ADARA::BankedEventPkt::ERROR_PIXELS;
		}

		if (src->second.m_banks[bank].empty()) {
			/* pulse->m_numBanks will double count banks if
			 * their events arrive via two different HW sources.
			 * This is good, as we'll need the room in the packet
			 * for a bank section header in each source section.
			 */
			pulse->m_numBanks++;
			src->second.m_activeBanks++;
			src->second.m_banks[bank].reserve(m_bankReserve);
		}

		translated.pixel = logical;
		translated.tof = events[i].tof;
		src->second.m_banks[bank].push_back(translated);
		pulse->m_numEvents++;
	}
}

void SMSControl::pulseRTDL(const ADARA::RTDLPkt &pkt)
{
	PulsePtr &pulse = getPulse(pkt.pulseId(), 0)->second;

	/* We don't log about an existing RTDL packet: we'll always have
	 * one if there is more than one pre-processor on the beam line.
	 */
	if (pulse->m_rtdl)
		return;

	/* Save off information about this pulse for the incoming pulse.
	 * We don't save the pulse charge here, as that is for the
	 * previous pulse.
	 */
	pulse->m_cycle = pkt.cycle();
	pulse->m_ringPeriod = pkt.ringPeriod();

	m_lastRingPeriod = pkt.ringPeriod();

	/* TODO handle pkt.badCycle() and pkt.badVeto(), etc. ?? */

	pulse->m_rtdl.reset(new ADARA::RTDLPkt(pkt));
}

void SMSControl::markPartial(uint64_t pulseId, uint32_t dup)
{
	PulsePtr &pulse = getPulse(pulseId, dup)->second;

	pulse->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
}

void SMSControl::markComplete(uint64_t pulseId, uint32_t dup,
			      uint32_t smsId)
{
	PulseMap::iterator current = getPulse(pulseId, dup);
	PulsePtr &pulse = current->second;

	pulse->m_pending.reset(smsId);
	if (pulse->m_pending.any())
		return;

	/* This pulse has now been marked complete by all active data sources.
	 * As we expect to get monotonically increasing pulse ids, all
	 * previously incomplete pulses can be considered to be partial
	 * pulses -- one or more data sources had a duplicated pulse or
	 * otherwise did not send data for that pulse.
	 *
	 * We can now walk from the beginning of the pending pulses to
	 * the current one, pushing the data to storage.
	 */
	for (PulseMap::iterator it = m_pulses.begin(); it != current; it++) {
		it->second->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
		recordPulse(it->second);
	}

	recordPulse(current->second);
	m_pulses.erase(m_pulses.begin(), ++current);
}

void SMSControl::recordPulse(PulsePtr &pulse)
{
	/* Send the RTDL packet, followed by the banked event packet */
	/* TODO send fast metadata packets as well, perhaps before events */

	// XXX avoid sending the RTDL for a pulse twice (if duplicated)

	if (pulse->m_rtdl) {
		/* Don't notify clients; we want to keep the banked event
		 * packet with the RTDL packet.
		 */
		StorageManager::addPacket(pulse->m_rtdl->packet(),
					  pulse->m_rtdl->packet_length(),
					  false);
	} else
		pulse->m_flags |= ADARA::BankedEventPkt::MISSING_RTDL;

	buildMonitorPacket(pulse);
	buildBankedPacket(pulse);
}

void SMSControl::buildMonitorPacket(PulsePtr &pulse)
{
	m_iovec.clear();
	m_hdrs.clear();

	m_iovec.reserve(1 + pulse->m_monitors.size() * 2);

	/* IMPORTANT: m_hdrs must be correctly sized, as we use pointers
	 * into the vector in the iovec(s) we submit to
	 * StorageManager::addPacket(). No reallocation is allowed after
	 * we've reserved the proper size.
	 */
	uint32_t size = 8 + pulse->m_monitors.size() * 3;
	m_hdrs.reserve(size);

	/* Common ADARA packet header */
	size += pulse->m_numMonEvents;
	size *= sizeof(uint32_t);
	size -= sizeof(ADARA::Header);
	m_hdrs.push_back(size);
	m_hdrs.push_back(ADARA::PacketType::BEAM_MONITOR_EVENT_V0);
	m_hdrs.push_back(pulse->m_id.first >> 32);
	m_hdrs.push_back(pulse->m_id.first);

	/* Beam monitor event header */
	m_hdrs.push_back(pulse->m_charge);
	m_hdrs.push_back(pulseEnergy(pulse->m_ringPeriod));
	m_hdrs.push_back(pulse->m_cycle);
	m_hdrs.push_back(pulse->m_flags);

	struct iovec iov;
	iov.iov_base = &m_hdrs.front();
	iov.iov_len = m_hdrs.size() * sizeof(uint32_t);
	m_iovec.push_back(iov);

	MonitorMap::iterator mIt, mEnd = pulse->m_monitors.end();
	for (mIt = pulse->m_monitors.begin(); mIt != mEnd; mIt++) {
		iov.iov_base = &m_hdrs.front() + m_hdrs.size();
		iov.iov_len = 3 * sizeof(uint32_t);
		m_iovec.push_back(iov);

		BeamMonitor &mon = mIt->second;
		uint32_t id_cnt = mIt->first << 22;
		id_cnt |= mon.m_eventTof.size();
		m_hdrs.push_back(id_cnt);
		m_hdrs.push_back(mon.m_sourceId);
		m_hdrs.push_back(mon.m_tofField);

		iov.iov_base = &mon.m_eventTof.front();
		iov.iov_len = mon.m_eventTof.size();
		iov.iov_len *= sizeof(uint32_t);
		m_iovec.push_back(iov);
	}

	StorageManager::addPacket(m_iovec);

	/* TODO update history of monitor event counts and preallocate
	 * accordingly to try to avoid reallocation events
	 */
}

void SMSControl::buildBankedPacket(PulsePtr &pulse)
{
	m_iovec.clear();
	m_hdrs.clear();

	uint32_t size = 1 + pulse->m_numBanks * 2;
	size += pulse->m_sources.size() * 4;
	m_iovec.reserve(size);

	/* IMPORTANT: m_hdrs must be correctly sized, as we use pointers
	 * into the vector in the iovec(s) we submit to
	 * StorageManager::addPacket(). No reallocation is allowed after
	 * we've reserved the proper size.
	 */
	size = 8 + pulse->m_sources.size() * 4;
	size += pulse->m_numBanks * 2;
	m_hdrs.reserve(size);

	/* Common ADARA packet header */
	size *= sizeof(uint32_t);
	size += pulse->m_numEvents * sizeof(ADARA::Event);
	size -= sizeof(ADARA::Header);
	m_hdrs.push_back(size);
	m_hdrs.push_back(ADARA::PacketType::BANKED_EVENT_V0);
	m_hdrs.push_back(pulse->m_id.first >> 32);
	m_hdrs.push_back(pulse->m_id.first);

	/* Banked event header */
	m_hdrs.push_back(pulse->m_charge);
	m_hdrs.push_back(pulseEnergy(pulse->m_ringPeriod));
	m_hdrs.push_back(pulse->m_cycle);
	m_hdrs.push_back(pulse->m_flags);

	struct iovec iov;
	iov.iov_base = &m_hdrs.front();
	iov.iov_len = m_hdrs.size() * sizeof(uint32_t);
	m_iovec.push_back(iov);

	SourceMap::iterator sIt, sEnd = pulse->m_sources.end();
	for (sIt = pulse->m_sources.begin(); sIt != sEnd; sIt++) {
		iov.iov_base = &m_hdrs.front() + m_hdrs.size();
		iov.iov_len = 4 * sizeof(uint32_t);
		m_iovec.push_back(iov);

		EventSource &src = sIt->second;
		m_hdrs.push_back(sIt->first);
		m_hdrs.push_back(src.m_intraPulseTime);
		m_hdrs.push_back(src.m_tofField);
		m_hdrs.push_back(src.m_activeBanks);

		for (uint32_t i = 0; i < src.m_banks.size(); i++) {
			if (src.m_banks[i].empty())
				continue;

			iov.iov_base = &m_hdrs.front() + m_hdrs.size();
			iov.iov_len = 2 * sizeof(uint32_t);
			m_iovec.push_back(iov);

			/* Because we're using unsigned integers,
			 * we'll translate the error pixels to bank
			 * -2 (0xfffffffe), and the unmapped pixels
			 * to bank -1 (0xffffffff). All other bank
			 * ids will get their real number.
			 */
			m_hdrs.push_back(i - Pulse::REAL_BANK_OFFSET);
			m_hdrs.push_back(src.m_banks[i].size());

			iov.iov_base = &src.m_banks[i].front();
			iov.iov_len = src.m_banks[i].size();
			iov.iov_len *= sizeof(ADARA::Event);
			m_iovec.push_back(iov);
		}
	}

	StorageManager::addPacket(m_iovec);

	/* TODO update history of bank event counts and size m_bankReserve
	 * accordingly to try to avoid reallocation events.
	 */
}

void SMSControl::updateDescriptor(const ADARA::DeviceDescriptorPkt &pkt,
				  uint32_t sourceId)
{
	m_meta->updateDescriptor(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableU32Pkt &pkt,
			     uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableDoublePkt &pkt,
			     uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}

void SMSControl::updateValue(const ADARA::VariableStringPkt &pkt,
			     uint32_t sourceId)
{
	m_meta->updateValue(pkt, sourceId);
}
