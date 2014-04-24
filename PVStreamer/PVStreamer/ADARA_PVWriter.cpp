/**
 * \file ADARA_PVWriter.cpp
 * \brief Source file for ADARA_PVWriter class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"

#include <sstream>
#include "ADARA_PVWriter.h"

using namespace std;

namespace SNS { namespace PVS { namespace ADARA {


/**
 * \brief Constructor for ADARA_PVWriter class.
 * \param a_streamer - The associate PVStreamer instance.
 * \param a_port - The tcp port thet the ADARA writer will listen on.
 */
ADARA_PVWriter::ADARA_PVWriter( PVStreamer &a_streamer, unsigned short a_port, unsigned long a_heartbeat )
    : PVWriter(a_streamer, ADARA_PROTOCOL ), m_active(true), m_port(a_port), m_heartbeat(a_heartbeat), m_listen_socket(INVALID_SOCKET)
{
    initWinSocket();

    m_socket_listen_thread = new boost::thread( boost::bind(&ADARA_PVWriter::socketListenThreadFunc, this));
    m_pkt_send_thread = new boost::thread( boost::bind(&ADARA_PVWriter::packetSendThreadFunc, this));
}

/**
 * \brief Destructor for ADARA_PVWriter class.
 */
ADARA_PVWriter::~ADARA_PVWriter()
{
    m_active = false;
    closesocket( m_listen_socket );

    boost::unique_lock<boost::recursive_mutex> lock(m_conn_mutex);

    // Dsconnect clients
    if ( connected())
    {
        for ( list<ClientInfo>::iterator ic = m_client_info.begin(); ic != m_client_info.end(); ic++ )
            shutdown( ic->sock, SD_SEND );
    }

    lock.unlock();

    // Wait on threads to exit
    m_socket_listen_thread->join();
    m_pkt_send_thread->join();

    // Delete threads
    delete m_socket_listen_thread;
    delete m_pkt_send_thread;

    WSACleanup();
}

/**
 * \brief Attaches a listener to an ADARA_PVWriter instance.
 * \param a_listener - The IADARAWriterListener instance to attach.
 */
void
ADARA_PVWriter::attachListener( IADARAWriterListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    if ( find(m_listeners.begin(), m_listeners.end(), &a_listener) == m_listeners.end())
    {
        m_listeners.push_back(&a_listener);
        a_listener.listening( m_addr, m_port );
    }
}

/**
 * \brief Detaches a listener from an ADARA_PVWriter instance.
 * \param a_listener - The IADARAWriterListener instance to detach.
 */
void
ADARA_PVWriter::detachListener( IADARAWriterListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    vector<IADARAWriterListener*>::iterator l = find(m_listeners.begin(), m_listeners.end(), &a_listener);
    if ( l != m_listeners.end())
    {
        m_listeners.erase(l);
    }
}

/**
 * \brief Broadcasts a client connection notification to all listeners.
 * \param a_address - Tcp address of the connected client.
 */
void
ADARA_PVWriter::notifyConnect( string &a_address )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    for ( vector<IADARAWriterListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l)
    {
        (*l)->connected( a_address );
    }
}

/**
 * \brief Broadcasts a client disconnection notification to all listeners.
 * \param a_address - Tcp address of the disconnected client.
 */
void
ADARA_PVWriter::notifyDisconnect( string &a_address )
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    for ( vector<IADARAWriterListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l)
    {
        (*l)->disconnected( a_address );
    }
}

/**
 * \brief Thread method for ADARA packet translation and transmission.
 */
void
ADARA_PVWriter::packetSendThreadFunc()
{
    PVStreamPacket *pvs_pkt;
    ADARAPacket     adara_pkt;
    ADARAPacket     heartbeat_pkt;
    bool            timeout_flag = false;
    string          payload;

    heartbeat_pkt.payload_len = 0;
    heartbeat_pkt.format = 0x00400900;
    heartbeat_pkt.nsec = 0;

    while(1)
    {
        pvs_pkt = m_writer_services->getFilledPacket( m_heartbeat, timeout_flag );
        if ( !pvs_pkt )
        {
            if ( timeout_flag )
            {
                // Send a heartbeat packet
                if ( connected())
                {
                    heartbeat_pkt.sec = (unsigned long)time(0) + EPICS_TIME_OFFSET;
                    sendPacket( heartbeat_pkt );
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            if ( connected()) // If connected, translate and send packet
            {
                if ( translate( *pvs_pkt, adara_pkt, payload ))
                {
                    if ( payload.length() )
                    {
                        sendPacket( adara_pkt, &payload );
                        payload.clear();
                    }
                    else
                        sendPacket( adara_pkt, 0 );
                }
            }
            m_writer_services->putFreePacket(pvs_pkt);
        }
    }
}

/**
 * \brief Method to (quickly) determine if any clients are currently connected.
 */
bool
ADARA_PVWriter::connected()
{
    return m_client_info.size() != 0;
}

/**
 * \brief Translates a PV stream packet into an ADARA packet.
 * \param a_pv_pkt - PV stream packet to translate (input).
 * \param a_adara_pkt - ADARA packet to receive translation (output).
 */
bool
ADARA_PVWriter::translate( PVStreamPacket &a_pv_pkt, ADARAPacket &a_adara_pkt, string &a_payload )
{
    a_adara_pkt.sec = a_pv_pkt.time.sec + EPICS_TIME_OFFSET;
    a_adara_pkt.nsec = a_pv_pkt.time.nsec;

    switch ( a_pv_pkt.pkt_type )
    {
    // ADARA does not currently handle devices going inactive, nor individial PVs going active/inactive
    case DeviceActive:
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device_id );
        return true;

    case VarInactive:
        // A var inactive will be translated to a value update with alarm condition for the affected pv
        a_adara_pkt.dev_id          = a_pv_pkt.device_id;
        a_adara_pkt.vvp.var_id      = a_pv_pkt.pv_info->m_id;

        a_adara_pkt.vvp.status      = Comm; // Send a communication error code
        a_adara_pkt.vvp.severity    = Major;

        switch ( a_pv_pkt.pv_info->m_type )
        {
        case PV_ENUM:
        case PV_INT:
            a_adara_pkt.format      = 0x800100;
            a_adara_pkt.payload_len = 16;
            a_adara_pkt.vvp.uval    = 0;
            break;

        case PV_UINT:
            a_adara_pkt.format      = 0x800100;
            a_adara_pkt.payload_len = 16;
            a_adara_pkt.vvp.uval    = 0;
            break;

        case PV_DOUBLE:
            a_adara_pkt.format      = 0x800200;
            a_adara_pkt.payload_len = 20;
            a_adara_pkt.vvp.dval    = 0.0;
            break;
        }
        return true;

    case VarUpdate:
        buildVVP( a_adara_pkt, *a_pv_pkt.pv_info, &a_pv_pkt );
        return true;
    }

    return false;
}

/**
 * \brief Builds an ADARA device descriptor packet
 * \param a_adara_pkt - ADARA packet to receive DDP data.
 * \param a_dev_id - ID of device to be described.
 * \param a_time - EPICS timestamp of device description (activation).
 */
void
ADARA_PVWriter::buildDDP( ADARAPacket &a_adara_pkt, string &a_payload, Identifier a_dev_id )
{
    stringstream sstr;

    a_adara_pkt.format  = 0x800000;
    a_adara_pkt.dev_id  = a_dev_id;

    // Reset payload stringstream

    a_payload.clear();

    // Encode XML

    sstr << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl
            << "  <device xmlns=\"http://public.sns.gov/schema/device.xsd\"" << endl
            << "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endl
            << "  xsi:schemaLocation=\"http://public.sns.gov/schema/device.xsd http://public.sns.gov/schema/device.xsd\">" << endl;

    sstr << "  <device_name>" << m_streamer.getDeviceName( a_dev_id ) << "</device_name>" << endl;

    const vector<const PVInfo*> & pvs = m_streamer.getDevicePVs( a_dev_id );

    // Generate enumeration definitions for enums used by this device only

    vector<const Enum*> enums_in_use;
    enums_in_use.reserve(pvs.size());

    for ( vector<const PVInfo*>::const_iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
    {
        if ( (*ipv)->m_enum )
        {
            if ( find( enums_in_use.begin(), enums_in_use.end(), (*ipv)->m_enum ) == enums_in_use.end())
                enums_in_use.push_back( (*ipv)->m_enum );
        }
    }

    if ( enums_in_use.size())
    {
        const map<Identifier,const Enum*> &enums = m_streamer.getEnums();

        sstr << "  <enumerations>" << endl;
        for ( vector<const Enum*>::const_iterator e = enums_in_use.begin(); e != enums_in_use.end(); ++e )
        {
            const map<int,const string> &elems = (*e)->getMap();

            sstr << "    <enumeration>" << endl;
            sstr << "      <enum_name>enum_" << setw(2) << setfill('0') << (*e)->getID() << "</enum_name>" << endl;
            for ( map<int,const string>::const_iterator iel = elems.begin(); iel != elems.end(); ++iel )
            {
                sstr << "        <enum_element>" << endl;
                sstr << "          <enum_element_name>" << iel->second << "</enum_element_name>" << endl;
                sstr << "          <enum_element_value>" << iel->first << "</enum_element_value>" << endl;
                sstr << "        </enum_element>" << endl;
            }
            sstr << "    </enumeration>" << endl;
        }
        sstr << "  </enumerations>" << endl;
    }

    // Generate process variable definitions

    sstr << "  <process_variables>" << endl;

    for ( vector<const PVInfo*>::const_iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
    {
        sstr << "    <process_variable>" << endl;
        sstr << "      <pv_name>" << (*ipv)->m_name << "</pv_name>" << endl;
        sstr << "      <pv_id>" << (*ipv)->m_id << "</pv_id>" << endl;
        if ( (*ipv)->m_type == PV_ENUM )
            sstr << "      <pv_type>enum_" << setw(2) << setfill('0') << (*ipv)->m_enum->getID() << "</pv_type>" << endl;
        else
            sstr << "      <pv_type>" << getTypeDescriptor((*ipv)->m_type) << "</pv_type>" << endl;
        if ( (*ipv)->m_units.size() )
            sstr << "      <pv_units>" << (*ipv)->m_units << "</pv_units>" << endl;

        if ( (*ipv)->m_hints.length())
        {
            sstr << "      <pv_hint>" << endl;
            sstr << "        " << (*ipv)->m_hints << endl;
            sstr << "      </pv_hint>" << endl;
        }

        sstr << "    </process_variable>" << endl;
    }

    sstr << "  </process_variables>" << endl;
    sstr << "</device>" << endl;

    // Save payload
    // TODO This copies the buffer twice - once to build a temp string from buffer, then into the payload string provided
    a_payload = sstr.str();
    // Copy xml len into packet
    a_adara_pkt.ddp.xml_len = (unsigned long) a_payload.size();

    a_adara_pkt.payload_len = 8 + a_adara_pkt.ddp.xml_len;

    // Round payload length up to nearsest 4 bytes
    int rem = a_adara_pkt.payload_len % 4;
    if ( rem )
    {
        // Pad buffer with nulls
        for ( int i = 0; i < (4-rem); ++i )
            a_payload.push_back('\0');

        // Adjust payload len field
        a_adara_pkt.payload_len += (4 - rem);
    }
}

/**
 * \brief Builds an ADARA variable value packet using either received or last known value
 * \param a_adara_pkt - ADARA packet to receive VVP data.
 * \param a_pv_info - The PVInfo instance of the associated variable.
 * \param a_pv_pkt - The PVStreamPacket to use (optional)
 * \param a_time - EPICS timestamp to use (if a_pv_pkt is not used).
 *
 * This method build a VVP packet for the PV specified by the PVInfo parameter. If
 * a_pv_pkt is provided, is is used for timestamp and variable values; otherwise
 * the a_tim parameter is used and the last known values are extracted from the
 * PVInfo instance.
 */
void
ADARA_PVWriter::buildVVP( ADARAPacket &a_adara_pkt, const PVInfo &a_pv_info, PVStreamPacket *a_pv_pkt )
{
    unsigned short alarms = 0;

    a_adara_pkt.dev_id      = a_pv_info.m_device_id;
    a_adara_pkt.vvp.var_id  = a_pv_info.m_id;

    if ( a_pv_pkt )
        alarms              = a_pv_pkt->alarms;
    else
        alarms              = a_pv_info.m_alarms;


    a_adara_pkt.vvp.status      = None;
    a_adara_pkt.vvp.severity    = NoAlarm;

    if ( alarms )
    {
        // TODO Need to revist/verify these alarm and status mappings
        if ( alarms & PV_COMM_ALARM )
        {
            a_adara_pkt.vvp.status      = Comm;
            a_adara_pkt.vvp.severity    = Major;
        }
        else if ( alarms & ( PV_HW_LIMIT_HI | PV_HW_LIMIT_LO ))
        {
            a_adara_pkt.vvp.status      = HwLimit;
            a_adara_pkt.vvp.severity    = Major;
        }
        else if ( a_pv_info.m_alarms & PV_HW_ALARM_HI )
        {
            a_adara_pkt.vvp.status      = High;
            a_adara_pkt.vvp.severity    = Minor;
        }
        else if ( a_pv_info.m_alarms & PV_HW_ALARM_LO )
        {
            a_adara_pkt.vvp.status      = Low;
            a_adara_pkt.vvp.severity    = Minor;
        }
    }

    switch ( a_pv_info.m_type )
    {
    case PV_ENUM:
    case PV_INT:
        a_adara_pkt.format          = 0x800100;
        a_adara_pkt.payload_len     = 16;
        if ( a_pv_pkt )
            a_adara_pkt.vvp.uval    = (unsigned long)a_pv_pkt->ival;
        else
            a_adara_pkt.vvp.uval    = (unsigned long)a_pv_info.m_ival;
        break;

    case PV_UINT:
        a_adara_pkt.format          = 0x800100;
        a_adara_pkt.payload_len     = 16;
        if ( a_pv_pkt )
            a_adara_pkt.vvp.uval    = a_pv_pkt->uval;
        else
            a_adara_pkt.vvp.uval    = a_pv_info.m_uval;
        break;

    case PV_DOUBLE:
        a_adara_pkt.format          = 0x800200;
        a_adara_pkt.payload_len     = 20;
        if ( a_pv_pkt )
            a_adara_pkt.vvp.dval    = a_pv_pkt->dval;
        else
            a_adara_pkt.vvp.dval    = a_pv_info.m_dval;
        break;
    }
}

/**
 * \brief Sends a source list packet to client socket (paket is empty for PVStreamer)
 * \param a_socket Client socket to send paket over. 
 */
void
ADARA_PVWriter::sendSourceInfo( SOCKET a_socket )
{
    ADARAPacket adara_pkt;

    // Use current time for DDP packets

    adara_pkt.payload_len = 0;
    adara_pkt.format = 0x200;
    adara_pkt.sec = (unsigned long)time(0) + EPICS_TIME_OFFSET;
    adara_pkt.nsec = 0;

    sendPacket( adara_pkt, 0, a_socket );
}

/**
 * \brief Attempts to send DDPs for all active devices to the specified client.
 * \param a_socket - Socket of client to receive DDPs.
 * \return True on success; false otherwise.
 */
bool
ADARA_PVWriter::sendActiveDeviceInfo( SOCKET a_socket )
{
    vector<Identifier> devs;

    m_streamer.getActiveDevices( devs );

    if ( devs.size() )
    {
        ADARAPacket adara_pkt;
        string      payload;

        // Use current time for DDP packets
        adara_pkt.sec = (unsigned long)time(0) + EPICS_TIME_OFFSET;
        adara_pkt.nsec = 0;

        // Send DDPs for real devices
        for ( vector<Identifier>::iterator idev = devs.begin(); idev != devs.end(); ++idev )
        {
            buildDDP( adara_pkt, payload, *idev );
            sendPacket( adara_pkt, &payload, a_socket );
        }

        vector<const PVInfo*>::const_iterator iv;

        // Send value updates for real devices
        for ( vector<Identifier>::iterator idev = devs.begin(); idev != devs.end(); ++idev )
        {
            try
            {
                const vector<const PVInfo*> &vars = m_streamer.getDevicePVs( *idev );
                for ( iv = vars.begin(); iv != vars.end(); ++iv )
                {
                    buildVVP( adara_pkt, **iv, 0 );
                    sendPacket( adara_pkt, 0, a_socket );
                }
            }
            catch(...)
            {
                // Device was undfined... just go to next
            }
        }

        return true;
    }

    return false;
}

/**
 * \brief Attempts to send an ADARA packet to the specified client.
 * \param a_adara_pkt - ADARA packet to send
 * \param a_socket - Socket of client to receive packet.
 */
void
ADARA_PVWriter::sendPacket( ADARAPacket & a_adara_pkt, string *a_payload, SOCKET a_socket )
{
    list<ClientInfo>::iterator ic;
    int rc;
    int len1 = (int)a_adara_pkt.payload_len + 16;

    if ( a_payload )
        len1 -= (int)a_payload->length();

    if ( a_socket != INVALID_SOCKET ) // Send to a specific clients
    {
        // Send packet structure
        rc = send( a_socket, (char*)&a_adara_pkt, len1, 0 );

        // Send optional payload
        if ( a_payload && a_payload->length() && rc != SOCKET_ERROR )
            rc = send( a_socket, a_payload->c_str(), (int)a_payload->length(), 0 );

        if ( rc == SOCKET_ERROR )
        {
            LOG_ERROR( "Socket send() failed. rc: " << rc );

            boost::lock_guard<boost::recursive_mutex> lock(m_conn_mutex);

            // Disconnect from client
            closesocket( a_socket );

            for ( ic = m_client_info.begin(); ic != m_client_info.end(); ++ic )
            {
                if ( ic->sock == a_socket )
                {
                    notifyDisconnect( ic->addr );
                    m_client_info.erase(ic);
                    break;
                }
            }
        }
    }
    else
    {
        // Send to ALL clients
        boost::lock_guard<boost::recursive_mutex> lock(m_conn_mutex);

        for ( ic = m_client_info.begin(); ic != m_client_info.end(); )
        {
            rc = send( ic->sock, (char*)&a_adara_pkt, len1, 0 );

            // Send optional payload
            if ( a_payload && a_payload->length() && rc != SOCKET_ERROR )
                rc = send( ic->sock, a_payload->c_str(), (int)a_payload->length(), 0 );

            if ( rc == SOCKET_ERROR )
            {
                LOG_ERROR( "Socket send() failed. rc: " << rc );

                // Disconnect from client
                closesocket( ic->sock );
                notifyDisconnect( ic->addr );
                ic = m_client_info.erase(ic);
            }
            else
                ic++;
        }
    }
}

/**
 * \brief Converts a DataType value into an ADARA DDP variable type (xml).
 * \param a_type - Data type to convert.
 * \return Text (const char*) of data type.
 */
const char *
ADARA_PVWriter::getTypeDescriptor( DataType a_type ) const
{
    switch ( a_type )
    {
    case PV_UINT:
    case PV_INT:
    case PV_ENUM:
        return "integer";
    case PV_DOUBLE:
        return "double";
    }

    return "unknown";
}

/**
 * \brief Socket listener thread for ADARA service.
 */
void
ADARA_PVWriter::socketListenThreadFunc()
{
    struct sockaddr client_addr;
    int             client_addr_len = sizeof(struct sockaddr);
    ClientInfo      info;

    while(1)
    {
        info.sock = accept( m_listen_socket, &client_addr, &client_addr_len );
        if ( info.sock == INVALID_SOCKET )
        {
            if ( m_active )
            {
                LOG_ERROR( "accept() failed. rc: " << WSAGetLastError() );
            }
            else
                break;
        }
        else
        {
            // New client processing

            info.ddp = false;
            info.addr = inet_ntoa( ((struct sockaddr_in &)client_addr).sin_addr );
            LOG_INFO( "ADARA client connected from " << info.addr );

            boost::unique_lock<boost::recursive_mutex> lock(m_conn_mutex);
            m_client_info.push_back( info );
            lock.unlock();

            notifyConnect( info.addr );

            // Send source information packet first
            sendSourceInfo( info.sock );

            // need to send it all currently active devies (if any)
            info.ddp = sendActiveDeviceInfo( info.sock );
        }
    }
}

/**
 * \brief Initialized Windows sockets library and configures listener socket.
 * \return True on success; false otherwise.
 */
void
ADARA_PVWriter::initWinSocket()
{
    int rc;
    struct addrinfo *result = 0, hints;
    WSADATA wsadata;

    rc = WSAStartup( 0x101, &wsadata );
    if ( rc )
        EXC( EC_WINDOWS_ERROR, "WSAStartup failed." );


    memset( &hints, 0, sizeof( hints ));
    hints.ai_family     = AF_INET;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;
    hints.ai_flags      = AI_PASSIVE;

    char port_str[20];
    sprintf_s( port_str,20, "%u", m_port );

    try
    {
        // Resolve the local address and port to be used by the server
        rc = getaddrinfo( 0, port_str, &hints, &result );
        if ( rc )
        {
            LOG_ERROR( "getaddrinfo() failed. rc: " << rc );
            EXC( EC_WINDOWS_ERROR, "getaddrinfo failed." );
        }

        m_listen_socket = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
        if ( m_listen_socket == INVALID_SOCKET )
        {
            LOG_ERROR( "socket() failed. rc: " << WSAGetLastError() );
            EXC( EC_WINDOWS_ERROR, "socket failed." );
        }

        rc = bind( m_listen_socket, result->ai_addr, (int)result->ai_addrlen );
        if ( rc == SOCKET_ERROR )
        {
            LOG_ERROR( "bind() failed. rc: " << WSAGetLastError() );
            EXC( EC_WINDOWS_ERROR, "bind failed." );
        }

        if ( listen( m_listen_socket, 5 /*max connections*/ ) == SOCKET_ERROR )
        {
            LOG_ERROR( "listen() failed. rc: " << WSAGetLastError() );
            closesocket( m_listen_socket );
            EXC( EC_WINDOWS_ERROR, "listen failed." );
        }

        // Get Server IP address (not sure why the above does not set it correctly in result
        PHOSTENT info = gethostbyname( 0 );
        if ( info && info->h_addrtype == AF_INET )
        {
            struct in_addr addr;
            if ( info->h_addr_list[0] )
            {
                addr.s_addr = *(unsigned long*)info->h_addr_list[0];
                m_addr = inet_ntoa( addr );
            }
        }

        LOG_INFO( "ADARA pv streaming service listening at " << m_addr << ":" << m_port );

        freeaddrinfo( result );
        result = 0;
    }
    catch( TraceException &e )
    {
        if ( result )
            freeaddrinfo( result );

        e.addContext( "WinSock initialize failed." );

        throw;
    }
    catch(...)
    {
        if ( result )
            freeaddrinfo( result );

        EXC( EC_UNKOWN_ERROR, "WinSock initialize failed." );
    }
}


}}}



