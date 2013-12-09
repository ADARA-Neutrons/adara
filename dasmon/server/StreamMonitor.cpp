#include "StreamMonitor.h"
#include <iostream>
#include <string.h>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "Utils.h"
#include <syslog.h>


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

/**
 * \brief StreamMonitor constructor.
 * \param a_sms_host - Hostname of SMS stream source
 * \param a_port - Port number of SMS stream source
 *
 * The StreamMonitor constructor performs limited initialization. Full initialization is not
 * performed until the start() method is called.
 */
#ifndef NO_DB
StreamMonitor::StreamMonitor( const std::string &a_sms_host, unsigned short a_port, DBConnectInfo *a_db_info, uint32_t a_maxtof )
#else
StreamMonitor::StreamMonitor( const std::string &a_sms_host, unsigned short a_port )
#endif
    : POSIXParser(), m_fd_in(-1), m_sms_host(a_sms_host), m_sms_port(a_port), m_stream_thread(0), m_metrics_thread(0),
      m_process_stream(true), m_mon_event_count(0), m_recording(false), m_run_num(0), m_run_timestamp(0),
      m_paused(false), m_scanning(false), m_scan_index(0), m_first_pulse_time(0), m_last_pulse_time(0), m_stream_size(0),
      m_stream_rate(0), m_ok(true), m_diagnostics(true), m_last_cycle(0), m_last_time(0), m_this_time(0),
      m_bnk_pkt_count(0), m_mon_pkt_count(0), m_maxtof(a_maxtof), m_pixmap_processed(false)
#ifndef NO_DB
     ,m_db_info(a_db_info)
#endif
{
    m_maxtof *= 10; // Convert from usec to 100 nsec units

#ifndef NO_DB
    if ( m_db_info )
        m_db_thread = new boost::thread( boost::bind( &StreamMonitor::dbThread, this ));
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
        a_listener.runStatus( m_recording, m_run_num, m_run_timestamp );
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
    char buf;

    syslog( LOG_INFO, "Stream monitor process thread started." );

    m_notify.connectionStatus( false, m_sms_host, m_sms_port );

    while ( m_process_stream )
    {
        try
        {
            while( m_process_stream )
            {
                if ( m_fd_in < 0 )
                {
                    // Not connected - attempt connection periodically
                    m_fd_in = connect();
                    if ( m_fd_in > -1 )
                        m_notify.connectionStatus( true, m_sms_host, m_sms_port );
                    else
                        sleep(5);
                }
                else
                {
                    // See if there is any data to read - if not wait a bit
                    if ( recv( m_fd_in, &buf, 1, MSG_PEEK ) == -1 )
                    {
                        if ( errno == EWOULDBLOCK )
                        {
                            usleep(200000);
                            continue;
                        }
                        else if ( errno != EINTR && errno != EAGAIN )
                        {
                            syslog( LOG_ERR, "recv() returned error code %i. Dropping connection.", errno );
                            // Connection lost
                            handleLostConnection();
                            // May have lost connection - dont wait for reconnect attempt
                            continue;
                        }
                    }

                    if ( !read( m_fd_in, 0, ADARA_IN_BUF_SIZE ))
                    {
                        syslog( LOG_WARNING, "ADARA::POSIXParser::read() returned 0. Dropping connection." );
                        // Connection lost due to source closing socket
                        handleLostConnection();
                    }
                    else
                    {
                        // Ran out of data in buffer, wait just a bit
                        usleep(20000);
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            // Primitive fault detection / reporting
            m_ok = false;

            // TODO Really need to notify someone that something BAD has happened!
            syslog( LOG_WARNING, "In processThread(): std::exception caught. Dropping connection. Exception = %s", e.what() );
            // Connection lost
            handleLostConnection();
            // This is probably a misbehaving data source, wait a bit before retrying
            // (This will rate limit syslog spamming somewhat)
            sleep(10);
        }
        catch(...)
        {
            // Primitive fault detection / reporting
            m_ok = false;

            // TODO Really need to notify someone that something BAD has happened!
            syslog( LOG_WARNING, "In processThread(): Unknown exception type caught. Dropping connection." );
            // Connection lost
            handleLostConnection();
            // This is probably a misbehaving data source, wait a bit before retrying
            // (This will rate limit syslog spamming somewhat)
            sleep(10);
        }
    }

    syslog( LOG_INFO, "Stream monitor process thread stopping." );
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
        return -1;

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
            // Set to non-blocking
            int flags = fcntl( sms_socket, F_GETFL, 0 ) | O_NONBLOCK;
            fcntl( sms_socket, F_SETFL, flags );

            // Send client hello to begin stream processing
            uint32_t data[5];

            data[0] = 4;
            data[1] = ADARA::PacketType::CLIENT_HELLO_V0;
            data[2] = time(0) - ADARA::EPICS_EPOCH_OFFSET;
            data[3] = 0;
            data[4] = 0;

            if ( write( sms_socket, data, sizeof(data)) == sizeof( data ))
            {
                syslog( LOG_NOTICE, "Connected to SMS." );
                return sms_socket;
            }
        }
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

    resetStreamStats();
    resetRunStats();

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
    m_pcharge.total = 0.0;
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
    m_beam_metrics.clear();
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
    unsigned short count = 0;

    syslog( LOG_INFO, "Stream metrics thread started." );

    while( m_process_stream )
    {
        sleep(1);

        // If connected, send beam info and beam metrics
        if ( m_fd_in > -1 )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Check low-count rate on banked events
            if ( !m_bnk_pkt_count )
            {
                m_bank_count_info.reset();
                m_pcharge.reset();
                m_pfreq.reset();
            }

            // Check low-count rate on monitor events
            if ( !m_mon_pkt_count )
                m_mon_count_info.clear();

            m_beam_metrics.m_count_rate = m_bank_count_info.average() * 60.0;
            m_beam_metrics.m_pulse_charge = m_pcharge.average();
            m_beam_metrics.m_pulse_freq =  m_pfreq.average();
            m_beam_metrics.m_stream_bps = m_stream_size; // Size = rate so long as polling is at 1 second

            m_beam_metrics.m_monitor_count_rate.clear();
            for ( map<uint32_t,CountInfo<uint64_t> >::iterator im = m_mon_count_info.begin(); im != m_mon_count_info.end(); ++im )
                m_beam_metrics.m_monitor_count_rate[im->first] = im->second.average() * 60;

            m_notify.beamMetrics( m_beam_metrics );

            // If recording, send run info and run metrics
            if ( m_recording )
            {
                m_run_metrics.m_total_charge = m_pcharge.total;
                m_notify.runMetrics( m_run_metrics );
            }

            m_stream_size = 0;
            m_bnk_pkt_count = 0;
            m_mon_pkt_count = 0;

            // Send stream metrics every 4 seconds
            if ( !(++count & 0x3 ))
                m_notify.streamMetrics( m_stream_metrics );
        }
    }

    syslog( LOG_INFO, "Stream metrics thread stopping." );
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

    boost::unique_lock<boost::mutex> lock(m_mutex);
    m_stream_size += a_pkt.packet_length();
    lock.unlock();

    try
    {
        switch (a_pkt.type())
        {
        // These packets shall always be processed
        case ADARA::PacketType::RUN_STATUS_V0:
        case ADARA::PacketType::PIXEL_MAPPING_V0:
        case ADARA::PacketType::RUN_INFO_V0:
        case ADARA::PacketType::BEAMLINE_INFO_V0:
        case ADARA::PacketType::DEVICE_DESC_V0:
        case ADARA::PacketType::VAR_VALUE_U32_V0:
        case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
        case ADARA::PacketType::STREAM_ANNOTATION_V0:
        case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
        case ADARA::PacketType::BANKED_EVENT_V0:
            return Parser::rxPacket(a_pkt);

        // Packet types that are not processes by StreamParser
        case ADARA::PacketType::GEOMETRY_V0:
        case ADARA::PacketType::RAW_EVENT_V0:
        case ADARA::PacketType::RTDL_V0:
        case ADARA::PacketType::SOURCE_LIST_V0:
        case ADARA::PacketType::TRANS_COMPLETE_V0:
        case ADARA::PacketType::CLIENT_HELLO_V0:
        case ADARA::PacketType::SYNC_V0:
        case ADARA::PacketType::HEARTBEAT_V0:
        case ADARA::PacketType::VAR_VALUE_STRING_V0:
            break;
        default:
            ++m_stream_metrics.m_invalid_pkt_type;
            break;
        }
    }
    catch(...)
    {
    cout << "rxPacket exception! pkt type: " << a_pkt.type() << endl;
        ++m_stream_metrics.m_invalid_pkt;
        throw;
    }

    //return POSIXParser::rxPacket(a_pkt);
    return false;
}


/**
 * \brief ADARA run status packet handler
 */
bool
StreamMonitor::rxPacket( const ADARA::RunStatusPkt &a_pkt )
{
    bool recording = false;
    switch (a_pkt.status())
    {
    case ADARA::RunStatus::RUN_BOF:
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
    }

    if ( recording && !m_recording )
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_recording = true;
        m_run_num = a_pkt.runNumber();
        m_run_timestamp = a_pkt.timestamp().tv_sec;

        m_pixmap_processed = false;
        resetRunStats();

        m_notify.runStatus( true, m_run_num, m_run_timestamp );

        // Clear all PVs - SMS will send active after RunStatus packet
        clearPVs();
    }
    else if ( !recording && m_recording )
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_recording = false;
        m_run_num = 0;
        m_run_timestamp = a_pkt.timestamp().tv_sec;

        resetRunStats();

        m_notify.runStatus( false, 0, m_run_timestamp );

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
 * \brief ADARA pixel mapping packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::PixelMappingPkt &a_pkt )
{
    if ( !m_pixmap_processed )
    {
        const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
        const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());
        const uint32_t *epos2;

        uint32_t        base_logical_id;
        uint32_t        pid;
        uint32_t        min_pid = 0;
        uint32_t        max_pid = 0;
        bool            first_pid = true;
        uint16_t        bank_id;
        uint16_t        pix_count;

        m_bank_info.clear();
        //m_pixel_to_bank.clear();

        // Build banks and analyze pid range
        while( rpos < epos )
        {
            base_logical_id = *rpos++;
            bank_id = (uint16_t)(*rpos >> 16);
            pix_count = (uint16_t)(*rpos & 0xFFFF);
            rpos++;

            // Save bank ID
            m_bank_info[bank_id] = BankInfo(bank_id);

            epos2 = rpos + pix_count;
            while ( rpos < epos2 )
            {
                pid = *rpos++;
                if ( first_pid )
                {
                    min_pid = max_pid = pid;
                    first_pid = false;
                }
                else
                {
                    if ( pid < min_pid )
                        min_pid = pid;
                    else if ( pid > max_pid )
                        max_pid = pid;
                }
            }
        }

        m_pixmap.clear();
        m_pixmap.resize( max_pid + 1, -1 );

        //cout << "min pid: " << min_pid << endl;
        //cout << "max pid: " << max_pid << endl;

        // Build pid-to-bank index
        rpos = (const uint32_t*)a_pkt.payload();
        while( rpos < epos )
        {
            base_logical_id = *rpos++;
            bank_id = (uint16_t)(*rpos >> 16);
            pix_count = (uint16_t)(*rpos & 0xFFFF);
            rpos++;

            epos2 = rpos + pix_count;
            while ( rpos < epos2 )
            {
                // Store logical pixel IDs to bank ID
                m_pixmap[*rpos++] = bank_id;
            }
        }
        m_pixmap_processed = true;
    }

    return false;
}


/**
 * \brief ADARA banked event packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BankedEventPkt &a_pkt )
{
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
    if ( flags & BankedEventPkt::ERROR_PIXELS )
         ++m_run_metrics.m_pixel_error_count;

    if ( flags & BankedEventPkt::PULSE_VETO )
         ++m_run_metrics.m_pulse_veto_count;

    if ( flags & BankedEventPkt::MISSING_RTDL )
         ++m_run_metrics.m_missing_rtdl_count;

    if ( flags & BankedEventPkt::MAPPING_ERROR )
         ++m_run_metrics.m_mapping_error_count;

    if ( flags & BankedEventPkt::DUPLICATE_PULSE )
         ++m_run_metrics.m_dup_pulse_count;

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
                    {
                        ++m_stream_metrics.m_bank_source_mismatch;
                        //cout << "BSM: bank: " << bank_id << " reported in " << source_id << " should be in " << ibank->second.m_source_id << endl;
                    }

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
                    {
                        ++m_stream_metrics.m_pixel_invalid_tof;
                        //cout << "TOF: " << tof << endl;
                        //cout << "TOF: " << hex << tof << dec << endl;
                    }

                    if ( pid >= m_pixmap.size() )
                        pixbank = -1;
                    else
                        pixbank = m_pixmap[pid];

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
        m_pfreq.addSample( 1000000000.0 /(pulse_time - m_last_pulse_time));

    m_last_pulse_time = pulse_time;
    m_bank_count_info.addSample( event_count );
    m_run_metrics.m_total_counts += event_count;
    m_run_metrics.m_time = (pulse_time - m_first_pulse_time)/1000000000.0;

    return false;
}


/**
 * \brief ADARA beam monitor packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BeamMonitorPkt &a_pkt )
{
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

    // Add zero count to monitors that had no events
    for ( map<uint32_t,CountInfo<uint64_t> >::iterator im = m_mon_count_info.begin(); im != m_mon_count_info.end(); ++im )
    {
        if ( m_mon_last_pulse[im->first] != a_pkt.pulseId())
        {
            im->second.addSample(0);
        }
    }

    return false;
}


/**
 * \brief ADARA run info pacet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::RunInfoPkt &a_pkt )
{
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
            // Primitive fault detection / reporting
            m_ok = false;
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
    boost::lock_guard<boost::mutex> lock(m_mutex);

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
    else if ( boost::istarts_with( a_source, "enum_" ))
        return PVT_ENUM;

    return PVT_INT;
}


/**
 * \brief ADARA device descriptor packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt )
{
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

                                switch ( pv_type )
                                {
                                case PVT_INT:
                                case PVT_UINT:
                                case PVT_ENUM:
                                    m_pvs[key] = new PVInfo<uint32_t>( pv_name, a_pkt.devId(), pv_id, pv_type, 0 );
                                    break;
                                case PVT_FLOAT:
                                case PVT_DOUBLE:
                                    m_pvs[key] = new PVInfo<double>( pv_name, a_pkt.devId(), pv_id, pv_type, 0 );
                                    break;
                                }

                                m_notify.pvDefined( pv_name );
                            }
                        }
                    }
                }
            }
        }
        catch( ... )
        {
            // Primitive fault detection / reporting
            m_ok = false;
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
    pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief ADARA variable update packet (double)
 */
bool
StreamMonitor::rxPacket( const ADARA::VariableDoublePkt &a_pkt )
{
    pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp(), a_pkt.status() );

    return false;
}


/**
 * \brief Process variable update method.
 *
 * This is a template method that handles PV updates. Updates are rate-limited and
 * listeners are notified as appropriate. Updates received before first pulse are
 * set to t-zero.
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

    // TODO This is a rate-limit HACK, needs to be MUCH more sophistacated!
    // Don't rate limit status changes
    if (( a_timestamp.tv_sec - ipv->second->m_time >= 1 ) || ( ipv->second->m_status != a_status ))
    {
        PVInfo<T> *pv = dynamic_cast<PVInfo<T> *>(ipv->second);
        if ( pv && (( pv->m_time == 0 ) || ( pv->m_value != a_value ) || ( pv->m_status != a_status )))
        {
            pv->m_value = a_value;
            pv->m_time = a_timestamp.tv_sec;
            pv->m_status = a_status;
            pv->m_updated = true;
            m_notify.pvValue( pv->m_name, a_value, a_status, pv->m_time );
        }
    }
}

template void StreamMonitor::pvValueUpdate<uint32_t>( Identifier a_device_id, Identifier a_pv_id, uint32_t a_value,
    const timespec &a_timestamp, VariableStatus::Enum a_status);
template void StreamMonitor::pvValueUpdate<double>( Identifier a_device_id, Identifier a_pv_id, double a_value,
    const timespec &a_timestamp, VariableStatus::Enum a_status);


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
void
StreamMonitor::dbThread()
{
    syslog( LOG_INFO, "Database update thread started." );

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
    vector<PVInfoBase*> pvs;
    vector<PVInfoBase*>::iterator ipvv;
    char buf[500];
    double value;
    bool send_all =  true;
    bool update;

    pvs.reserve(400);

    while ( 1 )
    {
        conn = PQconnectdb( connect_string.c_str() );
        if ( conn )
        {
            update = true;
            while ( update )
            {
                sleep( m_db_info->period );

                pvs.clear();

                // Cache updated PVs
                m_mutex.lock();

                for ( ipvm = m_pvs.begin(); ipvm != m_pvs.end(); ++ipvm )
                {
                    if ( ipvm->second->m_updated || send_all )
                    {
                        ipvm->second->m_updated = false;
                        pvs.push_back( ipvm->second );
                    }
                }

                m_mutex.unlock();

                // Send PV updates to database
                for ( ipvv = pvs.begin(); ipvv != pvs.end(); ++ipvv )
                {
                    if ( (*ipvv)->m_type == PVT_FLOAT || (*ipvv)->m_type == PVT_DOUBLE )
                        value = ((PVInfo<double>*)(*ipvv))->m_value;
                    else
                        value = ((PVInfo<uint32_t>*)(*ipvv))->m_value;

                    sprintf( buf, "select \"pvUpdate\"('%s','%s',%g,%u,%u)", m_beam_info.m_beam_sname.c_str(), (*ipvv)->m_name.c_str(), value, (unsigned short)(*ipvv)->m_status, (*ipvv)->m_time );
                    res = PQexec( conn, buf );
                    if ( !res || PQresultStatus( res ) != PGRES_TUPLES_OK )
                    {
                        syslog( LOG_ERR, "Database update call failed." );
                        syslog( LOG_ERR, PQresultErrorMessage( res ));
                        syslog( LOG_ERR, buf );

                        update = false;
                        break;
                    }

                    PQclear( res );
                    send_all = false;
                }
            }
            PQfinish( conn );
        }

        // Error may have been caused by DB being off-line, or network error
        // wait a bit and try connecting again
        sleep( 15 );
    }

    syslog( LOG_INFO, "Database update thread stopping." );
}
#endif


/**
 * \brief ADARA annotation packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::AnnotationPkt &a_pkt )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    switch( a_pkt.type() )
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

//////////////////////////////////////////////////////////////////////////////
// Notifier Class Imple

void
StreamMonitor::Notifier::addListener( IStreamListener &a_listener )
{
    //TODO These calls are not thread safe, but not a problame based on current usage
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
        m_listeners.push_back( &a_listener );
}

void
StreamMonitor::Notifier::removeListener( IStreamListener &a_listener )
{
    //TODO These calls are not thread safe, but not a problame based on current usage
    vector<IStreamListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}

void
StreamMonitor::Notifier::runStatus( bool a_recording, uint32_t a_run_number, uint32_t a_timestamp )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->runStatus( a_recording, a_run_number, a_timestamp );
}

void
StreamMonitor::Notifier::pauseStatus( bool a_paused )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pauseStatus( a_paused );
}

void
StreamMonitor::Notifier::scanStatus( bool a_scanning, uint32_t a_scan_number )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->scanStatus( a_scanning, a_scan_number );
}

void
StreamMonitor::Notifier::beamInfo( const BeamInfo &a_info )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->beamInfo( a_info );
}

void
StreamMonitor::Notifier::runInfo( const RunInfo &a_info )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->runInfo( a_info );
}


void
StreamMonitor::Notifier::beamMetrics( const BeamMetrics &a_metrics )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->beamMetrics( a_metrics );
}

void
StreamMonitor::Notifier::runMetrics( const RunMetrics &a_metrics )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->runMetrics( a_metrics );
}

void
StreamMonitor::Notifier::streamMetrics( const StreamMetrics &a_metrics )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->streamMetrics( a_metrics );
}

void
StreamMonitor::Notifier::pvDefined( const std::string &a_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvDefined( a_name );
}

void
StreamMonitor::Notifier::pvUndefined( const std::string &a_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvUndefined( a_name );
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status, uint32_t a_timestamp )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvValue( a_name, a_value, a_status, a_timestamp );
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status, uint32_t a_timestamp )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvValue( a_name, a_value, a_status, a_timestamp );
}

void
StreamMonitor::Notifier::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->connectionStatus( a_connected, a_host, a_port );
}

}}

