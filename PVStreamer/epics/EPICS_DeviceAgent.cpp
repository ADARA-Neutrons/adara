#include <iostream>
#include <syslog.h>
#include <alarm.h>

#include "CoreDefs.h"
#include "TraceException.h"
#include "EPICS_DeviceAgent.h"
#include "DeviceDescriptor.h"
#include "ConfigManager.h"
#include "StreamService.h"

using namespace std;

namespace PVS {
namespace EPICS {


DeviceAgent::DeviceAgent( IInputAdapterAPI &a_stream_api, DeviceDescriptor *a_device )
    : m_stream_api(a_stream_api), m_dev_desc(0), m_defined(false), m_active(true)
{
    m_ctrl_thread = new boost::thread(boost::bind( &DeviceAgent::controlThread, this ));

    update(a_device);
}


DeviceAgent::~DeviceAgent()
{
    m_active = false;
    stop();

    m_ctrl_thread->join();
}


void
DeviceAgent::update( DeviceDescriptor *a_device )
{
    syslog( LOG_DEBUG, "DeviceAgent::update(%s)", a_device->m_name.c_str() );

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
        //syslog( LOG_DEBUG, "Redefining existing device" );

        // Disconnect any existing connections that are no longer needed
        for ( ipv = old_desc->m_pvs.begin(); ipv != old_desc->m_pvs.end(); ++ipv )
        {
            if ( !a_device->getPV( (*ipv)->m_name ))
                disconnectPV( *ipv );
        }

        // If a decive is already defined, reuse any shared PV connections
        // Make connections for new PVs in updated device
        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
        {
            old_pv = old_desc->getPV( (*ipv)->m_name );
            if ( !old_pv )
                connectPV( *ipv );
            else
            {
                // PV is shared between old and new device
                // Transfer info to new pv
                (*ipv)->m_type = old_pv->m_type;
                (*ipv)->m_units = old_pv->m_units;
                if ( old_pv->m_type == PV_ENUM )
                    (*ipv)->m_enum = a_device->defineEnumeration( *old_pv->m_enum );

                // Update PV pointer on channel info
                idx = m_pv_index.find( old_pv->m_name );
                if ( idx != m_pv_index.end())
                {
                    ich = m_chan_info.find( idx->second );
                    if ( ich != m_chan_info.end())
                    {
                        ich->second.m_pv = *ipv;
                    }
                }
            }
        }

        // Clean-up & take ownership of descriptor (if used)
        if ( m_dev_desc )
            delete m_dev_desc;
    }
    else
    {
        //syslog( LOG_DEBUG, "Defining new device" );

        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
            connectPV( *ipv );
    }

    m_dev_desc = a_device;
    ca_flush_io();

    // If no new PV channels were created, state machne will not progress on it's own
    // Wake state machine in this case (doesn't matter if its notified more than once)
    m_state_cond.notify_one();
}


void
DeviceAgent::stop()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        if ( ich->second.m_subscribed )
            ca_clear_subscription( ich->second.m_evid );

        ca_clear_channel( ich->second.m_chid );
    }

    m_chan_info.clear();
    m_pv_index.clear();
}


bool
DeviceAgent::stopped()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);
    return m_chan_info.size() == 0;
}


void
DeviceAgent::connectPV( PVDescriptor *a_pv )
{
    // Create a CA channel
    ChanInfo info;
    info.m_pv = a_pv;

    if ( ca_create_channel( a_pv->m_name.c_str(), &epicsConnectionCallback, this, 0, &info.m_chid ) != ECA_NORMAL )
    {
        syslog( LOG_ERR, "Failed to create channel for PV: %s", a_pv->m_name.c_str() );
        EXCEPT( EC_EPICS_API, "Could not create PV channel" );
    }

    m_chan_info[info.m_chid] = info;
    m_pv_index[a_pv->m_name] = info.m_chid;

    syslog( LOG_DEBUG, "Connected PV: %s, chid: %li", a_pv->m_name.c_str(), (long)info.m_chid );
}


void
DeviceAgent::disconnectPV( PVDescriptor *a_pv )
{
    map<std::string,chid>::iterator ipv = m_pv_index.find( a_pv->m_name );
    if ( ipv != m_pv_index.end())
    {
        map<chid,ChanInfo>::iterator ich = m_chan_info.find(ipv->second);
        if ( ich != m_chan_info.end())
        {
            if ( ich->second.m_subscribed )
                ca_clear_subscription( ich->second.m_evid );

            ca_clear_channel( ich->second.m_chid );
            m_chan_info.erase( ich );

            syslog( LOG_DEBUG, "Disconnected PV: %s", a_pv->m_name.c_str() );
        }
        m_pv_index.erase( ipv );
    }
}


void
DeviceAgent::controlThread()
{
    map<chid,ChanInfo>::iterator ich;
    map<std::string,chid>::iterator idx;
    PVDescriptor *pv;
    size_t ready;

    syslog( LOG_DEBUG, "DevAg: control thread started" );

    while( 1 )
    {
        boost::unique_lock<boost::mutex> lock(m_mutex);

        m_state_cond.wait( lock );

        //syslog( LOG_DEBUG, "State machine running" );

        if ( !m_active )
            break;

        ready = 0;
        for ( ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
        {
            switch ( ich->second.m_chan_state )
            {
            case INFO_NEEDED:
                if ( ca_get_callback( epicsToCtrlRecordType( ich->second.m_pv->m_type ), ich->first, epicsEventCallback, this ) == ECA_NORMAL )
                    ich->second.m_chan_state = INFO_PENDING;
                break;
            case READY:
                ++ready;
                break;
            default:
                break;
            }
        }

        if ( m_dev_desc && !m_defined && ready == m_dev_desc->m_pvs.size() )
        {
            //syslog( LOG_DEBUG, "Device Ready" );

            // All PVs for current device are initialized
            // Defined device
            m_dev_record = m_cfg_mgr.defineDevice( *m_dev_desc );

            // Update all channel info objects with new device & pv references
            for ( idx = m_pv_index.begin(); idx != m_pv_index.end(); ++idx )
            {
                pv = m_dev_record->getPV( idx->first );
                ich = m_chan_info.find( idx->second );

                if ( pv && ich != m_chan_info.end() )
                {
                    ich->second.m_device = m_dev_record;
                    ich->second.m_pv = pv;
                }
            }

            m_defined = true;
            delete m_dev_desc;
            m_dev_desc = 0;

            sendLastValues();
        }
#if 0
        else
        {
            cout << "CTRL: [" << (long)m_dev_desc << "][" << m_defined << "][" << ready << "]";
            if ( m_dev_desc )
                cout << "[" << m_dev_desc->m_pvs.size() << "]";
            cout << endl;
        }
#endif
    }

    syslog( LOG_DEBUG, "DevAg: control thread exiting" );
}


void
DeviceAgent::epicsConnectionHandler( struct connection_handler_args a_args )
{
    cout << "ConHdlr op: " << a_args.op << ", chid: " << a_args.chid << endl;

    try
    {
        if ( a_args.op == CA_OP_CONN_UP )
        {
            syslog( LOG_DEBUG, "Connection UP: %li", (long)a_args.chid );

            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end())
            {
                ich->second.m_connected = true;

                if ( !ich->second.m_subscribed )
                {
                    ich->second.m_chan_state = UNINITIALIZED;

                    chtype type = ca_field_type( a_args.chid );
                    if ( VALID_DB_FIELD( type ))
                    {
                        // Save native type and units of PV
                        ich->second.m_pv->m_type = epicsToPVType( type );

                        if ( ca_create_subscription( epicsToTimeRecordType( type ), 0, ich->second.m_chid, DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
                                &epicsEventCallback, this, &ich->second.m_evid ) == ECA_NORMAL )
                        {
                            ich->second.m_subscribed = true;

                            if ( ich->second.m_pv->m_type == PV_STR ) // There is NO ctrl record for EPICS string types
                                ich->second.m_chan_state = READY;
                            else
                                ich->second.m_chan_state = INFO_NEEDED;

                            // Wake state machine
                            m_state_cond.notify_one();
                        }
                    }
                }
            }
        }
        else if ( a_args.op == CA_OP_CONN_DOWN )
        {
            syslog( LOG_DEBUG, "Connection DOWN: %li", (long)a_args.chid );

            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end())
            {
                ich->second.m_connected = false;

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
    cout << "EvHdlr ty: " << a_args.type << ", st: " << a_args.status << endl;

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
                    PVState &state = ich->second.m_pv_state;
                    switch ( a_args.type )
                    {
                        case DBR_TIME_STRING:
                            state.m_str_val = ((struct dbr_time_string *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_string, a_args.dbr )
                            break;
                        case DBR_TIME_SHORT:
                            state.m_int_val = ((struct dbr_time_short *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_short, a_args.dbr )
                            break;
                        case DBR_TIME_FLOAT:
                            state.m_real_val = ((struct dbr_time_float *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_float, a_args.dbr )
                            break;
                        case DBR_TIME_ENUM:
                            state.m_int_val = ((struct dbr_time_enum *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_enum, a_args.dbr )
                            break;
                        case DBR_TIME_CHAR:
                            state.m_int_val = ((struct dbr_time_char *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_char, a_args.dbr )
                            break;
                        case DBR_TIME_LONG:
                            state.m_int_val = ((struct dbr_time_long *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_long, a_args.dbr )
                            break;
                        case DBR_TIME_DOUBLE:
                            state.m_real_val = ((struct dbr_time_double *)a_args.dbr)->value;
                            SET_STATE( state, dbr_time_double, a_args.dbr )
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
                    // Note EPICS does not define ctrl structs (or units) for string types
                    // Enums are defined here also
                    switch ( a_args.type )
                    {
                        case DBR_CTRL_SHORT:
                            ich->second.m_pv->m_units = ((struct dbr_ctrl_short *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_FLOAT:
                            ich->second.m_pv->m_units = ((struct dbr_ctrl_float *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_CHAR:
                            ich->second.m_pv->m_units = ((struct dbr_ctrl_char *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_LONG:
                            ich->second.m_pv->m_units = ((struct dbr_ctrl_long *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_DOUBLE:
                            ich->second.m_pv->m_units = ((struct dbr_ctrl_double *)a_args.dbr)->units;
                            break;
                        case DBR_CTRL_ENUM:
                            {
                            map<int32_t,string> values;
                            for ( int i = 0; i < ((struct dbr_ctrl_enum *)a_args.dbr)->no_str; ++i )
                                values[i] = ((struct dbr_ctrl_enum *)a_args.dbr)->strs[i];

                            ich->second.m_pv->m_enum = ich->second.m_pv->m_device->defineEnumeration( values );
                            break;
                            }
                    }

                    if ( ich->second.m_chan_state == INFO_PENDING )
                    {
                        ich->second.m_chan_state = READY;
                        // Wake state machine
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
DeviceAgent::sendLastValues()
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

