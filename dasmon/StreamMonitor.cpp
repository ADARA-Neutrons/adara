#include "StreamMonitor.h"
#include <iostream>
#include <string.h>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
//#include <signal.h>
#include <fcntl.h>
#include "Utils.h"


using namespace std;

#define ADARA_IN_BUF_SIZE 0x100000


StreamMonitor::StreamMonitor( const std::string &a_sms_host, unsigned short a_port )
    : Parser(), m_fd_in(-1), m_sms_host(a_sms_host), m_sms_port(a_port), m_stream_thread(0),
      m_process_stream(true), m_bank_count(0), m_run_status(ADARA::RunStatus::NO_RUN),
      m_scanning(false), m_pcount(0), m_first_pulse_time(0), m_last_pulse_time(0),
      m_stream_size(0), m_stream_rate(0)
{
}


StreamMonitor::~StreamMonitor()
{
}


void
StreamMonitor::start()
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( !m_stream_thread )
    {
        m_process_stream = true;
        resetRunStats();
        resetStreamStats();
        m_stream_thread = new boost::thread( boost::bind( &StreamMonitor::processStream, this ));
    }
}


void
StreamMonitor::stop()
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_stream_thread )
    {
        m_process_stream = false;
        m_stream_thread->join();
        resetRunStats();
        resetStreamStats();
    }
}


void
StreamMonitor::getSMSHostInfo( std::string &a_hostname, unsigned short &a_port ) const
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    a_hostname = m_sms_host;
    a_port = m_sms_port;
}


void
StreamMonitor::setSMSHostInfo( std::string a_hostname, unsigned short a_port )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_stream_thread )
    {
        m_process_stream = false;
        m_stream_thread->join();

        if ( m_fd_in > -1 )
        {
            close( m_fd_in );
            m_fd_in = -1;
            m_notify.connectionStatus( true );
        }
    }

    m_sms_host = a_hostname;
    m_sms_port = a_port;

    resetRunStats();
    resetStreamStats();
    m_process_stream = true;
    m_stream_thread = new boost::thread( boost::bind( &StreamMonitor::processStream, this ));
}


void
StreamMonitor::processStream()
{
    char buf;
    unsigned short delay_count = 0;

    m_notify.connectionStatus( false );

    while ( m_process_stream )
    {
        try
        {
            while( m_process_stream )
            {
                if ( m_fd_in < 0 )
                {
                    // Not connected - attempt connection periodically
                    sleep(1);
                    m_fd_in = connect();
                    if ( m_fd_in > -1 )
                        m_notify.connectionStatus( true );
                }
                else
                {
                    // See if there is any data to read - if not wait a bit
                    if ( recv( m_fd_in, &buf, 1, MSG_PEEK ) == -1 )
                    {
                        if ( errno == EWOULDBLOCK )
                        {
                            usleep(200000);
                            // Detect stall stream & reset statistics
                            if ( ++delay_count == 5 )
                                resetStreamStats();

                            continue;
                        }
                        else if ( errno != EINTR && errno != EAGAIN )
                        {
                            // Connection lost
                            handleLostConnection();
                            continue;
                        }
                        else
                            delay_count = 0;
                    }
                    else
                        delay_count = 0;

                    if ( !read( m_fd_in, ADARA_IN_BUF_SIZE ))
                    {
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
        catch(...)
        {
            // Connection lost
            handleLostConnection();
        }
    }
}


int
StreamMonitor::connect()
{
    // Create socket connection to SMS
    int sms_socket = socket( AF_INET, SOCK_STREAM, 0);
    if ( sms_socket < 0 )
        return -1;

    struct hostent *server = gethostbyname( m_sms_host.c_str() );
    if ( !server )
        return -2;

    struct sockaddr_in server_addr;
    bzero( &server_addr, sizeof( server_addr ));

    server_addr.sin_family = AF_INET;
    bcopy( (char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length );
    server_addr.sin_port = htons( m_sms_port );

    if ( ::connect( sms_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0 )
        return -3;

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

    write( sms_socket, data, sizeof(data));

    return sms_socket;
}


void
StreamMonitor::handleLostConnection()
{
    m_notify.connectionStatus( false );
    close( m_fd_in );
    m_fd_in = -1;
}


void
StreamMonitor::resetRunStats()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_stats.clear();
    m_first_pulse_time = 0;
    m_pcharge.total = 0.0;
    m_pcount = 0;
    m_run_dup_pulse_count.total = 0;
    m_run_cycle_error_count.total = 0;
    m_run_pixel_error_count.total = 0;

    clearPVs();
}


void
StreamMonitor::resetStreamStats()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_bank_count_info.reset();
    for ( map<uint32_t,CountInfo<uint64_t> >::iterator m = m_mon_count_info.begin(); m != m_mon_count_info.end(); ++m )
        m->second.reset();

    m_pcharge.reset();
    m_pfreq.reset();
    m_stream_rate = 0;
}


uint64_t
StreamMonitor::getCountRate()
{
    return m_bank_count_info.average * 60.0;
}


void
StreamMonitor::getMonitorCountRates( std::map<uint32_t,uint64_t> &a_monitor_count_rates )
{
    a_monitor_count_rates.clear();

    boost::lock_guard<boost::mutex> lock(m_mutex);

    for ( map<uint32_t,CountInfo<uint64_t> >::iterator m = m_mon_count_info.begin(); m != m_mon_count_info.end(); ++m )
    {
        a_monitor_count_rates[m->first] = m->second.average * 60;
    }
}


double
StreamMonitor::getProtonCharge()
{
    return m_pcharge.average;
}


double
StreamMonitor::getPulseFrequency()
{
    return m_pfreq.average;
}


uint64_t
StreamMonitor::getRunPulseCount()
{
    return m_pcount;
}


double
StreamMonitor::getRunPulseCharge()
{
    return m_pcharge.total;
}


uint64_t
StreamMonitor::getPixelErrorCount()
{
    return m_run_pixel_error_count.total;
}


uint64_t
StreamMonitor::getPixelErrorRate()
{
    return m_run_pixel_error_count.average * 60;
}


uint64_t
StreamMonitor::getDuplicatePulseCount()
{
    return m_run_dup_pulse_count.total;
}


uint64_t
StreamMonitor::getCycleErrorCount()
{
    return m_run_cycle_error_count.total;
}


uint64_t
StreamMonitor::getByteRate() const
{
    return m_stream_rate;
}


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


bool
StreamMonitor::rxPacket( const ADARA::RunStatusPkt &a_pkt )
{
    if ( m_run_status != a_pkt.status() )
    {
        m_run_status = a_pkt.status();

        if ( m_run_status == ADARA::RunStatus::NEW_RUN )
        {
            // New run - reset statistics
            resetRunStats();

            // Update run status on GUI
            m_notify.runStatus( true, a_pkt.runNumber() );

            //m_listener->setRunStartTime();
        }
        else if ( m_run_status == ADARA::RunStatus::END_RUN )
        {
            // Update run status on GUI
            m_notify.runStatus( false, 0 );

            resetRunStats();

            // Make sure scan is stopped
            if ( m_scanning )
            {
                m_scanning = false;
                m_notify.scanStop();
            }
        }
    }

    return false;
}


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
    m_pcount++;

    uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

    if ( !m_first_pulse_time )
        m_first_pulse_time = pulse_time;

    if ( m_last_pulse_time )
        m_pfreq.addSample( 1000000000.0 /(pulse_time - m_last_pulse_time));

    m_last_pulse_time = pulse_time;
    m_bank_count_info.addSample( event_count );

    return false;
}


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

        rpos += 2 + event_count;
    }

    return false;
}


bool
StreamMonitor::rxPacket( const ADARA::RunInfoPkt &a_pkt )
{
    xmlDocPtr doc = xmlReadMemory( a_pkt.info().c_str(), a_pkt.info().length(), 0, 0, 0 );
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

                // TODO should this compare run no from RunStatusPkt?
                // m_run_info.run_number = boost::lexical_cast<unsigned long>( value );
                //if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
                if ( xmlStrcmp( node->name, (const xmlChar*)"proposal_id" ) == 0)
                    m_notify.proposalID( value );
                else if ( xmlStrcmp( node->name, (const xmlChar*)"run_title" ) == 0)
                    m_notify.runTitle( value );
                else if (xmlStrcmp( node->name, (const xmlChar*) "facility_name") == 0)
                    m_notify.facilityName( value );
                else if ( xmlStrcmp( node->name, (const xmlChar*)"sample" ) == 0)
                {
                    for ( xmlNode *sample_node = node->children; sample_node; sample_node = sample_node->next )
                    {
                        tag = (char*)sample_node->name;
                        getXmlNodeValue( sample_node, value );

                        if ( xmlStrcmp( sample_node->name, (const xmlChar*)"id" ) == 0)
                            m_notify.sampleID( value );
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"name" ) == 0)
                            m_notify.sampleName( value );
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"nature" ) == 0)
                            m_notify.sampleNature( value );
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"chemical_formula" ) == 0)
                            m_notify.sampleFormula( value );
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"environment" ) == 0)
                            m_notify.sampleEnvironment( value );
                    }
                }
                else if ( xmlStrcmp( node->name, (const xmlChar*)"users" ) == 0)
                {
                    string uid, uname, urole;

                    for ( xmlNode *user_node = node->children; user_node; user_node = user_node->next )
                    {
                        if ( xmlStrcmp( user_node->name, (const xmlChar*)"user" ) == 0)
                        {
                            for ( xmlNode *uinfo_node = user_node->children; uinfo_node; uinfo_node = uinfo_node->next )
                            {
                                tag = (char*)uinfo_node->name;
                                getXmlNodeValue( uinfo_node, value );

                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"id" ) == 0)
                                    uid = (char*)uinfo_node->children->content;
                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"name" ) == 0)
                                    uname = (char*)uinfo_node->children->content;
                                else if (xmlStrcmp( uinfo_node->name, (const xmlChar*)"role" ) == 0)
                                    urole = (char*)uinfo_node->children->content;
                            }

                            m_notify.userInfo( uid, uname, urole );
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

    return false;
}


bool
StreamMonitor::rxPacket( const ADARA::BeamlineInfoPkt &a_pkt )
{
    m_notify.beamInfo( a_pkt.id(), a_pkt.shortName(), a_pkt.longName() );

    return false;
}


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


bool
StreamMonitor::rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt )
{
    //cout << "DeviceDescriptorPkt" << endl;

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

                                if ( m_pvs.find(key) == m_pvs.end() )
                                {
                                    m_pvs[key] = new PVInfo( pv_name, a_pkt.devId(), pv_id, pv_type );
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


bool
StreamMonitor::rxPacket( const ADARA::VariableU32Pkt &a_pkt )
{
    pvValueUpdate( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp() );

    return false;
}


bool
StreamMonitor::rxPacket( const ADARA::VariableDoublePkt &a_pkt )
{
    pvValueUpdate( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp() );

    return false;
}


void
StreamMonitor::pvValueUpdate
(
    Identifier      a_device_id,
    Identifier      a_pv_id,
    double          a_value,
    const timespec &a_timestamp
)
{
    PVKey   key(a_device_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_mutex);

    std::map<PVKey,PVInfo*>::iterator ipv = m_pvs.find(key);

    if ( ipv == m_pvs.end() )
        return;
    // TODO Alert - bad stream packet!
    //THROW_TRACE( ERR_PV_NOT_DEFINED, "pvValueUpdate() failed - PV " << a_device_id << "." << a_pv_id << " not defined." )


    double t = 0;

    // Note: if first pulse has not arrived, truncate all PV times to 0
    if ( m_first_pulse_time )
    {
        uint64_t t1 = timespec_to_nsec( a_timestamp );

        // Truncate negative time offsets to 0
        if ( t1 > m_first_pulse_time )
            t = (t1 - m_first_pulse_time)/1000000000.0;
    }

    ipv->second->m_value = a_value;
    ipv->second->m_time = t;

    m_notify.pvValue( ipv->second->m_name, a_value );
}


void
StreamMonitor::clearPVs()
{
    for ( map<PVKey,PVInfo*>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
    {
        delete ipv->second;
    }

    m_pvs.clear();
}


bool
StreamMonitor::rxPacket( const ADARA::AnnotationPkt &a_pkt )
{
    switch( a_pkt.type() )
    {
    case ADARA::MarkerType::SCAN_START:
        m_scanning = true;
        m_notify.scanStart( a_pkt.scanIndex() );
        break;
    case ADARA::MarkerType::SCAN_STOP:
        m_scanning = false;
        m_notify.scanStop();
        break;
    case ADARA::MarkerType::PAUSE:
        //cout << "Paused!" << endl;
        m_notify.pauseStatus( true );
        break;
    case ADARA::MarkerType::RESUME:
        //cout << "Resume!" << endl;
        m_notify.pauseStatus( false );
        break;
    default:
        break;
    }


    return false;
}


void
StreamMonitor::gatherStats( const ADARA::Packet &a_pkt )
{
    static uint64_t next_time = 0;
    static uint64_t last_time = 0;

    boost::lock_guard<boost::mutex> lock(m_mutex);

    PktStats &stats = m_stats[a_pkt.type()];

    ++stats.count;

    if ( a_pkt.packet_length() < stats.min_pkt_size || !stats.min_pkt_size )
        stats.min_pkt_size = a_pkt.packet_length();

    if ( a_pkt.packet_length() > stats.max_pkt_size )
        stats.max_pkt_size = a_pkt.packet_length();

    stats.total_size += a_pkt.packet_length();

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
StreamMonitor::Notifier::runStatus( bool a_recording, unsigned long a_run_number )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->runStatus( a_recording, a_run_number );
}

void
StreamMonitor::Notifier::pauseStatus( bool a_paused )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pauseStatus( a_paused );
}

void
StreamMonitor::Notifier::scanStart( unsigned long a_scan_number )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->scanStart( a_scan_number );
}

void
StreamMonitor::Notifier::scanStop()
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->scanStop();
}

void
StreamMonitor::Notifier::runTitle( const std::string &a_run_title )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->runTitle( a_run_title );
}

void
StreamMonitor::Notifier::facilityName( const std::string &a_facility_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->facilityName( a_facility_name );
}

void
StreamMonitor::Notifier::beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->beamInfo( a_id, a_short_name, a_long_name );
}

void
StreamMonitor::Notifier::proposalID( const std::string &a_proposal_id )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->proposalID( a_proposal_id );
}

void
StreamMonitor::Notifier::sampleID( const std::string &a_sample_id )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->sampleID( a_sample_id );
}

void
StreamMonitor::Notifier::sampleName( const std::string &a_sample_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->sampleName( a_sample_name );
}

void
StreamMonitor::Notifier::sampleNature( const std::string &a_sample_nature )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->sampleNature( a_sample_nature );
}

void
StreamMonitor::Notifier::sampleFormula( const std::string &a_sample_formula )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->sampleFormula( a_sample_formula );
}

void
StreamMonitor::Notifier::sampleEnvironment( const std::string &a_sample_environment )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->sampleEnvironment( a_sample_environment );
}

void
StreamMonitor::Notifier::userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->userInfo( a_uid, a_uname, a_urole );
}


void
StreamMonitor::Notifier::pvDefined( const std::string &a_name )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvDefined( a_name );
}


void
StreamMonitor::Notifier::pvValue( const std::string &a_name, double a_value )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->pvValue( a_name, a_value );
}


void
StreamMonitor::Notifier::connectionStatus( bool a_connected )
{
    for ( vector<IStreamListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        (*l)->connectionStatus( a_connected );
}


