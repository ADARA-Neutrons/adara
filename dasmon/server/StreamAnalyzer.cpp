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


// Public Methods ------------------------------------------------------------


/** \brief StreamAnalyser constructor.
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
    m_fact_name[BIF_RUN_TIME]            = "RUN_TIME";
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


/** \brief StreamAnalyzer destructor.
  *
  */
StreamAnalyzer::~StreamAnalyzer()
{
    m_monitor.removeListener( *this );
    delete m_engine;
}


/**
  * This method loads rule and signal definitions from the dasmon configuration
  * file (dasmond.cfg) in the configuration directory (specified from command
  * line arg). If errors are found in the config file, they will be logged, but
  * dasmond will start regardless.
  */
void
StreamAnalyzer::loadConfig()
{
    RuleEngine::RuleInfo            rule;
    vector<RuleEngine::RuleInfo>    loaded_rules;
    SignalInfo                      signal;
    vector<SignalInfo>              loaded_signals;

    boost::char_separator<char> sep1(":");
    boost::char_separator<char> sep2(",");
    boost::tokenizer<boost::char_separator<char> >::iterator tok;

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
            map<string,string> errors;
            if ( !setDefinitions( loaded_rules, loaded_signals, errors ))
            {
                syslog( LOG_ERR, "Failed loading configuration file: %s", cfg.c_str() );

                for ( map<string,string>::iterator ie = errors.begin(); ie != errors.end(); ++ie )
                    syslog( LOG_ERR, "Config error on %s: %s", ie->first.c_str(), ie->second.c_str() );
            }
        }
        catch ( ... )
        {
            inf.close();
            syslog( LOG_ERR, "Failed loading configuration file: %s", cfg.c_str() );
        }
    }
}


/**
  * This method saves the current rule and signal configuration to the dasmond
  * config file (dasmond.cfg) in the configuration directory.
  */
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


/**
  * This method restores default configuration by copying the default config
  * file (dasmond_def.cfg) over the current config file (dasmond.cfg), then
  * reloads the configuration file.
  */
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


/**
  * This method sets the default configuration by copying the current config
  * file (dasmond.cfg) over the default config file (dasmond_def.cfg).
  */
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


/** \param a_listener - ISignalListener to attach
  *
  * This method attaches a signal listener to the StreamAnalyzer. Asserted
  * signals are NOT sent to the newly atached listener - an explicit call
  * must be made for that.
  */
void
StreamAnalyzer::attach( ISignalListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);

    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
        m_listeners.push_back( &a_listener );
}


/** \param a_listener - ISignalListener to detach
  *
  * This method detaches a signal listener from the StreamAnalyzer.
  */
void
StreamAnalyzer::detach( ISignalListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);

    vector<ISignalListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}


/** \brief Resends StreamAnalyzer state (asserted signals) to all attached listeners.
  */
void
StreamAnalyzer::resendState()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine->sendAsserted( *this );
}


/** \param a_rules - Vector to receive rule definitions
  * \param a_signals - Vector to receive signal definitions
  *
  * This method returns the current rule and signal definitions.
  */
void
StreamAnalyzer::getDefinitions( std::vector<RuleEngine::RuleInfo> &a_rules, std::vector<SignalInfo> &a_signals )
{
    m_engine->getDefinedRules( a_rules );

    a_signals.clear();
    for ( map<string,SignalInfo>::const_iterator s = m_signals.begin(); s != m_signals.end(); ++s )
        a_signals.push_back( s->second );
}


/** \param a_rules - New rules to be defined
  * \param a_signals - New signals to be defined
  * \param a_errors - Outputs per-rule/signal error messages
  *
  * This method attemps to set new rules and signals for the RuleEngine within the
  * StreamAnalyzer. If any errors are encountered while setting the new rules and
  * signals, the entire process will fail and the RuleEngine will retain it's state
  * prior to this call. This call replaces all current rules and signals with the
  * specified new rules and signals.
  */
bool
StreamAnalyzer::setDefinitions( const vector<RuleEngine::RuleInfo> &a_rules, const vector<SignalInfo> &a_signals, map<string,string> &a_errors )
{
    // The process for setting new rules is to create a new RuleEngine instance and
    // attempt to initizlize it with the provided rules. If this succeeds, then the
    // current engine will be replaced with the new engine, and all listeners will be
    // transferred. Next, old/invalid signals will be retracted, and new/updated signals
    // will be asserted.

    bool res = true;
    RuleEngine *engine = new RuleEngine();
    vector<RuleEngine::RuleInfo>::const_iterator r;

    for ( r = a_rules.begin(); r != a_rules.end(); ++r )
    {
        try
        {
            engine->defineRule( r->fact, r->expr );
        }
        catch ( std::exception &e )
        {
            // Rule failed to parse
            a_errors[r->fact] = e.what();
            res = false;
        }
        catch ( ... )
        {
            // Rule failed to parse
            a_errors[r->fact] = "Unknown exception";
            res = false;
        }
    }

    // Engine has been initialized successfully, now validate signals

    std::map<std::string,SignalInfo>  tmp_signals;
    int  i;
    string sig_fact, tmp;

    for ( vector<SignalInfo>::const_iterator sig = a_signals.begin(); sig != a_signals.end(); ++sig )
    {
        // Rule engine converts facts to upper case and trims spaces
        sig_fact = boost::to_upper_copy( sig->fact );
        boost::algorithm::trim( sig_fact );

        // The only real check to do here is to ensure referential integrity
        for ( r = a_rules.begin(); r != a_rules.end(); ++r )
        {
            // Rule engine converts facts to upper case and trims spaces
            tmp = boost::to_upper_copy( r->fact );
            boost::algorithm::trim( tmp );

            if ( sig_fact == tmp )
            {
                tmp_signals[sig_fact] = *sig;
                break;
            }
        }

        if ( r == a_rules.end())
        {
            // Is this a built-in fact?
            for ( i = 0; i < BIF_COUNT; ++i )
            {
                if ( m_fact_name[i] == sig_fact )
                {
                    tmp_signals[sig_fact] = *sig;
                    break;
                }
            }

            // If unassociated signal found, abort (probably a mistake)
            if ( i == BIF_COUNT )
            {
                a_errors[sig->name] = "References undefined rule";
                res = false;
            }
        }
    }

    // If any errors in rules or signals, abort now
    if ( !res )
        return false;

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


/** \param a_fact - Set to receive all available facts
  *
  * This method returns all available facts (asserted or not) that can be used
  * in a rule expression. It is acceptable to compose rules that reference
  * undefined facts as they may be asserted in the future.
  */
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
    a_facts.insert(m_fact_name[BIF_RUN_TIME]);
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


/** \brief This method asserts a fact (void type) by name
  * \param a_fact - Name of fact to assert
  *
  * This method is provided for use by other classes to assert a fact into the
  * rule engine contained within the StreamAnalyzer class.
  */
void
StreamAnalyzer::assertFact( const std::string &a_fact )
{
    m_engine->assert( a_fact );
}


/** \brief This method asserts a fact by name
  * \param a_fact - Name of fact to assert
  * \param a_value - New value of fact
  *
  * This method is provided for use by other classes to assert a fact with a
  * value into the rule engine contained within the StreamAnalyzer class.
  */
template<class T>
void
StreamAnalyzer::assertFact( const std::string &a_fact, T a_value )
{
    m_engine->assert<T>( a_fact, a_value );
}


/** \brief This method retracts a fact by name
  * \param a_fact - Name of fact to retract
  *
  * This method is provided for use by other classes to retract a fact from the
  * rule engine contained within the StreamAnalyzer class.
  */
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


// Private Methods ------------------------------------------------------------


/** \param a_map - Signal map to search
  * \param a_name - Signal name to search for
  *
  * This is a support method to search the signal map for a given signal name.
  * The signal map is indexed by signal fact (rule id) instead of signal name,
  * so a linear search must be performed.
  */
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


/** \brief This method debounces fact "noise" at run boundaries
  *
  * When a run is started or stopped, many facts in the rule engine must be
  * retracted due to PVs being redefined. The PVs that are unchanged will be
  * asserted again very quickly and would causes the associated rules or signals
  * to flicker. To avoid this, the rule engine is place in batch mode on run
  * transition, and this thread is used to end the batch mode after a specified
  * debounce time.
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


/** \brief This method starts batch mode for the rule engine
  * \param a_mask - Bitmask for batch context
  *
  * This method starts batch mode and applies a batch context mask. If multiple
  * contexts have started batch mode, batch mode will not be stopped until all
  * contexts have stopped.
  */
void
StreamAnalyzer::beginBatch( uint32_t a_mask )
{
    if ( !m_batch_mask )
        m_engine->beginBatch();

    m_batch_mask |= a_mask;
}


/** \brief This method stops batch mode for the rule engine
  * \param a_mask - Bitmask for batch context
  *
  * This method stops batch mode and removes the batch context mask. If multiple
  * contexts have started batch mode, batch mode will not be stopped until all
  * contexts have stopped.
  */
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


// IStreamListener Interface --------------------------------------------------


/** \brief Callback for run status updates
  * \param a_recording - When true, indicates system is recording
  * \param a_run_number - Run number of recording (0 when no recording)
  * \param a_timestamp - Timestamp of update (EPICS epoch)
  *
  * This method is called by the StreamMonitor instance whenever the system
  * starts or stops recording a run. The recording state and run number are
  * asserted and retracted accordingly. Beam and run info are retracted when
  * a run stops, and PVs are retracted at both starts and stops as the SMS
  * resends device descriptors after each transition.
  */
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

        // Retract now such that new values (or not) can be asserted from beam and run info packets
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
    else
    {
        m_engine->retract( m_fact[BIF_RECORDING] );
        m_engine->retract( m_fact[BIF_RUN_NUMBER] );

        //TODO Add retraction of beam and run info when protocol is changed
    }
}


/** \brief Callback for pause status updates
  * \param a_paused - When true, indicates system is paused
  *
  * This method is called by the StreamMonitor instance whenever the system
  * is paused or resumes. The pause state is asserted and retracted accordingly.
  */
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


/** \brief Callback for scan status updates
  * \param a_scanning - When true, indicates scanning is in progress
  * \param a_scan_index - Scan index (when scanning)
  *
  * This method is called by the StreamMonitor instance whenever the system
  * starts or stops a scan. The scan status and index are asserted when
  * a scan starts and retracted when a scan stops.
  */
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


/** \brief Callback for updated beam info.
  * \param a_info - Updated beam information
  *
  * This method is called on run starts and stops by the StreamMonitor instance
  * to update stream listeners with beam information. This presence of
  * individual items (being non-empty) is asserted into the rule engine as a
  * flag.
  */
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


/** \brief Callback for updated run info.
  * \param a_info - Updated run information
  *
  * This method is called on run starts and stops by the StreamMonitor instance
  * to update stream listeners with run information. This presence of
  * individual items (being non-empty) is asserted into the rule engine as a
  * flag.
  */
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


/** \brief Callback for updated beam metrics.
  * \param a_metrics - Updated beam metrics
  *
  * This method is called periodically by the StreamMonitor instance to update
  * stream listeners with various beam metrics. Metrics are asserted into the
  * rule engine (in batch mode to prevent flicker due to retracting monitor
  * count rates).
  */
void
StreamAnalyzer::beamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Batch mode is needed beause monitor rates must be retracted first to
    // clear-out stale values.
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


/** \brief Callback for updated run metrics.
  * \param a_metrics - Updated run metrics
  *
  * This method is called periodically by the StreamMonitor instance to update
  * stream listeners with various run metrics. Metrics are asserted into the
  * rule engine.
  */
void
StreamAnalyzer::runMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_engine->assert( m_fact[BIF_RUN_TIME], a_metrics.m_time );
    m_engine->assert( m_fact[BIF_RUN_TOTAL_CHARGE], a_metrics.m_total_charge );
    m_engine->assert( m_fact[BIF_RUN_TOTAL_COUNTS], a_metrics.m_total_counts );
    m_engine->assert( m_fact[BIF_PIX_ERR_COUNT], a_metrics.m_pixel_error_count );
    m_engine->assert( m_fact[BIF_DUP_PULSE_COUNT], a_metrics.m_dup_pulse_count );
    m_engine->assert( m_fact[BIF_MAP_ERROR_COUNT], a_metrics.m_mapping_error_count );
    m_engine->assert( m_fact[BIF_MISS_RTDL_COUNT], a_metrics.m_missing_rtdl_count );
    m_engine->assert( m_fact[BIF_PULSE_VETO_COUNT], a_metrics.m_pulse_veto_count );
}


void
StreamAnalyzer::streamMetrics( const StreamMetrics &a_metrics )
{
    // TODO assert stream metrics facts
}


/** \brief Callback to indicate PV has been defined
  * \param a_pv_name - Name of defined PV
  *
  * This method is called when a PV is defined. Not currently used.
  */
void
StreamAnalyzer::pvDefined( const std::string &a_name )
{
    (void)a_name;
}


/** \brief Callback to indicate PV has been undefined
  * \param a_pv_name - Name of undefined PV
  *
  * This method is called when a PV is undefined. Associated facts are
  * retracted and general limit/error facts are updated.
  */
void
StreamAnalyzer::pvUndefined( const std::string &a_pv_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    string pv_name = boost::to_upper_copy( a_pv_name );

    m_engine->retract( m_pv_prefix + pv_name );
    processPvStatus( pv_name, VariableStatus::OK, true ); // Needed to clean-up PV errors & limits
}


/** \brief Callback to update the value and status of a integer PV.
  * \param a_pv_name - Name of process variable
  * \param a_value - New value of PV
  * \param a_status - New status of PV
  * \param a_timestamp - Timestamp of update (EPICS epoch)
  *
  * This method is called when a PV value or status changes. If status is
  * disconnected, pv is retracted from rule engine; otherwise pv is asserted
  * with associated value. Status is processed by a call to porcessPVStatus().
  */
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


/** \brief Callback to update the value and status of a double PV.
  * \param a_pv_name - Name of process variable
  * \param a_value - New value of PV
  * \param a_status - New status of PV
  * \param a_timestamp - Timestamp of update (EPICS epoch)
  *
  * This method is called when a PV value or status changes. If status is
  * disconnected, pv is retracted from rule engine; otherwise pv is asserted
  * with associated value. Status is processed by a call to porcessPVStatus().
  */
void
StreamAnalyzer::pvValue( const std::string &a_pv_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp )
{
    (void)a_timestamp;

    boost::lock_guard<boost::mutex> lock(m_mutex);
    string pv_name = boost::to_upper_copy( a_pv_name );

    if ( a_status == VariableStatus::NO_COMMUNICATION || a_status == VariableStatus::UPSTREAM_DISCONNECTED  )
        m_engine->retract( m_pv_prefix + pv_name );
    else
        m_engine->assert( m_pv_prefix + pv_name, a_value );

    processPvStatus( pv_name, a_status, false );
}


/** \brief Updates facts based on PV limits and errors
  * \param a_pv_name - Name of PV to update from
  * \param a_status - Status code associated with PV
  * \param a_retracted - Indicates PV has been undefined
  *
  * This method updates the assertion state of the limit and error facts
  * associated with a PV based on the provided PV status. This method also
  * updates the GENERAL_PV_ERROR and GENERAL_PV_LIMIT facts as needed.
  */
void
StreamAnalyzer::processPvStatus( const string &a_pv_name, VariableStatus::Enum a_status, bool a_retracted )
{
    if ( !a_retracted && a_status != VariableStatus::OK )
    {
        // TODO Surely there is a better way to define PV status so code like the following can be avoided?
        if (( a_status >= VariableStatus::HIHI_LIMIT && a_status <= VariableStatus::LOW_LIMIT ) || a_status == VariableStatus::HARDWARE_LIMIT )
        {
            m_engine->assert( m_pv_lim_prefix + a_pv_name, (uint32_t)PV_LIMIT );

            if ( m_limit_pvs.empty())
                m_engine->assert( m_fact[BIF_GENERAL_PV_LIMIT] );

            m_limit_pvs.insert( a_pv_name );
        }
        else
        {
            m_engine->assert( m_pv_err_prefix + a_pv_name, (uint32_t)PV_ERROR );

            if ( m_error_pvs.empty())
                m_engine->assert( m_fact[BIF_GENERAL_PV_ERROR] );

            m_error_pvs.insert( a_pv_name );
        }
    }
    else
    {
        // Did this PV previously have an error or limit status?
        // If so, clean-up associated facts
        set<string>::iterator ipv = m_error_pvs.find( a_pv_name );
        if ( ipv != m_error_pvs.end() )
        {
            m_engine->retract( m_pv_err_prefix + a_pv_name );

            m_error_pvs.erase( ipv );
            if ( m_error_pvs.empty())
                m_engine->retract( m_fact[BIF_GENERAL_PV_ERROR] );
        }
        else
        {
            ipv = m_limit_pvs.find( a_pv_name );
            if ( ipv != m_limit_pvs.end() )
            {
                m_engine->retract( m_pv_lim_prefix + a_pv_name );

                m_limit_pvs.erase( ipv );
                if ( m_limit_pvs.empty())
                    m_engine->retract( m_fact[BIF_GENERAL_PV_LIMIT] );
            }
        }
    }
}


/** \brief Callback to update SMS connection state
  * \param a_connected - True if connected; false otherwise
  * \param a_host - New SMS hostname
  * \param a_port - New SMS port number
  *
  * This method is a callback from the StreamMonitor to indicate changes in the
  * connection state with the SMS. When the connection is made, a
  * "SMS_CONNECTED" fact is asserted into the rule engine. When the connection
  * is lost, all asserted facts are retracted (the SMS is the source for all
  * facts).
  */
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

// IFactListener Interface ----------------------------------------------------

/** \param a_fact - Name of fact that was asserted
  *
  * This method is a callback from the RuleEngine indicating that the specified
  * fact has been asserted. If a signal is associated with the given fact, then
  * the StreamAnalyzer notifies all signal listeners that the associated signal
  * is also asserted.
  */
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


/** \param a_fact - Name of fact that was retracted
  *
  * This method is a callback from the RuleEngine indicating that the specified
  * fact was retracted. If a signal is associated with the given fact, then the
  * StreamAnalyzer notifies all signal listeners that the associated signal is
  * also retracted.
  */
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
