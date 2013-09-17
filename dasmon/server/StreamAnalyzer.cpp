#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>
#include <syslog.h>

#include "ADARA.h"
#include "ComBus.h"
#include "StreamAnalyzer.h"

#include <iostream>
#include <fstream>

using namespace std;

namespace ADARA {
namespace DASMON {

#define RUN_BATCH_MASK      0x0001
#define METRICS_BATCH_MASK  0x0002
#define CONN_BATCH_MASK     0x0004

/**
 * \brief StreamAnalyser constructor.
 * \param a_monitor - StreamMonitor that will feed this analyzer
 * \param a_cfg_dir - Path to configuration files
 *
 * Constructs a new StreamAnalyzer and attaches to the specified monitor. Rule
 * configuration file is loaded and rule engine setup.
 */
StreamAnalyzer::StreamAnalyzer( ADARA::DASMON::StreamMonitor &a_monitor, const std::string &a_cfg_dir )
    :m_monitor(a_monitor), m_engine(0), m_pv_prefix("PV_"), m_pv_err_prefix("PVERR_"),
      m_pv_lim_prefix("PVLIM_"), m_cfg_dir( a_cfg_dir ), m_debounce_sec(0), m_batch_mask(0), m_ok(true)
{
    m_engine = new RuleEngine();
    m_monitor.addListener( *this );
    m_engine->attach( *this );

    m_fact_name[BIF_RECORDING]           = "RECORDING";
    m_fact_name[BIF_RUN_NUMBER]          = "RUN_NUMBER";
    m_fact_name[BIF_PAUSED]              = "PAUSED";
    m_fact_name[BIF_SCANNING]            = "SCANNING";
    m_fact_name[BIF_SCAN_INDEX]          = "SCAN_INDEX";
    m_fact_name[BIF_FAC_NAME]            = "FACILITY_NAME";
    m_fact_name[BIF_BEAM_ID]             = "BEAM_ID";
    m_fact_name[BIF_BEAM_SNAME]          = "BEAM_SHORT_NAME";
    m_fact_name[BIF_BEAM_LNAME]          = "BEAM_LONG_NAME";
    m_fact_name[BIF_RUN_TITLE]           = "RUN_TITLE";
    m_fact_name[BIF_PROP_ID]             = "PROPOSAL_ID";
    m_fact_name[BIF_SAMPLE_ID]           = "SAMPLE_ID";
    m_fact_name[BIF_SAMPLE_NAME]         = "SAMPLE_NAME";
    m_fact_name[BIF_SAMPLE_NAT]          = "SAMPLE_NATURE";
    m_fact_name[BIF_SAMPLE_FORM]         = "SAMPLE_FORMULA";
    m_fact_name[BIF_SAMPLE_ENV]          = "SAMPLE_ENVIRONMENT";
    m_fact_name[BIF_USER_INFO]           = "USER_INFO";
    m_fact_name[BIF_COUNT_RATE]          = "COUNT_RATE";
    m_fact_name[BIF_PULSE_CHARGE]        = "PULSE_CHARGE";
    m_fact_name[BIF_PULSE_FREQ]          = "PULSE_FREQ";
    m_fact_name[BIF_STREAM_RATE]         = "STREAM_RATE";
    m_fact_name[BIF_RUN_TOTAL_CHARGE]    = "RUN_TOTAL_CHARGE";
    m_fact_name[BIF_RUN_TOTAL_COUNTS]    = "RUN_TOTAL_COUNTS";
    m_fact_name[BIF_PIX_ERR_COUNT]       = "RUN_PIXEL_ERR_COUNT";
    m_fact_name[BIF_DUP_PULSE_COUNT]     = "RUN_DUP_PULSE_COUNT";
    m_fact_name[BIF_MAP_ERROR_COUNT]     = "RUN_MAP_ERROR_COUNT";
    m_fact_name[BIF_MISS_RTDL_COUNT]     = "RUN_MISS_RTDL_COUNT";
    m_fact_name[BIF_PULSE_VETO_COUNT]    = "RUN_PULSE_VETO_COUNT";
    m_fact_name[BIF_SMS_CONNECTED]       = "SMS_CONNECTED";
    m_fact_name[BIF_GENERAL_PV_LIMIT]    = "GENERAL_PV_LIMIT";
    m_fact_name[BIF_GENERAL_PV_ERROR]    = "GENERAL_PV_ERROR";

    for ( int i = 0; i < BIF_COUNT; ++i )
        m_fact[i] = m_engine->getFactHandle( m_fact_name[i] );

    loadConfig();

    m_debounce_thread = new boost::thread( boost::bind( &StreamAnalyzer::runDebounceThread, this ));
}


/**
 * StreamAnalyzer destructor.
 */
StreamAnalyzer::~StreamAnalyzer()
{
    m_monitor.removeListener( *this );
    delete m_engine;
}


void
StreamAnalyzer::loadConfig()
{
    RuleEngine::RuleInfo            rule;
    vector<RuleEngine::RuleInfo>    loaded_rules;
    SignalInfo                      signal;
    vector<SignalInfo>              loaded_signals;

    //string upper_expr = boost::to_upper_copy( a_expression );
    boost::char_separator<char> sep1(":");
    boost::char_separator<char> sep2(",");
    boost::tokenizer<boost::char_separator<char> >::iterator tok; // = tokens.begin();

    string cfg = m_cfg_dir + "dasmond.cfg";

    ifstream inf( cfg.c_str());
    string line;
    int mode = 0;

    if ( !inf.is_open())
    {
        syslog( LOG_ERR, "Could not open configuration file: %s", cfg.c_str() );
    }
    else
    {
        try
        {
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

                    if ( mode == 1 )
                    {
                        boost::tokenizer<boost::char_separator<char> > tokens( line, sep1 );
                        tok = tokens.begin();

                        if ( tok == tokens.end())
                            throw -1;
                        rule.fact = *tok++;
                        if ( tok == tokens.end())
                            throw -1;
                        rule.expr = *tok;
                        loaded_rules.push_back( rule );
                    }
                    else if ( mode == 2 )
                    {
                        boost::tokenizer<boost::char_separator<char> > tokens( line, sep2 );
                        tok = tokens.begin();

                        if ( tok == tokens.end())
                            throw -1;
                        signal.name = *tok++;
                        if ( tok == tokens.end())
                            throw -1;
                        signal.fact = *tok++;
                        if ( tok == tokens.end())
                            throw -1;
                        signal.source = *tok++;
                        if ( tok == tokens.end())
                            throw -1;
                        signal.level = ComBus::ComBusHelper::toLevel( *tok++ );
                        if ( tok == tokens.end())
                            throw -1;
                        signal.msg = *tok++;

                        loaded_signals.push_back( signal );
                    }
                }
            }

            inf.close();

            // Push rules and signals into engine
            setDefinitions( loaded_rules, loaded_signals );
        }
        catch ( ... )
        {
            inf.close();
            syslog( LOG_ERR, "Failed loading configuration file: %s", cfg.c_str() );
        }
    }
}


void
StreamAnalyzer::saveConfig()
{
    string cfg = m_cfg_dir + "dasmond.cfg";

    ofstream outf( cfg.c_str(), ios_base::out | ios_base::trunc );

    if ( !outf.is_open())
    {
        syslog( LOG_ERR, "Could not open configuration file: %s", cfg.c_str() );
        return;
    }

    // FACT_ID  Rule expression

    outf << "[rules]" << endl;

    vector<RuleEngine::RuleInfo> rules;
    m_engine->getDefinedRules( rules );

    for ( vector<RuleEngine::RuleInfo>::iterator rule = rules.begin(); rule != rules.end(); ++rule )
    {
        outf << rule->fact << ":" << rule->expr << endl;
    }

    // SIGNAL_ID,FACT_ID,SOURCE,LEVEL,Message

    outf << "[signals]" << endl;

    for ( map<string,SignalInfo>::iterator sig = m_signals.begin(); sig != m_signals.end(); ++sig )
    {
        outf << sig->second.name << "," << sig->second.fact << "," << sig->second.source << ",";
        outf << sig->second.level << "," << sig->second.msg << endl;
    }

    outf.close();
}


void
StreamAnalyzer::restoreDefaultConfig()
{
    try
    {
        // Move def config file to current

        boost::filesystem::path  cfg( m_cfg_dir + "dasmond.cfg" );
        boost::filesystem::path  cfg_bak( m_cfg_dir + "dasmond_def.cfg" );

        if ( boost::filesystem::exists( cfg_bak ))
        {
            if ( boost::filesystem::exists( cfg ))
                boost::filesystem::remove( cfg );
            boost::filesystem::copy_file( cfg_bak, cfg );

            loadConfig();
        }
    }
    catch ( ... )
    {
        syslog( LOG_ERR, "Could not restore default rule configuration file." );
    }
}


void
StreamAnalyzer::setDefaultConfig()
{
    try
    {
        boost::filesystem::path  cfg( m_cfg_dir + "dasmond.cfg" );
        boost::filesystem::path  cfg_bak( m_cfg_dir + "dasmond_def.cfg" );

        if ( boost::filesystem::exists( cfg ))
        {
            if ( boost::filesystem::exists( cfg_bak ))
                boost::filesystem::remove( cfg_bak );
            boost::filesystem::copy_file( cfg, cfg_bak );
        }
    }
    catch ( ... )
    {
        syslog( LOG_ERR, "Could not set default rule configuration file." );
    }
}

void
StreamAnalyzer::attach( ISignalListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);

    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
        m_listeners.push_back( &a_listener );
}


void
StreamAnalyzer::detach( ISignalListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);

    vector<ISignalListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}


#if 0

void
StreamAnalyzer::defineSignal( const std::string &a_expression )
{
    boost::char_separator<char> sep(",");
    boost::tokenizer<boost::char_separator<char> > tokens(a_expression, sep);
    boost::tokenizer<boost::char_separator<char> >::iterator tok = tokens.begin();
    SignalInfo info;

    if ( tok == tokens.end())
        throw "Syntax error";

    info.name = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    info.fact = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    info.source = *tok;

    if ( ++tok == tokens.end())
        throw "Syntax error";

    // Level can be a number from 0 to 6, or a symbol
    try
    {
        unsigned short tmp = boost::lexical_cast<unsigned short>( *tok );
        if ( tmp > 6 )
            throw "Syntax error (bad level)";
        info.level = (ADARA::Level)tmp;
    }
    catch(...)
    {
        // May also throw an exception (which is OK)
        info.level = ADARA::ComBus::ComBusHelper::toLevel( *tok );
    }

    if ( ++tok == tokens.end())
        throw "Syntax error";

    info.msg = *tok;

    m_signals[info.fact] = info;
}
#endif


void
StreamAnalyzer::resendState()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine->sendAsserted( *this );
}


void
StreamAnalyzer::getDefinitions( std::vector<RuleEngine::RuleInfo> &a_rules, std::vector<SignalInfo> &a_signals )
{
    m_engine->getDefinedRules( a_rules );

    a_signals.clear();
    for ( map<string,SignalInfo>::const_iterator s = m_signals.begin(); s != m_signals.end(); ++s )
        a_signals.push_back( s->second );
}


bool
StreamAnalyzer::setDefinitions( const vector<RuleEngine::RuleInfo> &a_rules, const vector<SignalInfo> &a_signals )
{
    // The process for setting new rules is to create a new RuleEngine instance and
    // attempt to initizlize it with the provided rules. If this succeeds, then the
    // current engine will be replaced with the new engine, and all listeners will be
    // transferred. Next, old/invalid signals will be retracted, and new/updated signals
    // will be asserted.

    RuleEngine *engine = new RuleEngine();
    vector<RuleEngine::RuleInfo>::const_iterator r;

    try
    {
        for ( r = a_rules.begin(); r != a_rules.end(); ++r )
        {
            engine->defineRule( r->fact, r->expr );
        }
    }
    catch ( ... )
    {
        // Rule failed to parse, abort
        // TODO Would be nice to provide some feedback as to what went wrong
        return false;
    }

    // Engine has been initialized successfully, now validate signals

    std::map<std::string,SignalInfo>  tmp_signals;
    int  i;
    for ( vector<SignalInfo>::const_iterator sig = a_signals.begin(); sig != a_signals.end(); ++sig )
    {
        // The only real check to do here is to ensure referential integrity
        for ( r = a_rules.begin(); r != a_rules.end(); ++r )
        {
            if ( sig->fact == r->fact )
            {
                tmp_signals[sig->fact] = *sig;
                break;
            }
        }

        if ( r == a_rules.end())
        {
            // Is this a built-in fact?
            for ( i = 0; i < BIF_COUNT; ++i )
            {
                if ( m_fact_name[i] == sig->fact )
                {
                    tmp_signals[sig->fact] = *sig;
                    break;
                }
            }

            // If unassociated signal found, abort (probably a mistake)
            if ( i == BIF_COUNT )
                return false;
        }
    }

    // Rules & signals are OK, now perform swap-out of engine and updating of listeners

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Prevent current engine from generating new assert/retacts while we're working
    m_engine->beginBatch();

    // Transfer currently asserted non-rule facts from old engine to new
    // Build a list of currently asserted signals

    vector<string>                      asserted_facts;
    vector<string>                      old_asserted_signals;
    vector<string>                      new_asserted_signals;
    vector<string>                      signals_to_assert;
    std::map<std::string,SignalInfo>    old_signals;
    map<string,SignalInfo>::iterator    iSig, iSig2;
    vector<string>::iterator            iAF;

    m_engine->getAsserted( asserted_facts );

    for ( iAF = asserted_facts.begin(); iAF != asserted_facts.end(); ++iAF )
    {
        iSig = m_signals.find( *iAF );
        if ( iSig != m_signals.end())
        {
            old_asserted_signals.push_back( iSig->second.name );
        }
    }

    // This call does NOT generate any assert/retract traffic
    engine->synchronize( *m_engine );

    // Build list of new asserted signals

    engine->getAsserted( asserted_facts );

    for ( iAF = asserted_facts.begin(); iAF != asserted_facts.end(); ++iAF )
    {
        iSig = tmp_signals.find( *iAF );
        if ( iSig != tmp_signals.end())
        {
            new_asserted_signals.push_back( iSig->second.name );
        }
    }

    // Retract any old signals that are no longer asserted
    for ( vector<string>::iterator iOld = old_asserted_signals.begin(); iOld != old_asserted_signals.end(); ++iOld )
    {
        if ( find( new_asserted_signals.begin(), new_asserted_signals.end(), *iOld ) == new_asserted_signals.end())
        {
            // m_signals is indexed by fact name, not signal name, so we have to search linearly
            iSig = findByName( m_signals, *iOld );
            if ( iSig != m_signals.end() )
                onRetract( iSig->second.fact );
        }
    }

    // Swap-out signals (so we can call onAssert method)
    old_signals = m_signals;
    m_signals = tmp_signals;

    // Determine if any new signals are present
    for ( vector<string>::iterator iNew = new_asserted_signals.begin(); iNew != new_asserted_signals.end(); ++iNew )
    {
        if ( find( old_asserted_signals.begin(), old_asserted_signals.end(), *iNew ) == old_asserted_signals.end())
            signals_to_assert.push_back( *iNew );
    }

    // Determine if any signals have different signal content
    for ( vector<string>::iterator iNew = new_asserted_signals.begin(); iNew != new_asserted_signals.end(); ++iNew )
    {
        // m_signals is indexed by fact name, not signal name, so we have to search linearly
        iSig = findByName( old_signals, *iNew );
        if ( iSig != old_signals.end() )
        {
            iSig2 = findByName( m_signals, *iNew );
            if ( iSig2 != m_signals.end() )
            {
                // Are the signal params different? If not, reassert
                if ( iSig->second.level != iSig2->second.level ||
                     iSig->second.source != iSig2->second.source ||
                     iSig->second.msg != iSig2->second.msg )
                {
                    signals_to_assert.push_back( *iNew );
                }
            }
        }
    }

    // Assert new/changed signals
    for ( vector<string>::iterator iNew = signals_to_assert.begin(); iNew != signals_to_assert.end(); ++iNew )
    {
        // m_signals is indexed by fact name, not signal name, so we have to search linearly
        iSig = findByName( m_signals, *iNew );
        if ( iSig != m_signals.end())
            onAssert( iSig->second.fact );
    }

    // Re-acquire fact handles for built-in-facts
    for ( int i = 0; i < BIF_COUNT; ++i )
        m_fact[i] = engine->getFactHandle( m_fact_name[i] );

    // Clean-up
    delete m_engine;

    // Make new engine and new signals active
    m_engine = engine;

    return true;
}

map<string,SignalInfo>::iterator
StreamAnalyzer::findByName( map<string,SignalInfo> &a_map, std::string a_name )
{
    map<string,SignalInfo>::iterator i;
    for ( i = a_map.begin(); i != a_map.end(); ++i )
    {
        if ( i->second.name == a_name )
            break;
    }

    return i;
}


void
StreamAnalyzer::getInputFacts( std::set<std::string> &a_facts ) const
{
    a_facts.insert(m_fact_name[BIF_RECORDING]);
    a_facts.insert(m_fact_name[BIF_RUN_NUMBER]);
    a_facts.insert(m_fact_name[BIF_PAUSED]);
    a_facts.insert(m_fact_name[BIF_SCANNING]);
    a_facts.insert(m_fact_name[BIF_SCAN_INDEX]);
    a_facts.insert(m_fact_name[BIF_FAC_NAME]);
    a_facts.insert(m_fact_name[BIF_BEAM_ID]);
    a_facts.insert(m_fact_name[BIF_BEAM_SNAME]);
    a_facts.insert(m_fact_name[BIF_BEAM_LNAME]);
    a_facts.insert(m_fact_name[BIF_RUN_TITLE]);
    a_facts.insert(m_fact_name[BIF_PROP_ID]);
    a_facts.insert(m_fact_name[BIF_SAMPLE_ID]);
    a_facts.insert(m_fact_name[BIF_SAMPLE_NAME]);
    a_facts.insert(m_fact_name[BIF_SAMPLE_NAT]);
    a_facts.insert(m_fact_name[BIF_SAMPLE_FORM]);
    a_facts.insert(m_fact_name[BIF_SAMPLE_ENV]);
    a_facts.insert(m_fact_name[BIF_USER_INFO]);
    a_facts.insert(m_fact_name[BIF_COUNT_RATE]);
    a_facts.insert(m_fact_name[BIF_PULSE_CHARGE]);
    a_facts.insert(m_fact_name[BIF_PULSE_FREQ]);
    a_facts.insert(m_fact_name[BIF_STREAM_RATE]);
    a_facts.insert(m_fact_name[BIF_RUN_TOTAL_CHARGE]);
    a_facts.insert(m_fact_name[BIF_RUN_TOTAL_COUNTS]);
    a_facts.insert(m_fact_name[BIF_PIX_ERR_COUNT]);
    a_facts.insert(m_fact_name[BIF_DUP_PULSE_COUNT]);
    a_facts.insert(m_fact_name[BIF_MAP_ERROR_COUNT]);
    a_facts.insert(m_fact_name[BIF_MISS_RTDL_COUNT]);
    a_facts.insert(m_fact_name[BIF_PULSE_VETO_COUNT]);
    a_facts.insert(m_fact_name[BIF_SMS_CONNECTED]);
    a_facts.insert(m_fact_name[BIF_GENERAL_PV_LIMIT]);
    a_facts.insert(m_fact_name[BIF_GENERAL_PV_ERROR]);

    vector<string> facts;
    m_engine->getAsserted( facts );
    for ( vector<string>::iterator f = facts.begin(); f != facts.end(); ++f )
    {
        // If asserted fact is not a built-in fact, then it is a PV fact
        if ( a_facts.find( *f ) == a_facts.end())
        {
            a_facts.insert(*f);
        }
    }
}

/** \brief This method debounces fact "noise" at run boundaries
  *
  * When a run is started or stopped, many facts in the rule engine must be retracted due
  * to PVs being redefined. The PVs that are unchanged will be asserted again very quickly
  * and would causes the associated rules or signals to flicker. To avoid this, the rule
  * engine is place in batch mode on run transition, and this thread is used to end the
  * batch mode after a specified debounce time.
  */
void
StreamAnalyzer::runDebounceThread()
{
    while ( 1 )
    {
        sleep( 1 );

        boost::unique_lock<boost::mutex> lock(m_mutex);
        if ( m_debounce_sec )
        {
            --m_debounce_sec;
            if ( !m_debounce_sec )
            {
                endBatch( RUN_BATCH_MASK );
            }
        }
    }
}


void
StreamAnalyzer::beginBatch( uint32_t a_mask )
{
    if ( !m_batch_mask )
        m_engine->beginBatch();

    m_batch_mask |= a_mask;
}


void
StreamAnalyzer::endBatch( uint32_t a_mask )
{
    if ( m_batch_mask )
    {
        m_batch_mask &= ~a_mask;

        if ( !m_batch_mask )
            m_engine->endBatch();
    }
}


void
StreamAnalyzer::assertFact( const std::string &a_fact )
{
    m_engine->assert( a_fact );
}

template<class T>
void
StreamAnalyzer::assertFact( const std::string &a_fact, T a_value )
{
    m_engine->assert<T>( a_fact, a_value );
}

void
StreamAnalyzer::retractFact( const std::string &a_fact )
{
    m_engine->retract( a_fact );
}

template void StreamAnalyzer::assertFact<bool>( const string &a_id, bool a_value );
template void StreamAnalyzer::assertFact<char>( const string &a_id, char a_value );
template void StreamAnalyzer::assertFact<int8_t>( const string &a_id, int8_t a_value );
template void StreamAnalyzer::assertFact<uint8_t>( const string &a_id, uint8_t a_value );
template void StreamAnalyzer::assertFact<int16_t>( const string &a_id, int16_t a_value );
template void StreamAnalyzer::assertFact<uint16_t>( const string &a_id, uint16_t a_value );
template void StreamAnalyzer::assertFact<int32_t>( const string &a_id, int32_t a_value );
template void StreamAnalyzer::assertFact<uint32_t>( const string &a_id, uint32_t a_value );
template void StreamAnalyzer::assertFact<float>( const string &a_id, float a_value );
template void StreamAnalyzer::assertFact<double>( const string &a_id, double a_value );

//////////////////////////////////////////////
// IStreamListener


void
StreamAnalyzer::runStatus( bool a_recording, uint32_t a_run_number, uint32_t a_timestamp )
{
    (void)a_timestamp;  // Don't use timestamp

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // For both starting and stopping a run, facts associated with PVs must be cleared
    // as the SMS will re-broadcast only active PVs at these transitions. This allows
    // stale PVs (from disconnected devices) to be cleared out.

    // In order to "debounce" signals that may flicker when this happens, batch mode
    // is initiated here, then ended a few seconds later by a timer. This way only
    // more persistent signal changes will be emitted rather than the bounce
    // caused by the clear and reassertion of facts.

    beginBatch( RUN_BATCH_MASK );
    m_debounce_sec = 2;

    // Retract all PV facts
    m_engine->retractPrefix( m_pv_prefix );
    m_engine->retractPrefix( m_pv_lim_prefix );
    m_engine->retractPrefix( m_pv_err_prefix );

    // Reset PVS at each run start boundary
    m_engine->retract( m_fact[BIF_GENERAL_PV_ERROR] );
    m_error_pvs.clear();
    m_engine->retract( m_fact[BIF_GENERAL_PV_LIMIT] );
    m_limit_pvs.clear();

    if ( a_recording )
    {
        m_engine->assert( m_fact[BIF_RECORDING] );
        m_engine->assert( m_fact[BIF_RUN_NUMBER], a_run_number );
    }
    else
    {
        m_engine->retract( m_fact[BIF_RECORDING] );
        m_engine->retract( m_fact[BIF_RUN_NUMBER] );

        m_engine->retract( m_fact[BIF_FAC_NAME] );
        m_engine->retract( m_fact[BIF_BEAM_ID] );
        m_engine->retract( m_fact[BIF_BEAM_SNAME] );
        m_engine->retract( m_fact[BIF_BEAM_LNAME] );
        m_engine->retract( m_fact[BIF_RUN_TITLE] );
        m_engine->retract( m_fact[BIF_PROP_ID] );
        m_engine->retract( m_fact[BIF_SAMPLE_ID] );
        m_engine->retract( m_fact[BIF_SAMPLE_NAME] );
        m_engine->retract( m_fact[BIF_SAMPLE_NAT] );
        m_engine->retract( m_fact[BIF_SAMPLE_FORM] );
        m_engine->retract( m_fact[BIF_SAMPLE_ENV] );
        m_engine->retract( m_fact[BIF_USER_INFO] );
    }
}


void
StreamAnalyzer::pauseStatus( bool a_paused )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_paused )
    {
        m_engine->assert( m_fact[BIF_PAUSED] );
    }
    else
    {
        m_engine->retract( m_fact[BIF_PAUSED] );
    }
}


void
StreamAnalyzer::scanStatus( bool a_scanning, uint32_t a_scan_index )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_scanning )
    {
        m_engine->assert( m_fact[BIF_SCANNING] );
        m_engine->assert( m_fact[BIF_SCAN_INDEX], a_scan_index );
    }
    else
    {
        m_engine->retract( m_fact[BIF_SCANNING] );
        m_engine->retract( m_fact[BIF_SCAN_INDEX] );
    }
}


void
StreamAnalyzer::beamInfo( const ADARA::DASMON::BeamInfo &a_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( !a_info.m_facility.empty() )
        m_engine->assert( m_fact[BIF_FAC_NAME] );
    if ( !a_info.m_beam_id.empty() )
        m_engine->assert( m_fact[BIF_BEAM_ID] );
    if ( !a_info.m_beam_sname.empty() )
        m_engine->assert( m_fact[BIF_BEAM_SNAME] );
    if ( !a_info.m_beam_lname.empty() )
        m_engine->assert( m_fact[BIF_BEAM_LNAME] );
}


void
StreamAnalyzer::runInfo( const ADARA::DASMON::RunInfo &a_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( !a_info.m_run_title.empty() )
        m_engine->assert( m_fact[BIF_RUN_TITLE] );
    if ( !a_info.m_proposal_id.empty() )
        m_engine->assert( m_fact[BIF_PROP_ID] );
    if ( !a_info.m_sample_id.empty() )
        m_engine->assert( m_fact[BIF_SAMPLE_ID] );
    if ( !a_info.m_sample_name.empty() )
        m_engine->assert( m_fact[BIF_SAMPLE_NAME] );
    if ( !a_info.m_sample_nature.empty() )
        m_engine->assert( m_fact[BIF_SAMPLE_NAT] );
    if ( !a_info.m_sample_formula.empty() )
        m_engine->assert( m_fact[BIF_SAMPLE_FORM] );
    if ( !a_info.m_sample_environ.empty() )
        m_engine->assert( m_fact[BIF_SAMPLE_ENV] );
    if ( !a_info.m_user_info.empty() )
        m_engine->assert( m_fact[BIF_USER_INFO] );
}


void
StreamAnalyzer::beamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    beginBatch( METRICS_BATCH_MASK );

    m_engine->assert( m_fact[BIF_COUNT_RATE], a_metrics.m_count_rate );

    m_engine->retractPrefix( "MONITOR_RATE_" );

    for ( map<uint32_t,double>::const_iterator im = a_metrics.m_monitor_count_rate.begin(); im != a_metrics.m_monitor_count_rate.end(); ++im )
        m_engine->assert( string("MONITOR_RATE_") + boost::lexical_cast<std::string>(im->first), im->second );

    m_engine->assert( m_fact[BIF_PULSE_CHARGE], a_metrics.m_pulse_charge );
    m_engine->assert( m_fact[BIF_PULSE_FREQ], a_metrics.m_pulse_freq );
    m_engine->assert( m_fact[BIF_STREAM_RATE], a_metrics.m_stream_bps );

    endBatch( METRICS_BATCH_MASK );
}


void
StreamAnalyzer::runMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine->assert( m_fact[BIF_RUN_TOTAL_CHARGE], a_metrics.m_total_charge );
    m_engine->assert( m_fact[BIF_RUN_TOTAL_COUNTS], a_metrics.m_total_counts );
    m_engine->assert( m_fact[BIF_PIX_ERR_COUNT], a_metrics.m_pixel_error_count );
    m_engine->assert( m_fact[BIF_DUP_PULSE_COUNT], a_metrics.m_dup_pulse_count );
    m_engine->assert( m_fact[BIF_MAP_ERROR_COUNT], a_metrics.m_mapping_error_count );
    m_engine->assert( m_fact[BIF_MISS_RTDL_COUNT], a_metrics.m_missing_rtdl_count );
    m_engine->assert( m_fact[BIF_PULSE_VETO_COUNT], a_metrics.m_pulse_veto_count );
}


void
StreamAnalyzer::pvDefined( const std::string &a_name )
{
    (void)a_name;
}


void
StreamAnalyzer::pvUndefined( const std::string &a_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    string pv_name = boost::to_upper_copy( a_name );

    m_engine->retract( m_pv_prefix + pv_name );
    processPvStatus( pv_name, VariableStatus::OK, true ); // Needed to clean-up PV errors & limits
}


void
StreamAnalyzer::pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status, uint32_t a_timestamp )
{
    (void)a_timestamp;

    boost::lock_guard<boost::mutex> lock(m_mutex);
    string pv_name = boost::to_upper_copy( a_name );

    if ( a_status == VariableStatus::NO_COMMUNICATION || a_status == VariableStatus::UPSTREAM_DISCONNECTED )
        m_engine->retract( m_pv_prefix + pv_name );
    else
        m_engine->assert( m_pv_prefix + pv_name, a_value );

    processPvStatus( pv_name, a_status, false );
}


void
StreamAnalyzer::pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp )
{
    (void)a_timestamp;

    boost::lock_guard<boost::mutex> lock(m_mutex);
    string pv_name = boost::to_upper_copy( a_name );

    if ( a_status == VariableStatus::NO_COMMUNICATION || a_status == VariableStatus::UPSTREAM_DISCONNECTED  )
        m_engine->retract( m_pv_prefix + pv_name );
    else
        m_engine->assert( m_pv_prefix + pv_name, a_value );

    processPvStatus( pv_name, a_status, false );
}


void
StreamAnalyzer::processPvStatus( const string &pv_name, VariableStatus::Enum a_status, bool a_retracted )
{
    if ( !a_retracted && a_status != VariableStatus::OK )
    {
        // TODO Surely there is a better way to define PV status so code like the following can be avoided?
        if (( a_status >= VariableStatus::HIHI_LIMIT && a_status <= VariableStatus::LOW_LIMIT ) || a_status == VariableStatus::HARDWARE_LIMIT )
        {
            m_engine->assert( m_pv_lim_prefix + pv_name, (uint32_t)PV_LIMIT );

            if ( m_limit_pvs.empty())
                m_engine->assert( m_fact[BIF_GENERAL_PV_LIMIT] );

            m_limit_pvs.insert( pv_name );
        }
        else
        {
            m_engine->assert( m_pv_err_prefix + pv_name, (uint32_t)PV_ERROR );

            if ( m_error_pvs.empty())
                m_engine->assert( m_fact[BIF_GENERAL_PV_ERROR] );

            m_error_pvs.insert( pv_name );
        }
    }
    else
    {
        // Did this PV previously have an error or limit status?
        // If so, clean-up associated facts
        set<string>::iterator ipv = m_error_pvs.find( pv_name );
        if ( ipv != m_error_pvs.end() )
        {
            m_engine->retract( m_pv_err_prefix + pv_name );

            m_error_pvs.erase( ipv );
            if ( m_error_pvs.empty())
                m_engine->retract( m_fact[BIF_GENERAL_PV_ERROR] );
        }
        else
        {
            ipv = m_limit_pvs.find( pv_name );
            if ( ipv != m_limit_pvs.end() )
            {
                m_engine->retract( m_pv_lim_prefix + pv_name );

                m_limit_pvs.erase( ipv );
                if ( m_limit_pvs.empty())
                    m_engine->retract( m_fact[BIF_GENERAL_PV_LIMIT] );
            }
        }
    }
}


void
StreamAnalyzer::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    (void)a_host;
    (void)a_port;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( a_connected )
        m_engine->assert( m_fact[BIF_SMS_CONNECTED] );
    else
    {
        beginBatch( CONN_BATCH_MASK );
        m_engine->retractAllFacts();
        endBatch( CONN_BATCH_MASK );
    }
}

// IFactListener Interface

void
StreamAnalyzer::onAssert( const std::string &a_fact )
{
    map<string,SignalInfo>::iterator isig = m_signals.find( a_fact );
    if ( isig != m_signals.end())
    {
        for ( vector<ISignalListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
            (*l)->signalAssert( isig->second );
    }
}


void
StreamAnalyzer::onRetract( const std::string &a_fact )
{
    map<string,SignalInfo>::iterator isig = m_signals.find( a_fact );
    if ( isig != m_signals.end())
    {
        for ( vector<ISignalListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
            (*l)->signalRetract( isig->second.name );
    }
}

}}
