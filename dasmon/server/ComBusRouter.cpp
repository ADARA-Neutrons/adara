#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "ComBusRouter.h"
#include "ComBusMessages.h"
#include "DASMonMessages.h"

using namespace std;

namespace ADARA {
namespace DASMON {

#define COMBUS_STATUS_PERIOD        5   ///< Period of ComBus status messages in seconds
#define PROC_TIMEOUT_UNRESPONSIVE   10  ///< Time (sec) before a ComBus proc is declared unresponsive
#define PROC_TIMEOUT_INACTIVE       20  ///< Time (sec) before a ComBus proc is declared inactive

/*
The ComBusRouter class provides a number of ComBus (ActiveMQ) messaging
functions for the dasmond service, as follows:

1)  The ComBusRouter class is a listener and translator for both the
    StreamMonitor and the StreamAnalyzer. Various notifications and
    signals and received and broadcast onto the dasmond app topic
    [domain].APP.DASMON.0 (see DASMonMessages.h for details). These
    messages deal primarily with ADARA stream status and metrics as
    well as business rule signalling.

2)  The ComBusRouter class serves as the entry-point for dasmond
    command and control messages. The messages are received on the
    [domain].INPUT.DASMON.0 topic and are typically peer-to-peer in
    nature (i.e. a reply will usually be sent to the sender). Received
    messages are translated into method calls to the StreamMonitor
    and/or StreamAnalyzer.

3)  The ComBusRouter class provides critical process monitoring via
    the ComBus [domain].STATUS.* topics. If required and important
    processes fail to provide timely status, apppropriate ComBus signals
    are generated for consumption by user-facing applications (i.e. GUI
    or logging apps). A process can be in three error states: 1) "un-
    responsive" - meaning no status has been received for some defined
    period, and 2) "faulted" - meaning status was received indicating
    some manner of fault, and 3) "inactive" - meaning the process has
    been non-responsive for a prolonged period of time and is presumed
    to be not running. Process status monitoring is implemented in the
    run() method.

4)  The ComBusRouter class provides internal health monitoring and
    status notification for critical dasmond threads. This feature is
    related to #3 above, except that the source if the status
    information is internal. This feature is also implemented in the
    run() method.
*/

/** \param a_monitor - The StreamMonitor that will be handled
  * \param a_analyzer - The StreamAnalyser that will be used
  *
  * Constructor for ComBusRouter class. The StreamMonitor and StreamAnalyzer instance
  * must be paired (the analyzer must be attached to the monitor); otherwise unexpected
  * behavior will result. This constructor causes the new ComBusRouter to attach itself
  * to the provided stream instances and initializes the ComBus interface.
  */
ComBusRouter::ComBusRouter( StreamMonitor &a_monitor, StreamAnalyzer &a_analyzer, uint16_t a_metrics_period )
    : m_monitor( a_monitor ), m_analyzer( a_analyzer ), m_combus( ADARA::ComBus::Connection::getInst() ),
      m_resend_state(false), m_sms_connected(false), m_combus_connected(false), m_recording(false),
      m_metrics_period(a_metrics_period), m_beam_metrics_count(0), m_run_metrics_count(0), m_stream_metrics_count(0)
{
    m_monitor.addListener( *this );
    m_analyzer.attach( *this );
    m_combus.attach( *this );
    m_combus.setInputListener( *this );
    m_combus.attach( *this, "STATUS.>" );
}


/** ComBusRouter destructor.
  */
ComBusRouter::~ComBusRouter()
{
    m_combus.detach( (ADARA::ComBus::IConnectionListener &)*this );
    m_combus.detach( (ADARA::ComBus::ITopicListener &)*this );
    m_analyzer.detach( *this );
}


/** This method provides a number of dasmon/ComBus oversight features including
  * sending ComBus status messages, monitoring other ComBus processing for
  * activity, and re-emitting current dasmon state when requested by an external
  * ComBus process. This method never returns and is the last method called by the
  * main thread of the dasmon process (dasmon does not provide an orderly shutdown
  * mechanism).
  */
void
ComBusRouter::run()
{
    unsigned short  count = 0;
    uint32_t        t;
    map<string,ProcInfo>::iterator ip;
    uint32_t last_proc_ticker = 0;
    uint32_t proc_stall = 0;
    uint32_t last_metrics_ticker = 0;
    uint32_t metrics_stall = 0;
    uint32_t last_db_ticker = 0;
    uint32_t db_stall = 0;

    // Need Sufficient Time for Worst-Case Scenario
    // With Latest ADARA/SMS Container/PauseMode Stack
    // Timeout/Cleanup Delays...
    uint32_t TIMEOUT = 300;

    while(1)
    {

        sleep(1);

        t = time(0);

        if ( last_proc_ticker == m_monitor.getProcTicker() )
        {
            if ( ++proc_stall == TIMEOUT )
            {
                syslog( LOG_ERR, "StreamMonitor stream processing thread appears to be hung. Thread state = %u, Notify state = %u", m_monitor.getProcState(), m_monitor.getNotifyState() );
                usleep(30000); // give syslog a chance...
            }
        }
        else
        {
            if ( proc_stall >= TIMEOUT )
            {
                syslog( LOG_ERR, "StreamMonitor stream processing thread appears to have recovered." );
                usleep(30000); // give syslog a chance...
            }

            last_proc_ticker = m_monitor.getProcTicker();
            proc_stall = 0;
        }

        if ( last_metrics_ticker == m_monitor.getMetricsTicker() )
        {
            if ( ++metrics_stall == TIMEOUT )
            {
                syslog( LOG_ERR, "StreamMonitor metrics thread appears to be hung. Thread state = %u, Notify state = %u", m_monitor.getMetricsState(), m_monitor.getNotifyState() );
                usleep(30000); // give syslog a chance...
            }
        }
        else
        {
            if ( metrics_stall >= TIMEOUT )
            {
                syslog( LOG_ERR, "StreamMonitor metrics thread appears to have recovered." );
                usleep(30000); // give syslog a chance...
            }

            last_metrics_ticker = m_monitor.getMetricsTicker();
            metrics_stall = 0;
        }

#ifndef NO_DB
        if ( m_monitor.getDbState() > 0 ) // Only monitor DB Thread if it has been started
        {
            if ( last_db_ticker == m_monitor.getDbTicker() )
            {
                if ( ++db_stall == TIMEOUT )
                {
                    syslog( LOG_ERR, "StreamMonitor DB thread appears to be hung. Thread state = %u, Notify state = %u", m_monitor.getDbState(), m_monitor.getNotifyState() );
                    usleep(30000); // give syslog a chance...
                }
            }
            else
            {
                if ( db_stall >= TIMEOUT )
                {
                    syslog( LOG_ERR, "StreamMonitor DB thread appears to have recovered." );
                    usleep(30000); // give syslog a chance...
                }

                last_db_ticker = m_monitor.getDbTicker();
                db_stall = 0;
            }
        }
#endif

        // Test/send status every 5 seconds

        if ( !(count % COMBUS_STATUS_PERIOD ))
        {
            // Check to be sure all threads are running (i.e. ticker values should be changing)
            // If a thread is stuck, set fault condition and log failed thread state

            if ( proc_stall < TIMEOUT
                    && metrics_stall < TIMEOUT
                    && db_stall < TIMEOUT )
            {
                if ( !(m_combus.status( ADARA::ComBus::STATUS_OK )) )
                {
                    syslog( LOG_ERR, "%s - %s",
                        "StreamMonitor Error",
                        "Broadcasting DASMON ComBus Status OK Message" );
                }
            }
            else
            {
                syslog( LOG_ERR,
                    "StreamMonitor %s - %s: %s=%u %s=%u %s=%u %s=%u",
                    "SYSTEM STALLED", "STATUS FAULT",
                    "proc_stall", proc_stall,
                    "metrics_stall", metrics_stall,
                    "db_stall", db_stall,
                    "TIMEOUT", TIMEOUT);
                usleep(30000); // give syslog a chance...
                if ( !(m_combus.status( ADARA::ComBus::STATUS_FAULT )) )
                {
                    syslog( LOG_ERR, "%s - %s",
                        "StreamMonitor Error",
                        "Broadcasting DASMON ComBus Status FAULT Message" );
                }
            }
        }

        // Watch for unresponsive / inactive processes
        boost::unique_lock<boost::mutex> lock(m_proc_mutex);

        for ( ip = m_procs.begin(); ip != m_procs.end();  )
        {
            if ( ip->second.status == ADARA::ComBus::STATUS_UNRESPONSIVE && t > ( ip->second.last_updated + PROC_TIMEOUT_INACTIVE ))
            {
                syslog( LOG_INFO, "Process %s has become INACTIVE.", ip->first.c_str() );
                usleep(30000); // give syslog a chance...

                ip->second.status = ADARA::ComBus::STATUS_INACTIVE;
                m_analyzer.retractFact( string("PROC_") + ip->first );

                m_procs.erase( ip++ );
            }
            else
            {
                if (( ip->second.status == ADARA::ComBus::STATUS_OK || ip->second.status == ADARA::ComBus::STATUS_FAULT ) && t > ( ip->second.last_updated + PROC_TIMEOUT_UNRESPONSIVE ))
                {
                    syslog( LOG_INFO, "Process %s has become UNRESPONSIVE.", ip->first.c_str() );
                    usleep(30000); // give syslog a chance...

                    ip->second.status = ADARA::ComBus::STATUS_UNRESPONSIVE;
                    m_analyzer.assertFact( string("PROC_") + ip->first, (int)ip->second.status );
                }

                ++ip;
            }
        }

        lock.unlock();

        // See if a process has requested a state update
        if ( m_resend_state )
        {
            m_monitor.resendState( *this );
            m_analyzer.resendState();
            m_resend_state = false;
        }

        ++count;
    }
}


/** \param a_src_proc - The client ComBus process
  * \param a_CID - The ComBus context ID for the request
  *
  * This method sends the current rule and signal definitions to the specified
  * client process.
  */
void
ComBusRouter::sendRuleDefinitions( const string &a_src_proc, const string &a_CID )
{
    ADARA::ComBus::DASMON::RuleDefinitions defs;

    m_analyzer.getDefinitions( defs.m_rules, defs.m_signals );
    m_combus.send( defs, a_src_proc, &a_CID );
}


/** \param a_msg - A message containing new rule and signal definitions
  *
  * This method sets the current rule and signal definitions. If the
  * StreamAnalyzer successfully sets the rules and signals, the new settings
  * are saved to the current config file, and if requested, set as the defaults
  * as well. The new (or unchanged) rules and signals are sent back to the
  * originating process as an acknowledgement (or nack).
  */
void
ComBusRouter::setRuleDefinitions( const ADARA::ComBus::MessageBase *a_msg )
{
    const ADARA::ComBus::DASMON::SetRuleDefinitions *set_msg =
            dynamic_cast<const ADARA::ComBus::DASMON::SetRuleDefinitions*>( a_msg );
    if ( set_msg )
    {
        // If this succeeds, current rules will be set to specified;
        // otherwise, current rules will remain unchanged

        ADARA::ComBus::DASMON::RuleErrors err_msg;

        if ( m_analyzer.setDefinitions( set_msg->m_rules, set_msg->m_signals, err_msg.m_errors ))
        {
            m_analyzer.saveConfig();

            if ( set_msg->m_set_default )
                m_analyzer.setDefaultConfig();

            ADARA::ComBus::AckReply ack;
            m_combus.send( ack, a_msg->getSourceID(), &a_msg->getCorrelationID());
        }
        else
        {
            m_combus.send( err_msg, a_msg->getSourceID(), &a_msg->getCorrelationID());
        }
    }
}


/**
  *
  */
void
ComBusRouter::sendInputFacts( const std::string &a_src_proc, const std::string &a_CID )
{
    ADARA::ComBus::DASMON::InputFacts facts;

    m_analyzer.getInputFacts( facts.m_facts );
    m_combus.send( facts, a_src_proc, &a_CID );
}


/**
  *
  */
void
ComBusRouter::sendPVs( const std::string &a_src_proc, const std::string &a_CID )
{
    ADARA::ComBus::DASMON::ProcessVariables pvs;

    boost::unique_lock<boost::mutex> lock(m_mutex);
    pvs.m_pvs = m_pvs;
    lock.unlock();

    m_combus.send( pvs, a_src_proc, &a_CID );
}


// IStreamListener Interface --------------------------------------------------


/** \brief Callback for run status updates
  * \param a_recording - When true, indicates system is recording
  * \param a_run_number - Run number of recording (0 when no recording)
  * \param a_timestamp - Timestamp of update (EPICS epoch)
  * \param a_timestamp_nanosec - Timestamp Nanosecs of update (EPICS epoch)
  *
  * This method is called by the StreamMonitor instance whenever the system
  * starts or stops recording a run. The received information is broadcast
  * in a RunStatusMessage on the APP.DASMON topic.
  */
void
ComBusRouter::runStatus( bool a_recording, uint32_t a_run_number,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    // Only respond if recording state has changed
    //if ( a_recording != m_recording )
    {
        boost::unique_lock<boost::mutex> lock(m_mutex);
        m_pvs.clear();
        lock.unlock();

        ComBus::DASMON::RunStatusMessage msg( a_recording, a_run_number,
            a_timestamp, a_timestamp_nanosec );
        m_combus.broadcast( msg );

        m_recording = a_recording;
    }
}


/** \brief Callback for pause status updates
  * \param a_paused - When true, indicates system is paused
  *
  * This method is called by the StreamMonitor instance whenever the system
  * is paused or resumes. The received information is broadcast in a
  * PauseStatusMessage on the APP.DASMON topic.
  */
void
ComBusRouter::pauseStatus( bool a_paused )
{
    ComBus::DASMON::PauseStatusMessage msg( a_paused );
    m_combus.broadcast( msg );
}


/** \brief Callback for scan status updates
  * \param a_scanning - When true, indicates scanning is in progress
  * \param a_scan_index - Scan index (when scanning)
  *
  * This method is called by the StreamMonitor instance whenever the system
  * starts or stops a scan. The received information is broadcast in a
  * ScanStatusMessage on the APP.DASMON topic.
  */
void
ComBusRouter::scanStatus( bool a_scanning, uint32_t a_scan_number )
{
    ComBus::DASMON::ScanStatusMessage msg( a_scanning, a_scan_number );
    m_combus.broadcast( msg );
}


/** \brief Callback for updated beam info.
  * \param a_info - Updated beam information
  *
  * This method is called on run starts and stops by the StreamMonitor instance
  * to update stream listeners with beam information. The received information
  * is broadcast in a BeamInfoMessage on the APP.DASMON topic.
  */
void
ComBusRouter::beamInfo( const BeamInfo &a_info )
{
    ComBus::DASMON::BeamInfoMessage msg( a_info );
    m_combus.broadcast( msg );
}


/** \brief Callback for updated run info.
  * \param a_info - Updated run information
  *
  * This method is called on run starts and stops by the StreamMonitor instance
  * to update stream listeners with run information. The received information
  * is broadcast in a RunInfoMessage on the APP.DASMON topic.
  */
void
ComBusRouter::runInfo( const RunInfo &a_info )
{
    ComBus::DASMON::RunInfoMessage msg( a_info );
    m_combus.broadcast( msg );
}


/** \brief Callback for updated beam metrics.
  * \param a_metrics - Updated beam metrics
  *
  * This method is called periodically by the StreamMonitor instance to update
  * stream listeners with various beam metrics. The received information
  * is broadcast in a BeamMetricsMessage on the APP.DASMON topic.
  */
void
ComBusRouter::beamMetrics( const BeamMetrics &a_metrics )
{
    if ( ++m_beam_metrics_count >= m_metrics_period )
    {
        m_beam_metrics_count = 0;

        ComBus::DASMON::BeamMetricsMessage msg( a_metrics );
        m_combus.broadcast( msg );
    }
}


/** \brief Callback for updated run metrics.
  * \param a_metrics - Updated run metrics
  *
  * This method is called periodically by the StreamMonitor instance to update
  * stream listeners with various run metrics. The received information
  * is broadcast in a RunMetricsMessage on the APP.DASMON topic.
  */
void
ComBusRouter::runMetrics( const RunMetrics &a_metrics )
{
    if ( ++m_run_metrics_count >= m_metrics_period )
    {
        m_run_metrics_count = 0;

        ComBus::DASMON::RunMetricsMessage msg( a_metrics );
        m_combus.broadcast( msg );
    }
}


void
ComBusRouter::streamMetrics( const StreamMetrics &a_metrics )
{
    if ( ++m_stream_metrics_count >= m_metrics_period )
    {
        m_stream_metrics_count = 0;

        ComBus::DASMON::StreamMetricsMessage msg( a_metrics );
        m_combus.broadcast( msg );
    }
}


/** \param a_name - Name of process variable
  *
  * This method is a callback from the StreamMonitor to indicate that a process
  * variable has been defined. The ComBusRouter does not use this callback.
  */
void
ComBusRouter::pvDefined( const std::string &a_name )
{
    (void)a_name;
}


/** \param a_name - Name of process variable
  *
  * This method is a callback from the StreamMonitor to indicate that a process
  * variable has been undefined. The ComBusRouter removes the PV from the cached
  * PV informations for subsequent client requests.
  */
void
ComBusRouter::pvUndefined( const std::string &a_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    map<string,ComBus::DASMON::ProcessVariables::PVData>::iterator ipv = m_pvs.find(a_name);
    if ( ipv != m_pvs.end())
        m_pvs.erase(ipv);
}


/** \param a_name - Name of process variable
  * \param a_value - New value of process variable
  * \param a_status - New status of process variable
  * \param a_timestamp - Timestamp of change
  * \param a_timestamp_nanosec - Timestamp Nanosecs of change
  *
  * This method is a callback from the StreamMonitor to indicate changes
  * in the value or status of an unsigned integer process variable in the
  * data stream.
  * The ComBusRouter caches this information for subsequent client requests.
  */
void
ComBusRouter::pvValue( const std::string &a_name,
        uint32_t a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_pvs[a_name] = ComBus::DASMON::ProcessVariables::PVData(
        a_value, a_status, a_timestamp, a_timestamp_nanosec );
}


/** \param a_name - Name of process variable
  * \param a_value - New value of process variable
  * \param a_status - New status of process variable
  * \param a_timestamp - Timestamp of change
  * \param a_timestamp_nanosec - Timestamp Nanosecs of change
  *
  * This method is a callback from the StreamMonitor to indicate changes
  * in the value or status of a double precision process variable in the
  * data stream.
  * The ComBusRouter caches this information for subsequent client requests.
  */
void
ComBusRouter::pvValue( const std::string &a_name,
        double a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_pvs[a_name] = ComBus::DASMON::ProcessVariables::PVData(
        a_value, a_status, a_timestamp, a_timestamp_nanosec );
}


/** \param a_name - Name of process variable
  * \param a_value - New value of process variable
  * \param a_status - New status of process variable
  * \param a_timestamp - Timestamp of change
  * \param a_timestamp_nanosec - Timestamp Nanosecs of change
  *
  * This method is a callback from the StreamMonitor to indicate changes
  * in the value or status of a string process variable in the data stream.
  * The ComBusRouter caches this information for subsequent client requests.
  */
void
ComBusRouter::pvValue( const std::string &a_name,
        string &a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_pvs[a_name] = ComBus::DASMON::ProcessVariables::PVData(
        a_value, a_status, a_timestamp, a_timestamp_nanosec );
}


/** \param a_name - Name of process variable
  * \param a_value - New value of process variable
  * \param a_status - New status of process variable
  * \param a_timestamp - Timestamp of change
  * \param a_timestamp_nanosec - Timestamp Nanosecs of change
  *
  * This method is a callback from the StreamMonitor to indicate changes
  * in the value or status of an unsigned integer process variable in the
  * data stream.
  * The ComBusRouter caches this information for subsequent client requests.
  */
void
ComBusRouter::pvValue( const std::string &a_name,
        vector<uint32_t> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_pvs[a_name] = ComBus::DASMON::ProcessVariables::PVData(
        a_value, a_status, a_timestamp, a_timestamp_nanosec );
}


/** \param a_name - Name of process variable
  * \param a_value - New value of process variable
  * \param a_status - New status of process variable
  * \param a_timestamp - Timestamp of change
  * \param a_timestamp_nanosec - Timestamp Nanosecs of change
  *
  * This method is a callback from the StreamMonitor to indicate changes
  * in the value or status of a double precision process variable in the
  * data stream.
  * The ComBusRouter caches this information for subsequent client requests.
  */
void
ComBusRouter::pvValue( const std::string &a_name,
        vector<double> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_pvs[a_name] = ComBus::DASMON::ProcessVariables::PVData(
        a_value, a_status, a_timestamp, a_timestamp_nanosec );
}


/** \param a_connected - Flag indicating SMS connection state
  * \param a_host - Host name of SMS
  * \param a_port - Port number of SMS
  *
  * This method is a callback from the StreamMonitor to indicate changes in
  * the SMS connection state. Dasmon must resend its state when the
  * StreamMonitor connects (or reconnects) to the SMS.
  */
void
ComBusRouter::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    ComBus::DASMON::ConnectionStatusMessage msg( a_connected, a_host, a_port );
    m_combus.broadcast( msg );

    if ( a_connected && !m_sms_connected )
    {
        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
    }

    m_sms_connected = a_connected;
}


// ISignalListener Interface --------------------------------------------------


/** \param a_signal - Information structure for signal that was asserted
  *
  * This method is a callback from the StreamAnalyzer indicating that the
  * specified signal was asserted. The ComBusRouter broadcasts this
  * information on the [domain].APP.DASMON topic.
  */
void
ComBusRouter::signalAssert( const SignalInfo &a_signal )
{
    ADARA::ComBus::SignalAssertMessage msg( a_signal.name, a_signal.source, a_signal.msg, a_signal.level );
    m_combus.broadcast( msg );
}


/** \param a_name - Name of signal that was retracted
  *
  * This method is a callback from the StreamAnalyzer indicating that the
  * specified signal was retracted. The ComBusRouter broadcasts this
  * information on the [domain].APP.DASMON topic.
  */
void
ComBusRouter::signalRetract( const std::string &a_name )
{
    ADARA::ComBus::SignalRetractMessage msg( a_name );
    m_combus.broadcast( msg );
}


// IStatusListener Interface --------------------------------------------------

/** \param a_connected - Flag indicating ComBus connection state
  *
  * This method is a callback from the ComBus library to indicate changes in
  * the connection state. Dasmon must resend its state when it connects (or
  * reconnects) to ComBus.
  */
void
ComBusRouter::comBusConnectionStatus( bool a_connected )
{
    if ( a_connected && !m_combus_connected )
    {
        // On reconnect, resend all asserted signals in case some fired while disconnected
        m_resend_state = true;
        syslog( LOG_NOTICE, "ComBus connection active." );
        usleep(30000); // give syslog a chance...
    }
    else if ( !a_connected && m_combus_connected )
    {
        syslog( LOG_ERR, "ComBus connection lost." );
        usleep(30000); // give syslog a chance...
    }

    m_combus_connected = a_connected;
}


// ITopicListener methods -----------------------------------------------------

/** /param a_msg - Received ComBus message
  *
  * The comBusMessage method is the callback for all ComBus messages received
  * on subscribed topics. The ComBusRouter class monitors all traffic on the
  * [domain].STATUS.* topic in order to maintain state information for all running
  * ComBus processess. This state information is injected into the StreamAnalyzer's
  * rule engine which allows users to configure rules and signals pretaining to
  * the status of important processes (using the ComBus process ID as a fact in
  * a rule expression).
  */
void
ComBusRouter::comBusMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    // Only interested in Status messages

    if ( a_msg.getMessageType() == ADARA::ComBus::MSG_STATUS )
    {
        ADARA::ComBus::StatusCode status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;

        boost::lock_guard<boost::mutex> lock(m_proc_mutex);

        // Update time is recorded such that the run() method can detected unresponsive
        // and inactive processes and update status accordingly.

        map<string,ProcInfo>::iterator ip = m_procs.find( a_msg.getSourceID() );
        if ( ip != m_procs.end() )
        {
            ip->second.status = status;
            ip->second.last_updated = time(0);
        }
        else
        {
            m_procs[a_msg.getSourceID()] = ProcInfo( status, status, false, time(0) );
        }

        // Inject process and state information into rule engine

        m_analyzer.assertFact( string("PROC_") + a_msg.getSourceID(), (int)status );
    }
}


// IInputListener methods -----------------------------------------------------


/** \param a_msg - A received ComBus message
  *
  * This method is called by the ComBus library when a message is received on
  * the [domain].INPUT.DASMON topic. This topic represents the public API of
  * the dasmon service and supports rule management, state queries, and process
  * variable queries. The state and PV queries are to support late-joining
  * ComBus processes.
  */
void
ComBusRouter::comBusInputMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    try
    {
        switch( a_msg.getMessageType() )
        {
        case ADARA::ComBus::MSG_EMIT_STATE:
            m_resend_state = true;
            break;

        case ADARA::ComBus::MSG_DASMON_GET_RULES:
            sendRuleDefinitions( a_msg.getSourceID(), a_msg.getCorrelationID() );
            break;

        case ADARA::ComBus::MSG_DASMON_SET_RULES:
            syslog( LOG_INFO, "Received request to set rules" );
            usleep(30000); // give syslog a chance...
            setRuleDefinitions( &a_msg );
            break;

        case ADARA::ComBus::MSG_DASMON_RESTORE_DEFAULT_RULES:
            syslog( LOG_INFO, "Received request to restore default rules" );
            usleep(30000); // give syslog a chance...
            m_analyzer.restoreDefaultConfig();
            sendRuleDefinitions( a_msg.getSourceID(), a_msg.getCorrelationID() );
            break;

        case ADARA::ComBus::MSG_DASMON_GET_INPUT_FACTS:
            sendInputFacts( a_msg.getSourceID(), a_msg.getCorrelationID() );
            break;

        case ADARA::ComBus::MSG_DASMON_GET_PVS:
            sendPVs( a_msg.getSourceID(), a_msg.getCorrelationID() );
            break;

        default:
            break;
        }
    }
    catch ( exception &e )
    {
        syslog( LOG_ERR, "Exception while processing command: %s", e.what() );
        usleep(30000); // give syslog a chance...
    }
    catch ( ... )
    {
        syslog( LOG_ERR, "Unkown exception while processing command" );
        usleep(30000); // give syslog a chance...
    }
}

}}

// vim: expandtab

