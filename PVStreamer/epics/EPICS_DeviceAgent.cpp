#include <iostream>

#include "CoreDefs.h"
#include "TraceException.h"
#include "EPICS_DeviceAgent.h"
#include "DeviceDescriptor.h"

using namespace std;

namespace PVS {
namespace EPICS {


DeviceAgent::DeviceAgent( IInputAdapterAPI &a_stream_api, ConfigManager &a_cfg_mgr, DeviceDescriptor *a_device )
    : m_stream_api(a_stream_api), m_cfg_mgr(a_cfg_mgr), m_device(0)
{
    cout << "New device: " << a_device->m_name << endl;

    update(a_device);

    //ca_client_status(4);
}


DeviceAgent::~DeviceAgent()
{
    // TODO Disconnect all PVs and wait for CBs to exit
}


void
DeviceAgent::update( DeviceDescriptor *a_device )
{
    vector<PVDescriptor*>::iterator ipv;

    if ( m_device )
    {
        // Disconnect any existing connections that are no longer needed
        for ( ipv = m_device->m_pvs.begin(); ipv != m_device->m_pvs.end(); ++ipv )
        {
            if ( !a_device->getPV( (*ipv)->m_name ))
            {
                disconnectPV( *ipv );
            }
        }

        // Clean-up & take ownership of descriptor
        DeviceDescriptor *old_device = m_device;
        m_device = a_device;
        delete old_device;

        // If a decive is already defined, reuse any shared PV connections
        // Make connections for new PVs in updated device
        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
        {
            if ( !m_device->getPV( (*ipv)->m_name ))
            {
                connectPV( *ipv );
            }
        }
    }
    else
    {
        m_device = a_device;

        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
        {
            connectPV( *ipv );
        }

    }

    //ca_flush_io();
    ca_pend_io(2);
    resendDeviceDescriptor();
}


void
DeviceAgent::stop()
{
}


bool
DeviceAgent::stopped()
{
    return true;
}


void
DeviceAgent::resendDeviceDescriptor()
{
    //TODO If all PVs connected and resolved, send device descriptor
    // Otherwise set reminder flag
}


void
DeviceAgent::connectPV( PVDescriptor *a_pv )
{
    // Create a CA channel
    ChanInfo info;

    if ( ca_create_channel( a_pv->m_name.c_str(), &epicsConnectionCallback, this, 0, &info.m_chid ) != ECA_NORMAL )
        EXCEPT( EC_EPICS_API, "Could not create PV channel" );

    m_chan_info[info.m_chid] = info;
    m_pv_index[a_pv->m_name] = info.m_chid;

    cout << "PV conn: " << a_pv->m_name << ", chid: " << info.m_chid << endl;
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

            cout << "PV disconn: " << a_pv->m_name << endl;
        }
        m_pv_index.erase( ipv );
    }
}


void
DeviceAgent::epicsConnectionHandler( struct connection_handler_args a_args )
{
    if ( a_args.op == CA_OP_CONN_UP )
    {
        cout << "CON UP: " << a_args.chid << endl;

        map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
        if ( ich != m_chan_info.end())
        {
            ich->second.m_connected = true;
            if ( !ich->second.m_subscribed )
            {
                chtype type = ca_field_type( a_args.chid );

                if ( VALID_DB_FIELD( type ))
                {
                    if ( ca_create_subscription( DBR_INT, 0, ich->second.m_chid, DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
                            &epicsEventCallback, this, &ich->second.m_evid ) == ECA_NORMAL )
                    {
                        ich->second.m_subscribed = true;
                    }
                }
            }
        }
    }
    else if ( a_args.op == CA_OP_CONN_DOWN )
    {
        cout << "CON DOWN: " << a_args.chid << endl;

        map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
        if ( ich != m_chan_info.end())
        {
            ich->second.m_connected = false;
        }
    }
}


void
DeviceAgent::epicsEventHandler( struct event_handler_args a_args )
{
    cout << "Event: " << a_args.chid << endl;

    if ( a_args.status == ECA_NORMAL && a_args.dbr != 0 )
    {
        // value is in dbr
        //a_args.type
    }
}


void
DeviceAgent::epicsConnectionCallback( struct connection_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent*)ca_puser( a_args.chid );

    cout << "con callback: " << agent << endl;

    if ( agent )
        agent->epicsConnectionHandler( a_args );
}


void
DeviceAgent::epicsEventCallback( struct event_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent *)ca_puser( a_args.chid );

    cout << "event callback: " << agent << endl;

    if ( agent )
        agent->epicsEventHandler( a_args );
}


}}

