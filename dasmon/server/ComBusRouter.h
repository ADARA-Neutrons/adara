#ifndef COMBUSROUTER_H
#define COMBUSROUTER_H

#include <string>
#include "ComBus.h"
#include "StreamMonitor.h"
#include "RuleEngine.h"

namespace ADARA {
namespace DASMON {

class ComBusRouter : public IStreamListener, public RuleEngine::IRuleListener, public ADARA::ComBus::IStatusListener
{
public:
    ComBusRouter( StreamMonitor &a_monitor, RuleEngine::StreamAnalyzer &a_analyzer );
    virtual ~ComBusRouter();

    void    run();

private:

    // IStreamListener Interface
    void    runStatus( bool a_recording, unsigned long a_run_number );
    void    pauseStatus( bool a_paused );
    void    scanStatus( bool a_scanning, unsigned long a_scan_number );
    void    beamInfo( const BeamInfo &a_info );
    void    runInfo( const RunInfo &a_info );
    void    beamMetrics( const BeamMetrics &a_metrics );
    void    runMetrics( const RunMetrics &a_metrics );
    void    pvDefined( const std::string &a_name );
    void    pvValue( const std::string &a_name, double a_value );
    void    connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

    // IRuleListener Interface
    void    reportEvent( const RuleEngine::Rule &a_rule );
    void    assertFinding( const std::string &a_id, const RuleEngine::Rule &a_rule );
    void    retractFinding( const std::string &a_id );

    // IStatusListener Interface
    void    comBusConnectionStatus( bool a_connected );

    StreamMonitor                  &m_monitor;
    RuleEngine::StreamAnalyzer     &m_analyzer;
    ComBus::Connection             &m_combus;
    bool                            m_resend_state;
    bool                            m_sms_connected;
    bool                            m_combus_connected;
};

}}

#endif // AMQPRULEROUTER_H
