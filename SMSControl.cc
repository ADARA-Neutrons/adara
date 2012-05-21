#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "StorageManager.h"
#include "DataSource.h"

#include <math.h>

SMSControl *SMSControl::m_singleton = NULL;

// TODO get from config file; this changes very infrequently, but will
// change after accelerator maintance and power upgrades.
uint32_t SMSControl::m_ringPeriod = 123456;

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

SMSControl::SMSControl(const std::string &beamline) :
	m_nextRunNumber(1), m_recording(false), m_nextSrcId(0),
	m_lastRingPeriod(0), m_numBanks(18), m_bankReserve(4096)
{
	if (m_singleton)
		throw std::runtime_error("SMSControl is a singleton");

	std::string prefix(beamline);
	prefix += ":SMS";

	m_pvRecording = boost::shared_ptr<smsRecordingPV>(new
						smsRecordingPV(prefix, this));
	m_pvRunNumber = boost::shared_ptr<smsRunNumberPV>(new
						smsRunNumberPV(prefix));

	addPV(m_pvRecording);
	addPV(m_pvRunNumber);

	/* TODO get old run number information from StorageManager */
	/* TODO m_numBanks will be set when we read in the bank map file,
	 * (and will need to have REAL_BANK_OFFSET added to it
	 */

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

void SMSControl::addSource(const std::string &uri)
{
	/* TODO will eventually need to configure fast metadata conversion,
	 * and if we expect to see pulse data from this source
	 */
	boost::shared_ptr<DataSource> src(new DataSource(uri, m_nextSrcId));
	m_sources.push_back(src);
	m_nextSrcId++;
	/* TODO check against the max number of sources? */
}

SMSControl::PulseMap::iterator SMSControl::getPulse(uint64_t id, uint32_t dup)
{
	PulseIdentifier pid(id, dup);
	PulseMap::iterator it;

	it = m_pulses.find(pid);
	if (it != m_pulses.end())
		return it;

	PulsePtr new_pulse(new Pulse(pid, m_activeSources, m_numBanks));

	if (dup)
		new_pulse->m_flags |= ADARA::BankedEventPkt::DUPLICATE_PULSE;

	return m_pulses.insert(make_pair(pid, new_pulse)).first;
}

bool SMSControl::mapEvent(uint32_t phys, uint32_t &logical, uint32_t &bank)
{
	/* TODO use pixel map to do mapping */
	logical = phys / 16;
	bank = phys % 16;

	/* XXX return true on error */
	return false;
}

void SMSControl::sourceUp(uint32_t id)
{
	m_activeSources.set(id);
}

void SMSControl::sourceDown(uint32_t id)
{
	PulseMap::iterator it, last;;

	/* Walk the pending pulses and mark them incomplete if they are still
	 * waiting for data form this source, as it's not going to come. Keep
	 * track of the last pulse that is completed by this process.
	 */
	last = m_pulses.end();
	for (it = m_pulses.begin(); it != m_pulses.end(); it++) {
		if (it->second->m_pending[id]) {
			it->second->m_flags |= ADARA::BankedEventPkt::PARTIAL_DATA;
			it->second->m_pending.reset(id);
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

	m_activeSources.reset(id);
}

void SMSControl::pulseEvents(const ADARA::RawDataPkt &pkt, uint32_t sourceId,
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

	/* We'll save this time and time again, but we can't use the one
	 * from the RTDL packet -- that was for the previous pulse.
	 */
	pulse->m_charge = pkt.pulseCharge();

	const ADARA::Event *events = pkt.events();
	uint32_t i, count = pkt.num_events();
	uint32_t phys, logical, bank;
	ADARA::Event translated;

	for (i = 0; i < count; i++) {
		phys = events[i].pixel;

		switch (phys >> 24) {
		case 0:
			if (mapEvent(phys, logical, bank))
				pulse->m_flags |= ADARA::BankedEventPkt::MAPPING_ERROR;
			bank += Pulse::REAL_BANK_OFFSET;
			break;
		case 7:
			/* TODO handle chopper events */
		case 4:
			/* TODO handle beam monitor */
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

		if (pulse->m_banks[bank].empty()) {
			pulse->m_activeBanks++;
			pulse->m_banks[bank].reserve(m_bankReserve);
		}

		translated.pixel = logical;
		translated.tof = events[i].tof;
		pulse->m_banks[bank].push_back(translated);
	}

	pulse->m_numEvents += count;
}

void SMSControl::pulseRTDL(const ADARA::RTDLPkt &pkt, uint32_t sourceId,
			   uint32_t dup)
{
	PulsePtr &pulse = getPulse(pkt.pulseId(), dup)->second;

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
			      uint32_t sourceId)
{
	PulseMap::iterator current = getPulse(pulseId, dup);
	PulsePtr &pulse = current->second;

	pulse->m_pending.reset(sourceId);
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

	if (pulse->m_rtdl) {
		/* Don't notify clients; we want to keep the banked event
		 * packet with the RTDL packet.
		 */
		StorageManager::addPacket(pulse->m_rtdl->packet(),
					  pulse->m_rtdl->packet_length(),
					  false);
	} else
		pulse->m_flags |= ADARA::BankedEventPkt::MISSING_RTDL;

	IoVector iovec(1 + (2 * pulse->m_activeBanks));
	uint32_t *data = new uint32_t[9 + (3 * pulse->m_activeBanks)];
	uint32_t *section_hdr;
	uint32_t i, ioidx;
	std::vector<EventVector> &banks = pulse->m_banks;

	data[0] = 9 + (3 * pulse->m_activeBanks) + (2 * pulse->m_numEvents);
	data[0] *= sizeof(uint32_t);

	data[1] = ADARA::PacketType::BANKED_EVENT_V0;
	data[2] = pulse->m_id.first >> 32;
	data[3] = pulse->m_id.first;

	data[4] = pulse->m_charge;
	data[5] = pulseEnergy(pulse->m_ringPeriod);
	data[6] = pulse->m_ringPeriod;
	data[7] = pulse->m_cycle;
	data[8] = pulse->m_flags;

	ioidx = 0;
	iovec[ioidx].iov_base = data;
	iovec[ioidx++].iov_len = 9 * sizeof(uint32_t);

	section_hdr = &data[9];
	for (i = 0; i < banks.size(); i++) {
		if (banks[i].empty())
			continue;

		iovec[ioidx].iov_base = section_hdr;
		iovec[ioidx++].iov_len = 2 * sizeof(uint32_t);

		/* Because we're using unsigned integers, we'll translate
		 * the error pixels to bank -2 (0xfffffffe), and the
		 * unmapped pixels to bank -1 (0xffffffff). All other
		 * bank ids will get their real number.
		 */
		*section_hdr++ = i - Pulse::REAL_BANK_OFFSET;
		*section_hdr++ = banks[i].size();

		iovec[ioidx].iov_base = &banks[i].front();
		iovec[ioidx++].iov_len = banks[i].size() * sizeof(ADARA::Event);
	}

	StorageManager::addPacket(iovec);

	/* TODO update history of bank event counts and size m_bankReserve
	 * accordingly to try to avoid reallocation events.
	 */
}
