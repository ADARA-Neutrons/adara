#include "StreamMonitor.h"
#include <iostream>
#include <sstream>
#include <string.h>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include <syslog.h>
#include <unistd.h>


// Only count pulses with pixel errors - not individual pixel errors
#define PIX_ERR_BY_PULSE

#ifndef NO_DB
#include "libpq-fe.h"
#endif

using namespace std;

namespace ADARA {
namespace DASMON {

#define ADARA_IN_BUF_SIZE 0x100000



bool PixPairComp( const pair<uint32_t,uint16_t> a, const pair<uint32_t,uint16_t> b )
{
    return a.first < b.first;
}

uint32_t StreamMonitor::m_proc_state = TS_INIT;
uint32_t StreamMonitor::m_notify_state = TS_INIT;

/**
 * \brief StreamMonitor constructor.
 * \param a_sms_host - Hostname of SMS stream source
 * \param a_port - Port number of SMS stream source
 *
 * The StreamMonitor constructor performs limited initialization. Full initialization is not
 * performed until the start() method is called.
 */
#ifndef NO_DB
StreamMonitor::StreamMonitor(
        const std::string &a_sms_host, unsigned short a_port,
        DBConnectInfo *a_db_info, uint32_t a_maxtof )
#else
StreamMonitor::StreamMonitor(
        const std::string &a_sms_host, unsigned short a_port )
#endif
    : POSIXParser(), m_fd_in(-1),
      m_sms_host(a_sms_host), m_sms_port(a_port),
      m_stream_thread(0), m_metrics_thread(0), m_process_stream(true),
      m_mon_event_count(0), m_recording(false), m_run_num(0),
      m_run_timestamp(0), m_run_timestamp_nanosec(0),
      m_paused(false), m_scanning(false), m_scan_index(0),
      m_first_pulse_time(0), m_last_pulse_time(0),
      m_stream_size(0), m_stream_rate(0), m_diagnostics(true),
      m_last_cycle(0), m_last_time(0), m_this_time(0),
      m_bnk_pkt_count(0), m_bnk_state_pkt_count(0), m_mon_pkt_count(0),
      m_maxtof(a_maxtof), m_pixbankmap_processed(false),
      m_proc_ticker(0), m_metrics_ticker(0), m_metrics_state(TS_INIT),
      m_in_prolog(false)
#ifndef NO_DB
    , m_db_info(a_db_info), m_db_ticker(0), m_db_state(TS_INIT)
#endif
{
    m_maxtof *= 10; // Convert from usec to 100 nsec units

#ifndef NO_DB
    if ( m_db_info )
    {
        m_db_thread = new boost::thread(
            boost::bind( &StreamMonitor::dbThread, this ) );
    }
#endif
}


/**
 * \brief StreamMonitor destructor.
 */
StreamMonitor::~StreamMonitor()
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);
    stopProcessing();
}


/**
 * \brief Begins stream processing.
 *
 * This method calls the startProcessing() method which performs full initialization and
 * provides connection management.
 */
void
StreamMonitor::start()
{
    syslog( LOG_INFO, "Start processing request." );
    usleep(30000); // give syslog a chance...

    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    startProcessing();
}


/**
 * \brief Halts stream processing.
 */
void
StreamMonitor::stop()
{
    syslog( LOG_INFO, "Stop processing request." );
    usleep(30000); // give syslog a chance...

    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    stopProcessing();
}


/**
 * \brief Retrieves SMS host information.
 * \param a_hostname - (output) Hostname of SMS stream source
 * \param a_port - (output) Port number of SMS stream source
 */
void
StreamMonitor::getSMSHostInfo( std::string &a_hostname, unsigned short &a_port ) const
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    a_hostname = m_sms_host;
    a_port = m_sms_port;
}


/**
 * \brief Sets SMS host information.
 * \param a_hostname - (input) Hostname of SMS stream source
 * \param a_port - (input) Port number of SMS stream source
 *
 * If stream processing is on-going, it is halted before the new connection information is
 * set, then restarted automatically.
 */
void
StreamMonitor::setSMSHostInfo( std::string a_hostname, unsigned short a_port )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    bool proc = (m_stream_thread != 0);

    if ( proc )
        stopProcessing();

    m_sms_host = a_hostname;
    m_sms_port = a_port;

    if ( proc )
        startProcessing();
}


/**
 * \brief Requests current stream state and metrics to be re-emitted.
 * \param a_listener - (input) IStreamListener instance to receieve state data.
 *
 * This method provides a mechanism to synchronize late-joining stream listeners that may have
 * missed earlier state or stream metrics data.
 */
void
StreamMonitor::resendState( IStreamListener &a_listener ) const
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_fd_in > -1 )
    {
        a_listener.connectionStatus( true, m_sms_host, m_sms_port );
        a_listener.runStatus( m_recording, m_run_num,
            m_run_timestamp, m_run_timestamp_nanosec );
        a_listener.pauseStatus( m_paused );
        a_listener.scanStatus( m_scanning, m_scan_index );

        if ( m_info_rcv == 3 )
        {
            a_listener.beamInfo( m_beam_info );
            a_listener.runInfo( m_run_info );
        }
    }
    else
    {
        a_listener.connectionStatus( false, m_sms_host, m_sms_port );
    }
}


/**
 * \brief Starts stream processing.
 *
 * This method performs additional initialization required to start stream processing (establishing
 * connection and monitoring threads).
 */
void
StreamMonitor::startProcessing()
{
    // Note - not gaurded b/c called only from gaurded internal methods
    if ( !m_stream_thread )
    {
        m_process_stream = true;
        resetRunStats();
        resetStreamStats();
        m_stream_thread = new boost::thread( boost::bind( &StreamMonitor::processThread, this ));
        m_metrics_thread = new boost::thread( boost::bind( &StreamMonitor::metricsThread, this ));
    }
}


/**
 * \brief Stops stream processing.
 *
 * This method halts start stream processing and disconnects from the data source.
 */
void
StreamMonitor::stopProcessing()
{
    // Note - not gaurded b/c called only from gaurded internal methods
    if ( m_stream_thread )
    {
        m_process_stream = false;
        m_stream_thread->join();
        m_metrics_thread->join();
        m_stream_thread = 0;
        m_metrics_thread = 0;

        if ( m_fd_in >= 0 )
            handleLostConnection();
    }
}


/**
 * \brief Stream processing thread.
 *
 * This method is the stream input, processing, and connection management thread. Incoming ADARA
 * packets are distributed to handlers via the recv() method().
 */
void
StreamMonitor::processThread()
{
    m_proc_state = TS_ENTER;
    m_proc_ticker = 0;

    std::string log_info;

    syslog( LOG_INFO, "Stream monitor process thread started." );
    usleep(30000); // give syslog a chance...

    m_notify.connectionStatus( false, m_sms_host, m_sms_port );
    m_proc_state = TS_RUNNING;

    while ( m_process_stream )
    {
        ++m_proc_ticker;

        try
        {
            while( m_process_stream )
            {
                if ( m_fd_in < 0 )
                {
                    m_proc_state = TS_CONNECTING;

                    // Not connected - attempt connection periodically
                    m_fd_in = connect();
                    if ( m_fd_in > -1 )
                    {
                        m_notify.connectionStatus( true, m_sms_host, m_sms_port );
                        m_proc_state = TS_RUNNING;
                    }
                    else
                        sleep(5);
                }
                // NOTE: This is POSIXParser::read()... ;-o
                else if ( !read( m_fd_in, log_info, 0, ADARA_IN_BUF_SIZE ))
                {
                    syslog( LOG_WARNING, "ADARA::POSIXParser::read() returned 0 (%s). Dropping connection.", log_info.c_str() );
                    usleep(30000); // give syslog a chance...
                    // Connection lost due to source closing socket
                    handleLostConnection();
                }
            }
        }
        catch( std::exception &e )
        {
            m_proc_state = TS_EXCEPTION;

            // TODO Really need to notify someone that something BAD has happened!
            syslog( LOG_WARNING, "In processThread(): std::exception caught. Dropping connection. Exception = %s", e.what() );
            usleep(30000); // give syslog a chance...
            // Connection lost
            handleLostConnection();
            // This is probably a misbehaving data source, wait a bit before retrying
            // (This will rate limit syslog spamming somewhat)
            for ( int i = 0; i < 10; ++i )
            {
                sleep(1);
                ++m_proc_ticker;
            }
        }
        catch(...)
        {
            m_proc_state = TS_EXCEPTION;

            // TODO Really need to notify someone that something BAD has happened!
            syslog( LOG_WARNING, "In processThread(): Unknown exception type caught. Dropping connection." );
            usleep(30000); // give syslog a chance...
            // Connection lost
            handleLostConnection();
            // This is probably a misbehaving data source, wait a bit before retrying
            // (This will rate limit syslog spamming somewhat)
            for ( int i = 0; i < 10; ++i )
            {
                sleep(1);
                ++m_proc_ticker;
            }
        }
    }

    syslog( LOG_INFO, "Stream monitor process thread stopping." );
    usleep(30000); // give syslog a chance...
    m_proc_state = TS_EXIT;
}


/**
 * \brief Connect to stream source
 * \return File descriptor on success, -1 on failure
 *
 * This method attempts to connect to the currently configures stream source (sms host/port)
 * and returns a file descriptor for the socket connection if successful (or -1 if a connection
 * could not be established). If a connection is established, an ADARA "client hello" packet
 * is sent prior to returning.
 */
int
StreamMonitor::connect()
{
    // Create socket connection to SMS
    int sms_socket = socket( AF_INET, SOCK_STREAM, 0);

    if ( sms_socket < 0 )
    {
        syslog( LOG_ERR, "Failed to Create SMS Socket!" );
        usleep(30000); // give syslog a chance...
        return -1;
    }

    struct hostent *server = gethostbyname( m_sms_host.c_str() );
    if ( server )
    {
        struct sockaddr_in server_addr;
        bzero( &server_addr, sizeof( server_addr ));

        server_addr.sin_family = AF_INET;
        bcopy( (char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length );
        server_addr.sin_port = htons( m_sms_port );

        if ( ::connect( sms_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) == 0 )
        {
            // Send client hello to begin stream processing
            uint32_t data[6];

            data[0] = 8;
            data[1] = ADARA_PKT_TYPE(
                ADARA::PacketType::CLIENT_HELLO_TYPE,
                ADARA::PacketType::CLIENT_HELLO_VERSION );
            data[2] = time(0) - ADARA::EPICS_EPOCH_OFFSET;
            data[3] = 0;
            data[4] = 0;

            // Version 1 ClientHelloPkt includes Paused/Flags
            // We Want to See It All, Even When Paused (I think/hope... :-)
            // So We Can Keep Updating the Web Monitor... ;-D
            data[5] = ADARA::ClientHelloPkt::SEND_PAUSE_DATA;

            if ( write( sms_socket, data, sizeof(data)) == sizeof( data ))
            {
                syslog( LOG_NOTICE, "Connected to SMS." );
                usleep(30000); // give syslog a chance...
                return sms_socket;
            }
            else
            {
                syslog( LOG_ERR, "Failed to Write Hello to SMS at %s!",
                    m_sms_host.c_str() );
                usleep(30000); // give syslog a chance...
            }
        }
        else
        {
            syslog( LOG_ERR, "Failed to Connect to SMS at %s!",
                m_sms_host.c_str() );
            usleep(30000); // give syslog a chance...
        }
    }
    else
    {
        syslog( LOG_ERR, "Failed to Get Host by Name for %s!",
            m_sms_host.c_str() );
        usleep(30000); // give syslog a chance...
    }

    close( sms_socket );
    return -1;
}


/**
 * \brief Clean-up after source connection is lost.
 */
void
StreamMonitor::handleLostConnection()
{
    m_notify.connectionStatus( false, m_sms_host, m_sms_port );

    close( m_fd_in );
    m_fd_in = -1;

    // Clear data from input ADARA buffer
    reset();

    // Log Pre-Disconnect Run Stats...
    stringstream ssr;
    m_run_metrics.print( ssr );
    syslog( LOG_ERR, "%s: Lost SMS Connection - %s",
        "handleLostConnection()", ssr.str().c_str() );
    usleep(30000); // give syslog a chance...

    // Log Pre-Disconnect Stream Stats...
    stringstream ssm;
    m_stream_metrics.print( ssm );
    syslog( LOG_ERR, "%s: Lost SMS Connection - %s",
        "handleLostConnection()", ssm.str().c_str() );
    usleep(30000); // give syslog a chance...

    resetRunStats();

    resetStreamStats();

    // Lock here as clearPVs does not provide gaurded access
    boost::lock_guard<boost::mutex> lock(m_mutex);
    clearPVs();
}


/**
 * \brief Resets run statistics.
 */
void
StreamMonitor::resetRunStats()
{
    m_info_rcv = 0;
    m_run_info.clear();
    m_run_metrics.clear();
    m_stream_metrics.clear();

    m_first_pulse_time = 0;
    m_pcharge.reset_total( 0.0 );
    m_mon_event_count = 0;
}


/**
 * \brief Resets stream statistics.
 */
void
StreamMonitor::resetStreamStats()
{
    m_bank_count_info.reset();
    m_mon_count_info.clear();
    m_pcharge.reset();
    m_pfreq.reset();
    m_stream_rate = 0;
}


/**
 * \brief Stream metrics update thread.
 *
 * This method provides a thread to periodically gather stream metrics and notify listeners via
 * the beamMetrics() and runMetrics() callbacks. Run metrics are only processed while a run is
 * being recorded.
 */
void
StreamMonitor::metricsThread()
{
    m_metrics_state = TS_ENTER;

    unsigned short  count = 0;
    BeamMetrics     beam_metrics;
    RunMetrics      run_metrics;
    StreamMetrics   stream_metrics;

    syslog( LOG_INFO, "Stream metrics thread started." );
    usleep(30000); // give syslog a chance...

    while( m_process_stream )
    {
        m_metrics_state = TS_SLEEP;
        ++m_metrics_ticker;
        sleep(1);

        // If connected to the SMS, send beam info and beam metrics
        if ( m_fd_in > -1 )
        {
            m_metrics_state = TS_RUNNING;
            boost::unique_lock<boost::mutex> lock(m_mutex);

            // Check low-count rate on banked events
            if ( !m_bnk_pkt_count && !m_bnk_state_pkt_count )
            {
                m_bank_count_info.reset();
                m_pcharge.reset();
                m_pfreq.reset();
            }

            // Check low-count rate on monitor events
            if ( !m_mon_pkt_count )
                m_mon_count_info.clear();

            beam_metrics.m_count_rate = m_bank_count_info.average() * 60.0;
            beam_metrics.m_pulse_charge = m_pcharge.average();
            beam_metrics.m_pulse_freq =  m_pfreq.average();
            beam_metrics.m_stream_bps = m_stream_size; // Size = rate so long as polling is at 1 second

            map<uint32_t,CountInfo<uint64_t> >::iterator im;
            beam_metrics.m_monitor_count_rate.clear();
            for ( im = m_mon_count_info.begin();
                    im != m_mon_count_info.end(); ++im )
            {
                beam_metrics.m_monitor_count_rate[im->first] =
                    im->second.average() * 60;
            }

            // Update total charge
            if ( m_recording )
            {
                m_run_metrics.m_total_charge = m_pcharge.total();
                run_metrics = m_run_metrics;
            }

            stream_metrics = m_stream_metrics;

            m_stream_size = 0;
            m_bnk_pkt_count = 0;
            m_bnk_state_pkt_count = 0;
            m_mon_pkt_count = 0;

            // Release lock and notify listeners
            lock.unlock();

            // Pre-Increment Send/Log Counter
            ++count;

            // Log Beam Metrics Every 8 Seconds
            if ( !(count & 0x7) )
            {
                stringstream ssb;
                beam_metrics.print( ssb );
                syslog( LOG_ERR, "%s: %s",
                    "metricsThread()", ssb.str().c_str() );
                usleep(30000); // give syslog a chance...
            }

            // Send Beam Metrics Every Second
            m_notify.beamMetrics( beam_metrics );

            // If Recording, Send Run Metrics Every Second
            if ( m_recording )
            {
                // Log Run Metrics Every 8 Seconds
                if ( !(count & 0x7) )
                {
                    stringstream ssr;
                    run_metrics.print( ssr );
                    syslog( LOG_ERR, "%s: %s",
                        "metricsThread()", ssr.str().c_str() );
                    usleep(30000); // give syslog a chance...
                }

                m_notify.runMetrics( run_metrics );
            }

            // Send Stream Metrics Every 4 Seconds
            if ( !(count & 0x3) )
            {
                // Log Stream Metrics Every 16 Seconds
                if ( !(count & 0xf) )
                {
                    stringstream ssm;
                    stream_metrics.print( ssm );
                    syslog( LOG_ERR, "%s: %s",
                        "metricsThread()", ssm.str().c_str() );
                    usleep(30000); // give syslog a chance...
                }

                m_notify.streamMetrics( stream_metrics );
            }
        }
    }

    syslog( LOG_INFO, "Stream metrics thread stopping." );
    usleep(30000); // give syslog a chance...

    ++m_metrics_ticker;
    m_metrics_state = TS_EXIT;
}


/**
 * \brief Top-level ADARA packet handler.
 *
 * See ADARAParser for documentation.
 */
bool
StreamMonitor::rxPacket( const ADARA::Packet &a_pkt )
{
    // Check stop flag (set by stop() method)
    if ( !m_process_stream )
        return false;

    ++m_proc_ticker;

    boost::unique_lock<boost::mutex> lock(m_mutex);
    m_stream_size += a_pkt.packet_length();
    m_run_metrics.m_total_bytes_count += a_pkt.packet_length();
    lock.unlock();

    try
    {
        switch (a_pkt.base_type())
        {
            // These packets shall always be processed
            case ADARA::PacketType::RUN_STATUS_TYPE:
            case ADARA::PacketType::PIXEL_MAPPING_TYPE:
            case ADARA::PacketType::PIXEL_MAPPING_ALT_TYPE:
            case ADARA::PacketType::RUN_INFO_TYPE:
            case ADARA::PacketType::BEAMLINE_INFO_TYPE:
            case ADARA::PacketType::DEVICE_DESC_TYPE:
            case ADARA::PacketType::VAR_VALUE_U32_TYPE:
            case ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE:
            case ADARA::PacketType::VAR_VALUE_STRING_TYPE:
            case ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE:
            case ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE:
            case ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE:
            case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE:
            case ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE:
            case ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE:
            case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE:
            case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
            case ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE:
            case ADARA::PacketType::BANKED_EVENT_TYPE:
            case ADARA::PacketType::BANKED_EVENT_STATE_TYPE:
                return Parser::rxPacket(a_pkt);

            // Packet types that are not processes by StreamParser
            case ADARA::PacketType::GEOMETRY_TYPE:
            case ADARA::PacketType::RAW_EVENT_TYPE:
            case ADARA::PacketType::MAPPED_EVENT_TYPE:
            case ADARA::PacketType::RTDL_TYPE:
            case ADARA::PacketType::SOURCE_LIST_TYPE:
            case ADARA::PacketType::TRANS_COMPLETE_TYPE:
            case ADARA::PacketType::CLIENT_HELLO_TYPE:
            case ADARA::PacketType::SYNC_TYPE:
            case ADARA::PacketType::HEARTBEAT_TYPE:
            case ADARA::PacketType::DATA_DONE_TYPE:
            case ADARA::PacketType::BEAM_MONITOR_CONFIG_TYPE:
            case ADARA::PacketType::DETECTOR_BANK_SETS_TYPE:
                break;

            default:
                ++m_stream_metrics.m_invalid_pkt_type;
                break;
        }
    }
    catch(...)
    {
        ++m_stream_metrics.m_invalid_pkt;
        throw;
    }

    return false;
}


/**
 * \brief ADARA run status packet handler
 */
bool
StreamMonitor::rxPacket( const ADARA::RunStatusPkt &a_pkt )
{
    m_proc_state = TS_PKT_RUN_STATUS;

    bool recording = false;
    switch (a_pkt.status())
    {
    case ADARA::RunStatus::RUN_BOF:
        m_in_prolog = true;
        m_notify.beginProlog();
        return false;

    case ADARA::RunStatus::RUN_EOF:
        return false;

    case ADARA::RunStatus::NEW_RUN:
        recording = true;
        break;

    case ADARA::RunStatus::STATE:
        if ( a_pkt.runNumber())
            recording = true;
        break;

    case ADARA::RunStatus::NO_RUN:
    case ADARA::RunStatus::END_RUN:
        break;

    case ADARA::RunStatus::PROLOGUE:
        return false;
    }

    m_in_prolog = true;
    m_notify.beginProlog();

    if ( recording && !m_recording )
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_recording = true;

        m_run_num = a_pkt.runNumber();
        m_run_timestamp = a_pkt.timestamp().tv_sec;
        m_run_timestamp_nanosec = a_pkt.timestamp().tv_nsec;

        m_pixbankmap_processed = false;

        // Subtract Run Status Packet from Total Bytes...
        // - This Packet is (Almost) the "First" One in the Run...
        // (We Can't "Know" to Deduct the Preceding "SYNC" Packet Size
        // (44 bytes) Here... ;-)
        m_run_metrics.m_total_bytes_count -= a_pkt.packet_length();

        // Log Pre-Run Run Stats...
        stringstream ssr;
        m_run_metrics.print( ssr );
        syslog( LOG_ERR, "%s: Pre-Run Stats - %s",
            "rxPacket(RunStatusPkt)", ssr.str().c_str() );
        usleep(30000); // give syslog a chance...

        // Log Pre-Disconnect Stream Stats...
        stringstream ssm;
        m_stream_metrics.print( ssm );
        syslog( LOG_ERR, "%s: Pre-Run Stats - %s",
            "rxPacket(RunStatusPkt)", ssm.str().c_str() );
        usleep(30000); // give syslog a chance...

        resetRunStats();

        // Now Add Run Status Packet Size to New Total Bytes...
        // - This Packet is (Almost) the "First" One in the Run...
        // (We Can't "Know" to Add the Preceding "SYNC" Packet Size
        // (44 bytes) Here... ;-)
        m_run_metrics.m_total_bytes_count += a_pkt.packet_length();

        m_notify.runStatus( true, m_run_num,
            m_run_timestamp, m_run_timestamp_nanosec );

        // Clear all PVs - SMS will send active after RunStatus packet
        clearPVs();
    }
    else if ( !recording && m_recording )
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_recording = false;

        m_run_num = 0;
        m_run_timestamp = a_pkt.timestamp().tv_sec;
        m_run_timestamp_nanosec = a_pkt.timestamp().tv_nsec;

        // Note: Ok to Leave Run Status Packet Size in Run Total Bytes...
        // - This Packet is Indeed the "Last" One in the Run...

        // Log Pre-Run Run Stats...
        stringstream ssr;
        m_run_metrics.print( ssr );
        syslog( LOG_ERR, "%s: Final Run Stats - %s",
            "rxPacket(RunStatusPkt)", ssr.str().c_str() );
        usleep(30000); // give syslog a chance...

        // Log Pre-Disconnect Stream Stats...
        stringstream ssm;
        m_stream_metrics.print( ssm );
        syslog( LOG_ERR, "%s: Final Run Stats - %s",
            "rxPacket(RunStatusPkt)", ssm.str().c_str() );
        usleep(30000); // give syslog a chance...

        resetRunStats();

        m_notify.runStatus( false, 0,
            m_run_timestamp, m_run_timestamp_nanosec );

        // Make sure scan is stopped
        if ( m_scanning )
        {
            m_scanning = false;
            m_scan_index = 0;
            m_notify.scanStatus( false, 0 );
        }

        // Clear all PVs - SMS will send active after RunStatus packet
        clearPVs();
    }

    return false;
}


/**
 * \brief ADARA pixel mapping (alt) packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::PixelMappingAltPkt &a_pkt )
{
    m_proc_state = TS_PKT_PIXEL_MAPPING;

    if ( !m_pixbankmap_processed )
    {
        const uint32_t *rpos = (const uint32_t *) a_pkt.mappingData();
        const uint32_t *epos = (const uint32_t *)
            ( a_pkt.mappingData() + a_pkt.payload_length()
                - sizeof(uint32_t) );
        const uint32_t *rpos_save;
        const uint32_t *epos2;

        int32_t physical_start, physical_stop;
        int32_t physical_step;

        int32_t logical_start, logical_stop;
        int32_t logical_step;

        int32_t logical;

        uint16_t bank_id;
        uint16_t is_shorthand;
        uint16_t pixel_count;

        uint32_t last_pid, pid;
        uint32_t max_pid = 0;
        bool first_pid = true;

        int16_t cnt;

        // Get number of banks (largest bank id) explicitly from packet
        uint16_t bank_count = (uint16_t) a_pkt.numBanks();

        syslog( LOG_INFO, "%s: Max Bank = %u",
            "ADARA::PixelMappingAltPkt", bank_count );
        usleep(30000); // give syslog a chance...

        m_bank_info.clear();

        m_pixbankmap.clear();

        // Now build banks and populate bank container

        uint32_t skip_pixel_count = 0;
        uint32_t tot_pixel_count = 0;
        uint32_t skip_sections = 0;
        uint32_t section_count = 0;

        // Build banks and analyze Logical pid range
        while ( rpos < epos )
        {
            // Base/Starting Physical PixelId
            physical_start = *rpos++;

            // BankID, 0/1=Direct/Shorthand Bit, Pixel Count...
            bank_id = (uint16_t)(*rpos >> 16);
            is_shorthand = (uint16_t)((*rpos & 0x8000) >> 15);
            pixel_count = (uint16_t)(*rpos & 0x7FFF);
            rpos++;

#ifdef DEBUG_PIXMAP
            syslog( LOG_INFO, "%s: %s = %d, %s = %u, %s = %u, %s = %u",
                "ADARA::PixelMappingAltPkt",
                "Base/Starting Physical", physical_start,
                "Bank ID", bank_id,
                "Is Shorthand", is_shorthand,
                "Count", pixel_count );
            usleep(30000); // give syslog a chance...
#endif

            // Process Shorthand PixelId Section...
            if ( is_shorthand )
            {
                // Stopping Physical PixelId
                physical_stop = *rpos++;

                // Base/Starting Logical PixelId
                logical_start = *rpos++;

                // Physical and Logical Step
                physical_step = (int16_t)(*rpos >> 16);
                logical_step = (int16_t)(*rpos & 0xFFFF);
                rpos++;

                // Stopping Logical PixelId
                logical_stop = *rpos++;

#ifdef DEBUG_PIXMAP
                syslog( LOG_INFO, "%s: %s %s=%d/%d/%d %s=%d/%d/%d",
                    "ADARA::PixelMappingAltPkt", "Shorthand Section",
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step );
                usleep(30000); // give syslog a chance...
#endif

                // Verify Physical PixelId Count Versus Section Count...
                cnt = ( physical_stop - physical_start + physical_step )
                    / physical_step;
                if ( cnt != (int32_t) pixel_count )
                {
                    syslog( LOG_ERR,
                        "%s: %s %s=%d/%d/%d %s=%d/%d/%d: %d != %d - %s",
                        "ADARA::PixelMappingAltPkt",
                        "Physical PixelId Count Mismatch",
                        "physical",
                        physical_start, physical_stop, physical_step,
                        "logical",
                        logical_start, logical_stop, logical_step,
                        cnt, pixel_count,
                        "Skip Section..." );
                    usleep(30000); // give syslog a chance...

                    // Next Section
                    skip_pixel_count += pixel_count;
                    skip_sections++;
                    continue;
                }

                // Verify Logical PixelId Count Versus Section Count...
                cnt = ( logical_stop - logical_start + logical_step )
                    / logical_step;
                if ( cnt != (int32_t) pixel_count )
                {
                    syslog( LOG_ERR,
                        "%s: %s %s=%d/%d/%d %s=%d/%d/%d: %d != %d - %s",
                        "ADARA::PixelMappingAltPkt",
                        "Logical PixelId Count Mismatch",
                        "physical",
                        physical_start, physical_stop, physical_step,
                        "logical",
                        logical_start, logical_stop, logical_step,
                        cnt, pixel_count,
                        "Skip Section..." );
                    usleep(30000); // give syslog a chance...

                    // Next Section
                    skip_pixel_count += pixel_count;
                    skip_sections++;
                    continue;
                }

                // Note: Assume Physical and Logical Counts
                // Match Each Other If They Both Match the
                // Same Internal Section Count... ;-D

                // Skip Unmapped Sections of Pixel Map...!
                if ( bank_id == (uint16_t) UNMAPPED_BANK )
                {
                    syslog( LOG_ERR,
                        "%s: Unmapped Shorthand Section - Skip...",
                        "ADARA::PixelMappingAltPkt" );
                    usleep(30000); // give syslog a chance...

                    // Next Section
                    skip_pixel_count += pixel_count;
                    skip_sections++;
                    continue;
                }

                // Determine Latest Max Logical PixelId...

                if ( logical_step > 0 )
                    pid = logical_stop;
                else
                    pid = logical_start; // Negative Step...

                bool set = false;

                if ( pid != (uint32_t) -1 )
                {
                    if ( first_pid )
                    {
                        max_pid = pid;
                        first_pid = false;
                        set = true;
                    }
                    else if ( pid > max_pid )
                    {
                        max_pid = pid;
                        set = true;
                    }
                }

                if ( set )
                {
                    m_pixbankmap.resize( max_pid + 1, -1 );

#ifdef DEBUG_PIXMAP
                    syslog( LOG_ERR,
                        "%s: Max Logical PixelId Set to %u",
                        "ADARA::PixelMappingAltPkt", max_pid );
                    usleep(30000); // give syslog a chance...
#endif
                }

                // Just Save the Logical PixelId to Bank Mapping...
                for ( logical=logical_start ;
                        logical != logical_stop ; logical += logical_step )
                {
                    m_pixbankmap[ (uint32_t) logical ] = bank_id;
                }

                // Append "Stop" PixelIds Too...! ;-D
                m_pixbankmap[ (uint32_t) logical_stop ] = bank_id;

                // Done with This Shorthand Section

                tot_pixel_count += pixel_count;

#ifdef DEBUG_PIXMAP
                syslog( LOG_INFO,
                    "%s: %s, %s=%u %s=%d/%d/%d %s=%d/%d/%d %s=%u %s=%u",
                    "ADARA::PixelMappingAltPkt",
                    "Done with Shorthand Section",
                    "bank_id", bank_id,
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step,
                    "count", pixel_count,
                    "total", tot_pixel_count );
                usleep(30000); // give syslog a chance...
#endif
            }

            // Process Direct PixelId Section...
            else
            {
                // Skip Unmapped Sections of Pixel Map...!
                if ( bank_id == (uint16_t) UNMAPPED_BANK )
                {
                    syslog( LOG_ERR,
                        "%s: Unmapped Direct Section - Skip...",
                        "ADARA::PixelMappingAltPkt" );
                    usleep(30000); // give syslog a chance...

                    // Next Section
                    skip_pixel_count += pixel_count;
                    rpos += pixel_count;
                    skip_sections++;
                    continue;
                }

                // Determine Latest Max Logical PixelId...

                epos2 = rpos + pixel_count;

                rpos_save = rpos;

                bool set = false;

                while ( rpos < epos2 )
                {
                    // Logical PixelId...
                    pid = *rpos++;
                    if ( pid == (uint32_t) -1 )
                        continue;
                    if ( first_pid )
                    {
                        max_pid = pid;
                        first_pid = false;
                        set = true;
                    }
                    else if ( pid > max_pid )
                    {
                        max_pid = pid;
                        set = true;
                    }
                }

                if ( set )
                {
                    m_pixbankmap.resize( max_pid + 1, -1 );

#ifdef DEBUG_PIXMAP
                    syslog( LOG_ERR,
                        "%s: Max Logical PixelId Set to %u",
                        "ADARA::PixelMappingAltPkt", max_pid );
                    usleep(30000); // give syslog a chance...
#endif
                }

                // Just Save the Logical PixelId to Bank Mapping...

                rpos = rpos_save;

                last_pid = -1;

                while ( rpos < epos2 )
                {
                    pid = *rpos++;
                    if ( pid == (uint32_t) -1 )
                        continue;
                    last_pid = pid;

                    // Store Bank ID to Logical PixelIds Map
                    m_pixbankmap[pid] = bank_id;
                }

                tot_pixel_count += pixel_count;

                // Done with This Direct Section

#ifdef DEBUG_PIXMAP
                syslog( LOG_INFO,
                    "%s: %s, %s=%u %s=%d %s=%u count=%u tot=%u",
                    "ADARA::PixelMappingAltPkt",
                    "Done with Direct Section",
                    "bank_id", bank_id,
                    "physical", physical_start,
                    "Last Logical PixelId", last_pid,
                    pixel_count, tot_pixel_count );
                usleep(30000); // give syslog a chance...
#endif
            }

            // Save bank ID
            // (Can be Overwritten in the case of
            // Multiple Pixel Map Section sub-headers for a given Bank,
            // but this is O.K., as BankInfo() is Just for Bookkeeping.
            m_bank_info[bank_id] = BankInfo(bank_id);

            section_count++;
        }

        // Also Add Unmapped and Error BankInfo to Map, as State=0...

        // Unmapped BankInfo
        m_bank_info[ UNMAPPED_BANK_INDEX ] =
            BankInfo( UNMAPPED_BANK_INDEX );

        // Error BankInfo
        m_bank_info[ ERROR_BANK_INDEX ] =
            BankInfo( ERROR_BANK_INDEX );

        // Done

        syslog( LOG_INFO,
            "%s: %s %s=%u %s=%u %s=%u, %s=%u %s=%u %s=%lu",
            "ADARA::PixelMappingAltPkt",
            "Done with Packet, PixelIds",
            "Tot", tot_pixel_count,
            "Max", max_pid,
            "Skip", skip_pixel_count,
            "Sections Used", section_count,
            "Skip", skip_sections,
            "PixelBankMap.size()", m_pixbankmap.size() );
        usleep(30000); // give syslog a chance...

        m_pixbankmap_processed = true;
    }

    return false;
}


/**
 * \brief ADARA pixel mapping packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::PixelMappingPkt &a_pkt )
{
    m_proc_state = TS_PKT_PIXEL_MAPPING;

    if ( !m_pixbankmap_processed )
    {
        const uint32_t *rpos = (const uint32_t *) a_pkt.mappingData();
        const uint32_t *epos = (const uint32_t *)
            ( a_pkt.mappingData() + a_pkt.payload_length() );
        const uint32_t *epos2;

        uint32_t        pid;
        uint32_t        max_pid = 0;
        bool            first_pid = true;
        uint16_t        bank_id;
        uint16_t        pixel_count;

        m_bank_info.clear();

        // Build banks and analyze Logical pid range
        while ( rpos < epos )
        {
            pid = *rpos++; // Base Logical ID
            bank_id = (uint16_t)(*rpos >> 16);
            pixel_count = (uint16_t)(*rpos & 0xFFFF);
            rpos++;

            // Save bank ID
            // (Can be Overwritten in the case of
            // Multiple Pixel Map Section sub-headers for a given Bank,
            // but this is O.K., as BankInfo() is Just for Bookkeeping. :-)
            m_bank_info[bank_id] = BankInfo(bank_id);

            // Max Logical PixelId for This Section is Base + Count - 1...
            pid += pixel_count - 1;

            if ( first_pid )
            {
                max_pid = pid;
                first_pid = false;
            }
            else if ( pid > max_pid )
                max_pid = pid;
        }

        m_pixbankmap.clear();
        m_pixbankmap.resize( max_pid + 1, -1 );

        // Build Logical-Pid-to-Bank Index
        rpos = (const uint32_t *) a_pkt.mappingData();
        while ( rpos < epos )
        {
            pid = *rpos++; // Base Logical ID
            bank_id = (uint16_t)(*rpos >> 16);
            pixel_count = (uint16_t)(*rpos & 0xFFFF);
            rpos++;

            epos2 = rpos + pixel_count;
            while ( rpos < epos2 )
            {
                // Store Bank ID to Logical PixelIds Map
                m_pixbankmap[pid++] = bank_id;
                rpos++;
            }
        }

        syslog( LOG_INFO, "%s: %s %s=%u, %s=%lu",
            "ADARA::PixelMappingPkt",
            "Done with Packet, PixelIds",
            "Max", max_pid,
            "PixelBankMap.size()", m_pixbankmap.size() );
        usleep(30000); // give syslog a chance...

        m_pixbankmap_processed = true;
    }

    return false;
}


/**
 * \brief ADARA banked event packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BankedEventPkt &a_pkt )
{
    m_proc_state = TS_PKT_BANKED_EVENT;

    if ( m_in_prolog )
    {
        m_in_prolog = false;
        m_notify.endProlog();
    }

    ++m_bnk_pkt_count;

    m_this_time = timespec_to_nsec(a_pkt.timestamp());

    if ( m_last_time )
    {
        if (((( m_last_cycle < 599 ) && ( a_pkt.cycle() != m_last_cycle + 1 )) ||
            ( m_last_cycle == 599 && a_pkt.cycle() != 0 )) )
            ++m_stream_metrics.m_cycle_err;

        if ( m_last_time > m_this_time )
            ++m_stream_metrics.m_invalid_pkt_time;
        else if ( m_last_time == m_this_time )
            ++m_stream_metrics.m_duplicate_packet;
        // TODO: This should depend on the Actual Facility Pulse Frequency!
        // (and account for Sub-60Hz operation, and 2nd Target Station!)
        else if ( fabs((m_this_time-m_last_time) - 16666666.0 ) > 1000000 )
            ++m_stream_metrics.m_pulse_freq_tol;
    }

    m_last_time = m_this_time;
    m_last_cycle = a_pkt.cycle();

    // Parse banked event packet to calulate pulse info, event count, and event rate
    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t flags = a_pkt.flags();

    // Check flags
    if ( flags & ERROR_PIXELS )
         ++m_run_metrics.m_pixel_error_count;

    if ( flags & PULSE_VETO )
         ++m_run_metrics.m_pulse_veto_count;

    if ( flags & MISSING_RTDL )
         ++m_run_metrics.m_missing_rtdl_count;

    if ( flags & MAPPING_ERROR )
         ++m_run_metrics.m_mapping_error_count;

    if ( flags & DUPLICATE_PULSE )
         ++m_run_metrics.m_dup_pulse_count;

    if ( flags & PCHARGE_UNCORRECTED
            || flags & VETO_UNCORRECTED )
    {
         ++m_run_metrics.m_pulse_pcharge_uncorrected;
    }

    if ( flags & GOT_METADATA )
         ++m_run_metrics.m_got_metadata_count;

    if ( flags & GOT_NEUTRONS )
         ++m_run_metrics.m_got_neutrons_count;

    if ( flags & HAS_STATES )
         ++m_run_metrics.m_has_states_count;

    // Count Total Pulses (with Data... ;-D)
    ++m_run_metrics.m_total_pulses_count;

    uint32_t        source_id;
    uint32_t        bank_count;
    int16_t         bank_id;
    uint32_t        event_count = 0;
    uint32_t        bank_event_count;
    const uint32_t *bank_endpos;
    uint32_t        tof, pid;
    int16_t         pixbank;
    map<int16_t,BankInfo>::iterator ibank;

    m_sources.clear();

    // Process banks per-source
    while ( rpos < epos )
    {
        source_id = *rpos++;

        if ( find( m_sources.begin(), m_sources.end(), source_id ) != m_sources.end())
            ++m_stream_metrics.m_duplicate_source;
        else
            m_sources.push_back( source_id );

        rpos += 2; // Skip over intrapulse time and TOF offset
        bank_count = *rpos++;

        // Process events per-bank
        while( bank_count-- )
        {
            bank_id = (int16_t)*rpos++;
            bank_event_count = *rpos++;

            if ( bank_id == -1 )
            {
                m_stream_metrics.m_pixel_map_err += bank_event_count;
                rpos += bank_event_count << 1;
            }
            else if ( bank_id == -2 )
            {
                m_stream_metrics.m_pixel_errors += bank_event_count;
                rpos += bank_event_count << 1;
            }
            else if (( ibank = m_bank_info.find( bank_id )) != m_bank_info.end() )
            {
                if ( ibank->second.m_last_pulse_time == 0 )
                {
                    ibank->second.m_source_id = source_id;
                    ibank->second.m_last_pulse_time = m_this_time;
                }
                else
                {
                    if ( ibank->second.m_source_id != source_id )
                        ++m_stream_metrics.m_bank_source_mismatch;

                    if ( ibank->second.m_last_pulse_time == m_this_time )
                        ++m_stream_metrics.m_duplicate_bank;
                }

                event_count += bank_event_count;

                bank_endpos = rpos + ( bank_event_count << 1 );
                while ( rpos < bank_endpos )
                {
                    tof = *rpos++;
                    pid = *rpos++;
                    if ( tof > m_maxtof ) // in 100nsec units, compare to 2e8 usec
                        ++m_stream_metrics.m_pixel_invalid_tof;

                    if ( pid >= m_pixbankmap.size() )
                        pixbank = -1;
                    else
                        pixbank = m_pixbankmap[pid];

                    if ( pixbank < 0 )
                        ++m_stream_metrics.m_pixel_unknown_id;
                    else if ( pixbank != bank_id )
                        ++m_stream_metrics.m_pixel_bank_mismatch;
                }
            }
            else
            {
                // This code should never get called unless the SMS fails somehow
                ++m_stream_metrics.m_invalid_bank_id;
                rpos += bank_event_count << 1;
            }
        }
    }

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_pcharge.addSample( a_pkt.pulseCharge() * 10.0 );

    uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

    if ( !m_first_pulse_time )
    {
        m_first_pulse_time = pulse_time;
    }

    if ( m_last_pulse_time )
    {
        m_pfreq.addSample( NANO_PER_SECOND_D
            / ( pulse_time - m_last_pulse_time ) );
    }

    m_last_pulse_time = pulse_time;
    m_bank_count_info.addSample( event_count );
    m_run_metrics.m_total_counts += event_count;
    m_run_metrics.m_time = ( pulse_time - m_first_pulse_time )
        / NANO_PER_SECOND_D;

    return false;
}


/**
 * \brief ADARA banked event state packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BankedEventStatePkt &a_pkt )
{
    m_proc_state = TS_PKT_BANKED_EVENT_STATE;

    if ( m_in_prolog )
    {
        m_in_prolog = false;
        m_notify.endProlog();
    }

    ++m_bnk_state_pkt_count;

    m_this_time = timespec_to_nsec(a_pkt.timestamp());

    if ( m_last_time )
    {
        if (((( m_last_cycle < 599 ) && ( a_pkt.cycle() != m_last_cycle + 1 )) ||
            ( m_last_cycle == 599 && a_pkt.cycle() != 0 )) )
            ++m_stream_metrics.m_cycle_err;

        if ( m_last_time > m_this_time )
            ++m_stream_metrics.m_invalid_pkt_time;
        else if ( m_last_time == m_this_time )
            ++m_stream_metrics.m_duplicate_packet;
        // TODO: This should depend on the Actual Facility Pulse Frequency!
        // (and account for Sub-60Hz operation, and 2nd Target Station!)
        else if ( fabs((m_this_time-m_last_time) - 16666666.0 ) > 1000000 )
            ++m_stream_metrics.m_pulse_freq_tol;
    }

    m_last_time = m_this_time;
    m_last_cycle = a_pkt.cycle();

    // Parse banked event packet to calulate pulse info, event count, and event rate
    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t flags = a_pkt.flags();

    // Check flags
    if ( flags & ERROR_PIXELS )
         ++m_run_metrics.m_pixel_error_count;

    if ( flags & PULSE_VETO )
         ++m_run_metrics.m_pulse_veto_count;

    if ( flags & MISSING_RTDL )
         ++m_run_metrics.m_missing_rtdl_count;

    if ( flags & MAPPING_ERROR )
         ++m_run_metrics.m_mapping_error_count;

    if ( flags & DUPLICATE_PULSE )
         ++m_run_metrics.m_dup_pulse_count;

    if ( flags & PCHARGE_UNCORRECTED
            || flags & VETO_UNCORRECTED )
    {
         ++m_run_metrics.m_pulse_pcharge_uncorrected;
    }

    if ( flags & GOT_METADATA )
         ++m_run_metrics.m_got_metadata_count;

    if ( flags & GOT_NEUTRONS )
         ++m_run_metrics.m_got_neutrons_count;

    if ( flags & HAS_STATES )
         ++m_run_metrics.m_has_states_count;

    // Count Total Pulses (with Data... ;-D)
    ++m_run_metrics.m_total_pulses_count;

    uint32_t        source_id;
    uint32_t        bank_count;
    int16_t         bank_id;
    uint32_t        state;
    uint32_t        event_count = 0;
    uint32_t        bank_event_count;
    const uint32_t *bank_endpos;
    uint32_t        tof, pid;
    int16_t         pixbank;
    map<int16_t,BankInfo>::iterator ibank;

    m_sources.clear();

    // Process banks per-source
    while ( rpos < epos )
    {
        source_id = *rpos++;

        if ( find( m_sources.begin(), m_sources.end(), source_id ) != m_sources.end())
            ++m_stream_metrics.m_duplicate_source;
        else
            m_sources.push_back( source_id );

        rpos += 2; // Skip over intrapulse time and TOF offset
        bank_count = *rpos++;

        // Process events per-bank
        while ( bank_count-- )
        {
            bank_id = (int16_t)*rpos++;
            state = *rpos++;
            bank_event_count = *rpos++;

            if ( bank_id == -1 )
            {
                m_stream_metrics.m_pixel_map_err += bank_event_count;
                rpos += bank_event_count << 1;
            }
            else if ( bank_id == -2 )
            {
                m_stream_metrics.m_pixel_errors += bank_event_count;
                rpos += bank_event_count << 1;
            }
            else if (( ibank = m_bank_info.find( bank_id )) != m_bank_info.end() )
            {
                if ( ibank->second.m_last_pulse_time == 0 )
                {
                    ibank->second.m_source_id = source_id;
                    ibank->second.m_last_pulse_time = m_this_time;
                }
                else
                {
                    if ( ibank->second.m_source_id != source_id )
                        ++m_stream_metrics.m_bank_source_mismatch;

                    if ( ibank->second.m_last_pulse_time == m_this_time )
                        ++m_stream_metrics.m_duplicate_bank;
                }

                event_count += bank_event_count;

                bank_endpos = rpos + ( bank_event_count << 1 );
                while ( rpos < bank_endpos )
                {
                    tof = *rpos++;
                    pid = *rpos++;
                    if ( tof > m_maxtof ) // in 100nsec units, compare to 2e8 usec
                        ++m_stream_metrics.m_pixel_invalid_tof;

                    if ( pid >= m_pixbankmap.size() )
                        pixbank = -1;
                    else
                        pixbank = m_pixbankmap[pid];

                    if ( pixbank < 0 )
                        ++m_stream_metrics.m_pixel_unknown_id;
                    else if ( pixbank != bank_id )
                        ++m_stream_metrics.m_pixel_bank_mismatch;
                }
            }
            else
            {
                // This code should never get called unless the SMS fails somehow
                ++m_stream_metrics.m_invalid_bank_id;
                rpos += bank_event_count << 1;
            }
        }
    }

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_pcharge.addSample( a_pkt.pulseCharge() * 10.0 );

    uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

    if ( !m_first_pulse_time )
    {
        m_first_pulse_time = pulse_time;
    }

    if ( m_last_pulse_time )
    {
        m_pfreq.addSample( NANO_PER_SECOND_D
            / ( pulse_time - m_last_pulse_time ) );
    }

    m_last_pulse_time = pulse_time;
    m_bank_count_info.addSample( event_count );
    m_run_metrics.m_total_counts += event_count;
    m_run_metrics.m_time = ( pulse_time - m_first_pulse_time )
        / NANO_PER_SECOND_D;

    return false;
}


/**
 * \brief ADARA beam monitor packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BeamMonitorPkt &a_pkt )
{
    m_proc_state = TS_PKT_BEAM_MONITOR_EVENT;

    if ( m_in_prolog )
    {
        m_in_prolog = false;
        m_notify.endProlog();
    }

    ++m_mon_pkt_count;

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint16_t monitor_id;
    uint32_t event_count;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Process events per-bank
    while( rpos < epos )
    {
        monitor_id = *rpos >> 22;
        event_count = *rpos++ & 0x003FFFFF;
        m_mon_event_count += event_count;
        m_mon_count_info[monitor_id].addSample( event_count );
        m_mon_last_pulse[monitor_id] = a_pkt.pulseId();

        rpos += 2 + event_count;
    }

    uint64_t dt;

    // Add zero sample to monitor that had no events, or remove them if no counts received for more than a second
    for ( map<uint32_t,CountInfo<uint64_t> >::iterator im = m_mon_count_info.begin(); im != m_mon_count_info.end(); )
    {
        dt = a_pkt.pulseId() - m_mon_last_pulse[im->first];
        if ( dt > 0 ) // && dt <= NANO_PER_SECOND_LL
        {
            im->second.addSample(0);
            ++im;
        }
        // else if ( dt > NANO_PER_SECOND_LL )
        // {
        //     m_mon_count_info.erase(im++);
        // }
        else
            ++im;
    }

    return false;
}


/**
 * \brief ADARA run info pacet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::RunInfoPkt &a_pkt )
{
    m_proc_state = TS_PKT_RUN_INFO;

    xmlDocPtr doc = xmlReadMemory( a_pkt.info().c_str(), a_pkt.info().length(), 0, 0, 0 );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( doc )
    {
        string tag;
        string value;

        try
        {
            for ( xmlNode *node = xmlDocGetRootElement(doc)->children; node; node = node->next )
            {
                tag = (char*)node->name;
                getXmlNodeValue( node, value );

                if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
                    m_run_info.m_run_num = boost::lexical_cast<uint32_t>( value );
                else if ( xmlStrcmp( node->name, (const xmlChar*)"proposal_id" ) == 0)
                    m_run_info.m_proposal_id = value;
                else if ( xmlStrcmp( node->name, (const xmlChar*)"run_title" ) == 0)
                    m_run_info.m_run_title = value;
                else if (xmlStrcmp( node->name, (const xmlChar*) "facility_name") == 0)
                    m_beam_info.m_facility = value;
                else if ( xmlStrcmp( node->name, (const xmlChar*)"sample" ) == 0)
                {
                    for ( xmlNode *sample_node = node->children; sample_node; sample_node = sample_node->next )
                    {
                        tag = (char*)sample_node->name;
                        getXmlNodeValue( sample_node, value );

                        if ( xmlStrcmp( sample_node->name, (const xmlChar*)"id" ) == 0)
                            m_run_info.m_sample_id = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"name" ) == 0)
                            m_run_info.m_sample_name = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"nature" ) == 0)
                            m_run_info.m_sample_nature = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"chemical_formula" ) == 0)
                            m_run_info.m_sample_formula = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"environment" ) == 0)
                            m_run_info.m_sample_environ = value;
                    }
                }
                else if ( xmlStrcmp( node->name, (const xmlChar*)"users" ) == 0)
                {
                    UserInfo info;

                    for ( xmlNode *user_node = node->children; user_node; user_node = user_node->next )
                    {
                        if ( xmlStrcmp( user_node->name, (const xmlChar*)"user" ) == 0)
                        {
                            info.m_id = "";
                            info.m_name = "";
                            info.m_role = "";

                            for ( xmlNode *uinfo_node = user_node->children; uinfo_node; uinfo_node = uinfo_node->next )
                            {
                                tag = (char*)uinfo_node->name;
                                getXmlNodeValue( uinfo_node, value );

                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"id" ) == 0)
                                    info.m_id = (char*)uinfo_node->children->content;
                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"name" ) == 0)
                                    info.m_name = (char*)uinfo_node->children->content;
                                else if (xmlStrcmp( uinfo_node->name, (const xmlChar*)"role" ) == 0)
                                    info.m_role = (char*)uinfo_node->children->content;
                            }

                            m_run_info.m_user_info.push_back( info );
                        }
                    }
                }
            }
        }
        catch( ... )
        {
            ++m_stream_metrics.m_bad_runinfo_xml;
        }

        xmlFreeDoc( doc );
    }
    else
    {
        ++m_stream_metrics.m_bad_runinfo_xml;
    }

    m_info_rcv |= 1;

    if ( m_info_rcv == 3 )
    {
        m_notify.beamInfo( m_beam_info );
        m_notify.runInfo( m_run_info );
    }

    return false;
}


/**
 * \brief ADARA beamline info packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BeamlineInfoPkt &a_pkt )
{
    m_proc_state = TS_PKT_BEAMLINE_INFO;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_beam_info.m_target_station_number = a_pkt.targetStationNumber();

    m_beam_info.m_beam_id = a_pkt.id();
    m_beam_info.m_beam_sname = a_pkt.shortName();
    m_beam_info.m_beam_lname = a_pkt.longName();

    m_info_rcv |= 2;

    if ( m_info_rcv == 3 )
    {
        m_notify.beamInfo( m_beam_info );
        m_notify.runInfo( m_run_info );
    }

    return false;
}


/**
 * \brief Converts text to ADARA process variable type.
 */
PVType
StreamMonitor::toPVType
(
    const char *a_source    ///< [in] Text-based variable type to convert
) const
{
    if ( boost::iequals( a_source, "integer" ))
        return PVT_INT;
    else if ( boost::iequals( a_source, "unsigned" ))
        return PVT_UINT;
    else if ( boost::iequals( a_source, "unsigned integer" ))
        return PVT_UINT;
    else if ( boost::iequals( a_source, "double" ))
        return PVT_DOUBLE;
    else if ( boost::iequals( a_source, "float" ))
        return PVT_FLOAT;
    else if ( boost::iequals( a_source, "string" ))
        return PVT_STRING;
    else if ( boost::istarts_with( a_source, "enum_" ))
        return PVT_ENUM;
    else if ( boost::iequals( a_source, "integer array" ))
        return PVT_UINT_ARRAY;
    else if ( boost::iequals( a_source, "double array" ))
        return PVT_DOUBLE_ARRAY;

    return PVT_INT;
}


/**
 * \brief ADARA device descriptor packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt )
{
    m_proc_state = TS_PKT_DEVICE_DESC;

    const string &xml =  a_pkt.description();

    xmlDocPtr doc = xmlReadMemory( xml.c_str(), xml.length(), 0, 0, 0 /* XML_PARSE_NOERROR | XML_PARSE_NOWARNING */ );
    if ( doc )
    {
        Identifier  pv_id = 0;
        string      pv_name;
        PVType      pv_type = PVT_INT;
        short       found;
        string      tag;
        string      value;
        map<PVKey,PVInfoBase*>::iterator ipv;

        try
        {
            xmlNode *root = xmlDocGetRootElement( doc );

            boost::lock_guard<boost::mutex> lock(m_mutex);

            for ( xmlNode* lev1 = root->children; lev1 != 0; lev1 = lev1->next )
            {
                if ( xmlStrcmp( lev1->name, (const xmlChar*)"process_variables" ) == 0)
                {
                    xmlNode *pvnode;

                    for ( xmlNode *pvsnode = lev1->children; pvsnode; pvsnode = pvsnode->next )
                    {
                        if ( xmlStrcmp( pvsnode->name, (const xmlChar*)"process_variable" ) == 0)
                        {
                            found = 0;

                            for ( pvnode = pvsnode->children; pvnode; pvnode = pvnode->next )
                            {
                                tag = (char*)pvnode->name;
                                getXmlNodeValue( pvnode, value );

                                if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_name" ) == 0)
                                {
                                    found |= 1;
                                    pv_name = value;
                                }
                                else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_id" ) == 0)
                                {
                                    found |= 2;
                                    pv_id = boost::lexical_cast<Identifier>( value );
                                }
                                else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_type" ) == 0)
                                {
                                    found |= 4;
                                    pv_type = toPVType( value.c_str() );
                                }
                            }

                            if ( found == 7 )
                            {
                                PVKey   key(a_pkt.devId(),pv_id);

                                ipv = m_pvs.find(key);
                                if ( ipv != m_pvs.end() )
                                {
                                    // This code will only be executed if a PV is redefined during
                                    // a run (or while idle). At the start of a run, all pvs are cleared
                                    // before DDP are processed, so they won't be found in the m_pvs map.
                                    m_notify.pvUndefined( ipv->second->m_name );

                                    delete ipv->second;
                                    m_pvs.erase( ipv );
                                }

                                vector<uint32_t> uint_nada;
                                vector<double> dbl_nada;

                                switch ( pv_type )
                                {
                                case PVT_INT:
                                case PVT_UINT:
                                case PVT_ENUM:
                                    m_pvs[key] = new PVInfo<uint32_t>(
                                        pv_name, a_pkt.devId(), pv_id,
                                        pv_type, 0 );
                                    break;
                                case PVT_FLOAT:
                                case PVT_DOUBLE:
                                    m_pvs[key] = new PVInfo<double>(
                                        pv_name, a_pkt.devId(), pv_id,
                                        pv_type, 0 );
                                    break;
                                case PVT_STRING:
                                    m_pvs[key] = new PVInfo<string>(
                                        pv_name, a_pkt.devId(), pv_id,
                                        pv_type, "" );
                                    break;
                                case PVT_UINT_ARRAY:
                                    m_pvs[key] =
                                        new PVInfo< vector<uint32_t> >(
                                            pv_name, a_pkt.devId(), pv_id,
                                            pv_type, uint_nada );
                                    break;
                                case PVT_DOUBLE_ARRAY:
                                    m_pvs[key] =
                                        new PVInfo< vector<double> >(
                                            pv_name, a_pkt.devId(), pv_id,
                                            pv_type, dbl_nada );
                                    break;
                                }

                                m_notify.pvDefined( pv_name );
                            }
                        }
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            ++m_stream_metrics.m_bad_ddp_xml;
        }

        xmlFreeDoc( doc );
    }
    else
    {
        ++m_stream_metrics.m_bad_ddp_xml;
    }


    return false;
}


/**
 * \brief ADARA variable update packet (uint32)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableU32Pkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_U32;

    pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA variable update packet (double)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableDoublePkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_DOUBLE;

    pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA variable update packet (string)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableStringPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_STRING;

    pvValueUpdate<string>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA variable update packet (uint32 array)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableU32ArrayPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_U32_ARRAY;

    pvValueUpdate< vector<uint32_t> >( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA variable update packet (double array)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableDoubleArrayPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_DOUBLE_ARRAY;

    pvValueUpdate< vector<double> >( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA multiple variable update packet (uint32)
 */
bool
StreamMonitor::rxPacket( const ADARA::MultVariableU32Pkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_U32;

    vector<uint32_t>::const_iterator it_vals = a_pkt.values().begin();
    vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    // struct timespec Ts = a_pkt.timestamp();
    // syslog( LOG_ERR, "%s %s=%u %s=%u %s=%u at %lu.%09lu",
        // "MultVariableU32Pkt()",
        // "devId", a_pkt.devId(), "varId", a_pkt.varId(),
        // "numVals", numVals,
        // Ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, Ts.tv_nsec );
    // usleep(30000); // give syslog a chance...

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR, "%s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                "MultVariableU32Pkt()",
                "devId", a_pkt.devId(), "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            usleep(30000); // give syslog a chance...
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts, a_pkt.status() );
    }

    return false;
}


/**
 * \brief ADARA multiple variable update packet (double)
 */
bool
StreamMonitor::rxPacket( const ADARA::MultVariableDoublePkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_DOUBLE;

    std::vector<double>::const_iterator it_vals = a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    // struct timespec Ts = a_pkt.timestamp();
    // syslog( LOG_ERR, "%s %s=%u %s=%u %s=%u at %lu.%09lu",
        // "MultVariableDoublePkt()",
        // "devId", a_pkt.devId(), "varId", a_pkt.varId(),
        // "numVals", numVals,
        // Ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, Ts.tv_nsec );
    // usleep(30000); // give syslog a chance...

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR, "%s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                "MultVariableDoublePkt()",
                "devId", a_pkt.devId(), "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            usleep(30000); // give syslog a chance...
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts, a_pkt.status() );
    }

    return false;
}


/**
 * \brief ADARA multiple variable update packet (string)
 */
bool
StreamMonitor::rxPacket( const ADARA::MultVariableStringPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_STRING;

    std::vector<string>::const_iterator it_vals = a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    // struct timespec Ts = a_pkt.timestamp();
    // syslog( LOG_ERR, "%s %s=%u %s=%u %s=%u at %lu.%09lu",
        // "MultVariableStringPkt()",
        // "devId", a_pkt.devId(), "varId", a_pkt.varId(),
        // "numVals", numVals,
        // Ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, Ts.tv_nsec );
    // usleep(30000); // give syslog a chance...

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR, "%s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                "MultVariableStringPkt()",
                "devId", a_pkt.devId(), "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            usleep(30000); // give syslog a chance...
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<string>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts, a_pkt.status() );
    }

    return false;
}


/**
 * \brief ADARA multiple variable update packet (uint32 array)
 */
bool
StreamMonitor::rxPacket( const ADARA::MultVariableU32ArrayPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_U32_ARRAY;

    std::vector< vector<uint32_t> >::const_iterator it_vals =
        a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    // struct timespec Ts = a_pkt.timestamp();
    // syslog( LOG_ERR, "%s %s=%u %s=%u %s=%u at %lu.%09lu",
        // "MultVariableU32ArrayPkt()",
        // "devId", a_pkt.devId(), "varId", a_pkt.varId(),
        // "numVals", numVals,
        // Ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, Ts.tv_nsec );
    // usleep(30000); // give syslog a chance...

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "%s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                "MultVariableU32ArrayPkt()",
                "devId", a_pkt.devId(), "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            usleep(30000); // give syslog a chance...
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate< vector<uint32_t> >( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts, a_pkt.status() );
    }

    return false;
}


/**
 * \brief ADARA multiple variable update packet (double array)
 */
bool
StreamMonitor::rxPacket( const ADARA::MultVariableDoubleArrayPkt &a_pkt )
{
    m_proc_state = TS_PKT_VAR_VALUE_DOUBLE_ARRAY;

    std::vector< vector<double> >::const_iterator it_vals =
        a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    // struct timespec Ts = a_pkt.timestamp();
    // syslog( LOG_ERR, "%s %s=%u %s=%u %s=%u at %lu.%09lu",
        // "MultVariableDoubleArrayPkt()",
        // "devId", a_pkt.devId(), "varId", a_pkt.varId(),
        // "numVals", numVals,
        // Ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, Ts.tv_nsec );
    // usleep(30000); // give syslog a chance...

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR, "%s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                "MultVariableDoubleArrayPkt()",
                "devId", a_pkt.devId(), "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            usleep(30000); // give syslog a chance...
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate< vector<double> >( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts, a_pkt.status() );
    }

    return false;
}


/**
 * \brief Process variable update method.
 *
 * This is a template method that handles PV updates. Updates are
 * rate-limited and listeners are notified as appropriate.
 * Updates received before first pulse are set to t-zero.
 */
template<class T>
void
StreamMonitor::pvValueUpdate
(
    Identifier      a_device_id,
    Identifier      a_pv_id,
    T               a_value,
    const timespec &a_timestamp,
    VariableStatus::Enum a_status
)
{
    PVKey   key(a_device_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_mutex);

    std::map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.find(key);

    // TODO Alert - bad stream packet (got value w/o ddp)
    if ( ipv == m_pvs.end() )
        return;

    // TODO This is a rate-limit HACK, needs to be MUCH more sophisticated!
    // Don't rate limit status changes
    if ( ( a_timestamp.tv_sec - ipv->second->m_time >= 1 )
            || ( ipv->second->m_status != a_status ) )
    {
        PVInfo<T> *pv = dynamic_cast<PVInfo<T> *>(ipv->second);
        if ( pv )
        {
            bool changed = false;

            if ( ( pv->m_time == 0 ) || ( pv->m_status != a_status ) )
                changed = true;

            else if ( pv->m_value != a_value )
                changed = true;

            if ( changed )
            {
                pv->m_value = a_value;
                pv->m_time = a_timestamp.tv_sec;
                pv->m_time_nanosec = a_timestamp.tv_nsec;
                pv->m_status = a_status;
                pv->m_updated = true;
                m_notify.pvValue( pv->m_name, a_value, a_status,
                    pv->m_time, pv->m_time_nanosec );
            }
        }
    }
}


/**
 * \brief Process variable update method.
 *
 * This is a template method that handles PV updates. Updates are
 * rate-limited and listeners are notified as appropriate.
 * Updates received before first pulse are set to t-zero.
 */
void
StreamMonitor::pvValueUpdate
(
    Identifier      a_device_id,
    Identifier      a_pv_id,
    vector<uint32_t> a_value,
    const timespec &a_timestamp,
    VariableStatus::Enum a_status
)
{
    PVKey   key(a_device_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_mutex);

    std::map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.find(key);

    // TODO Alert - bad stream packet (got value w/o ddp)
    if ( ipv == m_pvs.end() )
        return;

    // TODO This is a rate-limit HACK, needs to be MUCH more sophisticated!
    // Don't rate limit status changes
    if ( ( a_timestamp.tv_sec - ipv->second->m_time >= 1 )
            || ( ipv->second->m_status != a_status ) )
    {
        PVInfo< vector<uint32_t> > *pv =
            dynamic_cast<PVInfo< vector<uint32_t> > *>(ipv->second);

        if ( pv )
        {
            bool changed = false;

            if ( ( pv->m_time == 0 ) || ( pv->m_status != a_status ) )
                changed = true;

            else if ( pv->m_value.size() != a_value.size() )
                changed = true;

            else
            {
                for ( uint32_t i=0 ; i < pv->m_value.size() ; ++i )
                {
                    if ( pv->m_value[i] != a_value[i] )
                        changed = true;
                }
            }

            if ( changed )
            {
                pv->m_value = a_value;
                pv->m_time = a_timestamp.tv_sec;
                pv->m_time_nanosec = a_timestamp.tv_nsec;
                pv->m_status = a_status;
                pv->m_updated = true;
                m_notify.pvValue( pv->m_name, a_value, a_status,
                    pv->m_time, pv->m_time_nanosec );
            }
        }
    }
}


/**
 * \brief Process variable update method.
 *
 * This is a template method that handles PV updates. Updates are
 * rate-limited and listeners are notified as appropriate.
 * Updates received before first pulse are set to t-zero.
 */
void
StreamMonitor::pvValueUpdate
(
    Identifier      a_device_id,
    Identifier      a_pv_id,
    vector<double>  a_value,
    const timespec &a_timestamp,
    VariableStatus::Enum a_status
)
{
    PVKey   key(a_device_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_mutex);

    std::map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.find(key);

    // TODO Alert - bad stream packet (got value w/o ddp)
    if ( ipv == m_pvs.end() )
        return;

    // TODO This is a rate-limit HACK, needs to be MUCH more sophisticated!
    // Don't rate limit status changes
    if ( ( a_timestamp.tv_sec - ipv->second->m_time >= 1 )
            || ( ipv->second->m_status != a_status ) )
    {
        PVInfo< vector<double> > *pv =
            dynamic_cast<PVInfo< vector<double> > *>(ipv->second);

        if ( pv )
        {
            bool changed = false;

            if ( ( pv->m_time == 0 ) || ( pv->m_status != a_status ) )
                changed = true;

            else if ( pv->m_value.size() != a_value.size() )
                changed = true;

            else
            {
                for ( uint32_t i=0 ; i < pv->m_value.size() ; ++i )
                {
                    if ( pv->m_value[i] != a_value[i] )
                        changed = true;
                }
            }

            if ( changed )
            {
                pv->m_value = a_value;
                pv->m_time = a_timestamp.tv_sec;
                pv->m_time_nanosec = a_timestamp.tv_nsec;
                pv->m_status = a_status;
                pv->m_updated = true;
                m_notify.pvValue( pv->m_name, a_value, a_status,
                    pv->m_time, pv->m_time_nanosec );
            }
        }
    }
}


template void StreamMonitor::pvValueUpdate<uint32_t>(
    Identifier a_device_id, Identifier a_pv_id, uint32_t a_value,
    const timespec &a_timestamp, VariableStatus::Enum a_status );
template void StreamMonitor::pvValueUpdate<double>(
    Identifier a_device_id, Identifier a_pv_id, double a_value,
    const timespec &a_timestamp, VariableStatus::Enum a_status );
template void StreamMonitor::pvValueUpdate<string>(
    Identifier a_device_id, Identifier a_pv_id, string a_value,
    const timespec &a_timestamp, VariableStatus::Enum a_status );


/**
 * \brief Removes all cached process variable data and notifies listeners.
 */
void
StreamMonitor::clearPVs()
{
    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
    {
        m_notify.pvUndefined( ipv->second->m_name );
        delete ipv->second;
    }

    m_pvs.clear();
}


#ifndef NO_DB
struct PVInfoLite
{
    PVInfoLite( PVInfoBase* a_src )
        : m_name(a_src->m_name ), m_time(a_src->m_time), m_status(a_src->m_status)
    {}

    string      m_name;
    uint32_t    m_time;
    uint16_t    m_status;
};

#define BUF_SIZE 5000

void
StreamMonitor::dbThread()
{
    m_db_state = TS_ENTER;

    syslog( LOG_INFO, "Database update thread started." );
    usleep(30000); // give syslog a chance...

    // Attempt to connect
    string connect_string;

    if ( !m_db_info->host.empty() )
        connect_string += "host = " + m_db_info->host;

    if ( m_db_info->port > 0 )
        connect_string += " port = " + boost::lexical_cast<string>(m_db_info->port);

    if ( !m_db_info->user.empty() )
        connect_string += " user = " + m_db_info->user;

    if ( !m_db_info->pass.empty() )
        connect_string += " password = " + m_db_info->pass;

    connect_string += " dbname = " + m_db_info->name;

    PGconn *conn;
    PGresult *res;
    map<PVKey,PVInfoBase*>::iterator ipvm;
    string arr_str;
    char buf[BUF_SIZE];
    bool send_all =  true;
    bool update;
    int  i;

    vector<pair<PVInfoLite,uint32_t> >              int_pvs;
    vector<pair<PVInfoLite,uint32_t> >::iterator    iintpv;
    vector<pair<PVInfoLite,double> >                dbl_pvs;
    vector<pair<PVInfoLite,double> >::iterator      idblpv;
    vector<pair<PVInfoLite,string> >                str_pvs;
    vector<pair<PVInfoLite,string> >::iterator      istrpv;

    vector<uint32_t> int_vec;
    vector<double> dbl_vec;

    int_pvs.reserve(200);
    dbl_pvs.reserve(200);
    str_pvs.reserve(200);

    while ( 1 )
    {
        ++m_db_ticker;
        m_db_state = TS_CONNECTING;

        syslog( LOG_INFO,
            "Connecting to Database (%s=%s %s=%s %s=%s %s=%s)",
            "host", m_db_info->host.c_str(),
            "port", boost::lexical_cast<string>(m_db_info->port).c_str(),
            "user", m_db_info->user.c_str(),
            "dbname", m_db_info->name.c_str() );
        usleep(30000); // give syslog a chance...

        conn = PQconnectdb( connect_string.c_str() );

        if ( conn )
        {
            syslog( LOG_INFO, "Database Connected." );
            usleep(30000); // give syslog a chance...
            ++m_db_ticker;
            m_db_state = TS_RUNNING;

            update = true;
            while ( update )
            {
                ++m_db_ticker;
                m_db_state = TS_SLEEP;

                for ( i = 0; i < m_db_info->period; ++i )
                {
                    ++m_db_ticker;
                    sleep( 1 );
                }

                m_db_state = TS_RUNNING;

                // Cache updated PVs
                // Must ~copy~ data from PVs since they could be
                // deleted before we're done writing them to DB
                int_pvs.clear();
                dbl_pvs.clear();
                str_pvs.clear();

                m_mutex.lock();

                for ( ipvm = m_pvs.begin(); ipvm != m_pvs.end(); ++ipvm )
                {
                    if ( ipvm->second->m_updated || send_all )
                    {
                        ipvm->second->m_updated = false;
                        switch ( ipvm->second->m_type )
                        {
                        case PVT_FLOAT:
                        case PVT_DOUBLE:
                            dbl_pvs.push_back( make_pair(
                                PVInfoLite( ipvm->second ),
                                ((PVInfo<double>*)(ipvm->second))->
                                    m_value ) );
                            break;
                        case PVT_INT:
                        case PVT_UINT:
                        case PVT_ENUM:
                            int_pvs.push_back( make_pair(
                                PVInfoLite( ipvm->second ),
                                ((PVInfo<uint32_t>*)(ipvm->second))->
                                    m_value ) );
                            break;
                        case PVT_STRING:
                            str_pvs.push_back( make_pair(
                                PVInfoLite( ipvm->second ),
                                ((PVInfo<string>*)(ipvm->second))->
                                    m_value ) );
                            break;
                        // Pass Integer or Double Arrays as Strings...!
                        case PVT_DOUBLE_ARRAY:
                            dbl_vec = ((PVInfo< vector<double> >*)
                                (ipvm->second))->m_value;
                            Utils::printArrayString( dbl_vec, arr_str );
                            if ( arr_str.size() >= BUF_SIZE )
                            {
                                stringstream ss;
                                ss << "[" << dbl_vec.size() << "] = ( ";
                                ss << dbl_vec[0] << ", ";
                                ss << dbl_vec[1] << ", ";
                                ss << dbl_vec[2] << " ... ";
                                ss << dbl_vec[ dbl_vec.size() - 1 ]
                                    << " )";
                                str_pvs.push_back( make_pair(
                                    PVInfoLite( ipvm->second ),
                                        ss.str() ) );

                                syslog( LOG_ERR, "%s - Trimmed to [%s].",
                                    "Double Array Too Long",
                                    ss.str().c_str() );
                                usleep(30000); // give syslog a chance...
                            }
                            else
                            {
                                str_pvs.push_back( make_pair(
                                    PVInfoLite( ipvm->second ),
                                        arr_str ) );
                            }
                            break;
                        case PVT_UINT_ARRAY:
                            int_vec = ((PVInfo< vector<uint32_t> >*)
                                (ipvm->second))->m_value;
                            Utils::printArrayString( int_vec, arr_str );
                            if ( arr_str.size() >= BUF_SIZE )
                            {
                                stringstream ss;
                                ss << "[" << int_vec.size() << "] = ( ";
                                ss << int_vec[0] << ", ";
                                ss << int_vec[1] << ", ";
                                ss << int_vec[2] << " ... ";
                                ss << int_vec[ int_vec.size() - 1 ]
                                    << " )";
                                str_pvs.push_back( make_pair(
                                    PVInfoLite( ipvm->second ),
                                        ss.str() ) );

                                syslog( LOG_ERR, "%s - Trimmed to [%s].",
                                    "UInt Array Too Long",
                                    ss.str().c_str() );
                                usleep(30000); // give syslog a chance...
                            }
                            else
                            {
                                str_pvs.push_back( make_pair(
                                    PVInfoLite( ipvm->second ),
                                        arr_str ) );
                            }
                            break;
                        }
                    }
                }

                m_mutex.unlock();

                m_db_state = TS_DB_UPDATE;
                ++m_db_ticker;

                // Send double-value PV updates to database
                for ( idblpv = dbl_pvs.begin();
                        idblpv != dbl_pvs.end(); ++idblpv )
                {
                    sprintf( buf,
                        "select \"pvUpdate\"('%s','%s',%g,%u,%u)",
                        m_beam_info.m_beam_sname.c_str(),
                        idblpv->first.m_name.c_str(),
                        idblpv->second,
                        idblpv->first.m_status,
                        idblpv->first.m_time );

                    res = PQexec( conn, buf );
                    if ( !res || PQresultStatus( res ) != PGRES_TUPLES_OK )
                    {
                        syslog( LOG_ERR,
                            "Database Double Update Call Failed." );
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, PQresultErrorMessage( res ));
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, buf );
                        usleep(30000); // give syslog a chance...

                        PQclear( res );
                        update = false;
                        ++m_db_ticker;
                        continue;
                    }

                    PQclear( res );
                    send_all = false;
                    ++m_db_ticker;
                }

                ++m_db_ticker;

                // Send int-value PV updates to database
                for ( iintpv = int_pvs.begin();
                        iintpv != int_pvs.end(); ++iintpv )
                {
                    sprintf( buf,
                        "select \"pvUpdate\"('%s','%s',%u,%u,%u)",
                        m_beam_info.m_beam_sname.c_str(),
                        iintpv->first.m_name.c_str(),
                        iintpv->second,
                        iintpv->first.m_status,
                        iintpv->first.m_time );

                    res = PQexec( conn, buf );
                    if ( !res || PQresultStatus( res ) != PGRES_TUPLES_OK )
                    {
                        syslog( LOG_ERR,
                            "Database Int Update Call Failed." );
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, PQresultErrorMessage( res ));
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, buf );
                        usleep(30000); // give syslog a chance...

                        PQclear( res );
                        update = false;
                        ++m_db_ticker;
                        continue;
                    }

                    PQclear( res );
                    send_all = false;
                    ++m_db_ticker;
                }

                ++m_db_ticker;

                // Send string-value PV updates to database
                for ( istrpv = str_pvs.begin();
                        istrpv != str_pvs.end(); ++istrpv )
                {
                    string cmd_prefix = "select \"pvStringUpdate\"";

                    string strpv_value = istrpv->second;

                    if ( Utils::sanitizeString( strpv_value,
                            false /* a_preserve_uri */,
                            true /* a_preserve_whitespace */ ) )
                    {
                        syslog( LOG_WARNING,
                      "String PV \"%s\" Value Sanitized from [%s] to [%s]",
                            istrpv->first.m_name.c_str(),
                            istrpv->second.c_str(),
                            strpv_value.c_str() );
                        usleep(30000); // give syslog a chance...
                    }

                    size_t sz = cmd_prefix.size() + 1
                        + 1 + m_beam_info.m_beam_sname.size() + 1 + 1
                        + 1 + istrpv->first.m_name.size() + 1 + 1
                        + 1 + strpv_value.size() + 1 + 1
                        + 10 + 1 + 10 + 1 + 1; // Trailing '\0'...
                        // Note: Max UInt32 = 4294967295 (10 Digits)...

                    if ( sz >= BUF_SIZE )
                    {
                        // Too Big, Scrunch It Down... ;-b
                        stringstream ss;
                        ss << cmd_prefix << "("
                            << "'" << m_beam_info.m_beam_sname << "',"
                            << "'" << istrpv->first.m_name << "',"
                            << "'" << strpv_value.substr(0,99)
                                << "..." << "',"
                            << istrpv->first.m_status << ","
                            << istrpv->first.m_time << ")";

                        sprintf( buf, "%s", ss.str().c_str() );

                        syslog( LOG_ERR, "%s - Trimmed to [%s].",
                            "Database String Command Too Long", buf );
                        usleep(30000); // give syslog a chance...
                    }
                    else
                    {
                        sprintf( buf,
                            "%s('%s','%s','%s',%u,%u)",
                            cmd_prefix.c_str(),
                            m_beam_info.m_beam_sname.c_str(),
                            istrpv->first.m_name.c_str(),
                            strpv_value.c_str(),
                            istrpv->first.m_status,
                            istrpv->first.m_time );
                    }

                    res = PQexec( conn, buf );
                    if ( !res || PQresultStatus( res ) != PGRES_TUPLES_OK )
                    {
                        syslog( LOG_ERR,
                            "Database String Update Call Failed." );
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, PQresultErrorMessage( res ));
                        usleep(30000); // give syslog a chance...
                        syslog( LOG_ERR, buf );
                        usleep(30000); // give syslog a chance...

                        PQclear( res );
                        update = false;
                        ++m_db_ticker;
                        continue;
                    }

                    PQclear( res );
                    send_all = false;
                    ++m_db_ticker;
                }
            }
            PQfinish( conn );
            syslog( LOG_INFO, "Database Disconnected." );
            usleep(30000); // give syslog a chance...
            m_db_state = TS_DB_ERROR;
            ++m_db_ticker;
        }
        else
        {
            syslog( LOG_INFO, "Database Connect FAILED!" );
            usleep(30000); // give syslog a chance...
        }

        // Error may have been caused by DB being off-line,
        // or network error...
        // wait a bit and try connecting again
        syslog( LOG_INFO, "Sleeping Before Database Connection Attempt." );
        usleep(30000); // give syslog a chance...
        for ( i = 0; i < 15; ++i )
        {
            sleep( 1 );
            ++m_db_ticker;
        }
    }

    syslog( LOG_INFO, "Database update thread stopping." );
    usleep(30000); // give syslog a chance...
    m_db_state = TS_EXIT;
}
#endif


/**
 * \brief ADARA annotation packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::AnnotationPkt &a_pkt )
{
    m_proc_state = TS_PKT_STREAM_ANNOTATION;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    switch( a_pkt.marker_type() )
    {
    case ADARA::MarkerType::SCAN_START:
        m_scanning = true;
        m_scan_index = a_pkt.scanIndex();
        m_notify.scanStatus( true, a_pkt.scanIndex() );
        break;
    case ADARA::MarkerType::SCAN_STOP:
        m_scanning = false;
        m_scan_index = 0;
        m_notify.scanStatus( false, 0 );
        break;
    case ADARA::MarkerType::PAUSE:
        m_paused = true;
        m_notify.pauseStatus( true );
        break;
    case ADARA::MarkerType::RESUME:
        m_paused = false;
        m_notify.pauseStatus( false );
        break;
    default:
        break;
    }

    return false;
}



/**
 * \brief Extracts and trims the value string from an XML node.
 */
void
StreamMonitor::getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const
{
    if ( a_node->children && a_node->children->content )
    {
        a_value = (char*)a_node->children->content;
        boost::algorithm::trim( a_value );
    }
    else
        a_value = "";
}

///////////////////////////////////////////////////////////////////////////
// Notifier Class Implementation

void
StreamMonitor::Notifier::addListener( IStreamListener &a_listener )
{
    //TODO These calls are not thread safe,
    //but not a problame based on current usage
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener )
            == m_listeners.end() )
    {
        m_listeners.push_back( &a_listener );
    }
}

void
StreamMonitor::Notifier::removeListener( IStreamListener &a_listener )
{
    //TODO These calls are not thread safe,
    //but not a problame based on current usage
    vector<IStreamListener*>::iterator l =
        find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}

void
StreamMonitor::Notifier::runStatus( bool a_recording,
        uint32_t a_run_number,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_RUN_STATUS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->runStatus( a_recording, a_run_number,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::beginProlog()
{
    StreamMonitor::m_notify_state = TS_NOTIFY_BEGIN_PROLOG;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->beginProlog();
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::endProlog()
{
    StreamMonitor::m_notify_state = TS_NOTIFY_END_PROLOG;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->endProlog();
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pauseStatus( bool a_paused )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PAUSE_STATUS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pauseStatus( a_paused );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::scanStatus( bool a_scanning,
        uint32_t a_scan_number )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_SCAN_STATUS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->scanStatus( a_scanning, a_scan_number );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::beamInfo( const BeamInfo &a_info )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_BEAM_INFO;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->beamInfo( a_info );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::runInfo( const RunInfo &a_info )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_RUN_INFO;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->runInfo( a_info );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}


void
StreamMonitor::Notifier::beamMetrics( const BeamMetrics &a_metrics )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_BEAM_METRICS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->beamMetrics( a_metrics );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::runMetrics( const RunMetrics &a_metrics )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_RUN_METRICS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->runMetrics( a_metrics );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::streamMetrics( const StreamMetrics &a_metrics )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_STREAM_METRICS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->streamMetrics( a_metrics );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvDefined( const std::string &a_name )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_DEF;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvDefined( a_name );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvUndefined( const std::string &a_name )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_UNDEF;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvUndefined( a_name );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name,
        uint32_t a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_VAL_UINT;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvValue( a_name, a_value, a_status,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name,
        double a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_VAL_DBL;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvValue( a_name, a_value, a_status,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name,
        string &a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_VAL_STR;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvValue( a_name, a_value, a_status,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name,
        vector<uint32_t> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_VAL_UINT_ARRAY;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvValue( a_name, a_value, a_status,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name,
        vector<double> a_value, VariableStatus::Enum a_status,
        uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_PV_VAL_DBL_ARRAY;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->pvValue( a_name, a_value, a_status,
            a_timestamp, a_timestamp_nanosec );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

void
StreamMonitor::Notifier::connectionStatus( bool a_connected,
        const std::string &a_host, unsigned short a_port )
{
    StreamMonitor::m_notify_state = TS_NOTIFY_CONN_STATUS;

    for ( vector<IStreamListener*>::iterator l = m_listeners.begin();
            l != m_listeners.end();
            ++l, StreamMonitor::m_notify_state += 1000 )
    {
        (*l)->connectionStatus( a_connected, a_host, a_port );
    }

    StreamMonitor::m_notify_state = TS_NOTIFY_NONE;
}

}}

// vim: expandtab

