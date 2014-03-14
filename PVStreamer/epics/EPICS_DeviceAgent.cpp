#include <iostream>
#include <syslog.h>
#include <alarm.h>

#include "CoreDefs.h"
#include "TraceException.h"
#include "EPICS_DeviceAgent.h"
#include "DeviceDescriptor.h"
#include "ConfigManager.h"
#include "StreamService.h"
#include "ComBus.h"
#include "ComBusMessages.h"

using namespace std;

namespace PVS {
namespace EPICS {

//-------------------------------------------------------------------------------------------------
// PUBLIC DeviceAgent Methods
//-------------------------------------------------------------------------------------------------

/**
 * @brief DeviceAgent constructor
 * @param a_stream_api - Input adapter API for internal stream I/O
 * @param a_device - DeviceDescriptor for initial device to manage
 * @param a_epics_context - EPICS context needed for background threads
 *
 * Calling this constructor will cause the new DeviceAgent to immediately begin activities
 * for connecting to and streaming data for the specified device (from a private thread).
 */
DeviceAgent::DeviceAgent( IInputAdapterAPI &a_stream_api, DeviceDescriptor *a_device, struct ca_client_context *a_epics_context )
    : m_stream_api(a_stream_api), m_dev_desc(0), m_defined(false), m_state_changed(false), m_active(true),
      m_epics_context(a_epics_context), m_tid(0)
{
    m_ctrl_thread = new boost::thread( boost::bind( &DeviceAgent::controlThread, this ));

    update(a_device);
}


/**
 * @brief DeviceAgent destructor.
 *
 * This destructor will stop underlying I/O if not already stopped. Currently, there is
 * no risk of blocking when destroying a running DeviceAgent; however this could change in
 * the future (use stopped() to check asynchronously prior to deleting a DeviceAgent
 * instance).
 */
DeviceAgent::~DeviceAgent()
{
    m_active = false;
    stop();

    // Wake up ctrl thread and wait for it to exit
    m_state_cond.notify_one();
    m_ctrl_thread->join();
}


/**
 * @brief Handles new or updated to device descriptors
 * @param a_device - DeviceDescriptor for new/updated device
 *
 * This method is used to establish or update EPICS connections when a new device is defined or the
 * current device is updated. If a current device is defined, the passed-in device descriptor is
 * compared to the existing descriptor and connections are updated based on any detected differences.
 * This method does NOT inject any packets into the internal data stream.
 */
void
DeviceAgent::update( DeviceDescriptor *a_device )
{
    cout << "UPDATE DevDesc: " << hex << a_device << endl;

    boost::unique_lock<boost::mutex> lock(m_mutex);

    PVDescriptor*                   old_pv;
    vector<PVDescriptor*>::iterator ipv;
    map<chid,ChanInfo>::iterator    ich;
    map<string,chid>::iterator      idx;

    // Resetting m_defined will cause control thread to watch PVs and
    // perform post-connection duties - including defining device with config manager
    m_defined = false;

    // Use transient descriptor first, then configured device second
    DeviceDescriptor *old_desc = m_dev_desc?m_dev_desc:m_dev_record.get();

    if ( old_desc )
    {
        // Disconnect any existing connections that are no longer needed
        for ( ipv = old_desc->m_pvs.begin(); ipv != old_desc->m_pvs.end(); ++ipv )
        {
            cout << "Check PV: " << (*ipv)->m_name << endl;
            if ( !a_device->getPvByConnection( (*ipv)->m_connection ))
                disconnectPV( *ipv );
        }

        // If a decive is already defined, reuse any shared PV connections
        // Make connections for new PVs in updated device
        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
        {
            old_pv = old_desc->getPvByConnection( (*ipv)->m_connection );
            if ( !old_pv )
                connectPV( *ipv );
            else
            {
                // PV channel is shared between old and new device, reuse connection
                // Update PV pointer on channel info
                idx = m_pv_index.find( old_pv->m_connection );
                if ( idx != m_pv_index.end())
                {
                    ich = m_chan_info.find( idx->second );
                    if ( ich != m_chan_info.end())
                    {
                        cout << "REUSE chid: " << ich->first << " (PV: " << (*ipv)->m_connection << ", addr: " << hex << (*ipv) << ")" << endl;
                        // Update PV to new (temporary) PV
                        ich->second.m_pv = *ipv;
                        // Old device record is no longer valid - reset
                        ich->second.m_device.reset();
                        // Re-aqcuire metadata just in case it changed
                        ich->second.m_chan_state = INFO_NEEDED;
                    }
                }
            }
        }

        // Delete old temporary descriptor if set
        if ( m_dev_desc )
        {
            cout << "DELETE old temp DevDesc: " << hex << m_dev_desc << endl;
            delete m_dev_desc;
        }
    }
    else
    {
        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
            connectPV( *ipv );
    }

    a_device->print(cout);
    cout << "CHID map:" << endl;
    for ( ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        cout << "chid: " << ich->first << ", dev: " << ich->second.m_device.get() << ", pv: " << hex << ich->second.m_pv << ", ch st: " << ich->second.m_chan_state << endl;
        cout << "      evid: " << ich->second.m_evid << ", conn: " << ich->second.m_connected << ", sub: " << ich->second.m_subscribed << endl;
    }

    cout << "Name/CHID map:" << endl;
    for ( idx = m_pv_index.begin(); idx != m_pv_index.end(); ++idx )
    {
        cout << "PV: " << idx->first << ", chid: " << idx->second << endl;
    }

    m_dev_desc = a_device;
    ca_flush_io();

    // If no new PV channels were created, state machne will not progress on it's own
    // Wake state machine in this case (doesn't matter if its notified more than once)
    m_state_changed = true;
    m_state_cond.notify_one();
}


/**
 * @brief Handles metadata updates after device configuration
 *
 * This method is called by the state machine when metadata changes are detected after configuration
 * for at least one connected PV. Because metadata (PV type, units, enum) are part of the device
 * descriptor, the existing device must be redefined with the new metadata and a new device descriptor
 * emitted. This method copies the current device descriptor, updates the changed metadata, and
 * redefines the device through the configuration manager.
 */
void
DeviceAgent::metadataUpdated()
{
    // No lock is required since this method is only called from state machine that holds the lock.

    // Copy current device descriptor
    DeviceDescriptor *new_dev = new DeviceDescriptor( *m_dev_record );
    m_defined = false;
    m_dev_desc = new_dev;

    // Update channel info and channel state
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        ich->second.m_device.reset();
        ich->second.m_pv = m_dev_desc->getPvByConnection( ich->second.m_pv->m_connection );

        if ( ich->second.m_chan_state == INFO_AVAILABLE )
        {
            ich->second.m_pv->setMetadata( epicsToPVType( ich->second.m_ca_type ), ich->second.m_ca_units, ich->second.m_ca_enum_vals );
            ich->second.m_chan_state = READY;
        }
        else if ( ich->second.m_chan_state == READY )
        {
            // Re-request metadata from all other PV (they may have changed too)
            ich->second.m_chan_state = INFO_NEEDED;
        }
    }
}


/**
 * @brief Stops a DeviceAgent when associated device no longer needs to be streamed.
 *
 * This method causes the DeviceAgent to undefine any associated device and disconnect from
 * all underlying input sources (in this case EPICS channels). This method can be called
 * externally, and will also be called by the DeviceAgent destructor. Calling this method
 * more than once is safe and will have no side effects.
 */
void
DeviceAgent::stop()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    // If a device is currently configured, undefine it
    if ( m_dev_record.get())
    {
        m_stream_api.getCfgMgr().undefineDevice( m_dev_record );
        m_dev_record.reset();
    }

    // Clear CA channels and unsubscribe
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        if ( ich->second.m_subscribed )
            ca_clear_subscription( ich->second.m_evid );

        ca_clear_channel( ich->second.m_chid );
        ca_flush_io();
    }

    m_chan_info.clear();
    m_pv_index.clear();
}


/**
 * @brief This method determines if the DeviceAgent is in a stopped state
 * @return True if device is stopped; false otherwise
 *
 * This method determines if the DeviceAgent is stopped - meaning underlying inputs are
 * disconnected and there are no active or pending asynchronous I/O operations. This is
 * useful to know when a DeviceAgent can be destroyed safely without vlocking in the
 * destructor. Currently, this method is very simple as EPICS seems to ensure that no
 * additional callbacks will be made after clearing channels and subscriptions.
 */
bool
DeviceAgent::stopped()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);
    return m_chan_info.size() == 0;
}


//-------------------------------------------------------------------------------------------------
// PRIVATE DeviceAgent Methods
//-------------------------------------------------------------------------------------------------

/**
 * @brief Creates data connections to specified process variable
 * @param a_pv - Process variable descriptor for connection
 *
 * This method makes EPICS calls to connect to the specified process variable. This call is
 * asynchronous. Once the connections have been established, the epicsConnectionCallback method
 * will be called and the DeviceAgent state machine will be stimulated.
 */
void
DeviceAgent::connectPV( PVDescriptor *a_pv )
{
    ChanInfo info;
    info.m_pv = a_pv;

    // Create a CA channel
    if ( ca_create_channel( a_pv->m_connection.c_str(), &epicsConnectionCallback, this, 0, &info.m_chid ) == ECA_NORMAL )
    {
        // Note: don't flush I/O here - update() method will call it

        cout << "Connected to chid: " << info.m_chid << " (for PV: " << a_pv->m_connection << ", addr: " << hex << a_pv << ")" << endl;

        // Update channel info and PV name index structures
        m_chan_info[info.m_chid] = info;
        m_pv_index[a_pv->m_connection] = info.m_chid;
    }
    else
        syslog( LOG_ERR, "Failed to create channel for PV: %s", a_pv->m_connection.c_str() );
}


/**
 * @brief Disconnects the specified process variable
 * @param a_pv - Process variable descriptor for diconnection
 *
 * This method makes EPICS calls to disconnect to the specified process variable. This call is
 * synchronous (for now).
 */
void
DeviceAgent::disconnectPV( PVDescriptor *a_pv )
{
    map<std::string,chid>::iterator ipv = m_pv_index.find( a_pv->m_connection );
    if ( ipv != m_pv_index.end())
    {
        map<chid,ChanInfo>::iterator ich = m_chan_info.find(ipv->second);
        if ( ich != m_chan_info.end())
        {
            cout << "Disconnected from chid: " << ich->first << " (for PV: " << a_pv->m_connection << ", addr: " << hex << a_pv << ")" << endl;

            if ( ich->second.m_subscribed )
                ca_clear_subscription( ich->second.m_evid );
            else
                cout << "   (NOT SUBSCRIBED)" << endl;

            ca_clear_channel( ich->second.m_chid );

            // Update channel info index structures
            m_chan_info.erase( ich );

            // Don't flush I/O here - update() method will call it
        }

        // Update name index structures
        m_pv_index.erase( ipv );
    }
}


/**
 * @brief This method is the control thread for the DeviceAgent state machine
 */
void
DeviceAgent::controlThread()
{
    ca_attach_context( m_epics_context );

    map<chid,ChanInfo>::iterator ich;
    map<std::string,chid>::iterator idx;
    PVDescriptor *pv;
    size_t ready;
    bool device_changed = false;

    while( 1 )
    {
        try
        {
            boost::unique_lock<boost::mutex> lock(m_mutex);

            if ( !m_state_changed )
                m_state_cond.wait( lock );

            if ( !m_active )
                break;

            // Might have been woken spuriously
            if ( !m_state_changed )
                continue;

            m_state_changed = false;

            ready = 0;
            for ( ich = m_chan_info.begin(); ich != m_chan_info.end() && !m_state_changed; ++ich )
            {
                switch ( ich->second.m_chan_state )
                {
                case INFO_NEEDED:
                    cout << "GET CTRL, type: " << ich->second.m_ca_type << ", chid: " << ich->first << endl;
                    if ( ca_get_callback( epicsToCtrlRecordType( ich->second.m_ca_type ), ich->first, epicsEventCallback, this ) == ECA_NORMAL )
                        ich->second.m_chan_state = INFO_PENDING;
                    else
                    {
                        cout << "FAILED - ca_get_callback (type = " << ich->second.m_ca_type << ")" << endl;
                        syslog( LOG_ERR, "Failed to get channel info for PV: %s", ich->second.m_pv->m_connection.c_str() );
                    }
                    break;
                case INFO_AVAILABLE:
                    if ( m_defined )
                    {
                        if ( !ich->second.m_pv->equalMetadata( epicsToPVType( ich->second.m_ca_type ), ich->second.m_ca_units, ich->second.m_ca_enum_vals ))
                        {
                            cout << "METADATA UPDATE!!!" << endl;
                            metadataUpdated();
                            // Setup for another state machine pass
                            ready = 0;
                            m_state_changed = true;
                            break;
                        }
                    }

                    cout << "SET METADATA: PV " << ich->second.m_pv->m_connection << ", units: " << ich->second.m_ca_units << endl;
                    ich->second.m_pv->setMetadata( epicsToPVType( ich->second.m_ca_type ), ich->second.m_ca_units, ich->second.m_ca_enum_vals );
                    ich->second.m_chan_state = READY;
                    cout << "   (after): PV " << ich->second.m_pv->m_connection << ", units: " << ich->second.m_pv->m_units << endl;
                    ++ready;
                    break;
                case READY:
                    ++ready;
                    break;
                default:
                    break;
                }
            }

            // Flush any pending EPICS requests
            ca_flush_io();


            if ( m_dev_desc && !m_defined && ready > 0 && ready == m_dev_desc->m_pvs.size() )
            {
                device_changed = false;

                // Defined device with ConfigManager - this wil return the "real" DesviceDescriptor record
                DeviceRecordPtr new_rec = m_stream_api.getCfgMgr().defineDevice( *m_dev_desc );

                // If new record is the same as existing record, then nothing has changed
                if ( new_rec != m_dev_record )
                    device_changed = true;

                // Save new device record
                m_dev_record = new_rec;

                // Update channel info objects with new device & pv pointers (replaces m_dev_desc pointers)
                for ( idx = m_pv_index.begin(); idx != m_pv_index.end(); ++idx )
                {
                    // Careful! PV names can be changed in new_rec due to name conflicts!!!
                    pv = m_dev_record->getPvByConnection( idx->first );
                    ich = m_chan_info.find( idx->second );
                    if ( pv && ich != m_chan_info.end() )
                    {
                        // Replace temporary device and PV records with new managed records
                        ich->second.m_device = m_dev_record;
                        ich->second.m_pv = pv;
                    }
                    else
                    {
                        // Something has gone terribly wrong if we land here (logic/programming error)
                    }
                }

                // Send cached PV values only if the device actually changed
                if ( device_changed )
                    sendCurrentValues();

                m_defined = true;
                device_changed = false;

                // Delete temporary device descriptor
                cout << "Deleting temp dev ptr: " << hex << m_dev_desc << endl;
                delete m_dev_desc;
                m_dev_desc = 0;
            }
        }
        catch(...)
        {
            syslog( LOG_ERR, "Exception thrown in DevAgent::controlThread!" );
        }
    }
}


void
DeviceAgent::epicsConnectionHandler( struct connection_handler_args a_args )
{
    try
    {
        if ( a_args.op == CA_OP_CONN_UP )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end())
            {
                ich->second.m_connected = true;

                chtype type = ca_field_type( a_args.chid );
                if ( VALID_DB_FIELD( type ))
                {
                    // Save native type
                    ich->second.m_ca_type = type;

                    cout << "CONNECT! chid: " << ich->first << ", type: " << type << endl;

                    //if ( !ich->second.m_subscribed )
                    {
                        if ( ca_create_subscription( epicsToTimeRecordType( type ), 0, ich->second.m_chid, DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
                                &epicsEventCallback, this, &ich->second.m_evid ) == ECA_NORMAL )
                            ich->second.m_subscribed = true;
                        else
                            syslog( LOG_ERR, "Failed to create subscription for PV: %s", ich->second.m_pv->m_connection.c_str() );

                        ca_flush_io();
                    }

                    // Note: there is no way to know if the metadata on this channel has or hasn't changed, so assume it has changed
                    if ( ich->second.m_pv->m_type == PV_STR ) // There is NO ctrl record for EPICS string types
                        ich->second.m_chan_state = INFO_AVAILABLE;
                    else
                    {
                        cout << "INFO_NEEDED for chid: " << ich->first << endl;
                        ich->second.m_chan_state = INFO_NEEDED;
                    }
                    m_state_changed = true;
                    m_state_cond.notify_one();
                }
            }
        }
        else if ( a_args.op == CA_OP_CONN_DOWN )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end())
            {
                ich->second.m_connected = false;

                if ( ich->second.m_subscribed )
                {
                    ca_clear_subscription( ich->second.m_evid );
                    ich->second.m_subscribed = false;
                }

                // Set var state to disconnected
                ich->second.m_pv_state.m_status = epicsAlarmComm;
                ich->second.m_pv_state.m_severity = epicsSevMajor;

                // Do not try to send value/alarm data unless device is fully defined
                if ( !m_defined )
                    return;

                bool timeout;
                StreamPacket *pkt = m_stream_api.getFreePacket( 5000, timeout );
                if ( pkt )
                {
                    pkt->type = VariableUpdate;
                    pkt->device = ich->second.m_device;
                    pkt->pv = ich->second.m_pv;
                    pkt->state = ich->second.m_pv_state;

                    m_stream_api.putFilledPacket( pkt );
                }
            }
        }
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, "TraceException in DeviceAgent::epicsConnectionHandler()" );
        syslog( LOG_ERR, e.toString().c_str() );
    }
    catch( std::exception &e )
    {
        syslog( LOG_ERR, "Exception in DeviceAgent::epicsConnectionHandler()" );
        syslog( LOG_ERR, e.what() );
    }
    catch(...)
    {
        syslog( LOG_ERR, "Unknown exception in DeviceAgent::epicsConnectionHandler()" );
    }
}


#define SET_STATE( state, type, src ) \
    state.m_time.sec = ((struct type *)src)->stamp.secPastEpoch; \
    state.m_time.nsec = ((struct type *)src)->stamp.nsec; \
    state.m_status = ((struct type *)src)->status; \
    state.m_severity = ((struct type *)src)->severity;

void
DeviceAgent::epicsEventHandler( struct event_handler_args a_args )
{
    try
    {
        if ( a_args.status == ECA_NORMAL && a_args.dbr != 0 )
        {
            if ( epicsIsTimeRecordType( a_args.type ))
            {
                boost::lock_guard<boost::mutex> lock(m_mutex);

                map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
                if ( ich != m_chan_info.end())
                {
                    cout << "VALUE! chid: " << ich->first << ", type: " << a_args.type << endl;

                    PVState &state = ich->second.m_pv_state;
                    switch ( a_args.type )
                    {
                        case DBR_TIME_STRING:
                        cout << "STRING" << endl;
                            state.m_str_val = ((struct dbr_time_string *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_string, a_args.dbr )
                            break;
                        case DBR_TIME_SHORT:
                        cout << "SHORT" << endl;
                            state.m_int_val = ((struct dbr_time_short *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_short, a_args.dbr )
                            break;
                        case DBR_TIME_FLOAT:
                        cout << "FLOAT" << endl;
                            state.m_real_val = ((struct dbr_time_float *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_float, a_args.dbr )
                            break;
                        case DBR_TIME_ENUM:
                        cout << "ENUM" << endl;
                            state.m_int_val = ((struct dbr_time_enum *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_enum, a_args.dbr )
                            break;
                        case DBR_TIME_CHAR:
                        cout << "CHAR" << endl;
                            state.m_int_val = ((struct dbr_time_char *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_char, a_args.dbr )
                            break;
                        case DBR_TIME_LONG:
                        cout << "LONG" << endl;
                            state.m_int_val = ((struct dbr_time_long *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_long, a_args.dbr )
                            break;
                        case DBR_TIME_DOUBLE:
                        cout << "DOUBLE" << endl;
                            state.m_real_val = ((struct dbr_time_double *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_double, a_args.dbr )
                            break;
                    default:
                        cout << "UNKNOWN" << endl;
                        break;
                    }

                    // Do not try to send value/alarm data unless device is fully defined
                    if ( !m_defined )
                        return;

                    bool timeout;
                    StreamPacket *pkt = m_stream_api.getFreePacket( 5000, timeout );
                    if ( pkt )
                    {
                        pkt->type = VariableUpdate;
                        pkt->device = ich->second.m_device;
                        pkt->pv = ich->second.m_pv;
                        pkt->state = state;

                        m_stream_api.putFilledPacket( pkt );
                    }
                }
            }
            else if ( epicsIsCtrlRecordType( a_args.type ))
            {
                boost::lock_guard<boost::mutex> lock(m_mutex);

                map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
                if ( ich != m_chan_info.end())
                {
                    cout << "CONTROL! chid: " << ich->first << ", type: " << a_args.type << endl;

                    // Note EPICS does not define ctrl structs (or units) for string types
                    // Enums are defined here also
                    switch ( a_args.type )
                    {
                        case DBR_CTRL_SHORT:
                        cout << "SHORT" << endl;
                            ich->second.m_ca_units = ((struct dbr_ctrl_short *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_FLOAT:
                        cout << "FLOAT" << endl;
                            ich->second.m_ca_units = ((struct dbr_ctrl_float *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_CHAR:
                        cout << "CHAR" << endl;
                            ich->second.m_ca_units = ((struct dbr_ctrl_char *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_LONG:
                        cout << "LONG" << endl;
                            ich->second.m_ca_units = ((struct dbr_ctrl_long *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_DOUBLE:
                        cout << "DOUBLE" << endl;
                            ich->second.m_ca_units = ((struct dbr_ctrl_double *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_ENUM:
                            {
                        cout << "ENUM" << endl;
                            ich->second.m_ca_enum_vals.clear();
                            for ( int i = 0; i < ((struct dbr_ctrl_enum *)a_args.dbr)->no_str; ++i )
                                ich->second.m_ca_enum_vals[i] = ((struct dbr_ctrl_enum *)a_args.dbr)->strs[i];

                            //ich->second.m_pv->m_enum = ich->second.m_pv->m_device->defineEnumeration( values );
                            break;
                            }
                        default:
                            cout << "UNEXPECTED TYPE: " << a_args.type << endl;
                            break;
                    }

                    if ( ich->second.m_chan_state == INFO_PENDING )
                    {
                        cout << "INFO_AVAILABLE for chid: " << ich->first << endl;

                        ich->second.m_chan_state = INFO_AVAILABLE;
                        // Wake state machine
                        m_state_changed = true;
                        m_state_cond.notify_one();
                    }
                }
            }
        }
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, "TraceException in DeviceAgent::epicsEventHandler()" );
        syslog( LOG_ERR, e.toString().c_str() );
    }
    catch( std::exception &e )
    {
        syslog( LOG_ERR, "Exception in DeviceAgent::epicsEventHandler()" );
        syslog( LOG_ERR, e.what() );
    }
    catch(...)
    {
        syslog( LOG_ERR, "Unknown exception in DeviceAgent::epicsEventHandler()" );
    }
}


void
DeviceAgent::sendCurrentValues()
{
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        // If data has been received, timestamp will be non-zero
        if ( ich->second.m_pv_state.m_time.sec )
        {
            bool timeout;
            StreamPacket *pkt = m_stream_api.getFreePacket( 5000, timeout );
            if ( pkt )
            {
                pkt->type = VariableUpdate;
                pkt->device = ich->second.m_device;
                pkt->pv = ich->second.m_pv;
                pkt->state = ich->second.m_pv_state;

                m_stream_api.putFilledPacket( pkt );
            }
        }
    }
}


void
DeviceAgent::epicsConnectionCallback( struct connection_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent*)ca_puser( a_args.chid );

    if ( agent )
        agent->epicsConnectionHandler( a_args );
}


void
DeviceAgent::epicsEventCallback( struct event_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent *)ca_puser( a_args.chid );

    if ( agent )
        agent->epicsEventHandler( a_args );
}



bool
DeviceAgent::epicsIsTimeRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_TIME_STRING && a_rec_type <= DBR_TIME_DOUBLE )
        return true;
    else
        return false;
}


bool
DeviceAgent::epicsIsCtrlRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_CTRL_STRING && a_rec_type <= DBR_CTRL_DOUBLE )
        return true;
    else
        return false;
}


PVType
DeviceAgent::epicsToPVType( uint32_t a_rec_type )
{
    switch ( a_rec_type )
    {
    case DBR_STRING:    return PV_STR;
    case DBR_SHORT:     return PV_INT;
    case DBR_FLOAT:     return PV_REAL;
    case DBR_ENUM:      return PV_ENUM;
    case DBR_CHAR:      return PV_INT;
    case DBR_LONG:      return PV_INT;
    case DBR_DOUBLE:    return PV_REAL;
    default:
        EXCEPT_PARAM( EC_INVALID_PARAM, "Invalid PV type: " << a_rec_type );
    }
}


int32_t
DeviceAgent::epicsToTimeRecordType( uint32_t a_rec_type )
{
    switch ( a_rec_type )
    {
    case DBR_STRING:    return DBR_TIME_STRING;
    case DBR_SHORT:     return DBR_TIME_SHORT;
    case DBR_FLOAT:     return DBR_TIME_FLOAT;
    case DBR_ENUM:      return DBR_TIME_ENUM;
    case DBR_CHAR:      return DBR_TIME_CHAR;
    case DBR_LONG:      return DBR_TIME_LONG;
    case DBR_DOUBLE:    return DBR_TIME_DOUBLE;
    default:            return -1;
    }
}


int32_t
DeviceAgent::epicsToCtrlRecordType( uint32_t a_rec_type )
{
    switch ( a_rec_type )
    {
    case DBR_STRING:    return DBR_CTRL_STRING;
    case DBR_SHORT:     return DBR_CTRL_SHORT;
    case DBR_FLOAT:     return DBR_CTRL_FLOAT;
    case DBR_ENUM:      return DBR_CTRL_ENUM;
    case DBR_CHAR:      return DBR_CTRL_CHAR;
    case DBR_LONG:      return DBR_CTRL_LONG;
    case DBR_DOUBLE:    return DBR_CTRL_DOUBLE;
    default:            return -1;
    }
}


}}

