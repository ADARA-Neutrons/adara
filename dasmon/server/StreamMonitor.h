#ifndef STREAMMONITOR_H
#define STREAMMONITOR_H

#include <ADARAParser.h>
#include <map>
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
    virtual void runStatus( bool a_recording, unsigned long a_run_number ) = 0;
    virtual void pauseStatus( bool a_paused ) = 0;
    virtual void scanStatus( bool a_scanning, unsigned long a_scan_number ) = 0;
    virtual void beamInfo( const BeamInfo &a_info ) = 0;
    virtual void runInfo( const RunInfo &a_info ) = 0;
    virtual void beamMetrics( const BeamMetrics &a_metrics ) = 0;
    virtual void runMetrics( const RunMetrics &a_metrics ) = 0;
    virtual void pvDefined( const std::string &a_name ) = 0;
    virtual void pvValue( const std::string &a_name, double a_value ) = 0;
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
class PVInfo
{
public:
    /// PVInfoBase constructor
    PVInfo
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
        m_time(0.0),
        m_value(0.0)
    {}

    /// PVInfoBase destructor
    virtual ~PVInfo()
    {}

    std::string         m_name;         ///< Name of PV
    Identifier          m_device_id;    ///< ID of device that owns the PV
    Identifier          m_pv_id;        ///< ID of the PV
    PVType              m_type;         ///< Type of PV
    double              m_time;
    double              m_value;
};


#define WIN_AVG_SIZE 20


template<class T>
class CountInfo
{
public:
    CountInfo()
        : total(0), average(0.0), idx(0)
    {
        for ( int i = 0; i < WIN_AVG_SIZE; ++i )
            samples[i] = 0;
    }

    void addSample( T sample )
    {
        total += sample;
        average -= ((double)samples[idx])/WIN_AVG_SIZE;
        samples[idx++] = sample;

        if ( idx >= WIN_AVG_SIZE )
            idx = 0;

        average += ((double)sample)/WIN_AVG_SIZE;
    }

    void reset()
    {
        total = 0;
        average = 0.0;
        idx = 0;

        for ( int i = 0; i < WIN_AVG_SIZE; ++i )
            samples[i] = 0;
    }

    T           samples[WIN_AVG_SIZE];
    T           total;
    double      average;
    uint16_t    idx;
};


class StreamMonitor : public ADARA::Parser
{
public:
    StreamMonitor( const std::string &a_sms_host, unsigned short a_port = 31415 );
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

    // Polling API for instantaneous values
#if 0
    uint64_t        getCountRate();
    void            getMonitorCountRates( std::map<uint32_t,uint64_t> &a_monitor_count_rates );
    double          getProtonCharge();
    double          getPulseFrequency();
    uint64_t        getRunPulseCount();
    double          getRunPulseCharge();
    uint64_t        getPixelErrorCount();
    uint64_t        getPixelErrorRate();
    uint64_t        getDuplicatePulseCount();
    uint64_t        getCycleErrorCount();
    uint64_t        getByteRate() const;
    void            getStatistics( std::map<uint32_t,PktStats> &a_stats );
    const char*     getPktName( uint32_t a_pkt_type ) const;
    bool            getPV( const std::string &a_pv_name, double &a_value ) const;
#endif

private:
    class Notifier : public IStreamListener
    {
    public:
        void addListener( IStreamListener &a_listener );
        void removeListener( IStreamListener &a_listener );

        void runStatus( bool a_recording, unsigned long a_run_number  );
        void pauseStatus( bool a_paused );
        void scanStatus( bool a_scanning, unsigned long a_scan_number );
        void beamInfo( const BeamInfo &a_info );
        void runInfo( const RunInfo &a_info );
        void beamMetrics( const BeamMetrics &a_metrics );
        void runMetrics( const RunMetrics &a_metrics );
        void pvDefined( const std::string &a_name );
        void pvValue( const std::string &a_name, double a_value );
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

    using ADARA::Parser::rxPacket; // Shunt remaining rxPacket flavors to base class implementations

    void        gatherStats( const ADARA::Packet &a_pkt );
    void        getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const;
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id, double a_value, const timespec &a_timestamp );
    PVType      toPVType( const char *a_source ) const;
    void        clearPVs();

    int                             m_fd_in;
    std::string                     m_sms_host;
    unsigned short                  m_sms_port;
    boost::thread                  *m_stream_thread;
    boost::thread                  *m_metrics_thread;
    bool                            m_process_stream;
    Notifier                        m_notify;
    std::map<uint32_t,PktStats>     m_stats;
    uint32_t                        m_bank_count;
    CountInfo<uint64_t>             m_bank_count_info;
    std::map<uint32_t,CountInfo<uint64_t> >     m_mon_count_info;
    bool                            m_recording;
    unsigned long                   m_run_num;
    bool                            m_paused;
    short                           m_info_rcv;
    BeamInfo                        m_beam_info;
    BeamMetrics                     m_beam_metrics;
    RunInfo                         m_run_info;
    RunMetrics                      m_run_metrics;
    bool                            m_scanning;
    unsigned long                   m_scan_index;
    CountInfo<double>               m_pcharge;
    CountInfo<double>               m_pfreq;
    uint64_t                        m_first_pulse_time;
    uint64_t                        m_last_pulse_time;
    uint64_t                        m_stream_size;
    uint64_t                        m_stream_rate;
    std::map<PVKey,PVInfo*>         m_pvs;
    mutable boost::mutex            m_mutex;
    mutable boost::mutex            m_api_mutex;
};

}}

#endif // MONITORADAPTER_H
