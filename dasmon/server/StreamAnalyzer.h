#ifndef STREAMANALYZER_H
#define STREAMANALYZER_H

#include "ADARADefs.h"
#include "StreamMonitor.h"
#include "RuleEngine.h"

#include <map>
#include <set>
#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

namespace ADARA {
namespace DASMON {


class StreamAnalyzer: public IStreamListener, public RuleEngine::IFactListener
{
public:
    class ISignalListener
    {
    public:
        virtual void    signalAssert( const SignalInfo &a_signal ) = 0;
        virtual void    signalRetract( const std::string &a_name ) = 0;
    };

    StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor, const std::string &a_cfg_file );
    virtual ~StreamAnalyzer();

    void    setConfigSource( const std::string &a_file );
    void    loadConfig();
    void    saveConfig();
    void    restoreDefaultConfig();
    void    setDefaultConfig();
    void    attach( ISignalListener &a_listener );
    void    detach( ISignalListener &a_listener );
    void    defineSignal( const std::string &a_expression );
    void    resendState();
    void    getDefinitions( std::vector<RuleEngine::RuleInfo> &a_rules, std::vector<SignalInfo> &a_signals );
    bool    setDefinitions( const std::vector<RuleEngine::RuleInfo> &a_rules, const std::vector<SignalInfo> &a_signals );
    void    getInputFacts( std::map<std::string,std::string> &a_facts ) const;

private:
    std::map<std::string,SignalInfo>::iterator    findByName( std::map<std::string,SignalInfo> &a_map, std::string a_name );

    enum BIF
    {
        BIF_RECORDING = 0,
        BIF_RUN_NUMBER,
        BIF_PAUSED,
        BIF_SCANNING,
        BIF_SCAN_INDEX,
        BIF_FAC_NAME,
        BIF_BEAM_ID,
        BIF_BEAM_SNAME,
        BIF_BEAM_LNAME,
        BIF_RUN_TITLE,
        BIF_PROP_ID,
        BIF_SAMPLE_ID,
        BIF_SAMPLE_NAME,
        BIF_SAMPLE_FORM,
        BIF_SAMPLE_NAT,
        BIF_SAMPLE_ENV,
        BIF_USER_INFO,
        BIF_COUNT_RATE,
        //BIF_MON1_COUNT_RATE,
        //BIF_MON2_COUNT_RATE,
        //BIF_MON3_COUNT_RATE,
        //BIF_MON4_COUNT_RATE,
        //BIF_MON5_COUNT_RATE,
        //BIF_MON6_COUNT_RATE,
        //BIF_MON7_COUNT_RATE,
        //BIF_MON8_COUNT_RATE,
        BIF_PULSE_CHARGE,
        BIF_PULSE_FREQ,
        BIF_STREAM_RATE,
        BIF_RUN_PULSE_CHARGE,
        BIF_PIX_ERR_COUNT,
        BIF_DUP_PULSE_COUNT,
        BIF_CYCLE_ERR_COUNT,
        BIF_SMS_CONNECTED,
        BIF_GENERAL_PV_LIMIT,
        BIF_GENERAL_PV_ERROR,
        BIF_COUNT
    };

    // IStreamListener Interface
    void runStatus( bool a_recording, unsigned long a_run_number );
    void pauseStatus( bool a_paused );
    void scanStatus( bool a_scanning, unsigned long a_scan_number );
    void beamInfo( const BeamInfo &a_info );
    void runInfo( const RunInfo &a_run_info );
    void beamMetrics( const BeamMetrics &a_metrics );
    void runMetrics( const RunMetrics &a_metrics );
    void pvDefined( const std::string &a_name );
    void pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status );
    void pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status );
    void connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

    // IFactListener Interface
    void onAssert( const std::string &a_fact );
    void onRetract( const std::string &a_fact );

    void processPvStatus( const std::string &pv_name, VariableStatus::Enum a_status );

    ADARA::DASMON::StreamMonitor       &m_monitor;
    RuleEngine                         *m_engine;
    std::map<uint32_t,uint64_t>         m_monitor_rate;
    bool                                m_monitorx_rate;
    std::vector<ISignalListener*>       m_listeners;
    std::map<std::string,SignalInfo>    m_signals;
    std::string                         m_pv_prefix;
    std::string                         m_pv_err_prefix;
    std::string                         m_pv_lim_prefix;
    boost::mutex                        m_mutex;
    boost::mutex                        m_list_mutex;
    std::string                         m_cfg_dir;
    std::set<std::string>               m_error_pvs;
    std::set<std::string>               m_limit_pvs;
    RuleEngine::HFACT                   m_fact[BIF_COUNT];
    std::string                         m_fact_name[BIF_COUNT];
};

}}

#endif // RULEENGINE_H
