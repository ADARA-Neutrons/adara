#include "stdafx.h"

#include <sstream>

#include "ADARA_PVWriter.h"

using namespace std;

namespace SNS { namespace PVS { namespace ADARA {

// TODO Need a way to configure port nuber

ADARA_PVWriter::ADARA_PVWriter( PVStreamer &a_streamer, unsigned short a_port, size_t a_adara_buffer_size )
    : PVWriter(a_streamer, ADARA_PROTOCOL ), m_port(a_port), m_listen_socket(INVALID_SOCKET)
{
    initWinSocket();

    m_socket_listen_thread = new boost::thread( boost::bind(&ADARA_PVWriter::socketListenThreadFunc, this));
    m_pkt_send_thread = new boost::thread( boost::bind(&ADARA_PVWriter::packetSendThreadFunc, this));
}

ADARA_PVWriter::~ADARA_PVWriter()
{
    //TODO Wait for listen and send threads to exit?

    // TODO need to protect connections list
    if ( connected())
        for ( list<SOCKET>::iterator isock = m_client_sockets.begin(); isock != m_client_sockets.end(); isock++ )
            shutdown( *isock, SD_SEND );

    WSACleanup();
}


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


void
ADARA_PVWriter::notifyConnect()
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    for ( vector<IADARAWriterListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l)
    {
        (*l)->connected();
    }
}

void
ADARA_PVWriter::notifyDisconnect()
{
    boost::lock_guard<boost::mutex> lock(m_list_mutex);
    for ( vector<IADARAWriterListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l)
    {
        (*l)->disconnected();
    }
}

void
ADARA_PVWriter::packetSendThreadFunc()
{
    PVStreamPacket *pvs_pkt;
    ADARAPacket     adara_pkt;

    while(1)
    {
        pvs_pkt = m_writer_services->getFilledPacket();

        // If connected, translate and send packet
        if ( connected())
        {
            if ( translate( *pvs_pkt, adara_pkt ))
            {
                sendPacket( adara_pkt );
            }
        }

        m_writer_services->putFreePacket(pvs_pkt);
    }
}

bool
ADARA_PVWriter::connected()
{
    return m_client_sockets.size() != 0;
}

bool
ADARA_PVWriter::translate( PVStreamPacket &a_pv_pkt, ADARAPacket &a_adara_pkt )
{
    a_adara_pkt.sec = a_pv_pkt.time.sec + EPICS_TIME_OFFSET;
    a_adara_pkt.nsec = a_pv_pkt.time.nsec;

    switch ( a_pv_pkt.pkt_type )
    {
    // ADARA does not currently handle devices going inactive, nor individial PVs going active/inactive
    case DeviceActive:
        buildDDP( a_adara_pkt, a_pv_pkt.device_id, a_pv_pkt.time );
        return true;

    case VarStatusUpdate:
        return false;

    case VarValueUpdate:
        a_adara_pkt.dev_id          = a_pv_pkt.device_id;
        a_adara_pkt.vvp.var_id      = a_pv_pkt.pv_info->m_id;

        if ( a_pv_pkt.pv_info->m_alarms )
        {
            // TODO Need to revist these alarm and status mappings

            if ( a_pv_pkt.pv_info->m_alarms & ( PV_HW_LIMIT_HI | PV_HW_LIMIT_LO ))
            {
                a_adara_pkt.vvp.status      = HwLimit;
                a_adara_pkt.vvp.severity    = Major;
            }
            else if ( a_pv_pkt.pv_info->m_alarms & PV_HW_ALARM_HI )
            {
                a_adara_pkt.vvp.status      = High;
                a_adara_pkt.vvp.severity    = Minor;
            }
            else if ( a_pv_pkt.pv_info->m_alarms & PV_HW_ALARM_LO )
            {
                a_adara_pkt.vvp.status      = Low;
                a_adara_pkt.vvp.severity    = Minor;
            }
        }
        else
        {
            a_adara_pkt.vvp.status      = None;
            a_adara_pkt.vvp.severity    = NoAlarm;
        }

        switch ( a_pv_pkt.pv_info->m_type )
        {
        case PV_ENUM:
        case PV_INT:
            a_adara_pkt.format      = 0x800100;
            a_adara_pkt.payload_len = 16;
            a_adara_pkt.vvp.uval    = (unsigned long)a_pv_pkt.ival;
            break;

        case PV_UINT:
            a_adara_pkt.format      = 0x800100;
            a_adara_pkt.payload_len = 16;
            a_adara_pkt.vvp.uval    = a_pv_pkt.uval;
            break;

        case PV_DOUBLE:
            a_adara_pkt.format      = 0x800200;
            a_adara_pkt.payload_len = 20;
            a_adara_pkt.vvp.dval    = a_pv_pkt.dval;
            break;
        }
        return true;
    }

    return false;
}

void
ADARA_PVWriter::buildDDP( ADARAPacket &a_adara_pkt, Identifier dev_id, Timestamp a_time )
{
    /* For debugging MS padding - Yay!!!
    int sz1 = sizeof(ADARAPacket);
    int off1 = offsetof(ADARAPacket,payload_len);
    int off2 = offsetof(ADARAPacket,format);
    int off3 = offsetof(ADARAPacket,sec);
    int off4 = offsetof(ADARAPacket,nsec);
    int off5 = offsetof(ADARAPacket,dev_id);
    int off6 = offsetof(ADARAPacket,ddp.xml_len);
    int off7 = offsetof(ADARAPacket,ddp.xml);
    */

    a_adara_pkt.format  = 0x800000;
    a_adara_pkt.dev_id  = dev_id;
    a_adara_pkt.sec     = a_time.sec;
    a_adara_pkt.nsec    = a_time.nsec;

    stringstream   sstr;

    // Encode XML

    sstr << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl
            << "  <device xmlns=\"http://public.sns.gov/schema/device.xsd\"" << endl
            << "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endl
            << "  xsi:schemaLocation=\"http://public.sns.gov/schema/device.xsd http://public.sns.gov/schema/device.xsd\">" << endl;

    sstr << "  <device_name>" << (dev_id?m_streamer.getDeviceName( dev_id ):"VirtualDevice0") << "</device_name>" << endl;

    if ( dev_id )
    {
        const vector<const PVInfo*> & pvs = m_streamer.getDevicePVs( dev_id );
        sstr << "  <process_variables>" << endl;

        for ( vector<const PVInfo*>::const_iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
        {
            sstr << "    <process_variable>" << endl;
            sstr << "      <pv_name>" << (*ipv)->m_name << "</pv_name>" << endl;
            sstr << "      <pv_id>" << (*ipv)->m_id << "</pv_id>" << endl;
            sstr << "      <pv_type>" << getTypeDescriptor((*ipv)->m_type) << "</pv_type>" << endl;
            if ( (*ipv)->m_units.size() )
                sstr << "      <pv_units>" << (*ipv)->m_units << "</pv_units>" << endl;
            sstr << "    </process_variable>" << endl;
        }

        sstr << "  </process_variables>" << endl;
    }
    else
    {
        sstr << "  <device_description>Virtual device for sharing enumerations accross all devices.</device_description>" << endl;
        const map<Identifier,const Enum*> &enums = m_streamer.getEnums();
        
        if ( enums.size())
        {
            sstr << "  <enumerations>" << endl;
            for ( map<Identifier,const Enum*>::const_iterator ie = enums.begin(); ie != enums.end(); ++ie )
            {
                const map<int,const string> &elems = ie->second->getMap();

                sstr << "    <enumeration>" << endl;
                sstr << "      <enum_name>enum_" << setw(2) << setfill('0') << ie->second->getID() << "</enum_name>" << endl;
                for ( map<int,const string>::const_iterator iel = elems.begin(); iel != elems.end(); ++iel )
                {
                    sstr << "      <enum_element>" << endl;
                    sstr << "        <enum_element_name>" << iel->second << "</enum_element_name>" << endl;
                    sstr << "        <enum_element_value>" << iel->first << "</enum_element_value>" << endl;
                    sstr << "      </enum_element>" << endl;
                }
                sstr << "    </enumeration>" << endl;
            }
            sstr << "  </enumerations>" << endl;
        }
    }

    sstr << "</device>" << endl;

    // Copy xml into packet (will truncate if too long)
    a_adara_pkt.ddp.xml_len = (unsigned long) sstr.str().size();
    memcpy( a_adara_pkt.ddp.xml, sstr.str().c_str(), min(a_adara_pkt.ddp.xml_len,MAX_XML_LEN) );
    a_adara_pkt.ddp.xml[min(a_adara_pkt.ddp.xml_len,MAX_XML_LEN-1)] = 0;

    a_adara_pkt.payload_len = 8 + a_adara_pkt.ddp.xml_len + 1; // Add 1 for terminating null
}

void
ADARA_PVWriter::sendActiveDeviceInfo( SOCKET a_socket )
{
    ADARAPacket adara_pkt;
    vector<Identifier> devs;
    list<SOCKET>::iterator isock;
    Timestamp ts;

    ts.sec = (unsigned long)time(0);

    // Virtual device
    buildDDP( adara_pkt, 0, ts );
    sendPacket( adara_pkt, a_socket );

    m_streamer.getActiveDevices( devs );

    for ( vector<Identifier>::iterator idev = devs.begin(); idev != devs.end(); ++idev )
    {
        buildDDP( adara_pkt, *idev, ts );
        sendPacket( adara_pkt, a_socket );
    }
}


void
ADARA_PVWriter::sendPacket( ADARAPacket & a_adara_pkt, SOCKET a_socket )
{
    list<SOCKET>::iterator isock;
    int rc;

    if ( a_socket != INVALID_SOCKET )
    {
        // Send to a specific clients
        rc = send( a_socket, (char*)&a_adara_pkt, a_adara_pkt.payload_len + 16, 0 );
        if ( rc == SOCKET_ERROR )
        {
            // TODO Log error
            boost::lock_guard<boost::mutex> lock(m_conn_mutex);

            // Disconnect from client
            closesocket( a_socket );

            isock = find(m_client_sockets.begin(),m_client_sockets.end(),a_socket);
            if ( isock != m_client_sockets.end())
                m_client_sockets.erase(isock);

            notifyDisconnect();
        }
    }
    else
    {
        // Send to ALL clients
        boost::lock_guard<boost::mutex> lock(m_conn_mutex);

        for ( isock = m_client_sockets.begin(); isock != m_client_sockets.end(); )
        {
            rc = send( *isock, (char*)&a_adara_pkt, a_adara_pkt.payload_len + 16, 0 );
            if ( rc == SOCKET_ERROR )
            {
                // Disconnect from client
                closesocket( *isock );
                isock = m_client_sockets.erase(isock);
                notifyDisconnect();
            }
            else
                isock++;
        }
    }
}

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

// ---------- WINDOWS-Specific Sockets ----------------------------------------

bool
ADARA_PVWriter::initWinSocket()
{
    int rc;
    struct addrinfo *result = 0, *ptr = 0, hints;
    WSADATA wsadata;

    rc = WSAStartup( 0x101, &wsadata );
    if ( rc )
        throw -1;


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
            throw -2;

        m_listen_socket = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
        if ( m_listen_socket == INVALID_SOCKET )
            throw -3;

        rc = bind( m_listen_socket, result->ai_addr, (int)result->ai_addrlen );
        if ( rc == SOCKET_ERROR )
            throw -4;

        if ( listen( m_listen_socket, 5 /*max connections*/ ) == SOCKET_ERROR )
        {
            closesocket( m_listen_socket );
            throw -5;
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

        freeaddrinfo( result );
        result = 0;
    }
    catch(...)
    {
        // TODO Log error

        if ( result )
            freeaddrinfo( result );

        return false;
    }

    return true;
}


void
ADARA_PVWriter::socketListenThreadFunc()
{
    SOCKET  client_socket;
    while(1)
    {
        // TODO should get client address and log it
        client_socket = accept( m_listen_socket, 0, 0 );
        if ( client_socket == INVALID_SOCKET )
        {
            // TODO Log error
            // TODO exit thread if really bad error?
        }
        else
        {
            // New client - need to send it all currently active devies
            sendActiveDeviceInfo( client_socket );

            boost::unique_lock<boost::mutex> lock(m_conn_mutex);
            m_client_sockets.push_back( client_socket );
            lock.unlock();

            notifyConnect();
        }
    }
}


}}}



