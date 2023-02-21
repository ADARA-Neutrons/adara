#ifndef __SMS_CONTROL_H
#define __SMS_CONTROL_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <bitset>
#include <set>

#include <casdef.h>
#include <cadef.h>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "SMSControlPV.h"
#include "ReadyAdapter.h"
#include "Storage.h"

class smsStringPV;
class smsRunNumberPV;
class smsRecordingPV;
class smsErrorPV;
class smsBooleanPV;
class smsUint32PV;
class smsConnectedPV;
class PopPulseBufferPV;
class LogLevelPV;
class VersionPV;
class InstanceIdPV;
class PVPrefixPV;
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

	// Borrowed from PVStreamer/common/CoreDefs.h for now...

	enum PVType
	{
		PV_INT,
		PV_UINT,
		PV_REAL,
		PV_ENUM,
		PV_STR,
		PV_INT_ARRAY,
		PV_REAL_ARRAY
	};

	enum
	{
		EC_INVALID_OPERATION = 1,
		EC_INVALID_PARAM,
		EC_INVALID_CONFIG_DATA,
		EC_SOCKET_ERROR,
		EC_UNKOWN_ERROR,
		EC_EPICS_API,
		EC_WINDOWS_ERROR = 0x1000
	};

	// Borrowed from PVStreamer/common/Streamservice.h for now...

	/// Timestamp associated with device activity and variable values
	struct Timestamp
	{
		Timestamp() : sec(0), nsec(0) {}

		uint32_t sec;
		uint32_t nsec;
	};

	/// Holds last-known value and alarm state/severity
	/// for a process variable
	struct PVState
	{
		PVState()
			: m_uint_val(0),
			m_short_array(NULL), m_long_array(NULL),
			m_float_array(NULL), m_double_array(NULL),
			m_elem_count(0),
			m_status(0), m_severity(0)
		{}

		PVState( int16_t a_status, int16_t a_severity )
			: m_uint_val(0),
			m_short_array(NULL), m_long_array(NULL),
			m_float_array(NULL), m_double_array(NULL),
			m_elem_count(0),
			m_status(a_status), m_severity(a_severity)
		{}

		PVState( const PVState & a_state )
			// Copy *Double*, Covers Union...
			: m_double_val(a_state.m_double_val),
			m_str_val(a_state.m_str_val),
			m_short_array(NULL), m_long_array(NULL),
			m_float_array(NULL), m_double_array(NULL),
			m_elem_count(a_state.m_elem_count), m_time(a_state.m_time),
			m_status(a_state.m_status), m_severity(a_state.m_severity)
		{
			// Don't Allocate Anything if there are No Elements...
			// (Minimum Array Size is 2! :-)
			if ( m_elem_count > 1 )
			{
				if ( a_state.m_short_array != NULL )
				{
					m_short_array = new int16_t[m_elem_count];
					memcpy( m_short_array,
						a_state.m_short_array,
						m_elem_count * sizeof(int16_t) );
				}
				if ( a_state.m_long_array != NULL )
				{
					m_long_array = new int32_t[m_elem_count];
					memcpy( m_long_array,
						a_state.m_long_array,
						m_elem_count * sizeof(int32_t) );
				}
				if ( a_state.m_float_array != NULL )
				{
					m_float_array = new float[m_elem_count];
					memcpy( m_float_array,
						a_state.m_float_array,
						m_elem_count * sizeof(float) );
				}
				if ( a_state.m_double_array != NULL )
				{
					m_double_array = new double[m_elem_count];
					memcpy( m_double_array,
						a_state.m_double_array,
						m_elem_count * sizeof(double) );
				}
			}
		}

		~PVState()
		{
			delete[] m_short_array;
			delete[] m_long_array;
			delete[] m_float_array;
			delete[] m_double_array;
		}

    	PVState& operator=( const PVState & a_state )
    	{
        	// Free & Null Out Any Existing Numerical Arrays...
        	delete[] m_short_array;
        	m_short_array = (int16_t *) NULL;
        	delete[] m_long_array;
        	m_long_array = (int32_t *) NULL;
        	delete[] m_float_array;
        	m_float_array = (float *) NULL;
        	delete[] m_double_array;
        	m_double_array = (double *) NULL;

        	// Copy Scalar Fields...
			// (Copy *Double*, Covers Union!)
        	m_double_val = a_state.m_double_val;
        	m_str_val = a_state.m_str_val;
        	m_elem_count = a_state.m_elem_count;
        	m_time = a_state.m_time;
        	m_status = a_state.m_status;
        	m_severity = a_state.m_severity;

        	// Don't Allocate Anything if there are No Elements...
        	// (Minimum Array Size is 2! :-)
        	if ( m_elem_count > 1 )
        	{
            	if ( a_state.m_short_array != NULL )
            	{
                	m_short_array = new int16_t[m_elem_count];
                	memcpy( m_short_array,
                    	a_state.m_short_array,
						m_elem_count * sizeof(int16_t) );
            	}
            	if ( a_state.m_long_array != NULL )
            	{
                	m_long_array = new int32_t[m_elem_count];
                	memcpy( m_long_array,
                    	a_state.m_long_array,
						m_elem_count * sizeof(int32_t) );
            	}
            	if ( a_state.m_float_array != NULL )
            	{
                	m_float_array = new float[m_elem_count];
                	memcpy( m_float_array,
                    	a_state.m_float_array,
						m_elem_count * sizeof(float) );
            	}
            	if ( a_state.m_double_array != NULL )
            	{
                	m_double_array = new double[m_elem_count];
                	memcpy( m_double_array,
                    	a_state.m_double_array,
						m_elem_count * sizeof(double) );
            	}
        	}
        	return *this;
    	}

    	union
    	{
        	uint32_t m_uint_val;   ///< Used for both uint and enum types
        	int32_t m_int_val;
        	double m_double_val;
    	};
    	std::string m_str_val;

    	int16_t *m_short_array;
    	int32_t *m_long_array;
    	float *m_float_array;
    	double *m_double_array;

    	uint32_t m_elem_count;
    	Timestamp m_time;
    	int16_t m_status;       ///< EPICS alarm code
    	int16_t m_severity;     ///< EPICS severity code
	};

	// SMSControl-specific External EPICS PV Types...

	struct ExternalPV {

		ExternalPV(std::string name, std::string connection, PVType type)
			: m_name(name), m_connection(connection), m_type(type),
			m_elem_count(0)
		{ }

		std::string m_name;
		std::string m_connection;
		PVType m_type;
		uint32_t m_elem_count;
		std::string m_units;
	};

	typedef boost::shared_ptr<ExternalPV> ExternalPVPtr;

	ExternalPVPtr m_extRecordingPV;
	ExternalPVPtr m_extRunNumberPV;
	ExternalPVPtr m_extPausedPV;

	static ReadyAdapter *m_fdregChannelAccess;

	// Borrowed from PVStreamer/epics/EPICS_DeviceAgent.h for now...

	enum ChanState
	{
		UNINITIALIZED = 0,
		INFO_NEEDED,
		INFO_PENDING,
		INFO_AVAILABLE,
		READY
	};

	struct ChanInfo
	{
		ChanInfo()
			: m_chid(0), m_evid(0),
			m_chan_state(UNINITIALIZED), m_connected(false),
			m_subscribed(false)
		{}

		ExternalPVPtr m_pv;
		chid m_chid;
		evid m_evid;
		ChanState m_chan_state;
		PVState m_pv_state;
		bool m_connected;
		bool m_subscribed;
		unsigned long m_ca_type;
		unsigned long m_ca_elem_count;
		std::string m_ca_units;
		std::map<int32_t,std::string> m_ca_enum_vals;
	};

	boost::mutex m_mutex; // Mutex for EPICS Thread Locking...

	static int32_t epicsToTimeRecordType( uint32_t a_rec_type );
	static int32_t epicsToCtrlRecordType( uint32_t a_rec_type );

	static bool epicsIsTimeRecordType( uint32_t a_rec_type );
	static bool epicsIsCtrlRecordType( uint32_t a_rec_type );

	static PVType epicsToPVType(
		uint32_t a_rec_type, uint32_t a_elem_count );

	// PV channel ID to channel info map
	std::map<chid,ChanInfo> m_chan_info;

	// PV connection to channel id map
	std::map<std::string,chid> m_pv_index;

	// External EPICS PV Subscription Methods

	void ca_ready(void);
	
	void IPTS_ITEMS_Resend(void);

	void EPICSInit(void);

	void subscribePV( ExternalPVPtr pv );
	void unsubscribePV( ExternalPVPtr pv );

	void subscribeToPrimaryPVs( std::string PrimaryPVPrefix );
	void unsubscribePrimaryPVs(void);

	static void epicsConnectionHandler(
		struct connection_handler_args a_args );

	template<typename T>
	void updateState( const void *a_src, PVState &a_state );

	uint32_t uint32ValueOf( PVType a_type, PVState &a_state );
	bool boolValueOf( PVType a_type, PVState &a_state );

	static void epicsEventHandler( struct event_handler_args a_args );

	// SMSControl Internal PVs...
	void addPV(PVSharedPtr pv);

	static SMSControl *getInstance(void) { return m_singleton; }

	std::string getFacility(void) { return m_facility; }

	std::string getBeamlineId(void) { return m_beamlineId; }

	uint32_t getInstanceId(void) { return m_instanceId; }

	std::string getPVPrefix(void) { return m_pvPrefix; }

	std::string getPrimaryPVPrefix(void) { return m_primaryPVPrefix; }
	void setPrimaryPVPrefix( std::string PrimaryPVPrefix )
		{ m_primaryPVPrefix = PrimaryPVPrefix; }

	bool getRunNotesUpdatesEnabled(void) {
		m_runNotesUpdatesEnabled = m_pvRunNotesUpdatesEnabled->value();
		return m_runNotesUpdatesEnabled;
	}

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

	void updateMaxDataSourceTime( uint32_t srcId,
			struct timespec *ts ); // Wallclock Time...!

	struct timespec &oldestMaxDataSourceTime(void); // EPICS Time...!

	struct timespec &newestMaxDataSourceTime(void); // EPICS Time...!

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
	void updateValue(const ADARA::MultVariableU32Pkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::MultVariableDoublePkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::MultVariableStringPkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::MultVariableU32ArrayPkt &pkt,
			uint32_t sourceId);
	void updateValue(const ADARA::MultVariableDoubleArrayPkt &pkt,
			uint32_t sourceId);

	void extractLastValue(ADARA::MultVariableU32Pkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableDoublePkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableStringPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableU32ArrayPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableDoubleArrayPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);

	bool getRecording(void);

	void pauseRecording( struct timespec *ts ); // Wallclock Time...!
	void resumeRecording( struct timespec *ts ); // Wallclock Time...!

	void externalRunControl( struct timespec *ts,
			uint32_t scanIndex, std::string command );

	void updateValidRunInfo(bool isValid, std::string why,
			bool changedValid);

	void updateDataSourceConnectivity(void);

	uint32_t numConnectedDataSources(void)
		{ return m_numConnectedDataSources; }

	boost::shared_ptr<Markers> getMarkers(void) { return m_markers; }

	bool getUseAncientRunStatusPkt(void)
		{ return m_useAncientRunStatusPkt; }

	void updateVerbose(void);

	uint32_t verbose(void) { return m_verbose; }

	static void config(const boost::property_tree::ptree &conf);
	static void init(void);
	static void late_config(const boost::property_tree::ptree &conf);

	typedef std::vector<ADARA::Event> EventVector;

	// Fast Event/Meta-Data Bandwidth Statistics Collection Structures ;-D

	struct BandwidthStruct {

		BandwidthStruct( std::string device_name,
			std::string device_id_spec, std::string pv_label,
			uint32_t pixelId ) :
				m_device_name(device_name),
				m_device_id_spec(device_id_spec),
				m_pv_label(pv_label),
				m_pixelId(pixelId),
				m_count_second(0),
				m_count_minute(0),
				m_count_tenmin(0)
		{ }

		~BandwidthStruct(void)
		{
			m_pvBandwidthSecond.reset();
			m_pvBandwidthMinute.reset();
			m_pvBandwidthTenMin.reset();
		}

		void createBandwidthPVs( SMSControl *ctrl,
				std::string prefix, struct timespec now )
		{
			m_pvBandwidthSecond = boost::shared_ptr<smsUint32PV>(new
				smsUint32PV(prefix
					+ ":" + m_pv_label + "BandwidthSecond"));
			m_pvBandwidthMinute = boost::shared_ptr<smsUint32PV>(new
				smsUint32PV(prefix
					+ ":" + m_pv_label + "BandwidthMinute"));
			m_pvBandwidthTenMin = boost::shared_ptr<smsUint32PV>(new
				smsUint32PV(prefix
					+ ":" + m_pv_label + "BandwidthTenMin"));

			ctrl->addPV(m_pvBandwidthSecond);
			ctrl->addPV(m_pvBandwidthMinute);
			ctrl->addPV(m_pvBandwidthTenMin);

			m_pvBandwidthSecond->update( m_count_second, &now );
			m_pvBandwidthMinute->update( m_count_minute, &now );
			m_pvBandwidthTenMin->update( m_count_tenmin, &now );
		}

		void resetBandwidthStatistics(void)
		{
			m_count_second = 0;
			m_count_minute = 0;
			m_count_tenmin = 0;
		}

		std::string			m_device_name;
		std::string			m_device_id_spec;
		std::string			m_pv_label;

		// PixelId = 0xTDDDXXXX:
		//    T = PixelId Type/Class
		//       (Monitor = 0x4, Chopper = 0x7, Fast-Meta = 0x5/0x6, etc)
		//    DDD = Device ID (only for Meta-Data Events...!)
		//    XXXX = Chopper TOF, Analog Value,
		//       or Digital Trigger Falling/Rising (e.g. 0x000[0/1]...)
		// Note: for Neutron Detectors, Specific PixelId = 0x0DDDXXXX. :-D
		uint32_t			m_pixelId;

		uint32_t			m_count_second;
		uint32_t			m_count_minute;
		uint32_t			m_count_tenmin;

		boost::shared_ptr<smsUint32PV> m_pvBandwidthSecond;
		boost::shared_ptr<smsUint32PV> m_pvBandwidthMinute;
		boost::shared_ptr<smsUint32PV> m_pvBandwidthTenMin;
	};

	typedef boost::shared_ptr<BandwidthStruct> BandwidthStructPtr;

	struct SourceBandwidthStruct {

		SourceBandwidthStruct( std::string device_name ) :
				m_device_name(device_name)
		{
			m_eventBandwidth = BandwidthStructPtr( new BandwidthStruct(
				m_device_name, "Neutron Events", "Event",
				0x00000000 ) );
			m_metaBandwidth = BandwidthStructPtr( new BandwidthStruct(
				m_device_name, "Meta-Data Events", "Meta",
				0x70000000 ) );
			m_errBandwidth = BandwidthStructPtr( new BandwidthStruct(
				m_device_name, "Neutron Error Events", "Err",
				0x80000000 ) );
		}

		~SourceBandwidthStruct()
		{
			m_eventBandwidth.reset();
			m_metaBandwidth.reset();
			m_errBandwidth.reset();
		}

		std::string			m_device_name;

		BandwidthStructPtr m_eventBandwidth;
		BandwidthStructPtr m_metaBandwidth;
		BandwidthStructPtr m_errBandwidth;
	};

	typedef boost::shared_ptr<SourceBandwidthStruct>
		SourceBandwidthStructPtr;

	struct DataSourceBandwidthStruct {

		DataSourceBandwidthStruct( std::string device_name ) :
				m_device_name(device_name)
		{
			m_pulseBandwidth = BandwidthStructPtr( new BandwidthStruct(
				m_device_name, "Neutron Pulses", "Pulse", 0x00000000 ) );

			m_dataSourceBandwidth = SourceBandwidthStructPtr(
				new SourceBandwidthStruct( m_device_name ) );
		}

		~DataSourceBandwidthStruct()
		{
			m_pulseBandwidth.reset();

			m_dataSourceBandwidth.reset();

			// Reset HW Sources Bandwidth Structs...
			std::vector<SourceBandwidthStructPtr>::iterator hwSrcBWi;
			for ( hwSrcBWi=m_hwSourcesBandwidth.begin() ;
					hwSrcBWi != m_hwSourcesBandwidth.end() ; ++hwSrcBWi ) {
				(*hwSrcBWi).reset();
			}
			m_hwSourcesBandwidth.clear();
		}

		std::string			m_device_name;

		BandwidthStructPtr m_pulseBandwidth;

		SourceBandwidthStructPtr m_dataSourceBandwidth;

		std::vector<SourceBandwidthStructPtr> m_hwSourcesBandwidth;
		// Count of Number of Hardware Sources Already Provided in:
		//    ${SMS}:DataSource:<DataSourceIndex>:NumHWSources
	};

	typedef boost::shared_ptr<DataSourceBandwidthStruct>
		DataSourceBandwidthStructPtr;

	struct TriggerBandwidthStruct {

		TriggerBandwidthStruct( std::string device_name,
			std::string device_id_spec, std::string pv_label,
			uint32_t pixelId ) :
				m_device_name(device_name),
				m_device_id_spec(device_id_spec),
				m_pv_label(pv_label),
				m_pixelId(pixelId)
		{
			m_triggerBandwidth = BandwidthStructPtr(
				new BandwidthStruct(
					m_device_name, m_device_id_spec, m_pv_label,
					m_pixelId ) );

			m_triggerFallingBandwidth = BandwidthStructPtr(
				new BandwidthStruct(
					m_device_name, m_device_id_spec + "_Falling",
					m_pv_label, m_pixelId | 0x00000000 ) );
			m_triggerRisingBandwidth = BandwidthStructPtr(
				new BandwidthStruct(
					m_device_name, m_device_id_spec + "_Rising",
					m_pv_label, m_pixelId | 0x00000001 ) );
		}

		~TriggerBandwidthStruct()
		{
			m_triggerBandwidth.reset();
			m_triggerFallingBandwidth.reset();
			m_triggerRisingBandwidth.reset();
		}

		std::string			m_device_name;
		std::string			m_device_id_spec;
		std::string			m_pv_label;

		uint32_t			m_pixelId;

		BandwidthStructPtr m_triggerBandwidth;

		BandwidthStructPtr m_triggerFallingBandwidth;
		BandwidthStructPtr m_triggerRisingBandwidth;
	};

	typedef boost::shared_ptr<TriggerBandwidthStruct>
		TriggerBandwidthStructPtr;

	struct OverallBandwidthStruct {

		OverallBandwidthStruct( SMSControl *ctrl,
				std::string prefix, struct timespec now )
		{
			m_pvNumBeamMonitorsBandwidth =
				boost::shared_ptr<smsUint32PV>(new
					smsUint32PV(prefix
						+ ":Control:NumBeamMonitorsBandwidth"));
			m_pvNumChoppersBandwidth =
				boost::shared_ptr<smsUint32PV>(new
					smsUint32PV(prefix
						+ ":Control:NumChoppersBandwidth"));
			m_pvNumDigitalTriggersBandwidth =
				boost::shared_ptr<smsUint32PV>(new
					smsUint32PV(prefix
						+ ":Control:NumDigitalTriggersBandwidth"));
			m_pvNumAnalogSignalsBandwidth =
				boost::shared_ptr<smsUint32PV>(new
					smsUint32PV(prefix
						+ ":Control:NumAnalogSignalsBandwidth"));

			ctrl->addPV(m_pvNumBeamMonitorsBandwidth);
			ctrl->addPV(m_pvNumChoppersBandwidth);
			ctrl->addPV(m_pvNumDigitalTriggersBandwidth);
			ctrl->addPV(m_pvNumAnalogSignalsBandwidth);

			m_pvNumBeamMonitorsBandwidth->update( 0, &now );
			m_pvNumChoppersBandwidth->update( 0, &now );
			m_pvNumDigitalTriggersBandwidth->update( 0, &now );
			m_pvNumAnalogSignalsBandwidth->update( 0, &now );
		}

		~OverallBandwidthStruct()
		{
			// Reset Data Sources Bandwidth Structs...
			std::vector<DataSourceBandwidthStructPtr>::iterator DataSrcBWi;
			for ( DataSrcBWi=m_dataSourcesBandwidth.begin() ;
					DataSrcBWi != m_dataSourcesBandwidth.end() ;
					++DataSrcBWi ) {
				(*DataSrcBWi).reset();
			}
			m_dataSourcesBandwidth.clear();

			// Reset Beam Monitors Bandwidth Structs...
			std::vector<TriggerBandwidthStructPtr>::iterator BeamMonBWi;
			for ( BeamMonBWi=m_beamMonitorsBandwidth.begin() ;
					BeamMonBWi != m_beamMonitorsBandwidth.end() ;
					++BeamMonBWi ) {
				(*BeamMonBWi).reset();
			}
			m_beamMonitorsBandwidth.clear();

			m_pvNumBeamMonitorsBandwidth.reset();

			// Reset Choppers Bandwidth Structs...
			std::vector<TriggerBandwidthStructPtr>::iterator ChopperBWi;
			for ( ChopperBWi=m_choppersBandwidth.begin() ;
					ChopperBWi != m_choppersBandwidth.end() ;
					++ChopperBWi ) {
				(*ChopperBWi).reset();
			}
			m_choppersBandwidth.clear();

			m_pvNumChoppersBandwidth.reset();

			// Reset Digital Triggers Bandwidth Structs...
			std::vector<TriggerBandwidthStructPtr>::iterator DigTrigBWi;
			for ( DigTrigBWi=m_digitalTriggersBandwidth.begin() ;
					DigTrigBWi != m_digitalTriggersBandwidth.end() ;
					++DigTrigBWi ) {
				(*DigTrigBWi).reset();
			}
			m_digitalTriggersBandwidth.clear();

			m_pvNumDigitalTriggersBandwidth.reset();

			// Reset Analog Signals Bandwidth Structs...
			std::vector<BandwidthStructPtr>::iterator AnalSigBWi;
			for ( AnalSigBWi=m_analogSignalsBandwidth.begin() ;
					AnalSigBWi != m_analogSignalsBandwidth.end() ;
					++AnalSigBWi ) {
				(*AnalSigBWi).reset();
			}
			m_analogSignalsBandwidth.clear();

			m_pvNumAnalogSignalsBandwidth.reset();
		}

		// Events: 0x0XXXXXXX,
		//    Meta-Data: 0x[567]0000000,
		//    Errors: 0x8XXXXXXX
		std::vector<DataSourceBandwidthStructPtr> m_dataSourcesBandwidth;
		// Count of Number of DataSources Already Provided in:
		//    ${SMS}:Control:NumDataSources

		// Beam Monitors: 0x4DDD000[01]
		std::vector<TriggerBandwidthStructPtr> m_beamMonitorsBandwidth;
		boost::shared_ptr<smsUint32PV> m_pvNumBeamMonitorsBandwidth;

		// Choppers: 0x7DDDXXXX
		std::vector<TriggerBandwidthStructPtr> m_choppersBandwidth;
		boost::shared_ptr<smsUint32PV> m_pvNumChoppersBandwidth;

		// Digital Triggers: 0x5DDD000[01]
		std::vector<TriggerBandwidthStructPtr> m_digitalTriggersBandwidth;
		boost::shared_ptr<smsUint32PV> m_pvNumDigitalTriggersBandwidth;

		// Analog Signals: 0x6DDDXXXX
		std::vector<BandwidthStructPtr> m_analogSignalsBandwidth;
		boost::shared_ptr<smsUint32PV> m_pvNumAnalogSignalsBandwidth;
	};

	typedef boost::shared_ptr<OverallBandwidthStruct>
		OverallBandwidthStructPtr;

	OverallBandwidthStructPtr m_BW;

private:
	SMSControl();
	~SMSControl();

	typedef std::pair<uint64_t, uint32_t> PulseIdentifier;

	struct EventSource {
		EventSource( uint32_t intraPulse, uint32_t tofField ) :
				m_intraPulseTime(intraPulse),
				m_tofField(tofField),
				m_activeBanks(0)
		{ }

		uint32_t			m_intraPulseTime;
		uint32_t			m_tofField;
		uint32_t			m_activeBanks;

		EventVector			*m_banks_arr;

		// Note: "Number" of States Includes State 0...
		uint32_t			m_numStates;
		uint32_t			m_banks_arr_size; // Will be needed for realloc
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

	typedef std::map<uint32_t, EventVector> FastMetaMap;

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
		FastMetaMap				m_fastMetaEvents;
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

	boost::shared_ptr<LogLevelPV> m_pvLogLevel;

	boost::shared_ptr<VersionPV> m_pvVersion;

	boost::shared_ptr<InstanceIdPV> m_pvInstanceId;

	boost::shared_ptr<PVPrefixPV> m_pvAltPrimaryPVPrefix;

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

	std::vector<struct timespec> m_dataSourcesMaxTimes; // EPICS Time...!

	struct timespec m_oldestMaxDataSourceTime; // EPICS Time...!

	struct timespec m_newestMaxDataSourceTime; // EPICS Time...!

	uint32_t m_numConnectedDataSources;

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

	uint32_t m_maxBank;

	// Note: "Number" of States Includes State 0...
	uint32_t m_numStatesLast;
	uint32_t m_numStatesResetCount;

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

	static uint32_t m_instanceId;

	static std::string m_pvPrefix;

	static std::string m_primaryPVPrefix;

	static std::string m_altPrimaryPVPrefix;

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
	static bool m_savePixelMap;

	static bool m_useAncientRunStatusPkt;

	static bool m_allowNonOneToOnePixelMapping;

	static bool m_useOrigPixelMappingPkt;

	static bool m_notesCommentAutoReset; // Note: Live PV in Markers...!
	static bool m_runNotesUpdatesEnabled;
	boost::shared_ptr<smsBooleanPV> m_pvRunNotesUpdatesEnabled;

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

	boost::shared_ptr<smsUint32PV> m_pvVerbose;
	static uint32_t m_verbose;

	boost::shared_ptr<smsUint32PV> m_pvNumDataSources;

	boost::shared_ptr<CleanShutdownPV> m_pvCleanShutdown;

	boost::shared_ptr<smsUint32PV> m_pvNumLiveClients;

	std::vector< boost::shared_ptr<smsStringPV> > m_pvLiveClientNames;
	std::vector< boost::shared_ptr<smsUint32PV> > m_pvLiveClientStartTimes;
	std::vector< boost::shared_ptr<smsStringPV> > m_pvLiveClientFilePaths;
	std::vector< boost::shared_ptr<smsConnectedPV> > m_pvLiveClientStatuses;

	struct ca_client_context *m_epics_context;

	static SMSControl *m_singleton;

	pvExistReturn pvExistTest(const casCtx &, const char *pv_name);

	void addSources(const boost::property_tree::ptree &conf);
	void addSource(const std::string &name,
				const boost::property_tree::ptree &info, bool enabled);
	bool setRecording(bool val, struct timespec *ts); // Wallclock Time...!

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
