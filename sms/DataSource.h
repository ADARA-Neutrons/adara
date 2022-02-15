#ifndef __DATA_SOURCE_H
#define __DATA_SOURCE_H

#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>
#include <stdint.h>
#include <map>
#include <bitset>
#include <string>

#include "ADARAPackets.h"
#include "POSIXParser.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "ReadyAdapter.h"
#include "TimerAdapter.h"
#include "Markers.h"

extern "C" {
struct addrinfo;
}

class HWSource;
class smsStringPV;
class smsEnabledPV;
class DataSourceRequiredPV;
class smsConnectedPV;
class smsFloat64PV;
class smsBooleanPV;
class smsPassThruPV;
class smsUint32PV;

class DataSource : public ADARA::POSIXParser {
public:
	DataSource(const std::string &name, bool enabled, bool required,
		const std::string &uri, uint32_t id,
		double connect_retry, double connect_timeout,
		double data_timeout, uint32_t data_timeout_retry,
		bool ignore_eop, bool ignore_local_sawtooth,
		Markers::PassThru ignore_annotation_pkts,
		bool mixed_data_packets,
		bool check_source_sequence, bool check_pulse_sequence,
		uint32_t max_pulse_seq_list,
		unsigned int read_chunk,
		uint32_t rtdlNoDataThresh, bool save_input_stream);
	~DataSource();

	SMSControl *m_ctrl;

	bool m_readDelay;

	void resetPacketStats(void);

	std::string name(void) { return m_name; }

	void enabled(void);
	void disabled(void);

	void setRequired( bool is_required, bool force = false );

	bool isRequired(void) { return m_required; }

	bool isConnected(void) { return ( m_state == ACTIVE ); }

	bool ignoreLocalSAWTOOTH(void) { return m_ignore_local_sawtooth; }

	void setIntermittent( uint32_t smsId, bool intermittent );

	uint32_t getMaxPulseSeqList(void)
	{
		m_max_pulse_seq_list = m_pvMaxPulseSeqList->value();
		return m_max_pulse_seq_list;
	}

	void resetMaxTime(void)
	{
		m_maxTime.tv_sec = 0; // Wallclock Time...!
		m_maxTime.tv_nsec = 0;
	}

private:
	typedef std::map<uint32_t, boost::shared_ptr<HWSource> > HWSrcMap;

	enum State { DISABLED, IDLE, CONNECTING, ACTIVE };

	std::string m_name;
	std::string m_basename;
	std::string m_uri;
	bool m_enabled;
	bool m_required;
	ReadyAdapter *m_fdreg;
	TimerAdapter<DataSource> *m_timer;
	struct addrinfo *m_addrinfo;
	State m_state;
	uint32_t m_smsSourceId;
	int m_fd;
	HWSrcMap m_hwSources;
	double m_connect_retry;
	double m_connect_timeout;
	double m_data_timeout;
	uint32_t m_data_timeout_retry;
	uint32_t m_data_timeout_retry_count;
	bool m_ignore_eop;
	bool m_ignore_local_sawtooth;
	Markers::PassThru m_ignore_annotation_pkts;
	bool m_mixed_data_packets;
	bool m_check_source_sequence;
	bool m_check_pulse_sequence;
	uint32_t m_max_pulse_seq_list;
	unsigned int m_max_read_chunk;
	uint32_t m_rtdlNoDataThresh;
	bool m_save_input_stream;
	struct timespec m_maxTime; // Wallclock Time...!

	boost::shared_ptr<smsStringPV> m_pvName;
	boost::shared_ptr<smsStringPV> m_pvBaseName;
	boost::shared_ptr<smsStringPV> m_pvDataURI;
	boost::shared_ptr<smsEnabledPV> m_pvEnabled;
	boost::shared_ptr<DataSourceRequiredPV> m_pvRequired;
	boost::shared_ptr<smsConnectedPV> m_pvConnected;
	boost::shared_ptr<smsFloat64PV> m_pvConnectRetryTimeout;
	boost::shared_ptr<smsFloat64PV> m_pvConnectTimeout;
	boost::shared_ptr<smsFloat64PV> m_pvDataTimeout;
	boost::shared_ptr<smsUint32PV> m_pvDataTimeoutRetry;
	boost::shared_ptr<smsBooleanPV> m_pvIgnoreEoP;
	boost::shared_ptr<smsBooleanPV> m_pvIgnoreLocalSAWTOOTH;
	boost::shared_ptr<smsPassThruPV> m_pvIgnoreAnnotationPkts;
	boost::shared_ptr<smsBooleanPV> m_pvMixedDataPackets;
	boost::shared_ptr<smsBooleanPV> m_pvCheckSourceSequence;
	boost::shared_ptr<smsBooleanPV> m_pvCheckPulseSequence;
	boost::shared_ptr<smsUint32PV> m_pvMaxPulseSeqList;
	boost::shared_ptr<smsStringPV> m_pvMaxReadChunk;
	boost::shared_ptr<smsUint32PV> m_pvRTDLNoDataThresh;
	boost::shared_ptr<smsBooleanPV> m_pvSaveInputStream;

	boost::shared_ptr<smsUint32PV> m_pvPulseBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvPulseBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvPulseBandwidthTenMin;

	boost::shared_ptr<smsUint32PV> m_pvEventBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvEventBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvEventBandwidthTenMin;

	boost::shared_ptr<smsUint32PV> m_pvMetaBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvMetaBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvMetaBandwidthTenMin;

	boost::shared_ptr<smsUint32PV> m_pvErrBandwidthSecond;
	boost::shared_ptr<smsUint32PV> m_pvErrBandwidthMinute;
	boost::shared_ptr<smsUint32PV> m_pvErrBandwidthTenMin;

	boost::shared_ptr<smsUint32PV> m_pvNumHWSources;

	SMSControl::SourceSet m_hwIndices;

	std::vector< boost::shared_ptr<smsUint32PV> > m_pvHWSourceHwIds;
	std::vector< boost::shared_ptr<smsUint32PV> > m_pvHWSourceSmsIds;

	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceEventBandwidthSecond;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceEventBandwidthMinute;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceEventBandwidthTenMin;

	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceMetaBandwidthSecond;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceMetaBandwidthMinute;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceMetaBandwidthTenMin;

	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceErrBandwidthSecond;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceErrBandwidthMinute;
	std::vector< boost::shared_ptr<smsUint32PV> >
		m_pvHWSourceErrBandwidthTenMin;

	uint64_t m_lastRTDLPulseId;
	uint16_t m_lastRTDLCycle;
	uint32_t m_dupRTDL;

	void parseURI(std::string uri);

	void fdReady(void);

	void startConnect(void);
	void connectComplete(void);
	void dataReady(void);

	void dumpLastReadStats(std::string who);

	void unregisterHWSources(bool isSourceDown, bool stateChanged,
				std::string why);
	void connectionFailed(bool dumpStats, bool dumpDiscarded,
				State new_state);

	bool timerExpired(void);

	bool rxPacket(const ADARA::Packet &pkt);

	bool rxUnknownPkt(const ADARA::Packet &pkt);
	bool rxOversizePkt(const ADARA::PacketHeader *hdr,
				const uint8_t *chunk, unsigned int chunk_offset,
				unsigned int chunk_len);

	bool rxPacket(const ADARA::RawDataPkt &pkt);
	bool rxPacket(const ADARA::MappedDataPkt &pkt);

	bool handleDataPkt(const ADARA::RawDataPkt *pkt, bool is_mapped);

	bool rxPacket(const ADARA::RTDLPkt &pkt);
	bool rxPacket(const ADARA::SourceListPkt &pkt);
	bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
	bool rxPacket(const ADARA::VariableU32Pkt &pkt);
	bool rxPacket(const ADARA::VariableDoublePkt &pkt);
	bool rxPacket(const ADARA::VariableStringPkt &pkt);
	bool rxPacket(const ADARA::VariableU32ArrayPkt &pkt);
	bool rxPacket(const ADARA::VariableDoubleArrayPkt &pkt);
	bool rxPacket(const ADARA::MultVariableU32Pkt &pkt);
	bool rxPacket(const ADARA::MultVariableDoublePkt &pkt);
	bool rxPacket(const ADARA::MultVariableStringPkt &pkt);
	bool rxPacket(const ADARA::MultVariableU32ArrayPkt &pkt);
	bool rxPacket(const ADARA::MultVariableDoubleArrayPkt &pkt);
	bool rxPacket(const ADARA::AnnotationPkt &pkt);
	bool rxPacket(const ADARA::HeartbeatPkt &pkt);

	void resetBandwidthStatistics(void);

	void updateBandwidthSecond(struct timespec &now, bool do_log);
	void updateBandwidthMinute(struct timespec &now, bool do_log);
	void updateBandwidthTenMin(struct timespec &now, bool do_log);

	void onSavePrologue(bool capture_last);

	// Device Descriptor and Variable Packet Tracking
	// (for Saved Input Stream File Prologue...)
	typedef std::map<uint32_t, ADARA::PacketSharedPtr> VariablePktMap;

	struct DeviceVariables {
		uint32_t    m_devId;
		// No Need for a Source Tag here (a la MetaDataMgr.h)
		ADARA::PacketSharedPtr m_descriptorPkt;
		VariablePktMap  m_variablePkts;
	};

	typedef std::map<uint32_t, DeviceVariables> DeviceMap;

	DeviceMap m_devices;

	boost::signals2::connection m_connection;

	// Last Packet Debug
	uint32_t m_last_pkt_type;
	uint32_t m_last_pkt_len;
	time_t m_last_pkt_sec;
	long m_last_pkt_nsec;

	// Last Packet Counts
	uint32_t m_rtdl_pkt_counts;
	uint32_t m_data_pkt_counts;

	// Pulse/Event Bandwidth Statistics
	uint32_t	m_pulse_count_second;
	uint32_t	m_pulse_count_minute;
	uint32_t	m_pulse_count_tenmin;
	uint32_t	m_event_count_second;
	uint32_t	m_event_count_minute;
	uint32_t	m_event_count_tenmin;
	uint32_t	m_meta_count_second;
	uint32_t	m_meta_count_minute;
	uint32_t	m_meta_count_tenmin;
	uint32_t	m_err_count_second;
	uint32_t	m_err_count_minute;
	uint32_t	m_err_count_tenmin;

	struct timespec	m_last_second_time;
	struct timespec	m_last_minute_time;
	struct timespec	m_last_tenmin_time;
	uint32_t	m_last_minute;
	uint32_t	m_last_tenmin;

	boost::shared_ptr<HWSource> getHWSource(uint32_t hwId);

	friend class TimerAdapter<DataSource>;

	using ADARA::POSIXParser::rxPacket;
};

#endif /* __DATA_SOURCE_H */
