#ifndef COMBUSROUTER_H
#define COMBUSROUTER_H

#include <string>
#include "ComBus.h"
#include "DASMonMessages.h"
#include "StreamMonitor.h"
#include "StreamAnalyzer.h"
//#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

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
class ComBusRouter : public IStreamListener, public ADARA::ComBus::IConnectionListener, public ADARA::ComBus::IInputListener,
        public StreamAnalyzer::ISignalListener, public ADARA::ComBus::ITopicListener
{
public:
    ComBusRouter( StreamMonitor &a_monitor, StreamAnalyzer &a_analyzer );
    virtual ~ComBusRouter();

    void    run();

private:
    struct ProcInfo
    {
        ProcInfo()
            : status(ADARA::ComBus::STATUS_INACTIVE), prev_status(ADARA::ComBus::STATUS_INACTIVE), required(false), last_updated(0)
        {}

        ProcInfo( ADARA::ComBus::StatusCode a_status, ADARA::ComBus::StatusCode a_prev_status, bool a_required, uint32_t a_time )
            : status(a_status), prev_status(a_prev_status), required(a_required), last_updated(a_time)
        {}

        ADARA::ComBus::StatusCode   status;
        ADARA::ComBus::StatusCode   prev_status;
        bool                        required;
        uint32_t                    last_updated;
    };

    void    sendRuleDefinitions( const std::string &a_src_proc, const std::string &a_CID );
    void    setRuleDefinitions( const ADARA::ComBus::MessageBase *a_msg );
    void    sendInputFacts( const std::string &a_src_proc, const std::string &a_CID );
    void    sendPVs( const std::string &a_src_proc, const std::string &a_CID );

    // IStreamListener Interface
    void    runStatus( bool a_recording, uint32_t a_run_number, uint32_t a_timestamp );
    void    pauseStatus( bool a_paused );
    void    scanStatus( bool a_scanning, uint32_t a_scan_number );
    void    beamInfo( const BeamInfo &a_info );
    void    runInfo( const RunInfo &a_info );
    void    beamMetrics( const BeamMetrics &a_metrics );
    void    runMetrics( const RunMetrics &a_metrics );
    void    streamMetrics( const StreamMetrics &a_metrics );
    void    pvDefined( const std::string &a_name );
    void    pvUndefined( const std::string &a_name );
    void    pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status, uint32_t a_timestamp );
    void    pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp );
    void    connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port );

    // IStatusListener Interface
    void    comBusConnectionStatus( bool a_connected );

    // IInputListener Interface
    void    comBusInputMessage( const ADARA::ComBus::MessageBase &a_cmd );

    // ITopicListener Interface
    void    comBusMessage( const ADARA::ComBus::MessageBase &a_msg );

    // ISignalListener Interface
    void    signalAssert( const SignalInfo &a_signal );
    void    signalRetract( const std::string &a_name );

    StreamMonitor                  &m_monitor;
    StreamAnalyzer                 &m_analyzer;
    ComBus::Connection             &m_combus;
    bool                            m_resend_state;
    bool                            m_sms_connected;
    bool                            m_combus_connected;
    bool                            m_recording;
    std::map<std::string,ComBus::DASMON::ProcessVariables::PVData> m_pvs;
    mutable boost::mutex            m_mutex;
    std::map<std::string,ProcInfo>  m_procs;
    mutable boost::mutex            m_proc_mutex;
};

}}

#endif // AMQPRULEROUTER_H
