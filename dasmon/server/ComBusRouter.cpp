#include "ComBusRouter.h"
#include "DASMonMessages.h"

using namespace std;
using namespace RuleEngine;

namespace ADARA {
namespace DASMON {

// TODO Eventually connect to ComBus command topic to handle general commands


ComBusRouter::ComBusRouter( StreamMonitor &a_monitor, RuleEngine::StreamAnalyzer &a_analyzer )
    : m_monitor( a_monitor ), m_analyzer( a_analyzer ), m_combus( ADARA::ComBus::Connection::getInst() ),
      m_resend_state(false), m_sms_connected(false), m_combus_connected(false)

{
    m_monitor.addListener( *this );
    m_analyzer.addListener( *this );
    m_combus.attach( *this );
}


ComBusRouter::~ComBusRouter()
{
    m_combus.detach( *this );
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
            cout << "Resend State..." << endl;
            m_resend_state = false;
            m_monitor.resendState( *this );
            m_analyzer.resendAsserted( *this );
        }
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IStreamListener Interface

void
ComBusRouter::runStatus( bool a_recording, unsigned long a_run_number )
{
    cout << "runStatus: " << a_recording << " (" << a_run_number << ")" << endl;
    ComBus::DASMON::RunStatusMessage msg( a_recording, a_run_number );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::pauseStatus( bool a_paused )
{
    cout << "pauseStatus: " << a_paused << endl;
    ComBus::DASMON::PauseStatusMessage msg( a_paused );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::scanStatus( bool a_scanning, unsigned long a_scan_number )
{
    cout << "scanStatus: " << a_scanning << " (" << a_scan_number << ")" << endl;
    ComBus::DASMON::ScanStatusMessage msg( a_scanning, a_scan_number );
    m_combus.sendMessage( msg );
}

void
ComBusRouter::beamInfo( const BeamInfo &a_info )
{
    cout << "beamInfo..." << endl;
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
ComBusRouter::pvValue( const std::string &a_name, double a_value )
{
    (void)a_name;
    (void)a_value;
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
        cout << "GOT SMS CONNECT - RESEND!!!" << endl;

        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
    }

    m_sms_connected = a_connected;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IRuleListener Interface

void
ComBusRouter::reportEvent( const Rule &a_rule )
{
    cout << "event: " << a_rule.getName() << endl;
    ComBus::SignalEventMessage msg( a_rule.getName(), a_rule.getSource(), a_rule.getMessage(), a_rule.getLevel() );
    m_combus.sendMessage( msg );
}


void
ComBusRouter::assertFinding( const std::string &a_id, const Rule &a_rule )
{
    cout << "assert: " << a_rule.getName() << ", id: " << a_id << endl;
    ComBus::SignalAssertMessage msg( a_id, a_rule.getName(), a_rule.getSource(), a_rule.getMessage(), a_rule.getLevel() );
    m_combus.sendMessage( msg );
}


void
ComBusRouter::retractFinding( const std::string &a_id )
{
    cout << "retract: " << a_id << endl;
    ComBus::SignalRetractMessage msg( a_id );
    m_combus.sendMessage( msg );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IStatusListener Interface


void
ComBusRouter::comBusConnectionStatus( bool a_connected )
{
    if ( a_connected && !m_combus_connected )
    {
        cout << "GOT COMBUS CONNECT - RESEND!!!" << endl;
        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
    }
    m_combus_connected = a_connected;
}


}}
