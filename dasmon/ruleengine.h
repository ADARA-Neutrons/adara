#ifndef RULEENGINE_H
#define RULEENGINE_H

#include "StreamMonitor.h"
#include "ruledefs.h"
#include "rule.h"

#include <map>
#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

namespace RuleEng
{

class RuleEngine: public IStreamListener
{
public:
    RuleEngine( StreamMonitor &a_monitor );
    virtual ~RuleEngine();

    void    addListener( IRuleListener &a_listener );
    void    removeListener( IRuleListener &a_listener );
    void    configureRules( std::vector<RuleSettings> &a_rule_settings );

    static const char *toString( RuleType a_type );
    static const char *toString( RuleSeverity a_type );

protected:

    void runStatus( bool a_recording, unsigned long a_run_number );
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
        BIR_PIXEL_ERROR_COUNT,
        BIR_PIXEL_ERROR_RATE,
        BIR_DUPLICATE_PULSE_COUNT,
        BIR_CYCLE_ERROR_COUNT,
        BIR_PULSE_CHARGE,
        BIR_PULSE_FREQ,
        BIR_STREAM_RATE,

        BIR_TOTAL_RULE_COUNT
    };

    void            defineRule( const std::string &a_item, RuleType a_type, RuleSeverity a_severity, double a_value );
    void            definePvRule( const std::string &a_item, RuleType a_type, RuleSeverity a_severity, double a_value );
    void            populateBIRTable();
    BIR             toBIR( const char *a_name );
    RuleCategory    getCategory( RuleType a_type );
    RuleCategory    getCategory( BIR a_bir );
    void            pollThread();
    void            deleteRules();

    StreamMonitor                     &m_monitor;
    std::map<uint32_t,uint64_t>         m_monitor_rate;
    bool                                m_monitorx_rate;
    boost::thread                      *m_poll_thread;
    std::vector<RuleGroup*>             m_bir;
    std::map<std::string,RuleGroup*>    m_rules;
    std::map<std::string,RuleGroup*>    m_pv_rules;
    std::vector<IRuleListener*>         m_listeners;
    std::map<std::string,BIR>           m_bir_names;
    boost::mutex                        m_mutex;
    bool                                m_exit_flag;
};


}

#endif // RULEENGINE_H
