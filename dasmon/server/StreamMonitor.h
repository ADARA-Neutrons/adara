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

namespace ADARA {
namespace DASMON {

struct PktStats
{
    PktStats() : count(0), min_pkt_size(0), max_pkt_size(0), total_size(0) {}

    uint64_t    count;
    uint64_t    min_pkt_size;
    uint64_t    max_pkt_size;
    uint64_t    total_size;
};


class IStreamListener
{
public:
    virtual void connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port ) = 0;
    virtual void runStatus( bool a_recording, uint32_t a_run_number, uint32_t a_timestamp ) = 0;
    virtual void pauseStatus( bool a_paused ) = 0;
    virtual void scanStatus( bool a_scanning, uint32_t a_scan_number ) = 0;
    virtual void beamInfo( const BeamInfo &a_info ) = 0;
    virtual void runInfo( const RunInfo &a_info ) = 0;
    virtual void beamMetrics( const BeamMetrics &a_metrics ) = 0;
    virtual void runMetrics( const RunMetrics &a_metrics ) = 0;
    virtual void streamMetrics( const StreamMetrics &a_metrics ) = 0;
    virtual void pvDefined( const std::string &a_name ) = 0;
    virtual void pvUndefined( const std::string &a_name ) = 0;
    virtual void pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status, uint32_t a_timestamp ) = 0;
    virtual void pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp ) = 0;
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
    PVT_ENUM
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
        m_time(0),
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

#define WIN_AVG_SIZE 20


template<class T>
class CountInfo
{
public:
    CountInfo()
        : total(0), idx(0)
    {
        for ( int i = 0; i < WIN_AVG_SIZE; ++i )
            samples[i] = 0;
    }

    void addSample( T sample )
    {
        total += sample;
        samples[idx++] = sample;

        if ( idx == WIN_AVG_SIZE )
            idx = 0;
    }

    void reset()
    {
        total = 0;
        idx = 0;

        for ( int i = 0; i < WIN_AVG_SIZE; ++i )
            samples[i] = 0;
    }

    double average()
    {
        double avg = 0.0;

        for ( int i = 0; i < WIN_AVG_SIZE; ++i )
            avg += samples[i];

        return avg/WIN_AVG_SIZE;
    }

    T           samples[WIN_AVG_SIZE];
    T           total;
    uint16_t    idx;
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

class StreamMonitor : public ADARA::POSIXParser
{
public:
#ifndef NO_DB
    StreamMonitor( const std::string &a_sms_host, unsigned short a_sms_port, DBConnectInfo *a_db_info, uint32_t a_maxtof );
#else
    StreamMonitor( const std::string &a_sms_host, unsigned short a_sms_port = 31415 );
#endif
    virtual ~StreamMonitor();

    void            getSMSHostInfo( std::string &a_hostname, unsigned short &a_port ) const;
    void            setSMSHostInfo( std::string a_hostname, unsigned short a_port );
    void            start();
    void            stop();
    void            addListener( IStreamListener &a_listener )
                    { m_notify.addListener( a_listener ); }
    void            removeListener( IStreamListener &a_listener )
                    { m_notify.removeListener( a_listener ); }
    void            resendState( IStreamListener &a_listener ) const;
    bool            isOK() const { return m_ok; }
    void            enableDiagnostics( bool a_diagnostics ) { m_diagnostics = a_diagnostics; }

private:
    class Notifier : public IStreamListener
    {
    public:
        void addListener( IStreamListener &a_listener );
        void removeListener( IStreamListener &a_listener );

        void runStatus( bool a_recording, uint32_t a_run_number, uint32_t a_timestamp  );
        void pauseStatus( bool a_paused );
        void scanStatus( bool a_scanning, uint32_t a_scan_number );
        void beamInfo( const BeamInfo &a_info );
        void runInfo( const RunInfo &a_info );
        void beamMetrics( const BeamMetrics &a_metrics );
        void runMetrics( const RunMetrics &a_metrics );
        void streamMetrics( const StreamMetrics &a_metrics );
        void pvDefined( const std::string &a_name );
        void pvUndefined( const std::string &a_name );
        void pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status, uint32_t a_timestamp );
        void pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp );
        void connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

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
    bool        rxPacket( const ADARA::PixelMappingPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamMonitorPkt &a_pkt );
    bool        rxPacket( const ADARA::RunInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamlineInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::AnnotationPkt &a_pkt );

    using ADARA::POSIXParser::rxPacket; // Shunt remaining rxPacket flavors to base class implementations

    void        getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const;
    template<class T>
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id, T a_value, const timespec &a_timestamp, VariableStatus::Enum a_status );
    PVType      toPVType( const char *a_source ) const;
    void        clearPVs();

    int                             m_fd_in;
    std::string                     m_sms_host;
    unsigned short                  m_sms_port;
    boost::thread                  *m_stream_thread;
    boost::thread                  *m_metrics_thread;
    bool                            m_process_stream;
    Notifier                        m_notify;
    //std::map<uint32_t,PktStats>     m_stats;
    uint32_t                        m_bank_count;
    CountInfo<uint64_t>             m_bank_count_info;
    std::map<uint32_t,CountInfo<uint64_t> >     m_mon_count_info;
    std::map<uint32_t,uint64_t>     m_mon_last_pulse;
    uint64_t                        m_mon_event_count;
    bool                            m_recording;
    uint32_t                        m_run_num;
    uint32_t                        m_run_timestamp;
    bool                            m_paused;
    short                           m_info_rcv;
    BeamInfo                        m_beam_info;
    BeamMetrics                     m_beam_metrics;
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
    bool                            m_ok;
    mutable boost::mutex            m_mutex;
    mutable boost::mutex            m_api_mutex;
    bool                            m_diagnostics;
    uint32_t                        m_last_cycle;
    uint64_t                        m_last_time;
    uint64_t                        m_this_time;
    uint32_t                        m_bnk_pkt_count;
    uint32_t                        m_mon_pkt_count;
    uint32_t                        m_maxtof;

    struct BankInfo
    {
        BankInfo() : m_bank_id(0), m_source_id(0), m_last_pulse_time(0) {}
        BankInfo( uint16_t a_bank_id ) : m_bank_id(a_bank_id), m_source_id(0), m_last_pulse_time(0) {}

        uint16_t    m_bank_id;
        uint32_t    m_source_id;
        uint64_t    m_last_pulse_time;
    };

    std::map<int16_t,BankInfo>      m_bank_info;
    std::vector<uint32_t>           m_sources;
    std::vector<int16_t>            m_pixmap;
    bool                            m_pixmap_processed;
#ifndef NO_DB
    DBConnectInfo*                  m_db_info;
    boost::thread                  *m_db_thread;
#endif
};

}}

#endif // MONITORADAPTER_H
