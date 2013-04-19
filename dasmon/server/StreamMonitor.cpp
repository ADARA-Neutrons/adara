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

#ifdef USE_DB
#include "libpq-fe.h"
#endif

using namespace std;

namespace ADARA {
namespace DASMON {

#define ADARA_IN_BUF_SIZE 0x100000


/**
 * \brief StreamMonitor constructor.
 * \param a_sms_host - Hostname of SMS stream source
 * \param a_port - Port number of SMS stream source
 *
 * The StreamMonitor constructor performs limited initialization. Full initialization is not
 * performed until the start() method is called.
 */
#ifdef USE_DB
StreamMonitor::StreamMonitor( const std::string &a_sms_host, unsigned short a_port, DBConnectInfo *a_db_info )
#else
StreamMonitor::StreamMonitor( const std::string &a_sms_host, unsigned short a_port )
#endif
    : Parser(), m_fd_in(-1), m_sms_host(a_sms_host), m_sms_port(a_port), m_stream_thread(0), m_metrics_thread(0),
      m_process_stream(true), m_bank_count(0), m_recording(false), m_run_num(0), m_run_timestamp(0), m_paused(false),
      m_scanning(false), m_scan_index(0), m_first_pulse_time(0), m_last_pulse_time(0), m_stream_size(0),
      m_stream_rate(0)
#ifdef USE_DB
     ,m_db_info(a_db_info)
#endif
{
#ifdef USE_DB
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
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    startProcessing();
}


/**
 * \brief Halts stream processing.
 */
void
StreamMonitor::stop()
{
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

    //cout << "resend stat: ";
    if ( m_fd_in > -1 )
    {
        //cout << " connected" << endl;
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
        //cout << " not connected" << endl;
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
    if ( !m_stream_thread )
    {
        syslog( LOG_INFO, "Start processing request." );

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
    if ( m_stream_thread )
    {
        syslog( LOG_INFO, "Stop processing request." );

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
                    sleep(5);
                    m_fd_in = connect();
                    if ( m_fd_in > -1 )
                        m_notify.connectionStatus( true, m_sms_host, m_sms_port );
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
                            continue;
                        }
                    }

                    if ( !read( m_fd_in, ADARA_IN_BUF_SIZE ))
                    {
                        syslog( LOG_WARNING, "ADARA::Parser::read() returned 0. Dropping connection." );
                        // Connection lost
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
            syslog( LOG_WARNING, "In processThread(): std::exception caught. Dropping connection. Exception = %s", e.what() );
            // Connection lost
            handleLostConnection();
        }
        catch(...)
        {
            syslog( LOG_WARNING, "In processThread(): Unknown exception type caught. Dropping connection." );
            // Connection lost
            handleLostConnection();
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

    resetStreamStats();
    resetRunStats();
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
    m_stats.clear();

    m_first_pulse_time = 0;
    m_pcharge.total = 0.0;
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
    syslog( LOG_INFO, "Stream metrics thread started." );

    unsigned long last_pulse_count = 0;
    bool    stalled = false;

    while( m_process_stream )
    {
        sleep(2);

        // If connected, send beam info and beam metrics
        if ( m_fd_in > -1 )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Check for stalled stream
            if ( last_pulse_count && ( m_run_metrics.m_pulse_count == last_pulse_count ))
            {
                if ( !stalled )
                {
                    syslog( LOG_WARNING, "Detected stalled ADARA stream." );
                    resetStreamStats();
                    stalled = true;
                }
            }
            else
            {
                last_pulse_count = m_run_metrics.m_pulse_count;
                if ( stalled )
                {
                    stalled = false;
                    syslog( LOG_NOTICE, "Stalled ADARA stream has resumed." );
                }
            }

            m_beam_metrics.m_count_rate = m_bank_count_info.average() * 60.0;
            m_beam_metrics.m_pulse_charge = m_pcharge.average();
            m_beam_metrics.m_pulse_freq =  m_pfreq.average();
            m_beam_metrics.m_stream_bps = m_stream_rate;

            m_beam_metrics.m_monitor_count_rate.clear();
            for ( map<uint32_t,CountInfo<uint64_t> >::iterator im = m_mon_count_info.begin(); im != m_mon_count_info.end(); ++im )
                m_beam_metrics.m_monitor_count_rate[im->first] = im->second.average() * 60;

            m_notify.beamMetrics( m_beam_metrics );

            // If recording, send run info and run metrics
            if ( m_recording )
            {
                m_run_metrics.m_pulse_charge = m_pcharge.total;
                m_notify.runMetrics( m_run_metrics );
            }
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

    gatherStats( a_pkt );

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
    case ADARA::PacketType::BANKED_EVENT_V0:
    case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
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
    }

    //return Parser::rxPacket(a_pkt);
    return false;
}


/**
 * \brief ADARA run status packet handler
 */
bool
StreamMonitor::rxPacket( const ADARA::RunStatusPkt &a_pkt )
{
    //cout << "RunStat: " << a_pkt.status() << endl;

    bool recording = (a_pkt.status() != ADARA::RunStatus::NO_RUN) && (a_pkt.runNumber() != 0);

    if ( recording && !m_recording )
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_recording = true;
        m_run_num = a_pkt.runNumber();
        m_run_timestamp = a_pkt.timestamp().tv_sec;
        resetRunStats();

        m_notify.runStatus( true, m_run_num, m_run_timestamp );
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
    }

    return false;
}


/**
 * \brief ADARA pixel mapping packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::PixelMappingPkt &a_pkt )
{
//    cout << "PixelMappingPkt" << endl;

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    uint16_t        bank_id;
    uint16_t        pix_count;

    m_bank_count = 0;

    // Now build banks and populate bank container
    while( rpos < epos )
    {
        rpos++;  // TODO This infomation is not currently processed.
        bank_id = (uint16_t)(*rpos >> 16);
        pix_count = (uint16_t)(*rpos & 0xFFFF);
        rpos++;

        //cout << "  bank ID: " << bank_id << ", pix count: " << pix_count << endl;

        rpos += pix_count;
        m_bank_count++;
    }

    //cout << "  bank count: " << m_bank_count << endl;

    return false;
}


/**
 * \brief ADARA banked event packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BankedEventPkt &a_pkt )
{
    //cout << "BankedEventPkt" << endl;

    // Parse banked event packet to calulate pulse info, event count, and event rate
    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t bank_count;
    uint32_t bank_id;
    uint32_t event_count = 0;
    uint32_t bank_event_count;

    // Process banks per-source
    while ( rpos < epos )
    {
        rpos += 3; // Skip over source info
        bank_count = *rpos++;

        // Process events per-bank
        while( bank_count-- )
        {
            bank_id = *rpos++;
            bank_event_count = *rpos++;
            if ( bank_id < m_bank_count )
            {
                event_count += bank_event_count;
            }
            rpos += bank_event_count << 1;
        }
    }

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_pcharge.addSample( a_pkt.pulseCharge() * 10.0 );
    m_run_metrics.m_pulse_count++;

    uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

    if ( !m_first_pulse_time )
    {
        m_first_pulse_time = pulse_time;
        //cout << "Got first pulse: t = " << m_first_pulse_time << endl;
    }

    if ( m_last_pulse_time )
        m_pfreq.addSample( 1000000000.0 /(pulse_time - m_last_pulse_time));

    m_last_pulse_time = pulse_time;
    m_bank_count_info.addSample( event_count );

    return false;
}


/**
 * \brief ADARA beam monitor packet handler.
 */
bool
StreamMonitor::rxPacket( const ADARA::BeamMonitorPkt &a_pkt )
{
    //cout << "BeamMonitorPkt" << endl;

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
    if ( doc )
    {
        string tag;
        string value;

        boost::lock_guard<boost::mutex> lock(m_mutex);

        try
        {
            for ( xmlNode *node = xmlDocGetRootElement(doc)->children; node; node = node->next )
            {
                tag = (char*)node->name;
                getXmlNodeValue( node, value );

                if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
                    m_run_info.m_run_num = boost::lexical_cast<unsigned long>( value );
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
            // TODO ???
        }

        xmlFreeDoc( doc );
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
    //cout << "DDP: " << endl;

    const string &xml =  a_pkt.description();

    //cout << xml << endl;

    xmlDocPtr doc = xmlReadMemory( xml.c_str(), xml.length(), 0, 0, 0 /* XML_PARSE_NOERROR | XML_PARSE_NOWARNING */ );
    if ( doc )
    {
        Identifier  pv_id = 0;
        string      pv_name;
        PVType      pv_type = PVT_INT;
        short       found;
        string      tag;
        string      value;

        try
        {
            xmlNode *root = xmlDocGetRootElement( doc );

            //cout << "DDP: ";

            boost::lock_guard<boost::mutex> lock(m_mutex);

            for ( xmlNode* lev1 = root->children; lev1 != 0; lev1 = lev1->next )
            {
                //if ( xmlStrcmp( lev1->name, (const xmlChar*)"device_name" ) == 0)
                //{
                //    getXmlNodeValue( lev1, value );
                //    cout << value << endl;
                //}

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

                                if ( m_pvs.find(key) == m_pvs.end() )
                                {
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
        }
        catch( ... )
        {
        }

        xmlFreeDoc( doc );
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
    if ( a_timestamp.tv_sec - ipv->second->m_time >= 1 )
    {
        PVInfo<T> *pv = dynamic_cast<PVInfo<T> *>(ipv->second);
        if ( pv && (( pv->m_value != a_value ) || ( pv->m_status != a_status )))
        {
            pv->m_value = a_value;
            pv->m_time = a_timestamp.tv_sec;
            pv->m_status = a_status;
            pv->m_updated = true;
            m_notify.pvValue( pv->m_name, a_value, a_status );
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


#ifdef USE_DB
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

                    sprintf( buf, "select pvUpdate('%s','%s',%g,%u,%lu)", m_beam_info.m_beam_id.c_str(), (*ipvv)->m_name.c_str(), value, (unsigned short)(*ipvv)->m_status, (*ipvv)->m_time );
                    res = PQexec( conn, buf );
                    if ( !res || PQresultStatus( res ) != PGRES_TUPLES_OK )
                    {
                        syslog( LOG_NOTICE, "Database update call failed." );

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
 * \brief Gathers general statistics from all ADARA packet types.
 */
void
StreamMonitor::gatherStats( const ADARA::Packet &a_pkt )
{
    static uint64_t next_time = 0;
    static uint64_t last_time = 0;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Packet statistics are not currently used - commented out to reduce loading
    /*
    PktStats &stats = m_stats[a_pkt.type()];

    ++stats.count;

    if ( a_pkt.packet_length() < stats.min_pkt_size || !stats.min_pkt_size )
        stats.min_pkt_size = a_pkt.packet_length();

    if ( a_pkt.packet_length() > stats.max_pkt_size )
        stats.max_pkt_size = a_pkt.packet_length();

    stats.total_size += a_pkt.packet_length();
    */

    m_stream_size += a_pkt.packet_length();
    uint64_t t = timespec_to_nsec( a_pkt.timestamp() );

    if ( !next_time )
    {
        last_time = t;
        next_time = t + 1000000000;
    }
    else if ( t >= next_time )
    {
        m_stream_rate = 1000000000*m_stream_size/(t-last_time);
        last_time = t;
        next_time = t + 1000000000;
        m_stream_size = 0;
    }
}


#if 0
void
StreamMonitor::getStatistics( std::map<uint32_t,PktStats> &a_stats )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    a_stats = m_stats;
}

const char*
StreamMonitor::getPktName(
    uint32_t a_pkt_type   ///< [in] An ADARA packet type (defined in ADARA.h)
) const
{
    // Mask out packet version number
    switch ( a_pkt_type & 0xFFFFFF00 )
    {
    case ADARA::PacketType::RAW_EVENT_V0:
        return "Raw Event";
    case ADARA::PacketType::RTDL_V0:
        return "RTDL";
    case ADARA::PacketType::SOURCE_LIST_V0:
        return "Src List";
    case ADARA::PacketType::BANKED_EVENT_V0:
        return "Bank Event";
    case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
        return "Beam Mon";
    case ADARA::PacketType::PIXEL_MAPPING_V0:
        return "Pix Map";
    case ADARA::PacketType::RUN_STATUS_V0:
        return "Run Stat";
    case ADARA::PacketType::RUN_INFO_V0:
        return "Run Info";
    case ADARA::PacketType::TRANS_COMPLETE_V0:
        return "Tran Comp";
    case ADARA::PacketType::CLIENT_HELLO_V0:
        return "Cli Hello";
    case ADARA::PacketType::STREAM_ANNOTATION_V0:
        return "Annotation";
    case ADARA::PacketType::SYNC_V0:
        return "Sync";
    case ADARA::PacketType::HEARTBEAT_V0:
        return "Heart";
    case ADARA::PacketType::GEOMETRY_V0:
        return "Geom Info";
    case ADARA::PacketType::BEAMLINE_INFO_V0:
        return "Beam Info";
    case ADARA::PacketType::DEVICE_DESC_V0:
        return "DDP";
    case ADARA::PacketType::VAR_VALUE_U32_V0:
        return "VVP U32";
    case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
        return "VVP DBL";
    case ADARA::PacketType::VAR_VALUE_STRING_V0:
        return "VVP STR";
    }

    return "Unknown";
}
#endif

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
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end())
        m_listeners.push_back( &a_listener );
}

void
StreamMonitor::Notifier::removeListener( IStreamListener &a_listener )
{
    vector<IStreamListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase(l);
}

void
StreamMonitor::Notifier::runStatus( bool a_recording, unsigned long a_run_number, unsigned long a_timestamp )
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
StreamMonitor::Notifier::scanStatus( bool a_scanning, unsigned long a_scan_number )
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
StreamMonitor::Notifier::pvValue( const std::string &a_name, uint32_t a_value, VariableStatus::Enum a_status )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvValue( a_name, a_value, a_status );
}

void
StreamMonitor::Notifier::pvValue( const std::string &a_name, double a_value, VariableStatus::Enum a_status )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvValue( a_name, a_value, a_status );
}

void
StreamMonitor::Notifier::connectionStatus( bool a_connected, const std::string &a_host, unsigned short a_port )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->connectionStatus( a_connected, a_host, a_port );
}

}}

