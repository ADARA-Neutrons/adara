#ifndef COMBUSROUTER_H
#define COMBUSROUTER_H

#include <string>
#include "ComBus.h"
#include "StreamMonitor.h"
#include "StreamAnalyzer.h"

namespace ADARA {
namespace DASMON {
/**
 * The ComBusRouter class is repesponsible for emitting asserted rules from the rule engine (stream analyzer)
 * onto the SIGNAL topic of the ComBus.
 *
 * Change how signals are processed: All processes that emit signals must re-emit asserted signals every
 * 5 seconds. When a signal is retracted, a retraction message will be sent, and the signal will no longer be
 * re-emitted. This allows clients to utilize a hybrid stateful-statless approach such that signals can be refreshed
 * either from the re-emitted messages, or based on asserts and retracts. If retracts are used, a fail-safe timeout
 * must be employed to prevent stuck signals if the retract message is missed (due to process or broker crash).
 */
class ComBusRouter : public IStreamListener, public ADARA::ComBus::IStatusListener,
        public ADARA::ComBus::IControlListener, public StreamAnalyzer::ISignalListener
{
public:
    ComBusRouter( StreamMonitor &a_monitor, StreamAnalyzer &a_analyzer );
    virtual ~ComBusRouter();

    void    run();

private:
    void    sendRuleDefinitions( const std::string &a_src_proc, const std::string &a_CID );
    void    setRuleDefinitions( const ADARA::ComBus::ControlMessage *a_msg );

    // IStreamListener Interface
    void    runStatus( bool a_recording, unsigned long a_run_number );
    void    pauseStatus( bool a_paused );
    void    scanStatus( bool a_scanning, unsigned long a_scan_number );
    void    beamInfo( const BeamInfo &a_info );
    void    runInfo( const RunInfo &a_info );
    void    beamMetrics( const BeamMetrics &a_metrics );
    void    runMetrics( const RunMetrics &a_metrics );
    void    pvDefined( const std::string &a_name );
    void    pvValue( const std::string &a_name, uint32_t a_value );
    void    pvValue( const std::string &a_name, double a_value );
    void    connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

    // IStatusListener Interface
    void    comBusConnectionStatus( bool a_connected );

    // IControlListener Interface
    bool    comBusControlMessage( const ADARA::ComBus::ControlMessage &a_cmd );

    // ISignalListener Interface
    //void    signalAssert( const std::string &a_name, const std::string &a_source, ADARA::Level a_level, const std::string &a_msg );
    void    signalAssert( const SignalInfo &a_signal );
    void    signalRetract( const std::string &a_name );

    StreamMonitor                  &m_monitor;
    StreamAnalyzer                 &m_analyzer;
    ComBus::Connection             &m_combus;
    bool                            m_resend_state;
    bool                            m_sms_connected;
    bool                            m_combus_connected;
};

}}

#endif // AMQPRULEROUTER_H
