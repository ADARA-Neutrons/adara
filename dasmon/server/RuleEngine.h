#ifndef RULEENGINE_H
#define RULEENGINE_H

#include "StreamMonitor.h"
#include "RuleDefs.h"
#include "Rule.h"

#include <map>
#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

namespace RuleEngine
{

class StreamAnalyzer: public ADARA::DASMON::IStreamListener
{
public:
    StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor );
    virtual ~StreamAnalyzer();

    void    addListener( IRuleListener &a_listener );
    void    removeListener( IRuleListener &a_listener );
    void    configureRules( std::vector<RuleSettings> &a_rule_settings );
    void    resendAsserted( IRuleListener &a_listener );

    static const char *toString( RuleType a_type );

protected:

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

private:
    enum BIR
    {
        BIR_INVALID = 0,

        // EVENTS
        BIR_EVENT_LOST_STREAM_CONNECTION = 1,
        BIR_EVENT_DUPLICATE_PULSE,
        BIR_EVENT_PULSE_CYCLE_ERROR,
        BIR_EVENT_RECORD_START,
        BIR_EVENT_RECORD_STOP,
        BIR_EVENT_PAUSE_START,
        BIR_EVENT_PAUSE_STOP,
        BIR_EVENT_SCAN_START,
        BIR_EVENT_SCAN_STOP,

        // SIGNALS - Nonnumeric (defined/not defined only)
        BIR_SIGNAL_NONNUMERIC,

        BIR_RECORDING = BIR_SIGNAL_NONNUMERIC,
        BIR_PAUSED,
        BIR_SCANNING,
        BIR_FACILITY_NAME,
        BIR_BEAM_ID,
        BIR_BEAM_SHORT_NAME,
        BIR_BEAM_LONG_NAME,
        BIR_RUN_TITLE,
        BIR_PROPOSAL_ID,
        BIR_PROPOSAL_TITLE,
        BIR_SAMPLE_ID,
        BIR_SAMPLE_NAME,
        BIR_SAMPLE_ENV,
        BIR_SAMPLE_FORM,
        BIR_SAMPLE_NATURE,
        BIR_USER_INFO,

        // SIGNALS - Numeric (operators only)
        BIR_SIGNAL_NUMERIC,

        BIR_RUN_NUMBER = BIR_SIGNAL_NUMERIC,
        BIR_SCAN_NUMBER,
        BIR_COUNT_RATE,
        BIR_MONITOR0_COUNT_RATE,
        BIR_MONITOR1_COUNT_RATE,
        BIR_MONITOR2_COUNT_RATE,
        BIR_MONITOR3_COUNT_RATE,
        BIR_MONITOR4_COUNT_RATE,
        BIR_MONITOR5_COUNT_RATE,
        BIR_MONITOR6_COUNT_RATE,
        BIR_MONITOR7_COUNT_RATE,
        BIR_PIXEL_ERROR_RATE,
        BIR_PULSE_CHARGE,
        BIR_PULSE_FREQ,
        BIR_STREAM_RATE,
        BIR_RUN_PULSE_CHARGE,
        BIR_RUN_PIXEL_ERROR_COUNT,
        BIR_RUN_DUP_PULSE_COUNT,
        BIR_RUN_CYCLE_ERROR_COUNT,

        BIR_TOTAL_RULE_COUNT
    };

    void            defineRule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value );
    void            definePvRule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value );
    void            populateBIRTable();
    BIR             toBIR( const char *a_name );
    RuleCategory    getCategory( RuleType a_type );
    RuleCategory    getCategory( BIR a_bir );
    void            deleteRules();

    ADARA::DASMON::StreamMonitor       &m_monitor;
    std::map<uint32_t,uint64_t>         m_monitor_rate;
    bool                                m_monitorx_rate;
    //boost::thread                      *m_poll_thread;
    std::vector<RuleGroup*>             m_bir;
    std::map<std::string,RuleGroup*>    m_rules;
    std::map<std::string,RuleGroup*>    m_pv_rules;
    std::vector<IRuleListener*>         m_listeners;
    std::map<std::string,BIR>           m_bir_names;
    boost::mutex                        m_mutex;
};


}

#endif // RULEENGINE_H
