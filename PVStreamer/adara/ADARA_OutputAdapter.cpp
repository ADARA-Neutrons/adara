#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <time.h>
#include <iomanip>
#include <boost/lexical_cast.hpp>


#include "ADARA_OutputAdapter.h"
#include "ADARA.h"
#include "TraceException.h"

using namespace std;
using namespace PVS::ADARA;


namespace PVS {
namespace ADARA {

/**
 * @brief Constructor for ADARA OutputAdapter
 * @param a_stream_serv - Owning StreamService instance
 * @param a_port - TCP port to listen on
 * @param a_heartbeat - ADARA heartbeat period in milliseconds
 * @param a_no_heartbeat_pv - turn off PVSD Heartbeat Device/PV
 *
 * This constructor produces an ADARA OutputAdapter owned by the specified StreamService instance. The
 * StreamService will delete this OutputAdapter when it is destroyed. The ADARA OuputAdapter class
 * starts two threads: one for handling in-coming TCP client connection requests, and another for
 * processing, translating, and broadcasting packets from the internal data stream.
 */
OutputAdapter::OutputAdapter( StreamService &a_stream_serv, unsigned short a_port, unsigned long a_heartbeat, bool a_no_heartbeat_pv )
    : IOutputAdapter(a_stream_serv), m_active(true), m_socket_listen_thread(0), m_stream_proc_thread(0), m_port(a_port),
      m_heartbeat(a_heartbeat), m_no_heartbeat_pv(a_no_heartbeat_pv),
      m_listen_socket(-1)
{
    initSockets();

    if ( !m_no_heartbeat_pv )
        makeHeartbeatDevice();

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
  * sends them to all connected ADARA clients. Whenever input packets are
  * not received for a configured interval, ADARA heartbeat packets are
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
    vector<uint8_t> payload;

    heartbeat_pkt.payload_len = 0;
    heartbeat_pkt.format = ADARA_PKT_TYPE(
        ::ADARA::PacketType::HEARTBEAT_TYPE,
        ::ADARA::PacketType::HEARTBEAT_VERSION );
    heartbeat_pkt.nsec = 0;

    while ( 1 )
    {
        pvs_pkt = m_stream_api->getFilledPacket( m_heartbeat,
            timeout_flag );

        if ( !pvs_pkt )
        {
            if ( timeout_flag )
            {
                // Send a Heartbeat Packet (and a Heartbeat PV Update! :-D)
                if ( connected() )
                {
                    // Send Heartbeat Packet
                    heartbeat_pkt.sec =
                        (uint32_t)time(0) - EPICS_TIME_OFFSET;
                    payload.clear();
                    sendPacket( heartbeat_pkt, payload );

                    // Send Heartbeat PV Update...
                    if ( !m_no_heartbeat_pv )
                    {
                        m_heartbeat_pv_value++;

                        PVState state = PVState(
                            ::ADARA::VariableStatus::OK,
                            ::ADARA::VariableSeverity::OK );

                        state.m_uint_val = m_heartbeat_pv_value;
                        state.m_elem_count = 1;
                        state.m_time.sec =
                            (uint32_t)time(0) - EPICS_TIME_OFFSET;
                        state.m_time.nsec = 0;

                        updatePV( m_heartbeat_pv, state );

                        payload.clear();
                        buildVVP( adara_pkt,
                            m_heartbeat_pv, state, payload );
                        sendPacket( adara_pkt, payload );
                    }
                }
            }
            else
            {
                // A null packet w/o timeout means queues have been
                // deactivated and we should exit the thread.
                syslog( LOG_ERR,
                    "PVSD ERROR: %s: Null Packet/No Timeout - %s",
                    "OutputAdapter::streamProcessingThread()",
                    "Queues Deactivated, Exiting Thread" );
                usleep(33333); // give syslog a chance...
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
            if ( connected() )
            {
                payload.clear();
                if ( translate( *pvs_pkt, adara_pkt, payload ) )
                    sendPacket( adara_pkt, payload );
            }

            m_stream_api->putFreePacket( pvs_pkt );
        }
    }
}


/** \brief Translates a stream packet into an ADARA packet.
  * \param a_pv_pkt - stream packet to translate (input).
  * \param a_adara_pkt - ADARA packet to receive translation (output).
  */
bool
OutputAdapter::translate( StreamPacket &a_pv_pkt, OutPacket &a_adara_pkt,
        vector<uint8_t> &a_payload )
{
    // A Little Gratuitous Debug Logging... ;-b
    if ( a_pv_pkt.device == NULL ) {
        syslog( LOG_ERR, "PVSD ERROR: %s: Null Device! type=%d",
            "OutputAdapter::translate()", a_pv_pkt.type );
        usleep(33333); // give syslog a chance...
    }

    switch ( a_pv_pkt.type )
    {
    case DeviceDefined:
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device );
        return true;

    case DeviceRedefined:
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device );
        return true;

    case DeviceUndefined:
        // ADARA does not have a Device Undefined packet,
        // so just send a Regular Descriptor packet
        // with an "Empty" XML Descriptor to "Undefine" it... ;-D
        buildDDP( a_adara_pkt, a_payload, a_pv_pkt.device, false );
        return true;

    case VariableUpdate:
        if ( a_pv_pkt.pv == NULL ) {
            syslog( LOG_ERR, "PVSD ERROR: %s: Null PV! type=%d",
                "OutputAdapter::translate()", a_pv_pkt.type );
            usleep(33333); // give syslog a chance...
        }
        else if ( a_pv_pkt.pv->m_device == NULL ) {
            syslog( LOG_ERR,
                "PVSD ERROR: %s: Null PV Device! type=%d varId=0x%x/%d",
                "OutputAdapter::translate()", a_pv_pkt.type,
                a_pv_pkt.pv->m_id, a_pv_pkt.pv->m_id );
            usleep(33333); // give syslog a chance...
        }

        buildVVP( a_adara_pkt, a_pv_pkt.pv, a_pv_pkt.state, a_payload );
        return true;
    }

    return false;
}


/**
 * \brief Builds an ADARA device descriptor packet
 * \param a_adara_pkt - ADARA packet to receive DDP data.
 */
void
OutputAdapter::buildDDP( OutPacket &a_adara_pkt,
        vector<uint8_t> &a_payload,
        DeviceRecordPtr a_device,
        bool sendDescriptorXML )
{
    a_adara_pkt.format  = ADARA_PKT_TYPE(
        ::ADARA::PacketType::DEVICE_DESC_TYPE,
        ::ADARA::PacketType::DEVICE_DESC_VERSION );
    a_adara_pkt.sec     = (uint32_t)time(0) - EPICS_TIME_OFFSET;;
    a_adara_pkt.nsec    = 0;
    a_adara_pkt.dev_id  = a_device->m_id;

    // Encode XML

    stringstream sstr;

    if ( sendDescriptorXML )
    {
        sstr << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl
            << "  <device"
            << " xmlns=\"http://public.sns.gov/schema/device.xsd\""
            << endl
            << "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
            << endl
            << "  xsi:schemaLocation=\""
            << "http://public.sns.gov/schema/device.xsd"
            << " http://public.sns.gov/schema/device.xsd\">"
            << endl;

        sstr << "  <device_name>" << a_device->m_name << "</device_name>"
            << endl;

        // Generate enumeration definitions for enums
        // used by this device only

        if ( a_device->m_enums.size() )
        {
            sstr << "  <enumerations>" << endl;
            unsigned short id = 1;
            for ( vector<EnumDescriptor*>::const_iterator e =
                        a_device->m_enums.begin();
                    e != a_device->m_enums.end(); ++e, ++id )
            {
                sstr << "    <enumeration>" << endl;
                sstr << "      <enum_name>enum_"
                    << setw(2) << setfill('0') << id << "</enum_name>"
                    << endl;
                for ( map<int32_t,std::string>::const_iterator iel =
                            (*e)->m_values.begin();
                        iel != (*e)->m_values.end(); ++iel )
                {
                    sstr << "        <enum_element>" << endl;
                    sstr << "          <enum_element_name>" << iel->second
                        << "</enum_element_name>" << endl;
                    sstr << "          <enum_element_value>" << iel->first
                        << "</enum_element_value>" << endl;
                    sstr << "        </enum_element>" << endl;
                }
                sstr << "    </enumeration>" << endl;
            }
            sstr << "  </enumerations>" << endl;
        }

        // Generate process variable definitions

        sstr << "  <process_variables>" << endl;

        if ( a_device->m_active_pv && !(a_device->m_active_pv->m_ignore) )
        {
            sstr << "    <process_variable>" << endl;
            sstr << "      <pv_name>"
                << a_device->m_active_pv->m_name << "</pv_name>"
                << endl;
            sstr << "      <pv_connection>"
                << a_device->m_active_pv->m_connection
                << "</pv_connection>" << endl;
            sstr << "      <pv_id>"
                << a_device->m_active_pv->m_id << "</pv_id>" << endl;
            if ( a_device->m_active_pv->m_type == PV_ENUM )
            {
                sstr << "      <pv_type>enum_"
                    << setw(2) << setfill('0')
                    << a_device->m_active_pv->m_enum->m_id
                    << "</pv_type>" << endl;
            }
            else
            {
                sstr << "      <pv_type>"
                    << getPVTypeXML(a_device->m_active_pv->m_type)
                    << "</pv_type>" << endl;
            }
            if ( a_device->m_active_pv->m_units.size() )
            {
                sstr << "      <pv_units>"
                    << a_device->m_active_pv->m_units
                    << "</pv_units>" << endl;
            }
            if ( a_device->m_active_pv->m_ignore )
            {
                sstr << "      <pv_ignore>"
                    << a_device->m_active_pv->m_ignore
                    << "</pv_ignore>" << endl;
            }
            sstr << "    </process_variable>" << endl;
        }

        for ( vector<PVDescriptor*>::const_iterator ipv =
                    a_device->m_pvs.begin();
                ipv != a_device->m_pvs.end(); ++ipv )
        {
            sstr << "    <process_variable>" << endl;
            sstr << "      <pv_name>"
                << (*ipv)->m_name << "</pv_name>"
                << endl;
            sstr << "      <pv_connection>"
                << (*ipv)->m_connection
                << "</pv_connection>" << endl;
            sstr << "      <pv_id>"
                << (*ipv)->m_id << "</pv_id>" << endl;
            if ( (*ipv)->m_type == PV_ENUM )
            {
                sstr << "      <pv_type>enum_"
                    << setw(2) << setfill('0')
                    << (*ipv)->m_enum->m_id
                    << "</pv_type>" << endl;
            }
            else
            {
                sstr << "      <pv_type>"
                    << getPVTypeXML((*ipv)->m_type)
                    << "</pv_type>" << endl;
            }
            if ( (*ipv)->m_units.size() )
            {
                sstr << "      <pv_units>"
                    << (*ipv)->m_units
                    << "</pv_units>" << endl;
            }
            if ( (*ipv)->m_ignore )
            {
                sstr << "      <pv_ignore>"
                    << (*ipv)->m_ignore
                    << "</pv_ignore>" << endl;
            }
            sstr << "    </process_variable>" << endl;
        }

        sstr << "  </process_variables>" << endl;
        sstr << "</device>" << endl;

        // Save payload
        a_payload = vector<uint8_t>( sstr.str().size() );
        uint8_t *bytes = a_payload.data();
        memcpy( bytes,
            (uint8_t *)sstr.str().c_str(), sstr.str().size() );
    }

    // "Empty" Descriptor XML for Device "Undefine"...
    else
    {
        syslog( LOG_ERR,
        "%s: %s: %s Device [%s] (%s id=%d) %s",
            "PVSD ERROR", "OutputAdapter::buildDDP()",
            "Sending Empty Descriptor to *Undefine*",
            a_device->m_name.c_str(),
            "device", a_device->m_id,
            " - Setting Payload Size to Zero" );
        usleep(33333); // give syslog a chance...
    }

    a_adara_pkt.ddp.xml_len = (unsigned long) a_payload.size();
    a_adara_pkt.payload_len = 8 + a_adara_pkt.ddp.xml_len;

    // Round payload length up to nearsest 4 bytes
    int rem = a_adara_pkt.payload_len % 4;
    if ( rem )
    {
        // Pad buffer with nulls
        for ( int i = 0; i < (4-rem); ++i )
            a_payload.push_back((uint8_t)'\0');

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
OutputAdapter::buildVVP( OutPacket &a_adara_pkt,
        PVDescriptor *a_pv, PVState &a_state, vector<uint8_t> &a_payload )
{
    vector<uint32_t> uint_array;
    vector<double> double_array;
    uint8_t *bytes;

    stringstream arrayElems;
    uint32_t elemCount;
    uint32_t i;
    int rem;

    a_adara_pkt.dev_id          = a_pv->m_device->m_id;
    a_adara_pkt.vvp.var_id      = a_pv->m_id;
    a_adara_pkt.sec             = a_state.m_time.sec;
    a_adara_pkt.nsec            = a_state.m_time.nsec;

    a_adara_pkt.vvp.status      = a_state.m_status;
    a_adara_pkt.vvp.severity    = a_state.m_severity;

    switch ( a_pv->m_type )
    {
    case PV_ENUM:
    case PV_UINT:
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_U32_TYPE,
                ::ADARA::PacketType::VAR_VALUE_U32_VERSION );
        a_adara_pkt.payload_len     = 16;
        a_adara_pkt.vvp.uval        = a_state.m_uint_val;
        break;

    case PV_INT:
        // Send All Integers as "Unsigned" in the ADARA Protocol
        // - it all gets resolved in the STC/NeXus using the "pv_type"! :-D
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_U32_TYPE,
                ::ADARA::PacketType::VAR_VALUE_U32_VERSION );
        a_adara_pkt.payload_len     = 16;
        a_adara_pkt.vvp.uval        = (uint32_t) a_state.m_int_val;
        break;

    case PV_REAL:
        // Send All Floats as Doubles in the ADARA Protocol
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE,
                ::ADARA::PacketType::VAR_VALUE_DOUBLE_VERSION );
        a_adara_pkt.payload_len     = 20;
        a_adara_pkt.vvp.dval        = a_state.m_double_val;
        break;

    case PV_STR:
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_STRING_TYPE,
                ::ADARA::PacketType::VAR_VALUE_STRING_VERSION );
        a_adara_pkt.payload_len     = 16 + a_state.m_str_val.size();
        a_adara_pkt.vvp_str.str_len = a_state.m_str_val.size();

        // (For Now) Log String PV Value Updates...
        syslog( LOG_ERR,
            "%s: %s Device [%s] (%s id=%d) PV <%s> (%s) (pv id=%d) = [%s]",
            "OutputAdapter::buildVVP()",
            "String PV Value Update for",
            a_pv->m_device->m_name.c_str(),
            "device", a_pv->m_device->m_id,
            a_pv->m_name.c_str(), a_pv->m_connection.c_str(),
            a_pv->m_id, a_state.m_str_val.c_str() );
        usleep(33333); // give syslog a chance...

        // Set payload
        a_payload = vector<uint8_t>( a_state.m_str_val.size() );
        bytes = a_payload.data();
        memcpy( bytes,
            (uint8_t *)a_state.m_str_val.c_str(),
            a_state.m_str_val.size() );

        // Round payload length up to nearsest 4 bytes
        rem = a_adara_pkt.payload_len % 4;
        if ( rem )
        {
            // Pad buffer with nulls
            for ( int i = 0; i < (4-rem); ++i )
                a_payload.push_back((uint8_t)'\0');

            // Adjust payload len field
            a_adara_pkt.payload_len += (4 - rem);
        }
        break;

    case PV_INT_ARRAY:
        // Send All Integers as "Unsigned" in the ADARA Protocol
        // - it all gets resolved in the STC/NeXus using the "pv_type"! :-D
        elemCount = a_state.m_elem_count;
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE,
                ::ADARA::PacketType::VAR_VALUE_U32_ARRAY_VERSION );
        a_adara_pkt.payload_len     = 16
            + ( elemCount * sizeof(uint32_t) );

        // Set Payload (Minimum Array Size is 2... :-)
        uint_array = vector<uint32_t>( elemCount );
        if ( a_state.m_short_array != NULL )
        {
            for ( i=0 ; i < elemCount ; i++ )
            {
                uint_array[i] = (uint32_t) a_state.m_short_array[i];
            }
        }
        else if ( a_state.m_long_array != NULL )
        {
            for ( i=0 ; i < elemCount ; i++ )
            {
                // (int32_t -> uint32_t...)
                uint_array[i] = (uint32_t) a_state.m_long_array[i];
            }
        }
        else
        {
            syslog( LOG_ERR,
            "%s: %s: %s Device [%s] (%s id=%d) PV <%s> (%s) (pv id=%d) %s",
                "PVSD ERROR", "OutputAdapter::buildVVP()",
                "Missing Integer Array Data for",
                a_pv->m_device->m_name.c_str(),
                "device", a_pv->m_device->m_id,
                a_pv->m_name.c_str(), a_pv->m_connection.c_str(),
                a_pv->m_id, " - Setting Payload Size to Zero" );
            usleep(33333); // give syslog a chance...
            elemCount = 0;
        }

        // (For Now) Log Array Elements...
        arrayElems << "[";
        for ( i=0 ; i < elemCount ; i++ )
        {
            arrayElems << " " << uint_array[i];
        }
        arrayElems << " ]";
        syslog( LOG_INFO, "%s: uint_array = %s",
            "OutputAdapter::buildVVP()", arrayElems.str().c_str() );
        usleep(33333); // give syslog a chance...

        a_adara_pkt.vvp_array.elemCount = elemCount;
        a_payload = vector<uint8_t>( elemCount * sizeof(uint32_t) );
        bytes = a_payload.data();
        memcpy( bytes,
            (uint8_t *)uint_array.data(), elemCount * sizeof(uint32_t) );

        break;

    case PV_REAL_ARRAY:
        // Send All Floats as Doubles in the ADARA Protocol
        elemCount = a_state.m_elem_count;
        a_adara_pkt.format          =
            ADARA_PKT_TYPE(
                ::ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE,
                ::ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_VERSION );
        a_adara_pkt.payload_len     = 16 + ( elemCount * sizeof(double) );

        // Set Payload (Minimum Array Size is 2... :-)
        double_array = vector<double>( elemCount );
        if ( a_state.m_float_array != NULL )
        {
            for ( i=0 ; i < elemCount ; i++ )
            {
                double_array[i] = (double) a_state.m_float_array[i];
            }
        }
        else if ( a_state.m_double_array != NULL )
        {
            for ( i=0 ; i < elemCount ; i++ )
            {
                double_array[i] = a_state.m_double_array[i];
            }
        }
        else
        {
            syslog( LOG_ERR,
            "%s: %s: %s Device [%s] (%s id=%d) PV <%s> (%s) (pv id=%d) %s",
                "PVSD ERROR", "OutputAdapter::buildVVP()",
                "Missing Double Array Data for",
                a_pv->m_device->m_name.c_str(),
                "device", a_pv->m_device->m_id,
                a_pv->m_name.c_str(), a_pv->m_connection.c_str(),
                a_pv->m_id, " - Setting Payload Size to Zero" );
            usleep(33333); // give syslog a chance...
            elemCount = 0;
        }

        // (For Now) Log Array Elements...
        arrayElems << "[";
        for ( i=0 ; i < elemCount ; i++ )
        {
            arrayElems << " " << double_array[i];
        }
        arrayElems << " ]";
        syslog( LOG_INFO, "%s: double_array = %s",
            "OutputAdapter::buildVVP()", arrayElems.str().c_str() );
        usleep(33333); // give syslog a chance...

        a_adara_pkt.vvp_array.elemCount = elemCount;
        a_payload = vector<uint8_t>( elemCount * sizeof(double) );
        bytes = a_payload.data();
        memcpy( bytes,
            (uint8_t *)double_array.data(), elemCount * sizeof(double) );

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
    case PV_ENUM:
    case PV_UINT:
        return "unsigned integer";
    case PV_INT:
        return "integer";
    case PV_REAL:
        return "double";
    case PV_STR:
        return "string";
    case PV_INT_ARRAY:
        return "integer array";
    case PV_REAL_ARRAY:
        return "double array";
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

    // Add a PV state entry for the Active Status PV, If It's Not Ignored!
    if ( a_device->m_active_pv && !(a_device->m_active_pv->m_ignore) )
    {
        PVState state = PVState();

        state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
        state.m_time.nsec = 0;

        m_pv_state[a_device->m_active_pv] = state;
    }

    // Insert new PV state entries with disconnected status
    for ( vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin();
            ipv != a_device->m_pvs.end(); ++ipv )
    {
        PVState state = PVState();

        state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
        state.m_time.nsec = 0;

        m_pv_state[*ipv] = state;
    }
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
OutputAdapter::redefineDevice(
        DeviceRecordPtr a_device, DeviceRecordPtr a_old_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Add/remove only changed PV state entries
    map<PVDescriptor*,PVState>::iterator old_state;
    PVDescriptor *old_pv;
    bool found;

    // Transfer any Active Status PV State to new device, If Not Ignored!
    if ( a_device->m_active_pv && !(a_device->m_active_pv->m_ignore) )
    {
        found = false;

        // (Exactly) Same Active Status PV as Before, Re-Use State...
        if ( a_old_device->m_active_pv
            && !(a_old_device->m_active_pv->m_ignore)
            && !(a_old_device->m_active_pv->m_name.compare(
                a_device->m_active_pv->m_name ))
            && !(a_old_device->m_active_pv->m_connection.compare(
                a_device->m_active_pv->m_connection )) )
        {
            old_state = m_pv_state.find( a_old_device->m_active_pv );
            if ( old_state != m_pv_state.end() )
            {
                m_pv_state[a_device->m_active_pv] = old_state->second;
                found = true;
            }
        }

        if ( !found )
        {
            PVState state =
                PVState( ::ADARA::VariableStatus::NOT_REPORTED,
                    ::ADARA::VariableSeverity::INVALID );

            state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
            state.m_time.nsec = 0;

            m_pv_state[a_device->m_active_pv] = state;
        }
    }

    // Transfer last-known PVState to new state entries
    // (keyed on new PVDescriptor instances)
    vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin();
    for ( ; ipv != a_device->m_pvs.end(); ++ipv )
    {
        found = false;
        old_pv = a_old_device->getPvByName( (*ipv)->m_name );
        if ( old_pv )
        {
            old_state = m_pv_state.find( old_pv );
            if ( old_state != m_pv_state.end() )
            {
                m_pv_state[*ipv] = old_state->second;
                found = true;
            }
        }

        // If this is a brand new PV, set it's state to undefined/invalid
        // (until we get the first value from it)
        if ( !found )
        {
            PVState state =
                PVState( ::ADARA::VariableStatus::NOT_REPORTED,
                    ::ADARA::VariableSeverity::INVALID );

            state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
            state.m_time.nsec = 0;

            m_pv_state[*ipv] = state;
        }
    }

    // Add "new" device to configured device list
    m_devices.insert( a_device );

    // Remove old PV state entries
    undefineDevice( a_old_device );
}


/**
 * @brief Process device undefined internal stream packet
 * @param a_device - Reference to device being undefined
 *
 * This method updates internal state due to the removal of the specified
 * device. This method does NOT emit any ADARA packets, it only updates
 * internal state.
 */
void
OutputAdapter::undefineDevice( DeviceRecordPtr a_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Remove all pv state entries associated with device
    map<PVDescriptor*,PVState>::iterator ipv_state;

    // Remove any Active Status PV State, If Not Ignored!
    if ( a_device->m_active_pv && !(a_device->m_active_pv->m_ignore) )
    {
        ipv_state = m_pv_state.find( a_device->m_active_pv );
        if ( ipv_state != m_pv_state.end() )
        {
            m_pv_state.erase( ipv_state );
        }
    }

    for ( vector<PVDescriptor*>::iterator ipv = a_device->m_pvs.begin();
            ipv != a_device->m_pvs.end(); ++ipv )
    {
        ipv_state = m_pv_state.find( *ipv );
        if ( ipv_state != m_pv_state.end() )
        {
            m_pv_state.erase( ipv_state );
        }
    }

    set<DeviceRecordPtr>::iterator idev = m_devices.find( a_device );
    if ( idev != m_devices.end() )
    {
        m_devices.erase( idev );
    }
}


void
OutputAdapter::updatePV( PVDescriptor *a_pv, PVState &a_state )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<PVDescriptor*,PVState>::iterator ipv_state =
        m_pv_state.find( a_pv );

    if ( ipv_state != m_pv_state.end() )
    {
        ipv_state->second = a_state;
    }

    else
    {
        stringstream sstr;
        if ( a_pv != NULL && a_pv->m_device != NULL )
        {
            sstr << "Device [" << a_pv->m_device->m_name << "]"
                << " (device id=" << a_pv->m_device->m_id << ")";
        }
        else
        {
            sstr << "*** NO DEVICE ***";
        }
        if ( a_pv != NULL )
        {
            sstr << " PV <" << a_pv->m_name << ">"
                << " (" << a_pv->m_connection << ")";
        }
        else
        {
            sstr << " *** NO PV ***";
        }
        syslog( LOG_ERR,
            "%s %s: %s - PV Descriptor Not Found for State Update! %s",
            "PVSD ERROR:", "OutputAdapter::updatePV()", sstr.str().c_str(),
            "Ignoring..." );
        usleep(33333); // give syslog a chance...
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
        throw;
    }
    catch (...)
    {
        EXCEPT( EC_SOCKET_ERROR, "Unknown exception in initSockets()." );
    }
}


/** \brief Create Fake Heartbeat Device & PV for Keep-Alive Web Monitoring
 */
void
OutputAdapter::makeHeartbeatDevice(void)
{
    // Create Heartbeat Device & PV...

    m_heartbeat_device = DeviceRecordPtr( new DeviceDescriptor(
        "HeartbeatDevice", "pvsd", PVSD_PROTOCOL ) );

    m_heartbeat_device->m_id = -1;

    m_heartbeat_device->definePV( "HeartbeatPVSD", "HeartbeatPVSD",
        PV_UINT, 1, (EnumDescriptor *) NULL, "" );

    m_heartbeat_pv = m_heartbeat_device->getPvByName( "HeartbeatPVSD" );

    m_heartbeat_pv->m_id = -1;
    m_heartbeat_pv->m_ignore = true;

    defineDevice( m_heartbeat_device );

    // Initialize Heartbeat PV & State...

    m_heartbeat_pv_value = 0;

    PVState state = PVState( ::ADARA::VariableStatus::OK,
        ::ADARA::VariableSeverity::OK );

    state.m_uint_val = m_heartbeat_pv_value;
    state.m_elem_count = 1;
    state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
    state.m_time.nsec = 0;

    updatePV( m_heartbeat_pv, state );
}


/** \brief Method to (quickly) determine if any clients are currently connected.
  */
bool
OutputAdapter::connected()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_client_info.size() != 0;
}


/** \brief Method to return PVSD Server Listen IP/Port "Address".
  */
std::string
OutputAdapter::serverAddr()
{
    std::string server = m_addr;
    server += ":" + boost::lexical_cast<string>( m_port );
    return server;
}

/** \brief Method to return number of clients that are currently connected.
  */
uint32_t
OutputAdapter::numConnected()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_client_info.size();
}


/** \brief Method to return number of devices that are currently defined.
  */
uint32_t
OutputAdapter::numDevices()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_devices.size();
}


/** \brief Method to return number of PVs that are currently defined.
  */
uint32_t
OutputAdapter::numPVs()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_pv_state.size();
}


void
OutputAdapter::socketListenThread()
{
    struct sockaddr client_addr;
    socklen_t       client_addr_len = sizeof(struct sockaddr);
    ClientInfo      info;

    while ( 1 )
    {
        info.socket = accept( m_listen_socket,
            &client_addr, &client_addr_len );

        if ( info.socket < 0 )
        {
            int e = errno;
            syslog( LOG_ERR,
                "PVSD ERROR: %s: Socket Accept Returned Error (%d) - %s",
                "OutputAdapter::socketListenThread()",
                e, strerror(e) );
            usleep(33333); // give syslog a chance...

            if ( !m_active )
            {
                syslog( LOG_ERR, "PVSD ERROR: %s: %s",
                    "OutputAdapter::socketListenThread()",
                    "Output Adapter No Longer Active, Exiting Thread" );
                usleep(33333); // give syslog a chance...
                break;
            }
        }
        else
        {
            // New client - setup client info
            info.addr = inet_ntoa(
                ((struct sockaddr_in &)client_addr).sin_addr );

            syslog( LOG_ERR,
                "%s %s: Connected to ADARA SMS Client at %s (socket=%d)",
                "PVSD ERROR:", "OutputAdapter::socketListenThread()",
                info.addr.c_str(), info.socket );
            usleep(33333); // give syslog a chance...

            boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

            m_client_info.push_back( info );

            // Send source information packet first
            sendSourceInfo( info.socket );

            // need to send it all currently active devices (if any)
            sendCurrentData( info.socket );

            syslog( LOG_ERR,
                "%s %s: Initial Sends Complete to %s at %s (socket=%d)",
                "PVSD ERROR:", "OutputAdapter::socketListenThread()",
                "ADARA SMS Client", info.addr.c_str(), info.socket );
            usleep(33333); // give syslog a chance...
        }
    }
}


/** \brief Sends DDPs for all configured devices to the specified client.
  * \param a_socket - Socket of client to receive DDPs.
  */
void
OutputAdapter::sendCurrentData( int a_socket )
{
    OutPacket adara_pkt;

    vector<uint8_t> payload;

    syslog( LOG_INFO,
        "%s: Sending Current Data to ADARA SMS Client (socket=%d)",
        "OutputAdapter::sendCurrentData()", a_socket );
    usleep(33333); // give syslog a chance...

    // Use current time for DDP packets
    adara_pkt.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
    adara_pkt.nsec = 0;

    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    // Send DDPs for real devices
    for ( set<DeviceRecordPtr>::iterator idev = m_devices.begin();
            idev != m_devices.end(); ++idev )
    {
        syslog( LOG_INFO,
            "%s: Sending Device [%s] Descriptor to %s (socket=%d)",
            "OutputAdapter::sendCurrentData()", (*idev)->m_name.c_str(),
            "ADARA SMS Client", a_socket );
        usleep(33333); // give syslog a chance...

        payload.clear();
        buildDDP( adara_pkt, payload, *idev );
        sendPacket( adara_pkt, payload, a_socket );
    }

    // Send value updates for configured devices
    stringstream sentloghdr;
    sentloghdr << "OutputAdapter::sendCurrentData():"
        << " Sent PV Variable Value Update List to"
        << " ADARA SMS Client (socket=" << a_socket << ") -";
    stringstream pvlist;
    for ( map<PVDescriptor*,PVState>::iterator ipv = m_pv_state.begin();
            ipv != m_pv_state.end(); ++ipv )
    {
        // Construct Logging List of PV Names/Connection Strings...
        string pvstr = " <" + ipv->first->m_name + ">";
        if ( ipv->first->m_connection.compare( ipv->first->m_name ) )
            pvstr += "(" + ipv->first->m_connection + ")";
        if ( sentloghdr.str().size() + pvlist.str().size() + pvstr.size()
                > 2000 )
        {
            syslog( LOG_INFO,
                "%s: Sent PV %s List to %s (socket=%d) -%s...",
                "OutputAdapter::sendCurrentData()",
                "Variable Value Update", "ADARA SMS Client", a_socket,
                pvlist.str().c_str() );
            usleep(33333); // give syslog a chance...
            pvlist.str("");
        }
        pvlist << pvstr;

        payload.clear();
        buildVVP( adara_pkt, ipv->first, ipv->second, payload );
        sendPacket( adara_pkt, payload, a_socket );
    }

    syslog( LOG_INFO, "%s: Sent PV %s List to %s (socket=%d) -%s.",
        "OutputAdapter::sendCurrentData()", "Variable Value Update",
        "ADARA SMS Client", a_socket, pvlist.str().c_str() );
    usleep(33333); // give syslog a chance...
}


/** \brief Sends a source list packet to client socket (packet is empty for PVStreamer)
  * \param a_socket Client socket to send packet over.
  */
void
OutputAdapter::sendSourceInfo( int a_socket )
{
    OutPacket adara_pkt;

    vector<uint8_t> payload;

    syslog( LOG_INFO,
        "%s: Sending Source List to ADARA SMS Client (socket=%d)",
        "OutputAdapter::sendSourceInfo()", a_socket );
    usleep(33333); // give syslog a chance...

    adara_pkt.payload_len = 0;
    adara_pkt.format = ADARA_PKT_TYPE(
        ::ADARA::PacketType::SOURCE_LIST_TYPE,
        ::ADARA::PacketType::SOURCE_LIST_VERSION );
    adara_pkt.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
    adara_pkt.nsec = 0;

    sendPacket( adara_pkt, payload, a_socket );
}


void
OutputAdapter::sendPacket( OutPacket &a_adara_pkt,
        vector<uint8_t> &a_payload, int a_socket )
{
    bool res;
    uint32_t len = (int)a_adara_pkt.payload_len + 16;

    if ( a_payload.size() )
        len -= (int)a_payload.size();

    if ( a_socket > -1 ) // Send to a specific connection
    {
        res = send( a_socket, (char *)&a_adara_pkt, len );

        // Send optional payload
        if ( a_payload.size() && res )
        {
            res = send( a_socket,
                (const char *)a_payload.data(), (int)a_payload.size() );
        }

        if ( !res )
        {
            syslog( LOG_ERR, "PVSD ERROR: %s: Send Failed! (socket=%d)",
                "OutputAdapter::sendPacket()", a_socket );
            usleep(33333); // give syslog a chance...

            // Disconnect from client
            boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

            close( a_socket );

            for ( list<ClientInfo>::iterator ic = m_client_info.begin();
                    ic != m_client_info.end(); ++ic )
            {
                if ( ic->socket == a_socket )
                {
                    syslog( LOG_ERR, "PVSD ERROR: %s: %s at %s",
                        "OutputAdapter::sendPacket()",
                        "Disconnecting from ADARA SMS Client",
                        ic->addr.c_str() );
                    usleep(33333); // give syslog a chance...

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

        for ( list<ClientInfo>::iterator ic = m_client_info.begin();
                ic != m_client_info.end(); )
        {
            res = send( ic->socket, (char*)&a_adara_pkt, len );

            // Send optional payload
            if ( a_payload.size() && res )
            {
                res = send( ic->socket,
                    (const char *)a_payload.data(),
                    (int)a_payload.size() );
            }

            if ( !res )
            {
                syslog( LOG_ERR,
                    "PVSD ERROR: %s: Send Failed! (socket=%d) %s at %s",
                    "OutputAdapter::sendPacket()", ic->socket,
                    "Disconnecting from ADARA SMS Client",
                    ic->addr.c_str() );
                usleep(33333); // give syslog a chance...

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

    try
    {
        while ( sent < a_len )
        {
            rc = write( a_socket, a_data + sent, a_len - sent );
            if ( rc < 0 )
            {
                // Interrupted?
                if (( rc == EAGAIN || rc == EINTR ) && --max_retries )
                    continue;

                // Serious error
                syslog( LOG_ERR,
                    "%s: %s: Socket Write Failed! len=%u (socket=%d) - %s",
                    "PVSD ERROR", "OutputAdapter::send()", a_len, a_socket,
                    strerror( errno ) );
                usleep(33333); // give syslog a chance...
                return false;
            }

            sent += rc;
        }
    }
    catch ( exception &e )
    {
        syslog( LOG_ERR,
            "%s: %s: Socket Write Exception! len=%u (socket=%d) - %s",
            "PVSD ERROR", "OutputAdapter::send()", a_len, a_socket,
            e.what() );
        usleep(33333); // give syslog a chance...
        return false;
    }
    catch (...)
    {
        syslog( LOG_ERR,
            "%s: %s: Socket Write Unknown Exception! len=%u (socket=%d)",
            "PVSD ERROR", "OutputAdapter::send()", a_len, a_socket );
        usleep(33333); // give syslog a chance...
        return false;
    }

    return true;
}

}}

// vim: expandtab

