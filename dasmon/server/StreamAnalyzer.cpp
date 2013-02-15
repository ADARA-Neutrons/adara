#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
//#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>
#include "StreamAnalyzer.h"

#include <iostream>
#include <fstream>

using namespace std;


StreamAnalyzer::StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor )
    :m_monitor(a_monitor), m_pv_prefix("PV_")
{
    m_monitor.addListener( *this );
    m_engine.attach( *this );

    m_fact_recording            = m_engine.getFactHandle("RECORDING");
    m_fact_run_number           = m_engine.getFactHandle("RUN_NUMBER");
    m_fact_paused               = m_engine.getFactHandle("PAUSED");
    m_fact_scanning             = m_engine.getFactHandle("SCANNING");
    m_fact_scan_index           = m_engine.getFactHandle("SCAN_INDEX");
    m_fact_facility_name        = m_engine.getFactHandle("FACILITY_NAME");
    m_fact_beam_id              = m_engine.getFactHandle("BEAM_ID");
    m_fact_beam_sname           = m_engine.getFactHandle("BEAM_SHORT_NAME");
    m_fact_beam_lname           = m_engine.getFactHandle("BEAM_LONG_NAME");
    m_fact_run_title            = m_engine.getFactHandle("RUN_TITLE");
    m_fact_prop_id              = m_engine.getFactHandle("PROPOSAL_ID");
    m_fact_sample_id            = m_engine.getFactHandle("SAMPLE_ID");
    m_fact_sample_name          = m_engine.getFactHandle("SAMPLE_NAME");
    m_fact_sample_nature        = m_engine.getFactHandle("SAMPLE_NATURE");
    m_fact_sample_form          = m_engine.getFactHandle("SAMPLE_FORMULA");
    m_fact_sample_env           = m_engine.getFactHandle("SAMPLE_ENVIRONMENT");
    m_fact_user_info            = m_engine.getFactHandle("USER_INFO");
    m_fact_count_rate           = m_engine.getFactHandle("COUNT_RATE");
    m_fact_mon_count_rate[0]    = m_engine.getFactHandle("MON0_COUNT_RATE");
    m_fact_mon_count_rate[1]    = m_engine.getFactHandle("MON1_COUNT_RATE");
    m_fact_mon_count_rate[2]    = m_engine.getFactHandle("MON2_COUNT_RATE");
    m_fact_mon_count_rate[3]    = m_engine.getFactHandle("MON3_COUNT_RATE");
    m_fact_mon_count_rate[4]    = m_engine.getFactHandle("MON4_COUNT_RATE");
    m_fact_mon_count_rate[5]    = m_engine.getFactHandle("MON5_COUNT_RATE");
    m_fact_mon_count_rate[6]    = m_engine.getFactHandle("MON6_COUNT_RATE");
    m_fact_mon_count_rate[7]    = m_engine.getFactHandle("MON7_COUNT_RATE");
    m_fact_pulse_charge         = m_engine.getFactHandle("PULSE_CHARGE");
    m_fact_pulse_freq           = m_engine.getFactHandle("PULSE_FREQ");
    m_fact_stream_rate          = m_engine.getFactHandle("STREAM_RATE");
    m_fact_run_pulse_charge     = m_engine.getFactHandle("RUN_PULSE_CHARGE");
    m_fact_pix_err_count        = m_engine.getFactHandle("RUN_PIXEL_ERR_COUNT");
    m_fact_dup_pulse_count      = m_engine.getFactHandle("RUN_DUP_PULSE_COUNT");
    m_fact_cycle_err_count      = m_engine.getFactHandle("RUN_CYCLE_ERR_COUNT");
    m_fact_sms_connected        = m_engine.getFactHandle("SMS_CONNECTED");
}


StreamAnalyzer::~StreamAnalyzer()
{
    m_monitor.removeListener( *this );
}


void
StreamAnalyzer::loadConfig( const std::string &a_file )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    ifstream inf( a_file.c_str());
    string line;
    int mode = 0;

    if ( !inf.is_open())
        throw std::runtime_error( string("Could not open configuration file: ") + a_file );

    while( !inf.eof())
    {
        getline( inf, line );

        if ( line.empty() || line[0] == '#' )
            continue;
        if ( line == "[rules]" )
            mode = 1;
        else if ( line == "[signals]" )
            mode = 2;
        else
        {
            //cout << line << endl;
            if ( mode == 1 )
                m_engine.defineRule( line );
            else if ( mode == 2 )
                defineSignal( line );
        }
    }

    inf.close();
}


void
StreamAnalyzer::addListener( ISignalListener &a_listener )
{
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
        m_listeners.push_back( &a_listener );
}


void
StreamAnalyzer::removeListener( ISignalListener &a_listener )
{
    vector<ISignalListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}


void
StreamAnalyzer::defineSignal( const std::string &a_expression )
{
    boost::char_separator<char> sep(",");
    boost::tokenizer<boost::char_separator<char> > tokens(a_expression, sep);
    boost::tokenizer<boost::char_separator<char> >::iterator tok = tokens.begin();
    SignalInfo info;
    string fact;

    if ( tok == tokens.end())
        throw "Syntax error";

    info.sig_name = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    fact = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    info.sig_source = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    if ( *tok == "TRACE" )
        info.sig_level = ADARA::TRACE;
    else if ( *tok == "DEBUG" )
        info.sig_level = ADARA::DEBUG;
    else if ( *tok == "INFO" )
        info.sig_level = ADARA::INFO;
    else if ( *tok == "WARN" )
        info.sig_level = ADARA::WARN;
    else if ( *tok == "WARNING" )
        info.sig_level = ADARA::WARN;
    else if ( *tok == "ERROR" )
        info.sig_level = ADARA::ERROR;
    else if ( *tok == "FATAL" )
        info.sig_level = ADARA::FATAL;
    else
        throw "Syntax error (bad level)";

    if ( ++tok == tokens.end())
        throw "Syntax error";

    info.sig_msg = *tok;

    m_signals[fact] = info;
}

void
StreamAnalyzer::resendState()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine.sendAsserted( *this );
}

//////////////////////////////////////////////
// IStreamListener


void
StreamAnalyzer::runStatus( bool a_recording, unsigned long a_run_number )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_recording )
    {
        m_engine.assert( m_fact_recording );
        m_engine.assert( m_fact_run_number, a_run_number );
    }
    else
    {
        m_engine.retract( m_fact_recording );
        m_engine.retract( m_fact_run_number );

        m_engine.retract( m_fact_facility_name );
        m_engine.retract( m_fact_beam_id );
        m_engine.retract( m_fact_beam_sname );
        m_engine.retract( m_fact_beam_lname );
        m_engine.retract( m_fact_run_title );
        m_engine.retract( m_fact_prop_id );
        m_engine.retract( m_fact_sample_id );
        m_engine.retract( m_fact_sample_name );
        m_engine.retract( m_fact_sample_nature );
        m_engine.retract( m_fact_sample_form );
        m_engine.retract( m_fact_sample_env );
        m_engine.retract( m_fact_user_info );
    }
}


void
StreamAnalyzer::pauseStatus( bool a_paused )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_paused )
    {
        m_engine.assert( m_fact_paused );
    }
    else
    {
        m_engine.retract( m_fact_paused );
    }
}


void
StreamAnalyzer::scanStatus( bool a_scanning, unsigned long a_scan_index )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_scanning )
    {
        m_engine.assert( m_fact_scanning );
        m_engine.assert( m_fact_scan_index, a_scan_index );
    }
    else
    {
        m_engine.retract( m_fact_scanning );
        m_engine.retract( m_fact_scan_index );
    }
}


void
StreamAnalyzer::beamInfo( const ADARA::DASMON::BeamInfo &a_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( !a_info.m_facility.empty() )
        m_engine.assert( m_fact_facility_name );
    if ( !a_info.m_beam_id.empty() )
        m_engine.assert( m_fact_beam_id );
    if ( !a_info.m_beam_sname.empty() )
        m_engine.assert( m_fact_beam_sname );
    if ( !a_info.m_beam_lname.empty() )
        m_engine.assert( m_fact_beam_lname );
}


void
StreamAnalyzer::runInfo( const ADARA::DASMON::RunInfo &a_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( !a_info.m_run_title.empty() )
        m_engine.assert( m_fact_run_title );
    if ( !a_info.m_proposal_id.empty() )
        m_engine.assert( m_fact_prop_id );
    if ( !a_info.m_sample_id.empty() )
        m_engine.assert( m_fact_sample_id );
    if ( !a_info.m_sample_name.empty() )
        m_engine.assert( m_fact_sample_name );
    if ( !a_info.m_sample_nature.empty() )
        m_engine.assert( m_fact_sample_nature );
    if ( !a_info.m_sample_formula.empty() )
        m_engine.assert( m_fact_sample_form );
    if ( !a_info.m_sample_environ.empty() )
        m_engine.assert( m_fact_sample_env );
    if ( !a_info.m_user_info.empty() )
        m_engine.assert( m_fact_user_info );
}


void
StreamAnalyzer::beamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine.assert( m_fact_count_rate, a_metrics.m_count_rate );

    for ( short i = 0; i < a_metrics.m_num_monitors; ++i )
        m_engine.assert( m_fact_mon_count_rate[i], a_metrics.m_monitor_count_rate[i] );

    m_engine.assert( m_fact_pulse_charge, a_metrics.m_pulse_charge );
    m_engine.assert( m_fact_pulse_freq, a_metrics.m_pulse_freq );
    m_engine.assert( m_fact_stream_rate, a_metrics.m_stream_bps );
}


void
StreamAnalyzer::runMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine.assert( m_fact_run_pulse_charge, a_metrics.m_pulse_charge );
    m_engine.assert( m_fact_pix_err_count, a_metrics.m_pixel_error_count );
    m_engine.assert( m_fact_dup_pulse_count, a_metrics.m_dup_pulse_count );
    m_engine.assert( m_fact_cycle_err_count, a_metrics.m_cycle_error_count );
}


void
StreamAnalyzer::pvDefined( const std::string &a_name )
{
    (void)a_name;
}


void
StreamAnalyzer::pvValue( const std::string &a_name, uint32_t a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine.assert( m_pv_prefix + a_name, a_value );
}


void
StreamAnalyzer::pvValue( const std::string &a_name, double a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine.assert( m_pv_prefix + a_name, a_value );
}

void
StreamAnalyzer::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    (void)a_host;
    (void)a_port;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_connected )
        m_engine.assert( m_fact_sms_connected );
    else
        m_engine.retract( m_fact_sms_connected );
}

// IFactListener Interface

void
StreamAnalyzer::onAssert( const std::string &a_fact )
{
    map<string,SignalInfo>::iterator isig = m_signals.find( a_fact );
    if ( isig != m_signals.end())
    {
        for ( vector<ISignalListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        {
            (*l)->signalAssert( isig->second.sig_name, isig->second.sig_source, isig->second.sig_level, isig->second.sig_msg );
        }
    }
}


void
StreamAnalyzer::onRetract( const std::string &a_fact )
{
    map<string,SignalInfo>::iterator isig = m_signals.find( a_fact );
    if ( isig != m_signals.end())
    {
        for ( vector<ISignalListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        {
            (*l)->signalRetract( isig->second.sig_name );
        }
    }
}

