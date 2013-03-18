#include <string.h>
#include "ComBusRouter.h"
#include "DASMonMessages.h"

using namespace std;

namespace ADARA {
namespace DASMON {

// TODO Eventually connect to ComBus command topic to handle general commands


ComBusRouter::ComBusRouter( StreamMonitor &a_monitor, StreamAnalyzer &a_analyzer )
    : m_monitor( a_monitor ), m_analyzer( a_analyzer ), m_combus( ADARA::ComBus::Connection::getInst() ),
      m_resend_state(false), m_sms_connected(false), m_combus_connected(false)

{
    m_monitor.addListener( *this );
    m_analyzer.attach( *this );
    m_combus.attach( *this );
    m_combus.setControlListener( *this );
}


ComBusRouter::~ComBusRouter()
{
    m_combus.detach( *this );
    m_analyzer.detach( *this );
}


void
ComBusRouter::run()
{
    while(1)
    {
        sleep(5);
        m_combus.sendStatus( ADARA::ComBus::STATUS_RUNNING );
        if ( m_resend_state )
        {
            //cout << "Resend State..." << endl;
            m_monitor.resendState( *this );
            m_analyzer.resendState();
            m_resend_state = false;
        }
    }
}


void
ComBusRouter::sendRuleDefinitions( const string &a_src_proc, const string &a_CID )
{
    string cid = a_CID;
    ADARA::ComBus::DASMON::RuleDefinitions defs;

    m_analyzer.getDefinitions( defs.m_rules, defs.m_signals );
    m_combus.sendControl( defs, a_src_proc, cid );
}


void
ComBusRouter::setRuleDefinitions( const ADARA::ComBus::ControlMessage *a_msg )
{
    const ADARA::ComBus::DASMON::SetRuleDefinitions *set_msg =
            dynamic_cast<const ADARA::ComBus::DASMON::SetRuleDefinitions*>( a_msg );
    if ( set_msg )
    {
        // If this succeeds, current rules will be set to specified;
        // otherwise, current rules will remain unchanged
        //cout << "setting" << endl;
        m_analyzer.setDefinitions( set_msg->m_rules, set_msg->m_signals );
        //cout << "Saving new rules" << endl;
        m_analyzer.saveConfig();

        if ( set_msg->m_set_default )
        {
            //cout << "set as default" << endl;
            m_analyzer.setDefaultConfig();
        }
        //cout << "sending reply" << endl;
        sendRuleDefinitions( a_msg->getSourceName(), a_msg->m_correlation_id );
    }
}


void
ComBusRouter::sendInputFacts( const std::string &a_src_proc, const std::string &a_CID )
{
    string cid = a_CID;
    ADARA::ComBus::DASMON::InputFacts facts;

    m_analyzer.getInputFacts( facts.m_facts );
    m_combus.sendControl( facts, a_src_proc, cid );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IStreamListener Interface

void
ComBusRouter::runStatus( bool a_recording, unsigned long a_run_number )
{
    //cout << "runStatus: " << a_recording << " (" << a_run_number << ")" << endl;
    ComBus::DASMON::RunStatusMessage msg( a_recording, a_run_number );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::pauseStatus( bool a_paused )
{
    //cout << "pauseStatus: " << a_paused << endl;
    ComBus::DASMON::PauseStatusMessage msg( a_paused );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::scanStatus( bool a_scanning, unsigned long a_scan_number )
{
    //cout << "scanStatus: " << a_scanning << " (" << a_scan_number << ")" << endl;
    ComBus::DASMON::ScanStatusMessage msg( a_scanning, a_scan_number );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::beamInfo( const BeamInfo &a_info )
{
    //cout << "beamInfo..." << endl;
    ComBus::DASMON::BeamInfoMessage msg( a_info );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::runInfo( const RunInfo &a_info )
{
    //cout << "runInfo..." << endl;
    ComBus::DASMON::RunInfoMessage msg( a_info );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::beamMetrics( const BeamMetrics &a_metrics )
{
    ComBus::DASMON::BeamMetricsMessage msg( a_metrics );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::runMetrics( const RunMetrics &a_metrics )
{
    ComBus::DASMON::RunMetricsMessage msg( a_metrics );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::pvDefined( const std::string &a_name )
{
    (void)a_name;

    // TODO - Maybe eventually support a subscriber API for PVs?
    // Don't want to spam the system
}

void
ComBusRouter::pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status )
{
    (void)a_name;
    (void)a_value;
    (void)a_status;
    // TODO - Maybe eventually support a subscriber API for PVs?
    // Don't want to spam the system
}

void
ComBusRouter::pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status )
{
    (void)a_name;
    (void)a_value;
    (void)a_status;
    // TODO - Maybe eventually support a subscriber API for PVs?
    // Don't want to spam the system
}

void
ComBusRouter::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    //cout << "connectionStatus:." << a_connected << endl;

    ComBus::DASMON::ConnectionStatusMessage msg( a_connected, a_host, a_port );
    m_combus.sendMessage( msg );

    if ( a_connected && !m_sms_connected )
    {
        //cout << "RESEND b/c SMS CONNECTED" << endl;

        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
    }

    m_sms_connected = a_connected;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ISignalListener Interface

void
ComBusRouter::signalAssert( const SignalInfo &a_signal )
{
    //cout << "signal assert:  " << a_signal.name << endl;
    ComBus::SignalAssertMessage msg( a_signal.name, a_signal.source, a_signal.msg, a_signal.level );
    m_combus.sendMessage( msg );
}


void
ComBusRouter::signalRetract( const std::string &a_name )
{
    //cout << "signal retract: " << a_name << endl;
    ComBus::SignalRetractMessage msg( a_name );
    m_combus.sendMessage( msg );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IStatusListener Interface


void
ComBusRouter::comBusConnectionStatus( bool a_connected )
{
    if ( a_connected && !m_combus_connected )
    {
        //cout << "RESEND b/c COMBUS CONNECTED!!!" << endl;
        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
    }
    m_combus_connected = a_connected;
}


///////////////////////////////////////////////////////////
// IControlListener methods


bool
ComBusRouter::comBusControlMessage( const ADARA::ComBus::ControlMessage &a_msg )
{
    //cout << "Got Command: " << hex << a_msg.getMessageType() << endl;

    try
    {
        switch( a_msg.getMessageType() )
        {
        case ADARA::ComBus::MSG_CMD_EMIT_STATE:
            //cout << "Got: EMIT_STATE" << endl;
            m_resend_state = true;
            break;

        case ADARA::ComBus::MSG_DASMON_GET_RULES:
            //cout << "Got: GET_RULES" << endl;
            sendRuleDefinitions( a_msg.getSourceName(), a_msg.m_correlation_id );
            break;

        case ADARA::ComBus::MSG_DASMON_SET_RULES:
            //cout << "Got: SET_RULES" << endl;
            setRuleDefinitions( &a_msg );
            break;

        case ADARA::ComBus::MSG_DASMON_RESTORE_DEFAULT_RULES:
            //cout << "Got: RESTORE DEFAULT" << endl;
            m_analyzer.restoreDefaultConfig();
            sendRuleDefinitions( a_msg.getSourceName(), a_msg.m_correlation_id );
            break;

        case ADARA::ComBus::MSG_DASMON_GET_INPUT_FACTS:
            //cout << "Got: GET FACTS" << endl;
            sendInputFacts( a_msg.getSourceName(), a_msg.m_correlation_id );
            break;

        default:
            break;
        }
    }
    catch ( ... )
    {
    }

    return false;
}



}}
