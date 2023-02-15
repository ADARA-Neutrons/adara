#ifndef STREAMMONITOR_H
#define STREAMMONITOR_H

#include <POSIXParser.h>
#include <map>
#include <set>
#include <vector>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <libxml/tree.h>
#include "DASMonDefs.h"
#include "ADARAPackets.h"

namespace ADARA {
namespace DASMON {

/// Used to Identify "Special" Detector Bank Ids (Error & Unmapped)
enum SpecialBankIds
{
    UNMAPPED_BANK   = 0xffffffff,
    ERROR_BANK      = 0xfffffffe
};

/// Used to Identify "Special" Detector Bank Indices (Error & Unmapped)
enum SpecialBankIndices
{
    UNMAPPED_BANK_INDEX = 0,
    ERROR_BANK_INDEX    = 1,
    NUM_SPECIAL_BANKS   = 2
};

struct PktStats
{
    PktStats()
        : count(0), min_pkt_size(0), max_pkt_size(0), total_size(0) {}

    uint64_t    count;
    uint64_t    min_pkt_size;
    uint64_t    max_pkt_size;
    uint64_t    total_size;
};


class IStreamListener
{
public:
    virtual void connectionStatus( bool a_connected,
        const std::string &a_host, unsigned short a_port ) = 0;
    virtual void runStatus( bool a_recording, uint32_t a_run_number,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
    virtual void beginProlog() = 0;
    virtual void endProlog() = 0;
    virtual void pauseStatus( bool a_paused ) = 0;
    virtual void scanStatus( bool a_scanning, uint32_t a_scan_number ) = 0;
    virtual void beamInfo( const BeamInfo &a_info ) = 0;
    virtual void runInfo( const RunInfo &a_info ) = 0;
    virtual void beamMetrics( const BeamMetrics &a_metrics ) = 0;
    virtual void runMetrics( const RunMetrics &a_metrics ) = 0;
    virtual void streamMetrics( const StreamMetrics &a_metrics ) = 0;
    virtual void pvDefined( const std::string &a_name ) = 0;
    virtual void pvUndefined( const std::string &a_name ) = 0;
    virtual void pvValue( const std::string &a_name,
        uint32_t a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
    virtual void pvValue( const std::string &a_name,
        double a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
    virtual void pvValue( const std::string &a_name,
        std::string &a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
    virtual void pvValue( const std::string &a_name,
        std::vector<uint32_t> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
    virtual void pvValue( const std::string &a_name,
        std::vector<double> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec ) = 0;
};

/// Identifier type used for devices and process variables
typedef uint32_t Identifier;

/// Process varibale key used for maps and/or sets
typedef std::pair<Identifier,Identifier>    PVKey;

/// Process variable types
enum PVType
{
    PVT_INT,
    PVT_UINT,
    PVT_FLOAT,
    PVT_DOUBLE,
    PVT_ENUM,
    PVT_STRING,
    PVT_UINT_ARRAY,
    PVT_DOUBLE_ARRAY
};

/// Base class for all process variable (PV) types
class PVInfoBase
{
public:
    /// PVInfoBase constructor
    PVInfoBase
    (
        const std::string  &a_name,         ///< [in] Name of PV
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type          ///< [in] Type of PV
    )
    :
        m_name(a_name),
        m_device_id(a_device_id),
        m_pv_id(a_pv_id),
        m_type(a_type),
        m_time(0), m_time_nanosec(0),
        m_status(VariableStatus::OK),
        m_updated(false)
    {}

    /// PVInfoBase destructor
    virtual ~PVInfoBase()
    {}

    std::string             m_name;         ///< Name of PV
    Identifier              m_device_id;    ///< ID of device that owns the PV
    Identifier              m_pv_id;        ///< ID of the PV
    PVType                  m_type;         ///< Type of PV
    uint32_t                m_time;
    uint32_t                m_time_nanosec;
    VariableStatus::Enum    m_status;
    bool                    m_updated;
};

template<class T>
class PVInfo : public PVInfoBase
{
public:
    PVInfo
    (
        const std::string  &a_name,         ///< [in] Name of PV
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type,         ///< [in] Type of PV
        T                   a_value
    )
    : PVInfoBase( a_name, a_device_id, a_pv_id, a_type ), m_value(a_value)
    {}

    T                   m_value;
};


template<class T>
class CountInfo
{
public:
    CountInfo( uint16_t a_window_size = 60 )
        : m_average(0.0), m_cached(false), m_total(0), m_idx(0),
        m_win_sz(a_window_size)
    {
        m_samples = new T[m_win_sz]();
    }

    CountInfo( const CountInfo &a_info )
        : m_samples(0), m_win_sz(0)
    {
        *this = a_info;
    }

    ~CountInfo()
    {
        delete[] m_samples;
    }

    CountInfo& operator=( const CountInfo &a_info )
    {
        if ( m_samples )
        {
            if ( m_win_sz != a_info.m_win_sz )
            {
                delete[] m_samples;
                m_samples = new T[a_info.m_win_sz];
            }
        }
        else
        {
            m_samples = new T[a_info.m_win_sz];
        }

        m_average = a_info.m_average;
        m_cached = a_info.m_cached;
        m_total = a_info.m_total;
        m_idx = a_info.m_idx;
        m_win_sz = a_info.m_win_sz;

        for ( uint16_t i = 0; i < m_win_sz; ++i )
            m_samples[i] = a_info.m_samples[i];

        return *this;
    }

    void addSample( T sample )
    {
        m_total += sample;
        m_samples[m_idx++] = sample;
        m_cached = false;

        if ( m_idx == m_win_sz )
            m_idx = 0;
    }

    void reset()
    {
        m_average = 0.0;
        m_cached = false;
        m_total = 0;
        m_idx = 0;

        for ( uint16_t i = 0; i < m_win_sz; ++i )
            m_samples[i] = 0;
    }

    inline void reset_total( T a_total )
    {
        m_total = a_total;
    }

    inline T total()
    {
        return m_total;
    }

    double average()
    {
        // Note: does not use running average due to numerical instability
        // over very long time spans.
        // If average() is called multiple times for same sample set,
        // cached value is used.
        if ( !m_cached )
        {
            m_average = 0.0;

            for ( uint16_t i = 0; i < m_win_sz; ++i )
                m_average += m_samples[i];

            m_average = m_average/m_win_sz;
            m_cached = true;
        }

        return m_average;
    }

private:
    double      m_average;
    bool        m_cached;
    T          *m_samples;
    T           m_total;
    uint16_t    m_idx;
    uint16_t    m_win_sz;
};

#ifndef NO_DB
struct DBConnectInfo
{
    std::string     host;
    unsigned short  port;
    std::string     name;
    std::string     user;
    std::string     pass;
    unsigned short  period;
};
#endif

enum ThreadState
{
    TS_INIT = 0,
    TS_ENTER,
    TS_CONNECTING,
    TS_RUNNING,
    TS_EXCEPTION,
    TS_SLEEP,
    TS_EXIT,
    TS_PKT_RUN_STATUS           = 100,
    TS_PKT_PIXEL_MAPPING,
    TS_PKT_RUN_INFO,
    TS_PKT_BEAMLINE_INFO,
    TS_PKT_DEVICE_DESC,
    TS_PKT_VAR_VALUE_U32,
    TS_PKT_VAR_VALUE_DOUBLE,
    TS_PKT_VAR_VALUE_STRING,
    TS_PKT_VAR_VALUE_U32_ARRAY,
    TS_PKT_VAR_VALUE_DOUBLE_ARRAY,
    TS_PKT_STREAM_ANNOTATION,
    TS_PKT_BEAM_MONITOR_EVENT,
    TS_PKT_BANKED_EVENT,
    TS_PKT_BANKED_EVENT_STATE,
    TS_NOTIFY_NONE              = 200,
    TS_NOTIFY_RUN_INFO,
    TS_NOTIFY_BEAM_INFO,
    TS_NOTIFY_CONN_STATUS,
    TS_NOTIFY_RUN_STATUS,
    TS_NOTIFY_BEGIN_PROLOG,
    TS_NOTIFY_END_PROLOG,
    TS_NOTIFY_SCAN_STATUS,
    TS_NOTIFY_PAUSE_STATUS,
    TS_NOTIFY_RUN_METRICS,
    TS_NOTIFY_BEAM_METRICS,
    TS_NOTIFY_STREAM_METRICS,
    TS_NOTIFY_PV_DEF,
    TS_NOTIFY_PV_UNDEF,
    TS_NOTIFY_PV_VAL_UINT,
    TS_NOTIFY_PV_VAL_DBL,
    TS_NOTIFY_PV_VAL_STR,
    TS_NOTIFY_PV_VAL_UINT_ARRAY,
    TS_NOTIFY_PV_VAL_DBL_ARRAY,
    TS_DB_UPDATE                = 300,
    TS_DB_ERROR
};

class StreamMonitor : public ADARA::POSIXParser
{
public:
#ifndef NO_DB
    StreamMonitor( const std::string &a_sms_host,
        unsigned short a_sms_port,
        DBConnectInfo *a_db_info, uint32_t a_maxtof );
#else
    StreamMonitor( const std::string &a_sms_host,
        unsigned short a_sms_port = 31415 );
#endif
    virtual ~StreamMonitor();

    void            getSMSHostInfo( std::string &a_hostname,
                        unsigned short &a_port ) const;
    void            setSMSHostInfo( std::string a_hostname,
                        unsigned short a_port );
    void            start();
    void            stop();
    void            addListener( IStreamListener &a_listener )
                    { m_notify.addListener( a_listener ); }
    void            removeListener( IStreamListener &a_listener )
                    { m_notify.removeListener( a_listener ); }
    void            resendState( IStreamListener &a_listener ) const;
    void            enableDiagnostics( bool a_diagnostics )
                    { m_diagnostics = a_diagnostics; }


    inline uint32_t getProcTicker() { return m_proc_ticker; }
    inline uint32_t getProcState() { return m_proc_state; }
    inline uint32_t getNotifyState() { return m_notify_state; }
    inline uint32_t getMetricsTicker() { return m_metrics_ticker; }
    inline uint32_t getMetricsState() { return m_metrics_state; }
#ifndef NO_DB
    inline uint32_t getDbTicker() { return m_db_ticker; }
    inline uint32_t getDbState() { return m_db_state; }
#endif

private:
    class Notifier : public IStreamListener
    {
    public:
        void addListener( IStreamListener &a_listener );
        void removeListener( IStreamListener &a_listener );

        void runStatus( bool a_recording, uint32_t a_run_number,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec  );
        void beginProlog();
        void endProlog();
        void pauseStatus( bool a_paused );
        void scanStatus( bool a_scanning, uint32_t a_scan_number );
        void beamInfo( const BeamInfo &a_info );
        void runInfo( const RunInfo &a_info );
        void beamMetrics( const BeamMetrics &a_metrics );
        void runMetrics( const RunMetrics &a_metrics );
        void streamMetrics( const StreamMetrics &a_metrics );
        void pvDefined( const std::string &a_name );
        void pvUndefined( const std::string &a_name );
        void pvValue( const std::string &a_name,
                 uint32_t a_value, VariableStatus::Enum a_status,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec );
        void pvValue( const std::string &a_name,
                 double a_value, VariableStatus::Enum a_status,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec );
        void pvValue( const std::string &a_name,
                 std::string &a_value, VariableStatus::Enum a_status,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec );
        void pvValue( const std::string &a_name,
                 std::vector<uint32_t> a_value,
                 VariableStatus::Enum a_status,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec );
        void pvValue( const std::string &a_name,
                 std::vector<double> a_value,
                 VariableStatus::Enum a_status,
                 uint32_t a_timestamp, uint32_t a_timestamp_nanosec );
        void connectionStatus( bool a_connected,
                 const std::string &a_host, unsigned short a_port );

    private:
        std::vector<IStreamListener*>   m_listeners;
    };

    void        startProcessing();
    void        stopProcessing();
    void        processThread();
    int         connect();
    void        handleLostConnection();
    void        resetStreamStats();
    void        resetRunStats();
    void        metricsThread();
    void        dbThread();


    bool        rxPacket( const ADARA::Packet &a_pkt );
    bool        rxPacket( const ADARA::RunStatusPkt &a_pkt );
    bool        rxPacket( const ADARA::BankedEventPkt &a_pkt );
    bool        rxPacket( const ADARA::BankedEventStatePkt &a_pkt );
    bool        rxPacket( const ADARA::PixelMappingPkt &a_pkt );
    bool        rxPacket( const ADARA::PixelMappingAltPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamMonitorPkt &a_pkt );
    bool        rxPacket( const ADARA::RunInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamlineInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::VariableStringPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32ArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoubleArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableStringPkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableU32ArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableDoubleArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::AnnotationPkt &a_pkt );

    // Shunt remaining rxPacket flavors to base class implementations
    using ADARA::POSIXParser::rxPacket;

    void        getXmlNodeValue( xmlNode *a_node,
                    std::string & a_value ) const;
    template<class T>
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id,
                    T a_value,
                    const timespec &a_timestamp,
                    VariableStatus::Enum a_status );
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id,
                    std::vector<uint32_t> a_value,
                    const timespec &a_timestamp,
                    VariableStatus::Enum a_status );
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id,
                    std::vector<double> a_value,
                    const timespec &a_timestamp,
                    VariableStatus::Enum a_status );
    PVType      toPVType( const char *a_source ) const;
    void        clearPVs();

    struct BankInfo
    {
        BankInfo() : m_bank_id(0), m_source_id(0), m_last_pulse_time(0) {}
        BankInfo( uint16_t a_bank_id )
            : m_bank_id(a_bank_id), m_source_id(0), m_last_pulse_time(0) {}

        uint16_t    m_bank_id;
        uint32_t    m_source_id;
        uint64_t    m_last_pulse_time;
    };

    int                             m_fd_in;
    std::string                     m_sms_host;
    unsigned short                  m_sms_port;
    boost::thread                  *m_stream_thread;
    boost::thread                  *m_metrics_thread;
    bool                            m_process_stream;
    Notifier                        m_notify;
    uint32_t                        m_bank_count;
    CountInfo<uint64_t>             m_bank_count_info;
    std::map<uint32_t,CountInfo<uint64_t> >     m_mon_count_info;
    std::map<uint32_t,uint64_t>     m_mon_last_pulse;
    uint64_t                        m_mon_event_count;
    bool                            m_recording;
    uint32_t                        m_run_num;
    uint32_t                        m_run_timestamp;
    uint32_t                        m_run_timestamp_nanosec;
    bool                            m_paused;
    short                           m_info_rcv;
    BeamInfo                        m_beam_info;
    RunInfo                         m_run_info;
    RunMetrics                      m_run_metrics;
    StreamMetrics                   m_stream_metrics;
    bool                            m_scanning;
    uint32_t                        m_scan_index;
    CountInfo<double>               m_pcharge;
    CountInfo<double>               m_pfreq;
    uint64_t                        m_first_pulse_time;
    uint64_t                        m_last_pulse_time;
    uint64_t                        m_stream_size;
    uint64_t                        m_stream_rate;
    std::map<PVKey,PVInfoBase*>     m_pvs;
    mutable boost::mutex            m_mutex;
    mutable boost::mutex            m_api_mutex;
    bool                            m_diagnostics;
    uint32_t                        m_last_cycle;
    uint64_t                        m_last_time;
    uint64_t                        m_this_time;
    uint32_t                        m_bnk_pkt_count;
    uint32_t                        m_bnk_state_pkt_count;
    uint32_t                        m_mon_pkt_count;
    uint32_t                        m_maxtof;
    std::map<int16_t,BankInfo>      m_bank_info;
    std::vector<uint32_t>           m_sources;
    std::vector<int16_t>            m_pixbankmap;
    bool                            m_pixbankmap_processed;
    uint32_t                        m_proc_ticker;      ///< "Alive" indicator for stream processing thread
    static uint32_t                 m_proc_state;       ///< General state/step of stream processing thread.
    static uint32_t                 m_notify_state;
    uint32_t                        m_metrics_ticker;   ///< "Alive" indicator for stream metrics thread
    uint32_t                        m_metrics_state;    ///< General state/step of stream metrics thread.
    bool                            m_in_prolog;

#ifndef NO_DB
    DBConnectInfo*                  m_db_info;
    boost::thread                  *m_db_thread;
    uint32_t                        m_db_ticker;        ///< "Alive" indicator for db thread
    uint32_t                        m_db_state;         ///< General state/step of db thread.
#endif
};

}}

#endif // MONITORADAPTER_H

// vim: expandtab

