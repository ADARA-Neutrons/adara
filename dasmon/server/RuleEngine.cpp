#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include "RuleEngine.h"

#include <iostream>

using namespace std;

namespace RuleEngine
{

#define EVAL_SIGNAL( bir, cond ) \
{ if ( m_bir[bir] ) { m_bir[bir]->evaluate( cond, m_listeners ); }}

#define EVAL_EVENT( bir ) \
{ if ( m_bir[bir] ) { m_bir[bir]->event( m_listeners ); }}


StreamAnalyzer::StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor )
    :m_monitor(a_monitor), m_monitorx_rate(false)
{
    populateBIRTable();

    m_bir.resize( BIR_TOTAL_RULE_COUNT, 0 );

    vector<RuleSettings>    rules;

    rules.push_back(RuleSettings( "event lost stream connection", "SMS", EVENT, ADARA::ERROR ));
    rules.push_back(RuleSettings( "event record start", "SMS", EVENT, ADARA::INFO ));
    rules.push_back(RuleSettings( "event record stop", "SMS", EVENT, ADARA::INFO ));
    rules.push_back(RuleSettings( "event pause start", "SMS", EVENT, ADARA::INFO ));
    rules.push_back(RuleSettings( "event pause stop", "SMS", EVENT, ADARA::INFO ));
    rules.push_back(RuleSettings( "event scan start", "SMS", EVENT, ADARA::INFO ));
    rules.push_back(RuleSettings( "event scan stop", "SMS", EVENT, ADARA::INFO ));

    rules.push_back(RuleSettings( "recording", "SMS", DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "recording", "SMS", NOT_DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "paused", "SMS", DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "paused", "SMS", NOT_DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "scanning", "SMS", DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "scanning", "SMS", NOT_DEFINED, ADARA::INFO ));
    rules.push_back(RuleSettings( "facility name", "SMS", NOT_DEFINED, ADARA::WARN ));
    rules.push_back(RuleSettings( "proposal id", "SMS", NOT_DEFINED, ADARA::WARN ));

    rules.push_back(RuleSettings( "pulse charge", "SAS", LT, ADARA::WARN, 1.2e5 ));
    rules.push_back(RuleSettings( "pulse frequency", "SAS", LT, ADARA::WARN, 58 ));
    rules.push_back(RuleSettings( "pulse frequency", "SAS", GT, ADARA::WARN, 62 ));
    rules.push_back(RuleSettings( "event rate", "BEAM", LT, ADARA::WARN, 10 ));
    rules.push_back(RuleSettings( "event rate", "BEAM", GT, ADARA::WARN, 1000 ));
    rules.push_back(RuleSettings( "monitor0 rate", "BEAM", LT, ADARA::WARN, 65 ));
    rules.push_back(RuleSettings( "monitor1 rate", "BEAM", LT, ADARA::WARN, 125 ));
    rules.push_back(RuleSettings( "stream rate", "SMS", LT, ADARA::WARN, 100 ));

    rules.push_back(RuleSettings( "energyReq", "DAS", NOT_DEFINED, ADARA::ERROR, 0.0, true ));
    rules.push_back(RuleSettings( "energyReq", "DAS", LTE, ADARA::ERROR, 0.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", "CHOPPER", NOT_DEFINED, ADARA::ERROR, 0.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", "CHOPPER", LT, ADARA::WARN, 180.0, true ));
    rules.push_back(RuleSettings( "chopper0_TDC", "CHOPPER", GT, ADARA::WARN, 190.0, true ));

    configureRules(  rules );

    m_monitor.addListener( *this );
}


StreamAnalyzer::~StreamAnalyzer()
{
    m_monitor.removeListener( *this );
    deleteRules();
}


void
StreamAnalyzer::addListener( IRuleListener &a_listener )
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
StreamAnalyzer::removeListener( IRuleListener &a_listener )
{
    vector<IRuleListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}


void
StreamAnalyzer::resendAsserted( IRuleListener &a_listener )
{
    for ( map<string,RuleGroup*>::iterator igrp = m_rules.begin(); igrp != m_rules.end(); ++igrp )
        igrp->second->resendAsserted( a_listener );

    for ( map<string,RuleGroup*>::iterator igrp = m_pv_rules.begin(); igrp != m_pv_rules.end(); ++igrp )
        igrp->second->resendAsserted( a_listener );
}


void
StreamAnalyzer::configureRules( vector<RuleSettings> &a_rule_settings )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        deleteRules();

        for ( vector<RuleSettings>::iterator r = a_rule_settings.begin(); r != a_rule_settings.end(); ++r )
        {
            if ( r->m_pv )
                definePvRule( r->m_name, r->m_source, r->m_type, r->m_level, r->m_value );
            else
                defineRule( r->m_name, r->m_source, r->m_type, r->m_level, r->m_value );
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
StreamAnalyzer::deleteRules()
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
StreamAnalyzer::defineRule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value )
{
    // Make sure RuleType and item match for built-in-rules
    BIR bir = toBIR( a_name.c_str() );
    if ( bir == BIR_INVALID )
    {
        string msg = string("Nonexistant built-in rule: ") + a_name;
        throw std::runtime_error( msg );
    }

    if ( getCategory( bir ) != getCategory( a_type ))
    {
        string msg = string("BIR rule category mismatch for rule: ") + a_name;
        throw std::runtime_error( msg );
    }

    Rule *rule = new Rule( a_name, a_source, a_type, a_level, a_value );
    RuleGroup *rg = 0;
    map<string,RuleGroup*>::iterator irg = m_rules.find( rule->getName() );

    if ( irg == m_rules.end())
    {
        rg = new RuleGroup();
        m_rules[rule->getName()] = rg;
    }
    else
    {
        rg = irg->second;
    }

    rg->defineRule( rule );

    m_bir[bir] = rg;
}


void
StreamAnalyzer::definePvRule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value )
{
    if ( a_type == EVENT )
    {
        string msg = string("PV rules cannot be event type, pv:") + a_name;
        throw std::runtime_error( msg );
    }

    Rule *rule = new Rule( a_name, a_source, a_type, a_level, a_value );
    RuleGroup *rg = 0;
    map<string,RuleGroup*>::iterator irg = m_pv_rules.find( rule->getName() );

    if ( irg == m_pv_rules.end())
    {
        rg = new RuleGroup();
        m_pv_rules[rule->getName()] = rg;
    }
    else
    {
        rg = irg->second;
    }

    rg->defineRule( rule );
}


const char *
StreamAnalyzer::toString( RuleType a_type )
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


//////////////////////////////////////////////
// IStreamListener


void
StreamAnalyzer::runStatus( bool a_recording, unsigned long a_run_number )
{
    if ( a_recording )
    {
        EVAL_SIGNAL( BIR_RECORDING, true );
        EVAL_EVENT( BIR_EVENT_RECORD_START );
        EVAL_SIGNAL( BIR_RUN_NUMBER, (double)a_run_number );
    }
    else
    {
        cout << "Hey Hey Hey!!!" << endl;
        EVAL_SIGNAL( BIR_RECORDING, false );
        EVAL_EVENT( BIR_EVENT_RECORD_STOP );
        EVAL_SIGNAL( BIR_RUN_NUMBER, (double)a_run_number );
    }
}


void
StreamAnalyzer::pauseStatus( bool a_paused )
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
StreamAnalyzer::scanStatus( bool a_scanning, unsigned long a_scan_number )
{
    EVAL_SIGNAL( BIR_SCANNING, a_scanning );

    if ( a_scanning )
    {
        EVAL_SIGNAL( BIR_SCAN_NUMBER, (double)a_scan_number );
        EVAL_EVENT( BIR_EVENT_SCAN_START );
    }
    else
    {
        EVAL_EVENT( BIR_EVENT_SCAN_STOP );
    }
}


void
StreamAnalyzer::beamInfo( const ADARA::DASMON::BeamInfo &a_info )
{
    EVAL_SIGNAL( BIR_FACILITY_NAME, !a_info.m_facility.empty() );
    EVAL_SIGNAL( BIR_BEAM_ID, !a_info.m_beam_id.empty() );
    EVAL_SIGNAL( BIR_BEAM_SHORT_NAME, !a_info.m_beam_sname.empty() );
    EVAL_SIGNAL( BIR_BEAM_LONG_NAME, !a_info.m_beam_lname.empty() );
}


void
StreamAnalyzer::runInfo( const ADARA::DASMON::RunInfo &a_info )
{
    EVAL_SIGNAL( BIR_RUN_TITLE, !a_info.m_run_title.empty() );
    EVAL_SIGNAL( BIR_PROPOSAL_ID, !a_info.m_proposal_id.empty() );
    EVAL_SIGNAL( BIR_SAMPLE_ID, !a_info.m_sample_id.empty() );
    EVAL_SIGNAL( BIR_SAMPLE_NAME, !a_info.m_sample_name.empty() );
    EVAL_SIGNAL( BIR_SAMPLE_NATURE, !a_info.m_sample_nature.empty() );
    EVAL_SIGNAL( BIR_SAMPLE_FORM, !a_info.m_sample_formula.empty() );
    EVAL_SIGNAL( BIR_SAMPLE_ENV, !a_info.m_sample_environ.empty() );
    EVAL_SIGNAL( BIR_USER_INFO, !a_info.m_user_info.empty() );
}


void
StreamAnalyzer::beamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics )
{
    EVAL_SIGNAL( BIR_COUNT_RATE, a_metrics.m_count_rate );

    if ( m_monitorx_rate )
    {
        for ( short i = 0; i < a_metrics.m_num_monitors; ++i )
        {
            if ( m_bir[BIR_MONITOR0_COUNT_RATE + i])
                m_bir[BIR_MONITOR0_COUNT_RATE + i]->evaluate( a_metrics.m_monitor_count_rate[i], m_listeners );
        }
    }

    EVAL_SIGNAL( BIR_PULSE_CHARGE, a_metrics.m_pulse_charge );
    EVAL_SIGNAL( BIR_PULSE_FREQ, a_metrics.m_pulse_freq );
    EVAL_SIGNAL( BIR_STREAM_RATE, (double)a_metrics.m_stream_bps );
}


void
StreamAnalyzer::runMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    EVAL_SIGNAL( BIR_RUN_PULSE_CHARGE, a_metrics.m_pulse_charge );
    EVAL_SIGNAL( BIR_RUN_PIXEL_ERROR_COUNT, (double)a_metrics.m_pixel_error_count );
    EVAL_SIGNAL( BIR_RUN_DUP_PULSE_COUNT, (double)a_metrics.m_dup_pulse_count );
    EVAL_SIGNAL( BIR_RUN_CYCLE_ERROR_COUNT, (double)a_metrics.m_cycle_error_count );
}


void
StreamAnalyzer::pvDefined( const std::string &a_name )
{
    map<string,RuleGroup*>::iterator igrp = m_pv_rules.find( a_name );
    if ( igrp != m_pv_rules.end())
    {
        igrp->second->evaluate( true, m_listeners );
    }
}


void
StreamAnalyzer::pvValue( const std::string &a_name, double a_value )
{
    map<string,RuleGroup*>::iterator igrp = m_pv_rules.find( a_name );
    if ( igrp != m_pv_rules.end())
    {
        igrp->second->evaluate( a_value, m_listeners );
    }
}


void
StreamAnalyzer::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    (void)a_host;
    (void)a_port;

    if ( !a_connected )
        EVAL_EVENT( BIR_EVENT_LOST_STREAM_CONNECTION );
}



void
StreamAnalyzer::populateBIRTable()
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
    m_bir_names["pixel error rate"] = BIR_PIXEL_ERROR_RATE;
    m_bir_names["pulse charge"] = BIR_PULSE_CHARGE;
    m_bir_names["pulse frequency"] = BIR_PULSE_FREQ;
    m_bir_names["stream rate"] = BIR_STREAM_RATE;
    m_bir_names["run pulse charge"] = BIR_RUN_PULSE_CHARGE;
    m_bir_names["run pixel error count"] = BIR_RUN_PIXEL_ERROR_COUNT;
    m_bir_names["run duplicate pulse count"] = BIR_RUN_DUP_PULSE_COUNT;
    m_bir_names["run cycle error count"] = BIR_RUN_CYCLE_ERROR_COUNT;
}

StreamAnalyzer::BIR
StreamAnalyzer::toBIR( const char *a_name )
{
    string name(a_name);
    std::transform( name.begin(), name.end(), name.begin(), ::tolower );

    map<string,BIR>::iterator i = m_bir_names.find( name );
    if ( i != m_bir_names.end())
        return i->second;

    return BIR_INVALID;
}


RuleCategory
StreamAnalyzer::getCategory( RuleType a_type )
{
    if ( a_type < RULE_SIGNAL_NONNUMERIC )
        return RC_EVENT;
    else if ( a_type < RULE_SIGNAL_NUMERIC )
        return RC_SIGNAL_NONNUMERIC;
    else
        return RC_SIGNAL_NUMERIC;
}


RuleCategory
StreamAnalyzer::getCategory( BIR a_bir )
{
    if ( a_bir < BIR_SIGNAL_NONNUMERIC )
        return RC_EVENT;
    else if ( a_bir < BIR_SIGNAL_NUMERIC )
        return RC_SIGNAL_NONNUMERIC;
    else
        return RC_SIGNAL_NUMERIC;
}

}

