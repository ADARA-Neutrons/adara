#ifndef STREAMANALYZER_H
#define STREAMANALYZER_H

#include "ADARADefs.h"
#include "StreamMonitor.h"
#include "RuleEngine.h"

#include <map>
#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>


class ISignalListener
{
public:
    virtual void    signalAssert( const std::string &a_name, const std::string &a_source, ADARA::Level a_level, const std::string &a_msg ) = 0;
    virtual void    signalRetract( const std::string &a_name ) = 0;
};


class StreamAnalyzer: public ADARA::DASMON::IStreamListener, public IFactListener
{
public:
    StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor );
    virtual ~StreamAnalyzer();

    void    loadConfig( const std::string &a_file );
    void    addListener( ISignalListener &a_listener );
    void    removeListener( ISignalListener &a_listener );
    void    defineSignal( const std::string &a_expression );
    void    resendState();

private:

    // IStreamListener Interface
    void runStatus( bool a_recording, unsigned long a_run_number );
    void pauseStatus( bool a_paused );
    void scanStatus( bool a_scanning, unsigned long a_scan_number );
    void beamInfo( const ADARA::DASMON::BeamInfo &a_info );
    void runInfo( const ADARA::DASMON::RunInfo &a_run_info );
    void beamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics );
    void runMetrics( const ADARA::DASMON::RunMetrics &a_metrics );
    void pvDefined( const std::string &a_name );
    void pvValue( const std::string &a_name, double a_value );
    void connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

    // IFactListener Interface
    void onAssert( const std::string &a_fact );
    void onRetract( const std::string &a_fact );

    struct SignalInfo
    {
        std::string     sig_name;
        std::string     sig_source;
        ADARA::Level    sig_level;
        std::string     sig_msg;
    };

    ADARA::DASMON::StreamMonitor       &m_monitor;
    RuleEngine                          m_engine;
    std::map<uint32_t,uint64_t>         m_monitor_rate;
    bool                                m_monitorx_rate;
    std::vector<ISignalListener*>       m_listeners;
    std::map<std::string,SignalInfo>    m_signals;
    std::string                         m_pv_prefix;
    boost::mutex                        m_mutex;

    HFACT   m_fact_recording;
    HFACT   m_fact_run_number;
    HFACT   m_fact_paused;
    HFACT   m_fact_scanning;
    HFACT   m_fact_scan_index;
    HFACT   m_fact_facility_name;
    HFACT   m_fact_beam_id;
    HFACT   m_fact_beam_sname;
    HFACT   m_fact_beam_lname;
    HFACT   m_fact_run_title;
    HFACT   m_fact_prop_id;
    HFACT   m_fact_sample_id;
    HFACT   m_fact_sample_name;
    HFACT   m_fact_sample_nature;
    HFACT   m_fact_sample_form;
    HFACT   m_fact_sample_env;
    HFACT   m_fact_user_info;
    HFACT   m_fact_count_rate;
    HFACT   m_fact_mon_count_rate[8];
    HFACT   m_fact_pulse_charge;
    HFACT   m_fact_pulse_freq;
    HFACT   m_fact_stream_rate;
    HFACT   m_fact_run_pulse_charge;
    HFACT   m_fact_pix_err_count;
    HFACT   m_fact_dup_pulse_count;
    HFACT   m_fact_cycle_err_count;
    HFACT   m_fact_sms_connected;
};


#endif // RULEENGINE_H
