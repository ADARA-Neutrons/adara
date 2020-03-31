#ifndef __SMS_CONTROL_H
#define __SMS_CONTROL_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <bitset>
#include <set>

#include <casdef.h>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "Storage.h"

class smsStringPV;
class smsRunNumberPV;
class smsRecordingPV;
class smsErrorPV;
class smsBooleanPV;
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

#define SOURCE_SET_SIZE (256)

	typedef std::bitset<SOURCE_SET_SIZE> SourceSet;

	typedef boost::shared_ptr<casPV> PVSharedPtr;

	void show(unsigned level) const;

	pvExistReturn pvExistTest(const casCtx &, const caNetAddr &,
				  const char *pv_name);
	pvAttachReturn pvAttach(const casCtx &ctx, const char *pv_name);
	void addPV(PVSharedPtr pv);

	static SMSControl *getInstance(void) { return m_singleton; }

	std::string getFacility(void) { return m_facility; }

	std::string getBeamlineId(void) { return m_beamlineId; }

	void sourceUp(uint32_t srcId);
	void sourceDown(uint32_t srcId, bool stateChanged);

	uint32_t registerEventSource(uint32_t srcId, uint32_t hwId);
	void unregisterEventSource(uint32_t srcId, uint32_t smsId);

	void pulseEvents(const ADARA::RawDataPkt &pkt,
			uint32_t hwId, uint32_t dup,
			bool is_mapped, bool mixed_data_packets,
			uint32_t &event_count, uint32_t &meta_count,
			uint32_t &err_count);

	void pulseRTDL(const ADARA::RTDLPkt &pkt, uint32_t dup);

	void markPartial(uint64_t pulseId, uint32_t dup);
	void markComplete(uint64_t pulseId, uint32_t dup, uint32_t smsId);

	void popPulseBuffer(int32_t pulse_index);

	void resetSourcesReadDelay(void);
	void setSourcesReadDelay(void);

	uint32_t getIntermittentDataThreshold(void)
		{ return m_intermittentDataThreshold; }

	void resetPacketStats(void);

	int32_t registerLiveClient(std::string clientName,
			boost::shared_ptr<smsStringPV> & pvName,
			boost::shared_ptr<smsUint32PV> & pvRequestedStartTime,
			boost::shared_ptr<smsStringPV> & pvCurrentFilePath,
			boost::shared_ptr<smsConnectedPV> & pvStatus);
	void unregisterLiveClient(int32_t clientId);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &pkt,
			uint32_t sourceId);

	void updateValue(const ADARA::VariableU32Pkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::VariableDoublePkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::VariableStringPkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::VariableU32ArrayPkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::VariableDoubleArrayPkt &pkt,
			uint32_t sourceId);

	bool getRecording(void);

	void pauseRecording(void);
	void resumeRecording(void);

	void updateValidRunInfo(bool isValid, std::string why,
			bool changedValid);

	void updateDataSourceConnectivity(void);

	boost::shared_ptr<Markers> getMarkers(void) { return m_markers; }

	static void config(const boost::property_tree::ptree &conf);
	static void init(void);
	static void late_config(const boost::property_tree::ptree &conf);

private:
	SMSControl();
	~SMSControl();

	typedef std::pair<uint64_t, uint32_t> PulseIdentifier;

	typedef std::vector<ADARA::Event> EventVector;

	typedef std::pair<uint32_t, uint32_t> BankIndex;

	typedef std::map< BankIndex, EventVector > BankMap;

	struct EventSource {
		EventSource( uint32_t intraPulse, uint32_t tofField ) :
				m_intraPulseTime(intraPulse),
				m_tofField(tofField),
				m_activeBanks(0)
		{ }

		uint32_t			m_intraPulseTime;
		uint32_t			m_tofField;
		uint32_t			m_activeBanks;

		BankMap				m_banks;
	};

	typedef std::map<uint32_t, EventSource> SourceMap;

	struct BeamMonitor {
		BeamMonitor(uint32_t srcId, uint32_t tofField) :
				m_sourceId(srcId), m_tofField(tofField)
		{ }

		uint32_t			m_sourceId;
		uint32_t			m_tofField;
		std::vector<uint32_t>		m_eventTof;
	};

	typedef std::map<uint32_t, BeamMonitor> MonitorMap;

	typedef std::vector<uint32_t> ChopperEvents;

	typedef std::map<uint32_t, ChopperEvents> ChopperMap;

	struct Pulse {
		Pulse(const PulseIdentifier &id, const SourceSet &srcs) :
				m_id(id), m_pending(srcs), m_numEventSources(srcs.count()),
				m_numEvents(0), m_numBanks(0), m_numMonEvents(0),
				m_charge(0), m_vetoFlags(0), m_cycle(0),
				m_ringPeriod(0), m_flags(0)
		{ }

		PulseIdentifier			m_id;
		SourceSet				m_pending;
		uint32_t				m_numEventSources;
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
	};

	MonitorMap				m_allMonitors;

	typedef boost::shared_ptr<Pulse> PulsePtr;

	typedef std::map<PulseIdentifier, PulsePtr> PulseMap;

	std::map<std::string, PVSharedPtr> m_pv_map;
	uint32_t m_nextRunNumber;
	uint32_t m_currentRunNumber;
	bool m_recording;
	uint32_t m_nextSrcId;

	boost::shared_ptr<smsStringPV> m_pvVersion;
	boost::shared_ptr<smsRunNumberPV> m_pvRunNumber;
	boost::shared_ptr<smsRecordingPV> m_pvRecording;
	boost::shared_ptr<smsErrorPV> m_pvSummary;
	boost::shared_ptr<smsStringPV> m_pvSummaryReason;

	bool m_summaryIsError; // Reverse Logic... ;-Q
	bool m_summaryRunInfo;
	bool m_summaryDataSources;
	bool m_summaryOther;

	std::string m_reason;
	std::string m_reasonBase;
	std::string m_reasonRunInfo;
	std::string m_reasonDataSources;
	std::string m_reasonOther;

	bool checkRequiredDataSources( std::string & why );

	void setSummaryReason(bool setBase, bool changedValid,
			bool major = false);

	std::vector<boost::shared_ptr<DataSource> > m_dataSources;

	uint32_t m_eventSourcesIndex[ SOURCE_SET_SIZE ];
	SourceSet m_eventSources;
	bool m_noRegisteredEventSources;
	uint32_t m_noRegisteredEventSourcesCount;

	SourceSet m_liveClients;

	PulseMap m_pulses;
	PulseIdentifier m_lastPid;
	PulseMap::iterator m_lastPulseIt;
	uint64_t m_lastPulseId;
	uint32_t m_lastRingPeriod;

	uint32_t m_monitorReserve;
	uint32_t m_bankReserve;
	uint32_t m_chopperReserve;
	uint32_t m_fastMetaReserve;

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

	static uint32_t m_targetStationNumber;

	static std::string m_version;
	static std::string m_facility;
	static std::string m_beamlineId;
	static std::string m_beamlineShortName;
	static std::string m_beamlineLongName;
	static std::string m_geometryPath;
	static std::string m_pixelMapPath;

	boost::shared_ptr<smsUint32PV> m_pvNoEoPPulseBufferSize;
	static uint32_t m_noEoPPulseBufferSize;

	boost::shared_ptr<smsUint32PV> m_pvMaxPulseBufferSize;
	static uint32_t m_maxPulseBufferSize;

	boost::shared_ptr<PopPulseBufferPV> m_pvPopPulseBuffer;

	boost::shared_ptr<smsBooleanPV> m_pvNoRTDLPulses;
	static bool m_noRTDLPulses;

	static uint64_t m_interPulseTimeChopGlitchMin;
	static uint64_t m_interPulseTimeChopGlitchMax;

	static uint64_t m_interPulseTimeChopperMin;
	static uint64_t m_interPulseTimeChopperMax;

	boost::shared_ptr<smsBooleanPV> m_pvDoPulsePchgCorrect;
	boost::shared_ptr<smsBooleanPV> m_pvDoPulseVetoCorrect;
	static uint64_t m_interPulseTimeMin;
	static uint64_t m_interPulseTimeMax;
	static bool m_doPulsePchgCorrect;
	static bool m_doPulseVetoCorrect;

	static bool m_sendSampleInRunInfo;

	static bool m_allowNonOneToOnePixelMapping;

	static bool m_useOrigPixelMappingPkt;

	static bool m_notesCommentAutoReset;

	boost::shared_ptr<smsUint32PV> m_pvIntermittentDataThreshold;
	static uint32_t m_intermittentDataThreshold;

	boost::shared_ptr<smsUint32PV> m_pvNeutronEventStateBits;
	boost::shared_ptr<smsBooleanPV> m_pvNeutronEventSortByState;
	static uint32_t m_neutronEventStateBits;
	static uint32_t m_neutronEventStateMask;
	static bool m_neutronEventSortByState;

	boost::shared_ptr<smsBooleanPV> m_pvIgnoreInterleavedSawtooth;
	static bool m_ignoreInterleavedSawtooth;

	boost::shared_ptr<smsUint32PV> m_pvMonitorTOFBits;
	static uint32_t m_monitorTOFBits;
	static uint32_t m_monitorTOFMask;

	boost::shared_ptr<smsUint32PV> m_pvChopperTOFBits;
	static uint32_t m_chopperTOFBits;
	static uint32_t m_chopperTOFMask;

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
	void correctPChargeVeto(PulsePtr &pulse, PulsePtr &next_pulse);
	void recordPulse(PulsePtr &pulse);
	void addMonitorEvent(const ADARA::RawDataPkt &pkt, PulsePtr &pulse,
				uint32_t id, uint32_t tof);
	void addChopperEvent(const ADARA::RawDataPkt &pkt, PulsePtr &pulse,
				uint32_t id, uint32_t tof);

	void buildBankedPacket(PulsePtr &pulse);
	void buildBankedStatePacket(PulsePtr &pulse);
	void buildMonitorPacket(PulsePtr &pulse);
	void buildChopperPackets(PulsePtr &pulse);
	void buildFastMetaPackets(PulsePtr &pulse);

	uint32_t pulseEnergy(uint32_t ringPeriod);

	friend class smsRecordingPV;
};

#endif /* __SMSCAS_H */
