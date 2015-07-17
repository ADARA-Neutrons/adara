#ifndef __SMS_CONTROL_H
#define __SMS_CONTROL_H

// NEVER USE THIS...
//    - it turns out Boost Pool kinda sux (at least this latest version)
//       -> if things go awry, reallocation/unallocation TAKES FOREVER!!!
#define DO_POOL

#include <boost/property_tree/ptree.hpp>
#ifdef DO_POOL
#include <boost/pool/pool_alloc.hpp>
#endif
#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <bitset>
#include <set>

#include <casdef.h>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "Storage.h"

class smsStringPV;
class smsRunNumberPV;
class smsRecordingPV;
class smsErrorPV;
class smsUint32PV;
class smsConnectedPV;
class PopPulseBufferPV;
class CleanShutdownPV;
class RunInfo;
class Geometry;
class DataSource;
class PixelMap;
class BeamlineInfo;
class BeamMonitorConfig;
class DetectorBankSet;
class MetaDataMgr;
class FastMeta;
class Markers;

class SMSControl : public caServer {
public:
	typedef boost::shared_ptr<casPV> PVSharedPtr;

	void show(unsigned level) const;

	pvExistReturn pvExistTest(const casCtx &, const caNetAddr &,
				  const char *pv_name);
	pvAttachReturn pvAttach(const casCtx &ctx, const char *pv_name);
	void addPV(PVSharedPtr pv);

	static SMSControl *getInstance(void) { return m_singleton; }

	uint32_t getTargetNumber(void) { return m_targetNumber; }

	std::string getBeamlineId(void) { return m_beamlineId; }

	void sourceUp(uint32_t smsId);
	void sourceDown(uint32_t smsId);

	uint32_t registerEventSource(uint32_t hwId);
	void unregisterEventSource(uint32_t smsId);

	void pulseEvents(const ADARA::RawDataPkt &pkt,
			uint32_t hwId, uint32_t dup, bool is_mapped);
	void pulseRTDL(const ADARA::RTDLPkt &pkt, uint32_t dup);

	void markPartial(uint64_t pulseId, uint32_t dup);
	void markComplete(uint64_t pulseId, uint32_t dup, uint32_t smsId);

	void popPulseBuffer(int32_t pulse_index);

	void resetSourcesReadDelay(void);
	void setSourcesReadDelay(void);

	void resetPacketStats(void);

	int32_t registerLiveClient(std::string clientName,
			boost::shared_ptr<smsStringPV> & pvName,
			boost::shared_ptr<smsUint32PV> & pvRequestedStartTime,
			boost::shared_ptr<smsStringPV> & pvCurrentFilePath,
			boost::shared_ptr<smsConnectedPV> & pvStatus);
	void unregisterLiveClient(int32_t clientId);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::VariableU32Pkt &pkt, uint32_t sourceId);
	void updateValue(const ADARA::VariableDoublePkt &pkt,
			 uint32_t sourceId);
	void updateValue(const ADARA::VariableStringPkt &pkt,
			 uint32_t sourceId);

	static void config(const boost::property_tree::ptree &conf);
	static void init(void);
	static void late_config(const boost::property_tree::ptree &conf);

private:
	SMSControl();
	~SMSControl();

	typedef std::bitset<256> SourceSet;
	typedef std::pair<uint64_t, uint32_t> PulseIdentifier;

	typedef std::vector<ADARA::Event
#ifdef DO_POOL
		, boost::pool_allocator<ADARA::Event,
			boost::default_user_allocator_new_delete,
			boost::details::pool::default_mutex,
			100000>
#endif
		> EventVector;

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
		std::vector<EventVector
#ifdef DO_POOL
			, boost::pool_allocator<EventVector,
				boost::default_user_allocator_new_delete,
				boost::details::pool::default_mutex,
				128>
#endif
			> m_banks;
	};

#ifdef DO_POOL
	typedef boost::fast_pool_allocator<
		std::pair<uint32_t, EventSource>,
		boost::default_user_allocator_new_delete,
		boost::details::pool::default_mutex,
		16> SourceMapAlloc;
#endif

	typedef std::map<uint32_t, EventSource
#ifdef DO_POOL
		, std::less<uint32_t>, SourceMapAlloc
#endif
		> SourceMap;

	struct BeamMonitor {
		BeamMonitor(uint32_t srcId, uint32_t tofField) :
				m_sourceId(srcId), m_tofField(tofField)
		{ }

		uint32_t			m_sourceId;
		uint32_t			m_tofField;
		std::vector<uint32_t
#ifdef DO_POOL
			, boost::pool_allocator<uint32_t,
				boost::default_user_allocator_new_delete,
				boost::details::pool::default_mutex,
				100000>
#endif
			> m_eventTof;
	};

#ifdef DO_POOL
	typedef boost::fast_pool_allocator<
		std::pair<uint32_t, BeamMonitor>,
		boost::default_user_allocator_new_delete,
		boost::details::pool::default_mutex,
		16> MonitorMapAlloc;
#endif

	typedef std::map<uint32_t, BeamMonitor
#ifdef DO_POOL
		, std::less<uint32_t>, MonitorMapAlloc
#endif
		> MonitorMap;

	typedef std::vector<uint32_t
#ifdef DO_POOL
		, boost::pool_allocator<uint32_t,
			boost::default_user_allocator_new_delete,
			boost::details::pool::default_mutex,
			10000>
#endif
		> ChopperEvents;

#ifdef DO_POOL
	typedef boost::fast_pool_allocator<
		std::pair<uint32_t, ChopperEvents>,
		boost::default_user_allocator_new_delete,
		boost::details::pool::default_mutex,
		16> ChopperMapAlloc;
#endif

	typedef std::map<uint32_t, ChopperEvents
#ifdef DO_POOL
		, std::less<uint32_t>, ChopperMapAlloc
#endif
		> ChopperMap;

	struct Pulse {
		Pulse(const PulseIdentifier &id, const SourceSet &srcs) :
				m_id(id), m_pending(srcs),
				m_numEvents(0), m_numBanks(0), m_numMonEvents(0),
				m_charge(0), m_vetoFlags(0), m_cycle(0),
				m_ringPeriod(0), m_flags(0)
		{ }

		PulseIdentifier			m_id;
		SourceSet				m_pending;
		boost::shared_ptr<ADARA::RTDLPkt>	m_rtdl;
		SourceMap				m_pulseSources;
		MonitorMap				m_monitors;
		ChopperMap				m_chopperEvents;
		EventVector				m_fastMetaEvents;
		uint32_t				m_numEvents;
		uint32_t				m_numBanks;
		uint32_t				m_numMonEvents;
		uint32_t				m_charge;
		uint32_t				m_vetoFlags;
		uint32_t				m_cycle;
		uint32_t				m_ringPeriod;
		uint32_t				m_flags;

		/* We use bank 0 and 1 to store the error bank (-2) and
		 * unknown mapping bank (-1) respectively, so account
		 * for where the real detector banks start in m_banks
		 */
		enum { REAL_BANK_OFFSET = 2 };
	};

	MonitorMap				m_allMonitors;

	typedef boost::shared_ptr<Pulse> PulsePtr;

#ifdef DO_POOL
	typedef boost::fast_pool_allocator<
		std::pair<PulseIdentifier, PulsePtr>,
		boost::default_user_allocator_new_delete,
		boost::details::pool::default_mutex,
		1000> PulseMapAlloc;
#endif

	typedef std::map<PulseIdentifier, PulsePtr
#ifdef DO_POOL
		, std::less<PulseIdentifier>, PulseMapAlloc
#endif
		> PulseMap;

	std::map<std::string, PVSharedPtr> m_pv_map;
	uint32_t m_nextRunNumber;
	uint32_t m_currentRunNumber;
	bool m_recording;
	uint32_t m_nextSrcId;
	boost::shared_ptr<smsStringPV> m_pvVersion;
	boost::shared_ptr<smsRunNumberPV> m_pvRunNumber;
	boost::shared_ptr<smsRecordingPV> m_pvRecording;
	boost::shared_ptr<smsErrorPV> m_pvSummary;
	std::vector<boost::shared_ptr<DataSource> > m_dataSources;
	SourceSet m_activeSources;
	SourceSet m_eventSources;
	SourceSet m_liveClients;
	PulseMap m_pulses;
	uint64_t m_lastPulseId;
	uint32_t m_lastRingPeriod;
	uint32_t m_bankReserve;
	boost::shared_ptr<RunInfo> m_runInfo;
	boost::shared_ptr<Geometry> m_geometry;
	boost::shared_ptr<PixelMap> m_pixelMap;
	boost::shared_ptr<BeamlineInfo> m_beamlineInfo;
	boost::shared_ptr<BeamMonitorConfig> m_bmonConfig;
	boost::shared_ptr<DetectorBankSet> m_detBankSets;
	boost::shared_ptr<MetaDataMgr> m_meta;
	boost::shared_ptr<FastMeta> m_fastmeta;
	boost::shared_ptr<Markers> m_markers;
	std::set<uint32_t> m_choppers;

	uint32_t m_maxBanks;
	IoVector m_iovec;
	std::vector<uint32_t> m_hdrs;

	static uint32_t m_targetNumber;

	static std::string m_version;
	static std::string m_beamlineId;
	static std::string m_beamlineShortName;
	static std::string m_beamlineLongName;
	static std::string m_geometryPath;
	static std::string m_pixelMapPath;

	boost::shared_ptr<smsUint32PV> m_pvNoEoPPulseBufferSize;
	static uint32_t m_noEoPPulseBufferSize;

	boost::shared_ptr<PopPulseBufferPV> m_pvPopPulseBuffer;

	boost::shared_ptr<smsUint32PV> m_pvNumDataSources;

	boost::shared_ptr<CleanShutdownPV> m_pvCleanShutdown;

	boost::shared_ptr<smsUint32PV> m_pvNumLiveClients;

	std::vector< boost::shared_ptr<smsStringPV> > m_pvLiveClientNames;
	std::vector< boost::shared_ptr<smsUint32PV> > m_pvLiveClientStartTimes;
	std::vector< boost::shared_ptr<smsStringPV> > m_pvLiveClientFilePaths;
	std::vector< boost::shared_ptr<smsConnectedPV> > m_pvLiveClientStatuses;

	static SMSControl *m_singleton;

	pvExistReturn pvExistTest(const casCtx &, const char *pv_name);

	void addSources(const boost::property_tree::ptree &conf);
	void addSource(const std::string &name,
				const boost::property_tree::ptree &info, bool enabled);
	bool setRecording(bool val);

	PulseMap::iterator getPulse(uint64_t id, uint32_t dup);
	void recordPulse(PulsePtr &pulse);
	bool mapEvent(uint32_t phys, uint32_t &logical, uint32_t &bank);
	void addMonitorEvent(const ADARA::RawDataPkt &pkt, PulsePtr &pulse,
				uint32_t id, uint32_t tof);
	void addChopperEvent(const ADARA::RawDataPkt &pkt, PulsePtr &pulse,
				uint32_t id, uint32_t tof);

	void buildBankedPacket(PulsePtr &pulse);
	void buildMonitorPacket(PulsePtr &pulse);
	void buildChopperPackets(PulsePtr &pulse);
	void buildFastMetaPackets(PulsePtr &pulse);

	friend class smsRecordingPV;
};

#endif /* __SMSCAS_H */
