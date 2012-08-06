#ifndef __SMS_CONTROL_H
#define __SMS_CONTROL_H

#include <boost/smart_ptr.hpp>
#include <string>
#include <map>
#include <vector>
#include <bitset>

#include <casdef.h>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "Storage.h"

class smsRunNumberPV;
class smsRecordingPV;
class RunInfo;
class Geometry;
class DataSource;
class PixelMap;
class BeamlineInfo;
class MetaDataMgr;

class SMSControl : public caServer {
public:
	typedef boost::shared_ptr<casPV> PVSharedPtr;

	SMSControl(const std::string &beamlineId,
		   const std::string &beamlineShortName,
		   const std::string &beamlineLongName);
	~SMSControl();

	void show(unsigned level) const;

	pvExistReturn pvExistTest(const casCtx &, const caNetAddr &,
				  const char *pv_name);
	pvAttachReturn pvAttach(const casCtx &ctx, const char *pv_name);
	void addPV(PVSharedPtr pv);

	static SMSControl *getInstance(void) { return m_singleton; }

	void addSource(const std::string &uri);
	void sourceUp(uint32_t smsId);
	void sourceDown(uint32_t smsId);

	uint32_t registerEventSource(uint32_t hwId);
	void unregisterEventSource(uint32_t smsId);

	void pulseEvents(const ADARA::RawDataPkt &pkt, uint32_t hwId,
			 uint32_t dup);
	void pulseRTDL(const ADARA::RTDLPkt &pkt);

	void markPartial(uint64_t pulseId, uint32_t dup);
	void markComplete(uint64_t pulseId, uint32_t dup, uint32_t smsId);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &pkt,
			      uint32_t sourceId);
	void updateValue(const ADARA::VariableU32Pkt &pkt, uint32_t sourceId);
	void updateValue(const ADARA::VariableDoublePkt &pkt,
			 uint32_t sourceId);
	void updateValue(const ADARA::VariableStringPkt &pkt,
			 uint32_t sourceId);

private:
	typedef std::bitset<256> SourceSet;
	typedef std::pair<uint64_t, uint32_t> PulseIdentifier;

	typedef std::vector<ADARA::Event> EventVector;

	struct EventSource {
		EventSource(uint32_t intraPulse, uint32_t tofField,
			    uint32_t nBanks) :
				m_intraPulseTime(intraPulse),
				m_tofField(tofField),
				m_activeBanks(0),
				m_banks(nBanks, EventVector())
		{ }

		uint32_t			m_intraPulseTime;
		uint32_t			m_tofField;
		uint32_t			m_activeBanks;
		std::vector<EventVector>	m_banks;
	};

	typedef std::map<uint32_t, EventSource> SourceMap;

	struct Pulse {
		Pulse(const PulseIdentifier &id, const SourceSet &srcs) :
				m_id(id), m_pending(srcs), m_numEvents(0),
				m_numBanks(0), m_charge(0), m_cycle(0),
				m_ringPeriod(0), m_flags(0)
		{ }

		PulseIdentifier				m_id;
		SourceSet				m_pending;
		boost::shared_ptr<ADARA::RTDLPkt>	m_rtdl;
		SourceMap				m_sources;
		uint32_t				m_numEvents;
		uint32_t				m_numBanks;
		uint32_t				m_charge;
		uint32_t				m_cycle;
		uint32_t				m_ringPeriod;
		uint32_t				m_flags;
		// TODO fast meta updates

		/* We use bank 0 and 1 to store the error bank (-2) and
		 * unknown mapping bank (-1) respectively, so account
		 * for where the real detector banks start in m_banks
		 */
		enum { REAL_BANK_OFFSET = 2 };
	};

	typedef boost::shared_ptr<Pulse> PulsePtr;
	typedef std::map<PulseIdentifier, PulsePtr> PulseMap;

	std::map<std::string, PVSharedPtr> m_pv_map;
	uint32_t m_nextRunNumber;
	bool m_recording;
	uint32_t m_nextSrcId;
	boost::shared_ptr<smsRunNumberPV> m_pvRunNumber;
	boost::shared_ptr<smsRecordingPV> m_pvRecording;
	std::vector<boost::shared_ptr<DataSource> > m_sources;
	SourceSet m_activeSources;
	SourceSet m_eventSources;
	PulseMap m_pulses;
	uint32_t m_lastRingPeriod;
	uint32_t m_bankReserve;
	boost::shared_ptr<RunInfo> m_runInfo;
	boost::shared_ptr<Geometry> m_geometry;
	boost::shared_ptr<PixelMap> m_pixelMap;
	boost::shared_ptr<BeamlineInfo> m_beamlineInfo;
	boost::shared_ptr<MetaDataMgr> m_meta;

	uint32_t m_maxBanks;
	IoVector m_iovec;
	std::vector<uint32_t> m_hdrs;

	static SMSControl *m_singleton;
	static uint32_t m_ringPeriod;

	pvExistReturn pvExistTest(const casCtx &, const char *pv_name);

	bool setRecording(bool val);

	PulseMap::iterator getPulse(uint64_t id, uint32_t dup);
	void recordPulse(PulsePtr &pulse);
	bool mapEvent(uint32_t phys, uint32_t &logical, uint32_t &bank);

	friend class smsRecordingPV;
};

#endif /* __SMSCAS_H */
