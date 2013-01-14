#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include "ruleengine.h"

#include <iostream>

using namespace std;

namespace RuleEng
{

#define EVAL_SIGNAL( bir, cond ) \
{ if ( m_bir[bir] ) { m_bir[bir]->evaluate( cond, m_listeners ); }}

#define EVAL_EVENT( bir ) \
{ if ( m_bir[bir] ) { m_bir[bir]->event( m_listeners ); }}


RuleEngine::RuleEngine( StreamMonitor &a_monitor )
    :m_monitor(a_monitor), m_monitorx_rate(false), m_exit_flag(false)
{
    populateBIRTable();

    m_poll_thread = new boost::thread( boost::bind( &RuleEngine::pollThread, this ));

    m_bir.resize( BIR_TOTAL_RULE_COUNT, 0 );

    vector<RuleSettings>    rules;

    rules.push_back(RuleSettings( "event lost stream connection", EVENT, CRITICAL ));
    rules.push_back(RuleSettings( "event record start", EVENT, INFO ));
    rules.push_back(RuleSettings( "event record stop", EVENT, INFO ));
    rules.push_back(RuleSettings( "event pause start", EVENT, INFO ));
    rules.push_back(RuleSettings( "event pause stop", EVENT, INFO ));
    rules.push_back(RuleSettings( "event scan start", EVENT, INFO ));
    rules.push_back(RuleSettings( "event scan stop", EVENT, INFO ));

    rules.push_back(RuleSettings( "recording", DEFINED, INFO ));
    rules.push_back(RuleSettings( "recording", NOT_DEFINED, INFO ));
    rules.push_back(RuleSettings( "paused", DEFINED, INFO ));
    rules.push_back(RuleSettings( "paused", NOT_DEFINED, INFO ));
    rules.push_back(RuleSettings( "scanning", DEFINED, INFO ));
    rules.push_back(RuleSettings( "scanning", NOT_DEFINED, INFO ));
    rules.push_back(RuleSettings( "facility name", NOT_DEFINED, CRITICAL ));
    rules.push_back(RuleSettings( "proposal id", NOT_DEFINED, CRITICAL ));

    rules.push_back(RuleSettings( "pulse charge", LT, MINOR, 1.2e5 ));
    rules.push_back(RuleSettings( "pulse frequency", LT, MINOR, 58 ));
    rules.push_back(RuleSettings( "pulse frequency", GT, MINOR, 62 ));
    rules.push_back(RuleSettings( "event rate", LT, MINOR, 10 ));
    rules.push_back(RuleSettings( "event rate", GT, MINOR, 1000 ));
    rules.push_back(RuleSettings( "monitor0 rate", LT, MINOR, 65 ));
    rules.push_back(RuleSettings( "monitor1 rate", LT, MINOR, 125 ));
    rules.push_back(RuleSettings( "stream rate", LT, MINOR, 100 ));

    rules.push_back(RuleSettings( "energyReq", NOT_DEFINED, CRITICAL, 0.0, true ));
    rules.push_back(RuleSettings( "energyReq", LTE, CRITICAL, 0.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", NOT_DEFINED, MAJOR, 0.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", LT, MINOR, 180.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", GT, MINOR, 190.0, true ));

    configureRules(  rules );

    m_monitor.addListener( *this );
}


RuleEngine::~RuleEngine()
{
    m_monitor.removeListener( *this );
    m_exit_flag = true;
    m_poll_thread->join();
    deleteRules();
}


void
RuleEngine::addListener( IRuleListener &a_listener )
{
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
    {
        m_listeners.push_back( &a_listener );

        // Send all asserted rules to this listener
        for ( map<string,RuleGroup*>::iterator igrp = m_rules.begin(); igrp != m_rules.end(); ++igrp )
            igrp->second->resendAsserted( a_listener );

        for ( map<string,RuleGroup*>::iterator igrp = m_pv_rules.begin(); igrp != m_pv_rules.end(); ++igrp )
            igrp->second->resendAsserted( a_listener );
    }
}


void
RuleEngine::removeListener( IRuleListener &a_listener )
{
    vector<IRuleListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}


void
RuleEngine::configureRules( vector<RuleSettings> &a_rule_settings )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        deleteRules();

        for ( vector<RuleSettings>::iterator r = a_rule_settings.begin(); r != a_rule_settings.end(); ++r )
        {
            if ( r->m_pv )
                definePvRule( r->m_item, r->m_type, r->m_severity, r->m_value );
            else
                defineRule( r->m_item, r->m_type, r->m_severity, r->m_value );
        }

        m_monitorx_rate = false;
        for ( int i = 0; i < 8; ++i )
        {
            if ( m_bir[BIR_MONITOR0_COUNT_RATE+i] )
            {
                m_monitorx_rate = true;
                break;
            }
        }
    }
    catch(...)
    {
        deleteRules();
        throw;
    }
}


void
RuleEngine::deleteRules()
{
    map<string,RuleGroup*>::iterator irg;

    for ( irg = m_rules.begin(); irg != m_rules.end(); ++irg )
        delete irg->second;

    for ( irg = m_pv_rules.begin(); irg != m_pv_rules.end(); ++irg )
        delete irg->second;

    m_rules.clear();
    m_pv_rules.clear();
}


void
RuleEngine::defineRule( const std::string &a_item, RuleType a_type, RuleSeverity a_severity, double a_value )
{
    // Make sure RuleType and item match for built-in-rules
    BIR bir = toBIR( a_item.c_str() );
    if ( bir == BIR_INVALID )
    {
        string msg = string("Nonexistant built-in rule: ") + a_item;
        throw std::runtime_error( msg );
    }

    if ( getCategory( bir ) != getCategory( a_type ))
    {
        string msg = string("BIR rule category mismatch for rule: ") + a_item;
        throw std::runtime_error( msg );
    }

    Rule *rule = new Rule( a_item, "", a_type, a_severity, a_value );
    RuleGroup *rg = 0;
    map<string,RuleGroup*>::iterator irg = m_rules.find( rule->getItem() );

    if ( irg == m_rules.end())
    {
        rg = new RuleGroup();
        m_rules[rule->getItem()] = rg;
    }
    else
    {
        rg = irg->second;
    }

    rg->defineRule( rule );

    m_bir[bir] = rg;
}


void
RuleEngine::definePvRule( const std::string &a_item, RuleType a_type, RuleSeverity a_severity, double a_value )
{
    if ( a_type == EVENT )
    {
        string msg = string("PV rules cannot be event type, pv:") + a_item;
        throw std::runtime_error( msg );
    }

    Rule *rule = new Rule( a_item, "Process variable ", a_type, a_severity, a_value );
    RuleGroup *rg = 0;
    map<string,RuleGroup*>::iterator irg = m_pv_rules.find( rule->getItem() );

    if ( irg == m_pv_rules.end())
    {
        rg = new RuleGroup();
        m_pv_rules[rule->getItem()] = rg;
    }
    else
    {
        rg = irg->second;
    }

    rg->defineRule( rule );
}


void
RuleEngine::pollThread()
{
    map<uint32_t,uint64_t>::iterator imon;

    while(!m_exit_flag)
    {
        sleep(1);

        boost::lock_guard<boost::mutex> lock(m_mutex);

        EVAL_SIGNAL( BIR_COUNT_RATE, (double)m_monitor.getCountRate() );

        if ( m_monitorx_rate )
        {
            m_monitor.getMonitorCountRates( m_monitor_rate );
            for ( imon = m_monitor_rate.begin(); imon != m_monitor_rate.end(); ++imon )
            {
                if ( imon->first < 8 )
                {
                    if ( m_bir[BIR_MONITOR0_COUNT_RATE + imon->first])
                        m_bir[BIR_MONITOR0_COUNT_RATE + imon->first]->evaluate( (double)imon->second, m_listeners );
                }
            }
        }

        EVAL_SIGNAL( BIR_PULSE_CHARGE, (double)m_monitor.getProtonCharge() );
        EVAL_SIGNAL( BIR_PULSE_FREQ, (double)m_monitor.getPulseFrequency() );
        EVAL_SIGNAL( BIR_STREAM_RATE, (double)m_monitor.getByteRate() );
    }
}


const char *
RuleEngine::toString( RuleType a_type )
{
    switch( a_type )
    {
    case EVENT: return "Event";
    case DEFINED: return "Defined";
    case NOT_DEFINED: return "Not Defined";
    case EQ: return "At value";
    case NEQ: return "Not at value";
    case LT: return "< Limit";
    case LTE: return "<= Limit";
    case GT: return "> Limit";
    case GTE: return ">= Limit";
    default: return "";
    }
}


const char *
RuleEngine::toString( RuleSeverity a_type )
{
    switch( a_type )
    {
    case INFO: return "Info";
    case MINOR: return "Minor";
    case MAJOR: return "Major";
    case CRITICAL: return "Critical";
    default: return "";
    }
}

//////////////////////////////////////////////
// IStreamListener


void
RuleEngine::runStatus( bool a_recording, unsigned long a_run_number )
{
    if ( a_recording )
    {
        EVAL_SIGNAL( BIR_RECORDING, true );
        EVAL_EVENT( BIR_EVENT_RECORD_START );
        EVAL_SIGNAL( BIR_RUN_NUMBER, (double)a_run_number );
    }
    else
    {
        EVAL_SIGNAL( BIR_RECORDING, false );
        EVAL_EVENT( BIR_EVENT_RECORD_STOP );
        EVAL_SIGNAL( BIR_RUN_NUMBER, (double)a_run_number );
    }
}


void
RuleEngine::pauseStatus( bool a_paused )
{
    if ( a_paused )
    {
        EVAL_SIGNAL( BIR_PAUSED, true );
        EVAL_EVENT( BIR_EVENT_PAUSE_START );
    }
    else
    {
        EVAL_SIGNAL( BIR_PAUSED, false );
        EVAL_EVENT( BIR_EVENT_PAUSE_STOP );
    }
}


void
RuleEngine::scanStart( unsigned long a_scan_number )
{
    EVAL_SIGNAL( BIR_SCANNING, true );
    EVAL_SIGNAL( BIR_SCAN_NUMBER, (double)a_scan_number );
    EVAL_EVENT( BIR_EVENT_SCAN_START );
}


void
RuleEngine::scanStop()
{
    EVAL_SIGNAL( BIR_SCANNING, false );
    EVAL_EVENT( BIR_EVENT_SCAN_STOP );
}


void
RuleEngine::runTitle( const std::string &a_run_title )
{
    if ( a_run_title.size() )
        EVAL_SIGNAL( BIR_RUN_TITLE, true );
}


void
RuleEngine::facilityName( const std::string &a_facility_name )
{
    if ( a_facility_name.size() )
        EVAL_SIGNAL( BIR_FACILITY_NAME, true );
}


void
RuleEngine::beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name )
{
    if ( a_id.size() )
        EVAL_SIGNAL( BIR_BEAM_ID, true );

    if ( a_short_name.size() )
        EVAL_SIGNAL( BIR_BEAM_SHORT_NAME, true );

    if ( a_long_name.size() )
        EVAL_SIGNAL( BIR_BEAM_LONG_NAME, true );
}


void
RuleEngine::proposalID( const std::string &a_proposal_id )
{
    if ( a_proposal_id.size() )
        EVAL_SIGNAL( BIR_PROPOSAL_ID, true );
}


void
RuleEngine::sampleID( const std::string &a_sample_id )
{
    if ( a_sample_id.size() )
        EVAL_SIGNAL( BIR_SAMPLE_ID, true );
}


void
RuleEngine::sampleName( const std::string &a_sample_name )
{
    if ( a_sample_name.size() )
        EVAL_SIGNAL( BIR_SAMPLE_NAME, true );
}


void
RuleEngine::sampleNature( const std::string &a_sample_nature )
{
    if ( a_sample_nature.size() )
        EVAL_SIGNAL( BIR_SAMPLE_NATURE, true );
}


void
RuleEngine::sampleFormula( const std::string &a_sample_formula )
{
    if ( a_sample_formula.size() )
        EVAL_SIGNAL( BIR_SAMPLE_FORM, true );
}


void
RuleEngine::sampleEnvironment( const std::string &a_sample_environment )
{
    if ( a_sample_environment.size() )
        EVAL_SIGNAL( BIR_SAMPLE_ENV, true );
}


void
RuleEngine::userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole )
{
    if ( a_uid.size() && a_uname.size() && a_urole.size() )
        EVAL_SIGNAL( BIR_USER_INFO, true );
}


void
RuleEngine::pvDefined( const std::string &a_name )
{
    map<string,RuleGroup*>::iterator igrp = m_pv_rules.find( a_name );
    if ( igrp != m_pv_rules.end())
    {
        igrp->second->evaluate( true, m_listeners );
    }
}


void
RuleEngine::pvValue( const std::string &a_name, double a_value )
{
    map<string,RuleGroup*>::iterator igrp = m_pv_rules.find( a_name );
    if ( igrp != m_pv_rules.end())
    {
        igrp->second->evaluate( a_value, m_listeners );
    }
}


void
RuleEngine::connectionStatus( bool a_connected )
{
    if ( !a_connected )
        EVAL_EVENT( BIR_EVENT_LOST_STREAM_CONNECTION );
}



void
RuleEngine::populateBIRTable()
{
    // Events
    m_bir_names["event lost stream connection"] = BIR_EVENT_LOST_STREAM_CONNECTION;
    m_bir_names["event duplicate pulse"] = BIR_EVENT_DUPLICATE_PULSE;
    m_bir_names["event pulse cycle error"] = BIR_EVENT_PULSE_CYCLE_ERROR;
    m_bir_names["event record start"] = BIR_EVENT_RECORD_START;
    m_bir_names["event record stop"] = BIR_EVENT_RECORD_STOP;
    m_bir_names["event pause start"] = BIR_EVENT_PAUSE_START;
    m_bir_names["event pause stop"] = BIR_EVENT_PAUSE_STOP;
    m_bir_names["event scan start"] = BIR_EVENT_SCAN_START;
    m_bir_names["event scan stop"] = BIR_EVENT_SCAN_STOP;

    // Signals non-numeric
    m_bir_names["recording"] = BIR_RECORDING;
    m_bir_names["paused"] = BIR_PAUSED;
    m_bir_names["scanning"] = BIR_SCANNING;
    m_bir_names["facility name"] = BIR_FACILITY_NAME;
    m_bir_names["beam id"] = BIR_BEAM_ID;
    m_bir_names["beam short name"] = BIR_BEAM_SHORT_NAME;
    m_bir_names["beam long name"] = BIR_BEAM_LONG_NAME;
    m_bir_names["run title"] = BIR_RUN_TITLE;
    m_bir_names["proposal id"] = BIR_PROPOSAL_ID;
    m_bir_names["proposal title"] = BIR_PROPOSAL_TITLE;
    m_bir_names["sample id"] = BIR_SAMPLE_ID;
    m_bir_names["sample name"] = BIR_SAMPLE_NAME;
    m_bir_names["sample environment"] = BIR_SAMPLE_ENV;
    m_bir_names["sample formula"] = BIR_SAMPLE_FORM;
    m_bir_names["sample nature"] = BIR_SAMPLE_NATURE;
    m_bir_names["user info"] = BIR_USER_INFO;

    // Signals - numeric
    m_bir_names["run number"] = BIR_RUN_NUMBER;
    m_bir_names["scan number"] = BIR_SCAN_NUMBER;
    m_bir_names["event rate"] = BIR_COUNT_RATE;
    m_bir_names["monitor0 rate"] = BIR_MONITOR0_COUNT_RATE;
    m_bir_names["monitor1 rate"] = BIR_MONITOR1_COUNT_RATE;
    m_bir_names["monitor2 rate"] = BIR_MONITOR2_COUNT_RATE;
    m_bir_names["monitor3 rate"] = BIR_MONITOR3_COUNT_RATE;
    m_bir_names["monitor4 rate"] = BIR_MONITOR4_COUNT_RATE;
    m_bir_names["monitor5 rate"] = BIR_MONITOR5_COUNT_RATE;
    m_bir_names["monitor6 rate"] = BIR_MONITOR6_COUNT_RATE;
    m_bir_names["monitor7 rate"] = BIR_MONITOR7_COUNT_RATE;
    m_bir_names["pixel error count"] = BIR_PIXEL_ERROR_COUNT;
    m_bir_names["pixel error rate"] = BIR_PIXEL_ERROR_RATE;
    m_bir_names["duplicate pulse count"] = BIR_DUPLICATE_PULSE_COUNT;
    m_bir_names["cycle error count"] = BIR_CYCLE_ERROR_COUNT;
    m_bir_names["pulse charge"] = BIR_PULSE_CHARGE;
    m_bir_names["pulse frequency"] = BIR_PULSE_FREQ;
    m_bir_names["stream rate"] = BIR_STREAM_RATE;
}

RuleEngine::BIR
RuleEngine::toBIR( const char *a_name )
{
    string name(a_name);
    std::transform( name.begin(), name.end(), name.begin(), ::tolower );

    map<string,BIR>::iterator i = m_bir_names.find( name );
    if ( i != m_bir_names.end())
        return i->second;

    return BIR_INVALID;
}


RuleCategory
RuleEngine::getCategory( RuleType a_type )
{
    if ( a_type < RULE_SIGNAL_NONNUMERIC )
        return RC_EVENT;
    else if ( a_type < RULE_SIGNAL_NUMERIC )
        return RC_SIGNAL_NONNUMERIC;
    else
        return RC_SIGNAL_NUMERIC;
}


RuleCategory
RuleEngine::getCategory( BIR a_bir )
{
    if ( a_bir < BIR_SIGNAL_NONNUMERIC )
        return RC_EVENT;
    else if ( a_bir < BIR_SIGNAL_NUMERIC )
        return RC_SIGNAL_NONNUMERIC;
    else
        return RC_SIGNAL_NUMERIC;
}

}

