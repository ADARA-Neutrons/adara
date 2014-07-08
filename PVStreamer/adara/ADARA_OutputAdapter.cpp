#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <iomanip>
#include <boost/lexical_cast.hpp>


#include "ADARA_OutputAdapter.h"
#include "ADARA.h"
#include "TraceException.h"

using namespace std;
using namespace PVS::ADARA;

/// Defines the difference between EPICS and Posix timestamp values
#define EPICS_TIME_OFFSET 631152000

namespace PVS {
namespace ADARA {

/**
 * @brief Constructor for ADARA OutputAdapter
 * @param a_stream_serv - Owning StreamService instance
 * @param a_port - TCP port to listen on
 * @param a_heartbeat - ADARA heartbeat period in milliseconds
 *
 * This constructor produces an ADARA OutputAdapter owned by the specified StreamService instance. The
 * StreamService will delete this OutputAdapter when it is destroyed. The ADARA OuputAdapter class
 * starts two threads: one for handling in-coming TCP client connection requests, and another for
 * processing, translating, and broadcasting packets from the internal data stream.
 */
OutputAdapter::OutputAdapter( StreamService &a_stream_serv, unsigned short a_port, unsigned long a_heartbeat )
    : IOutputAdapter(a_stream_serv), m_active(true), m_socket_listen_thread(0), m_stream_proc_thread(0), m_port(a_port),
      m_heartbeat(a_heartbeat), m_listen_socket(-1)
{
    initSockets();

    m_socket_listen_thread = new boost::thread( boost::bind( &OutputAdapter::socketListenThread, this ));
    m_stream_proc_thread = new boost::thread( boost::bind( &OutputAdapter::streamProcessingThread, this ));
}


/**
 * @brief Destructor for the ADARA OutputAdapter class.
 *
 * This destructor should only be called by the owning StreamService instance, and before doing so,
 * stream queues must be deactivated.
 */
OutputAdapter::~OutputAdapter()
{
    // The owning StreamService instance MUST deactivate() the queues before
    // destroying adapters.

    m_active = false;

    for ( list<ClientInfo>::iterator ic = m_client_info.begin(); ic != m_client_info.end(); ++ic )
    {
        shutdown( ic->socket, SHUT_RDWR );
        close( ic->socket );
    }

    if ( m_listen_socket > -1 )
    {
        shutdown( m_listen_socket, SHUT_RDWR );
        close( m_listen_socket );
    }

    m_stream_proc_thread->join();
    m_socket_listen_thread->join();
}



/** \brief Thread method for ADARA packet translation and transmission.
  *
  * This method runs a background thread that receives filled stream packets
  * from the internal data stream, translates them into ADARA protocol, then
  * sends them to all connected ADARA clients. Whenever input packets are not
  * received for a configured interval, ADARA heartbeat packets are
  * transmitted as required by the ADARA protocol. When no clients are
  * connected, input packets are simply returned to the stream unused.
  */
void
OutputAdapter::streamProcessingThread()
{
    StreamPacket   *pvs_pkt;
    OutPacket       adara_pkt;
    OutPacket       heartbeat_pkt;
    bool            timeout_flag = false;
    string          payload;

    heartbeat_pkt.payload_len = 0;
    heartbeat_pkt.format = ::ADARA::PacketType::HEARTBEAT_V0;
    heartbeat_pkt.nsec = 0;

    while ( 1 )
    {
        pvs_pkt = m_srteam_api->getFilledPacket( m_heartbeat, timeout_flag );
        if ( !pvs_pkt )
        {
            if ( timeout_flag )
            {
                // Send a heartbeat packet
                if ( connected() )
                {
                    heartbeat_pkt.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
                    sendPacket( heartbeat_pkt, 0 );
                }
            }
            else
            {
                // A null packet w/o timeout means queues have been deactivated and we should exit
                break;
            }
        }
        else
        {
            // Update internal state data (regardless of connection status)
            switch ( pvs_pkt->type )
            {
            case DeviceDefined:
                defineDevice( pvs_pkt->device );
                break;

            case DeviceRedefined:
                redefineDevice( pvs_pkt->device, pvs_pkt->old_device );
                break;

            case DeviceUndefined:
                undefineDevice( pvs_pkt->device );
                break;

            case VariableUpdate:
                updatePV( pvs_pkt->pv, pvs_pkt->state );
                break;
            }

            // If connected, translate and send packet(s)
            if ( connected())
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

            m_srteam_api->putFreePacket( pvs_pkt );
        }
    }
}


/** \brief Translates a stream packet into an ADARA packet.
  * \param a_pv_pkt - stream packet to translate (input).
  * \param a_adara_pkt - ADARA packet to receive translation (output).
  */
bool
OutputAdapter::translate( StreamPacket &a_pv_pkt, OutPacket &a_adara_pkt, string &a_payload )
{
    switch ( a_pv_pkt.type )
    {
    case DeviceDefined:
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device );
        return true;

    case DeviceRedefined:
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device );
        return true;

    case DeviceUndefined:
        // Nothing to do since ADARA does not have a device undefined packet
        return false;

    case VariableUpdate:
        buildVVP( a_adara_pkt, a_pv_pkt.pv, a_pv_pkt.state, a_payload );
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
OutputAdapter::buildDDP( OutPacket &a_adara_pkt, string &a_payload, DeviceRecordPtr a_device )
{
    stringstream sstr;

    a_adara_pkt.format  = ::ADARA::PacketType::DEVICE_DESC_V0;
    a_adara_pkt.sec     = (uint32_t)time(0) - EPICS_TIME_OFFSET;;
    a_adara_pkt.nsec    = 0;
    a_adara_pkt.dev_id  = a_device->m_id;

    // Reset payload stringstream

    a_payload.clear();

    // Encode XML

    sstr << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl
            << "  <device xmlns=\"http://public.sns.gov/schema/device.xsd\"" << endl
            << "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endl
            << "  xsi:schemaLocation=\"http://public.sns.gov/schema/device.xsd http://public.sns.gov/schema/device.xsd\">" << endl;

    sstr << "  <device_name>" << a_device->m_name << "</device_name>" << endl;

    // Generate enumeration definitions for enums used by this device only

    if ( a_device->m_enums.size() )
    {
        sstr << "  <enumerations>" << endl;
        unsigned short id = 1;
        for ( vector<EnumDescriptor*>::const_iterator e = a_device->m_enums.begin(); e != a_device->m_enums.end(); ++e, ++id )
        {
            sstr << "    <enumeration>" << endl;
            sstr << "      <enum_name>enum_" << setw(2) << setfill('0') << id << "</enum_name>" << endl;
            for ( map<int32_t,std::string>::const_iterator iel = (*e)->m_values.begin(); iel != (*e)->m_values.end(); ++iel )
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

    for ( vector<PVDescriptor*>::const_iterator ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
    {
        sstr << "    <process_variable>" << endl;
        sstr << "      <pv_name>" << (*ipv)->m_name << "</pv_name>" << endl;
        sstr << "      <pv_id>" << (*ipv)->m_id << "</pv_id>" << endl;
        if ( (*ipv)->m_type == PV_ENUM )
            sstr << "      <pv_type>enum_" << setw(2) << setfill('0') << (*ipv)->m_enum->m_id << "</pv_type>" << endl;
        else
            sstr << "      <pv_type>" << getPVTypeXML((*ipv)->m_type) << "</pv_type>" << endl;
        if ( (*ipv)->m_units.size() )
            sstr << "      <pv_units>" << (*ipv)->m_units << "</pv_units>" << endl;

        sstr << "    </process_variable>" << endl;
    }

    sstr << "  </process_variables>" << endl;
    sstr << "</device>" << endl;

    // Save payload
    a_payload = sstr.str();
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


/** \brief Builds an ADARA variable value packet
  * \param a_adara_pkt - ADARA packet to receive VVP data.
  * \param a_device - The DeviceDescriptor instance of the associated variable.
  * \param a_pv - The PVDescriptor instance of the associated variable.
  * \param a_time - Unix-epoch timestamp.
  *
  * This method build a VVP packet for the PV specified by the parameters.
  */
void
OutputAdapter::buildVVP( OutPacket &a_adara_pkt, PVDescriptor *a_pv, PVState a_state, string &a_payload )
{
    a_payload.clear();

    a_adara_pkt.dev_id          = a_pv->m_device->m_id;
    a_adara_pkt.vvp.var_id      = a_pv->m_id;
    a_adara_pkt.sec             = a_state.m_time.sec;
    a_adara_pkt.nsec            = a_state.m_time.nsec;

    a_adara_pkt.vvp.status      = a_state.m_status;
    a_adara_pkt.vvp.severity    = a_state.m_severity;

    switch ( a_pv->m_type )
    {
    case PV_ENUM:
    case PV_INT:
        a_adara_pkt.format          = ::ADARA::PacketType::VAR_VALUE_U32_V0;     // TODO ADARA protocol doesn't support signed ints
        a_adara_pkt.payload_len     = 16;
        a_adara_pkt.vvp.uval        = a_state.m_int_val;
        break;

    case PV_UINT:
        a_adara_pkt.format          = ::ADARA::PacketType::VAR_VALUE_U32_V0;
        a_adara_pkt.payload_len     = 16;
        a_adara_pkt.vvp.uval        = a_state.m_uint_val;
        break;

    case PV_REAL:
        a_adara_pkt.format          = ::ADARA::PacketType::VAR_VALUE_DOUBLE_V0;
        a_adara_pkt.payload_len     = 20;
        a_adara_pkt.vvp.dval        = a_state.m_real_val;
        break;

    case PV_STR:
        a_adara_pkt.format          = ::ADARA::PacketType::VAR_VALUE_STRING_V0;
        a_adara_pkt.payload_len     = 16 + a_state.m_str_val.size();
        a_adara_pkt.vvp_str.str_len = a_state.m_str_val.size();

        // Set payload
        a_payload = a_state.m_str_val;

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
        break;
    }
}




/** \brief Converts a PVType value into an ADARA DDP variable type (xml tag).
  * \param a_type - PVType to convert.
  * \return Text (const char*) of data type.
  */
const char *
OutputAdapter::getPVTypeXML( PVType a_type ) const
{
    switch ( a_type )
    {
    case PV_UINT:
    case PV_INT:
    case PV_ENUM:
        return "integer";
    case PV_REAL:
        return "double";
    case PV_STR:
        return "string";
    }

    return "unknown";
}


/**
 * @brief Updates internal state data for a newly defined device
 * @param a_device - Newly defined device
 */
void
OutputAdapter::defineDevice( DeviceRecordPtr a_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Add this device to configured device list
    m_devices.insert( a_device );

    // Insert new PV state entries with disconnected status
    for ( vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
        m_pv_state[*ipv] = PVState();
}


/**
 * @brief Processes device redefinition internalt stream packets
 * @param a_device - Reference to new device definition
 * @param a_old_device - Reference to old device definition
 *
 * This method updates internal state due to differences between the old and new definitions of the
 * specified device. This method does NOT emit any ADARA packets, it only updates internal state. Any
 * PVs that are in-common between the new and old definitions will have their last-known state transfered
 * to the new PVDescriptor objects. Any PVs that have been dropped are moved to a "garbage" container
 * where they will be further processed and eventually flushed.
 */
void
OutputAdapter::redefineDevice( DeviceRecordPtr a_device, DeviceRecordPtr a_old_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Add/remove only changed PV state entries
    map<PVDescriptor*,PVState>::iterator old_state;
    PVDescriptor *old_pv;
    bool found;

    // Transfer last-known PVState to new state entries (keyed on new PVDescriptor instances)
    vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin();
    for ( ; ipv != a_device->m_pvs.end(); ++ipv )
    {
        found = false;
        old_pv = a_old_device->getPvByName( (*ipv)->m_name );
        if ( old_pv )
        {
            old_state = m_pv_state.find( old_pv );
            if ( old_state != m_pv_state.end())
            {
                m_pv_state[*ipv] = old_state->second;
                found = true;
            }
        }

        // If this is a brand new PV, set it's state to undefined/invalid (until we get the first value from it)
        if ( !found )
            m_pv_state[*ipv] = PVState( ::ADARA::VariableStatus::NOT_REPORTED, ::ADARA::VariableSeverity::INVALID );
    }

    // Add "new" device to configured device list
    m_devices.insert( a_device );

    // Remove old PV state entries
    undefineDevice( a_old_device );
}


/**
 * @brief Process device undefined internal stream packet
 * @param a_device - Reference to device being undefined
 * @param a_undefine_pvs - Flag indicating if member PVs should be placed in undef_pvs set
 *
 * This method updates internal state due to the removal of the specified device. This method does NOT
 * emit any ADARA packets, it only updates internal state. If the a_undefine_pvs flag is set, all PVs
 * of the device will be placed in a "garbage" container where they will be further processed and
 * eventually flushed.
 */
void
OutputAdapter::undefineDevice( DeviceRecordPtr a_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Remove all pv state entries associated with device
    map<PVDescriptor*,PVState>::iterator ipv_state;

    for ( vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
    {
        ipv_state = m_pv_state.find( *ipv );
        if ( ipv_state != m_pv_state.end())
            m_pv_state.erase( ipv_state );
    }

    set<DeviceRecordPtr>::iterator idev = m_devices.find( a_device );
    if ( idev != m_devices.end())
        m_devices.erase( idev );
}


void
OutputAdapter::updatePV( PVDescriptor *a_pv, PVState a_state )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<PVDescriptor*,PVState>::iterator ipv_state = m_pv_state.find( a_pv );
    if ( ipv_state != m_pv_state.end())
    {
        ipv_state->second = a_state;
    }
}


void
OutputAdapter::initSockets()
{
    try
    {
        int rc;
        struct addrinfo *result = 0;
        struct addrinfo hints;

        memset( &hints, 0, sizeof( hints ));
        hints.ai_family     = AF_INET;
        hints.ai_socktype   = SOCK_STREAM;
        hints.ai_protocol   = IPPROTO_TCP;
        hints.ai_flags      = AI_PASSIVE;

        string port_str = boost::lexical_cast<string>( m_port );

        // Resolve the local address and port to be used by the server
        rc = getaddrinfo( 0, port_str.c_str(), &hints, &result );
        if ( rc )
            EXCEPT( EC_SOCKET_ERROR, strerror( errno ));

        m_listen_socket = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
        if ( m_listen_socket < 0 )
            EXCEPT( EC_SOCKET_ERROR, strerror( errno ));

        int yes = 1;
        if ( setsockopt( m_listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
            EXCEPT( EC_SOCKET_ERROR, strerror( errno ));

        rc = bind( m_listen_socket, result->ai_addr, (int)result->ai_addrlen );
        if ( rc < 0 )
            EXCEPT( EC_SOCKET_ERROR, strerror( errno ));

        if ( listen( m_listen_socket, 5 /*max connections*/ ) < 0 )
        {
            close( m_listen_socket );
            EXCEPT( EC_SOCKET_ERROR, strerror( errno ));
        }

        // Get Server IP address (not sure why the above does not set it correctly in result
        struct hostent *info = gethostbyname( "localhost" );
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
    catch ( TraceException &e )
    {
        e.addContext( "Unable to configure ADARA tcp socket (port " + boost::lexical_cast<string>(m_port) + ")" );
        throw e;
    }
    catch (...)
    {
        EXCEPT( EC_SOCKET_ERROR, "Unknown exception in initSockets()." );
    }
}


/** \brief Method to (quickly) determine if any clients are currently connected.
  */
bool
OutputAdapter::connected()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_client_info.size() != 0;
}


void
OutputAdapter::socketListenThread()
{
    struct sockaddr client_addr;
    socklen_t       client_addr_len = sizeof(struct sockaddr);
    ClientInfo      info;

    while(1)
    {
        info.socket = accept( m_listen_socket, &client_addr, &client_addr_len );
        if ( info.socket < 0 )
        {
            if ( !m_active )
                break;
        }
        else
        {
            // New client - setup client info
            info.addr = inet_ntoa( ((struct sockaddr_in &)client_addr).sin_addr );

            boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

            m_client_info.push_back( info );

            // Send source information packet first
            sendSourceInfo( info.socket );

            // need to send it all currently active devies (if any)
            sendCurrentData( info.socket );
        }
    }
}


/** \brief Sends DDPs for all configured devices to the specified client.
  * \param a_socket - Socket of client to receive DDPs.
  */
void
OutputAdapter::sendCurrentData( int a_socket )
{
    OutPacket   adara_pkt;
    string      payload;

    // Use current time for DDP packets
    adara_pkt.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
    adara_pkt.nsec = 0;

    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Send DDPs for real devices
    for ( set<DeviceRecordPtr>::iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
    {
        buildDDP( adara_pkt, payload, *idev );
        sendPacket( adara_pkt, &payload, a_socket );
    }

    // Send value updates for configured devices
    for ( map<PVDescriptor*,PVState>::iterator ipv = m_pv_state.begin(); ipv != m_pv_state.end(); ++ipv )
    {
        buildVVP( adara_pkt, ipv->first, ipv->second, payload );
        if ( payload.size() )
            sendPacket( adara_pkt, &payload, a_socket );
        else
            sendPacket( adara_pkt, 0, a_socket );
    }
}


/** \brief Sends a source list packet to client socket (paket is empty for PVStreamer)
  * \param a_socket Client socket to send paket over.
  */
void
OutputAdapter::sendSourceInfo( int a_socket )
{
    OutPacket adara_pkt;

    adara_pkt.payload_len = 0;
    adara_pkt.format = ::ADARA::PacketType::SOURCE_LIST_V0;
    adara_pkt.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
    adara_pkt.nsec = 0;

    sendPacket( adara_pkt, 0, a_socket );
}


void
OutputAdapter::sendPacket( OutPacket &a_adara_pkt, std::string *a_payload, int a_socket )
{
    bool res;
    uint32_t len = (int)a_adara_pkt.payload_len + 16;

    if ( a_payload )
        len -= (int)a_payload->length();

    if ( a_socket > -1 ) // Send to a specific connection
    {
        res = send( a_socket, (char *)&a_adara_pkt, len );

        // Send optional payload
        if ( a_payload && a_payload->length() && res )
            res = send( a_socket, a_payload->c_str(), (int)a_payload->length() );

        if ( !res )
        {
            // Disconnect from client
            boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

            close( a_socket );

            for ( list<ClientInfo>::iterator ic = m_client_info.begin(); ic != m_client_info.end(); ++ic )
            {
                if ( ic->socket == a_socket )
                {
                    //notifyDisconnect( ic->addr );
                    m_client_info.erase(ic);
                    break;
                }
            }
        }
    }
    else
    {
        // Send to ALL clients
        boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

        for ( list<ClientInfo>::iterator ic = m_client_info.begin(); ic != m_client_info.end(); )
        {
            res = send( ic->socket, (char*)&a_adara_pkt, len );

            // Send optional payload
            if ( a_payload && a_payload->length() && res )
                res = send( ic->socket, a_payload->c_str(), (int)a_payload->length() );

            if ( !res )
            {
                // Disconnect from client
                close( ic->socket );
                //notifyDisconnect( ic->addr );
                ic = m_client_info.erase(ic);
            }
            else
                ic++;
        }
    }
}


bool
OutputAdapter::send( int a_socket, const char *a_data, uint32_t a_len )
{
    ssize_t     rc;
    size_t      sent = 0;
    uint16_t    max_retries = 5;

    while ( sent < a_len )
    {
        rc = write( a_socket, a_data + sent, a_len - sent );
        if ( rc < 0 )
        {
            // Interrupted?
            if (( rc == EAGAIN || rc == EINTR ) && --max_retries )
                continue;

            // Serious error
            return false;
        }

        sent += rc;
    }

    return true;
}

}}
