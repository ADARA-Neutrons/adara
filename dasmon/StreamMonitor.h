#ifndef MONITORADAPTER_H
#define MONITORADAPTER_H

#include <ADARAParser.h>
#include <map>
#include <vector>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <libxml/tree.h>

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
    virtual void runStatus( bool a_recording, unsigned long a_run_number ) = 0;
    virtual void pauseStatus( bool a_paused ) = 0;
    virtual void scanStart( unsigned long a_scan_number ) = 0;
    virtual void scanStop() = 0;
    virtual void runTitle( const std::string &a_run_title ) = 0;
    virtual void facilityName( const std::string &a_facility_name ) = 0;
    virtual void beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name ) = 0;
    virtual void proposalID( const std::string &a_proposal_id ) = 0;
    virtual void sampleID( const std::string &a_sample_id ) = 0;
    virtual void sampleName( const std::string &a_sample_name ) = 0;
    virtual void sampleNature( const std::string &a_sample_nature ) = 0;
    virtual void sampleFormula( const std::string &a_sample_formula ) = 0;
    virtual void sampleEnvironment( const std::string &a_sample_environment ) = 0;
    virtual void userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole ) = 0;
    virtual void pvDefined( const std::string &a_name ) = 0;
    virtual void pvValue( const std::string &a_name, double a_value ) = 0;
    virtual void connectionStatus( bool a_connected ) = 0;
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
        m_type(a_type)
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

    // Polling API for instantaneous values

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

private:
    class Notifier : public IStreamListener
    {
    public:
        void addListener( IStreamListener &a_listener );
        void removeListener( IStreamListener &a_listener );

        void runStatus( bool a_recording, unsigned long a_run_number  );
        void pauseStatus( bool a_paused );
        void scanStart( unsigned long a_scan_number );
        void scanStop();
        void runTitle( const std::string &a_run_title );
        void facilityName( const std::string &a_facility_name );
        void beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name );
        void proposalID( const std::string &a_proposal_id );
        void sampleID( const std::string &a_sample_id );
        void sampleName( const std::string &a_sample_name );
        void sampleNature( const std::string &a_sample_nature );
        void sampleFormula( const std::string &a_sample_formula );
        void sampleEnvironment( const std::string &a_sample_environment );
        void userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole );
        void pvDefined( const std::string &a_name );
        void pvValue( const std::string &a_name, double a_value );
        void connectionStatus( bool a_connected );

    private:
        std::vector<IStreamListener*>   m_listeners;
    };

    void        processStream();
    int         connect();
    void        handleLostConnection();
    void        resetStreamStats();
    void        resetRunStats();


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
    bool                            m_process_stream;
    Notifier                        m_notify;
    std::map<uint32_t,PktStats>     m_stats;
    uint32_t                        m_bank_count;
    CountInfo<uint64_t>             m_bank_count_info;
    std::map<uint32_t,CountInfo<uint64_t> >     m_mon_count_info;
    ADARA::RunStatus::Enum          m_run_status;
    bool                            m_scanning;
    CountInfo<double>               m_pcharge;
    CountInfo<double>               m_pfreq;
    uint64_t                        m_pcount;
    CountInfo<uint64_t>             m_run_dup_pulse_count;
    CountInfo<uint64_t>             m_run_cycle_error_count;
    CountInfo<uint64_t>             m_run_pixel_error_count;
    uint64_t                        m_first_pulse_time;
    uint64_t                        m_last_pulse_time;
    uint64_t                        m_stream_size;
    uint64_t                        m_stream_rate;
    std::map<PVKey,PVInfo*>         m_pvs;
    boost::mutex                    m_mutex;
    mutable boost::mutex            m_api_mutex;
};

#endif // MONITORADAPTER_H
