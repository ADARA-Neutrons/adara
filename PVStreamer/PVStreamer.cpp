#include "stdafx.h"

#include <boost/thread/locks.hpp>

#include "PVStreamer.h"
#include "PVConfig.h"
#include "PVReader.h"
#include "PVWriter.h"

using namespace std;

namespace SNS { namespace PVS {

// ---------- Static initialization -------------------------------------------

Identifier PVStreamer::m_next_enum_id = 1;

// ---------- Constructors & Destructors --------------------------------------

PVStreamer::PVStreamer( size_t a_pkt_buffer_size, size_t a_max_notify_pkts )
: m_writer(0), m_pkt_buffer_size(a_pkt_buffer_size), m_max_notify_pkts(a_max_notify_pkts),m_stream_listeners_thread(0)
{
    if ( m_pkt_buffer_size < 2 || m_max_notify_pkts > m_pkt_buffer_size )
        throw -1;

    if ( !m_max_notify_pkts )
        m_max_notify_pkts = a_pkt_buffer_size >> 1;

    // Fill free queue;
    for ( size_t i = 0; i < a_pkt_buffer_size; ++i )
        m_free_que.put( new PVStreamPacket());

    // Start stream listener notify thread
    m_stream_listeners_thread = new boost::thread( boost::bind(&PVStreamer::streamListenersNotifyThreadFunc, this));
}

PVStreamer::~PVStreamer()
{
    if ( m_stream_listeners_thread )
        delete m_stream_listeners_thread;

    // Delete enums
    for ( map<unsigned long,Enum*>::iterator e = m_enums.begin(); e != m_enums.end(); ++e )
        delete e->second;
}

// ---------- General public methods ------------------------------------------

void
PVStreamer::attachConfigListener( IPVConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);
    if ( find(m_config_listeners.begin(), m_config_listeners.end(), &a_listener) == m_config_listeners.end())
    {
        m_config_listeners.push_back(&a_listener);
    }
}

void
PVStreamer::detachConfigListener( IPVConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);
    vector<IPVConfigListener*>::iterator l = find(m_config_listeners.begin(), m_config_listeners.end(), &a_listener);
    if ( l != m_config_listeners.end())
    {
        m_config_listeners.erase(l);
    }
}

void
PVStreamer::attachStreamListener( IPVStreamListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);
    if ( find(m_stream_listeners.begin(), m_stream_listeners.end(), &a_listener) == m_stream_listeners.end())
    {
        m_stream_listeners.push_back( &a_listener );
    }
}

void
PVStreamer::detachStreamListener( IPVStreamListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);
    vector<IPVStreamListener*>::iterator l = find(m_stream_listeners.begin(), m_stream_listeners.end(), &a_listener);
    if ( l != m_stream_listeners.end())
    {
        m_stream_listeners.erase(l);
    }
}


IPVConfigServices*
PVStreamer::attach( PVConfig &a_config )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_config.find(a_config.getProtocol()) == m_config.end())
    {
        m_config[a_config.getProtocol()] = &a_config;
        return this;
    }

    throw -1;
}

IPVReaderServices*
PVStreamer::attach( PVReader &a_reader )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_readers.find(a_reader.getProtocol()) == m_readers.end())
    {
        m_readers[a_reader.getProtocol()] = &a_reader;
        return this;
    }

    throw -1;
}

IPVWriterServices*
PVStreamer::attach( PVWriter &a_writer )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( !m_writer )
    {
        m_writer = &a_writer;
        return this;
    }

    throw -1;
}

// ---------- Device & PV access methods --------------------------------------

/*
void
PVStreamer::getActivePVs( map<Identifier,vector<const PVInfo*> > &a_pvs ) const
{
    a_pvs.clear();

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    for (map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
    {
        if ( ipv->second->m_active )
        {
            a_pvs[ipv->second->m_device_id].push_back( ipv->second );
        }
    }
}
*/

void
PVStreamer::getActiveDevices( std::vector<Identifier> &a_devs ) const
{
    a_devs.clear();

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    for (map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
    {
        if ( ipv->second->m_active )
        {
            if ( !a_devs.size() || (a_devs.size() && a_devs.back() != ipv->second->m_device_id ))
                a_devs.push_back(ipv->second->m_device_id);
        }
    }
}

bool
PVStreamer::isPVDefined( Identifier a_dev_id, Identifier a_pv_id ) const
{
    PVKey key(a_dev_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.find(key);
    if ( ipv != m_pv_info.end())
        return true;
    else
        return false;
}

const PVInfo*
PVStreamer::getPV( Identifier a_dev_id, Identifier a_pv_id ) const
{
    PVKey key(a_dev_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.find(key);
    if ( ipv != m_pv_info.end())
        return ipv->second;

    throw -1;
}

const map<Identifier,const Enum*> &
PVStreamer::getEnums() const
{
    return (const map<Identifier,const Enum*>&)m_enums;
}

const vector<const PVInfo*>&
PVStreamer::getDevicePVs( Identifier a_dev_id ) const
{
    return (const vector<const PVInfo*>&) getWriteableDevicePVs( a_dev_id );
}

bool
PVStreamer::isDeviceDefined( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end())
        return true;
    else
        return false;
}

std::string
PVStreamer::getDeviceName( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end() )
        return idev->second->name;

    throw -1;
}

bool
PVStreamer::isAppDefined( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    return m_apps.find(a_app_id) != m_apps.end();
}

const vector<Identifier>&
PVStreamer::getAppDevices( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,AppInfo*>::const_iterator iapp = m_apps.find(a_app_id);
    if ( iapp != m_apps.end())
    {
        return (const vector<Identifier>&) iapp->second->devices;
    }
    else
        throw -1;
}

// ---------- IPVConfigServices methods ---------------------------------------

void
PVStreamer::defineApp( Protocol a_protocol, Identifier a_app_id, const std::string &a_source )
{
    AppInfo *info = 0;

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    if ( m_apps.find(a_app_id) == m_apps.end())
    {
        info = new AppInfo;

        info->app_id = a_app_id;
        info->protocol = a_protocol;
        info->source = a_source;

        m_apps[a_app_id] = info;
    }
}

void
PVStreamer::undefineApp( Identifier a_app_id )
{
    // TODO Implement Me!!!
}

void
PVStreamer::defineDevice( Protocol a_protocol, Identifier a_dev_id, const string &a_name, const string &a_source, Identifier a_app_id )
{
    DeviceInfo *info = 0;

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    if ( m_devices.find( a_dev_id ) == m_devices.end())
    {
        info = new DeviceInfo;

        info->device_id = a_dev_id;
        info->app_id = a_app_id;
        info->name = a_name;
        info->protocol = a_protocol;
        info->source = a_source;

        m_devices[a_dev_id] = info;

        // If an application is specified, add this device to it
        if ( a_app_id )
        {
            map<Identifier,AppInfo*>::iterator ia = m_apps.find(a_app_id);
            if ( ia != m_apps.end())
                ia->second->devices.push_back(a_dev_id);
            else
                throw -1;
        }
    }
    else
        throw -1;
}

void
PVStreamer::undefineDevice( Identifier a_dev_id )
{
    //TODO Implement Me!!!
}

void
PVStreamer::undefineDeviceIfNoPVs( Identifier a_dev_id )
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::iterator idev = m_devices.find( a_dev_id );

    if ( idev != m_devices.end())
    {
        if ( !idev->second->pvs.size())
        {
            map<Identifier,AppInfo*>::iterator iapp = m_apps.find( idev->second->app_id );
            if ( iapp != m_apps.end())
            {
                vector<Identifier>::iterator ii = find(iapp->second->devices.begin(),iapp->second->devices.end(),a_dev_id);
                if ( ii != iapp->second->devices.end())
                    iapp->second->devices.erase(ii);
            }

            delete idev->second;
            m_devices.erase(idev);
        }
    }
}


void
PVStreamer::definePV( PVInfo & info )
{
    PVKey key(info.m_device_id,info.m_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::iterator idev = m_devices.find(info.m_device_id);

    if ( idev == m_devices.end())
        throw -1;

    if ( m_pv_info.find(key) == m_pv_info.end())
    {
        if ( info.m_protocol != idev->second->protocol || info.m_source != idev->second->source )
            throw -1;

        idev->second->pvs.push_back(&info);
        m_pv_info[key] = &info;
    }
    else
        throw -1;
}

void
PVStreamer::undefinePV( Identifier a_dev_id, Identifier a_pv_id )
{
    //TODO Implement Me!!!
}

const Enum*
PVStreamer::getEnum( Identifier a_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,Enum*>::const_iterator e = m_enums.find(a_id);
    if ( e != m_enums.end())
        return e->second;
    else
        return 0;
}

const Enum*
PVStreamer::defineEnum( const map<int,string> &a_values )
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    for ( map<Identifier,Enum*>::const_iterator e = m_enums.begin(); e != m_enums.end(); ++e )
    {
        if ( *(e->second) == a_values )
            return e->second;
    }

    Enum *new_enum = new Enum( m_next_enum_id, a_values );
    m_enums[m_next_enum_id++] = new_enum;

    return new_enum;
}

void
PVStreamer::configurationLoaded( Protocol a_protocol, const string &a_source )
{
    // Notify config listeners
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);

    for ( vector<IPVConfigListener*>::iterator il = m_config_listeners.begin(); il != m_config_listeners.end(); ++il )
        (*il)->configurationLoaded( a_protocol, a_source );

}

/**
 * This method finds a PV entry by "friendly name". This method is used when loading
 * configuration files and only a PV name is available. Use sparingly as it is a
 * linear string-value based search.
 */
PVInfo*
PVStreamer::getWriteablePV( const std::string & a_name ) const
{
    for ( map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
    {
        if ( ipv->second->m_name == a_name )
            return ipv->second;
    }

    return 0;
}


// ---------- IPVReaderServices methods ----------

PVStreamPacket*
PVStreamer::getFreePacket()
{
    return m_free_que.get();
}

void
PVStreamer::putFilledPacket( PVStreamPacket *a_pkt )
{
    m_fill_que.put(a_pkt);
}

PVInfo*
PVStreamer::getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const
{
    return (PVInfo*)getPV( a_dev_id, a_pv_id );
}

vector<PVInfo*>&
PVStreamer::getWriteableDevicePVs( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end() )
        return idev->second->pvs;
    else
        throw -1;
}

void
PVStreamer::getSourceInfo( Protocol a_protocol, const string &a_source, map<Identifier,vector<PVInfo*> > &a_info ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    a_info.clear();
    for ( map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
    {
        if ( idev->second->protocol == a_protocol && idev->second->source == a_source )
        {
            a_info[idev->first] = idev->second->pvs;
        }
    }
}

// ---------- IPVWriterServices methods ----------

PVStreamPacket*
PVStreamer::getFilledPacket()
{
    return m_fill_que.get();
}

void
PVStreamer::putFreePacket( PVStreamPacket *a_pkt )
{
    // If the notify buffer is backed-up, bypass it. This will cause stream
    // listeners to miss packets under heavy load, but it will maintain the
    // output stream integrity.

    if ( m_notify_que.size() < m_max_notify_pkts )
        m_notify_que.put(a_pkt);
    else
        m_free_que.put(a_pkt);
}

// ---------- IPVStreamListener support methods ----------

void
PVStreamer::streamListenersNotifyThreadFunc()
{
    PVStreamPacket* pkt;

    while(1)
    {
        pkt = m_notify_que.get();
        notifyStreamListeners(pkt);
        m_free_que.put(pkt);
    }
}

void
PVStreamer::notifyStreamListeners( PVStreamPacket *a_pkt )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);

    if ( m_stream_listeners.size() )
    {
        try
        {
            switch( a_pkt->pkt_type )
            {
            case DeviceActive:
                {
                    string name = getDeviceName( a_pkt->device_id );
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->deviceActive( a_pkt->time, a_pkt->device_id, name );
                }
                break;

            case DeviceInactive:
                {
                    string name = getDeviceName( a_pkt->device_id );
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->deviceInactive( a_pkt->time, a_pkt->device_id, name );
                }
                break;

            case VarActive:
                for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                    (*il)->pvActive( a_pkt->time, *(a_pkt->pv_info) );
                break;

            case VarInactive:
                for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                    (*il)->pvInactive( a_pkt->time, *(a_pkt->pv_info) );
                break;

            case VarStatusUpdate:
                for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                    (*il)->pvStatusUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->alarms );
                break;

            case VarValueUpdate:
                switch ( a_pkt->pv_info->m_type )
                {
                case PV_INT:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->ival );
                    break;

                case PV_ENUM:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->ival, a_pkt->pv_info->m_enum );
                    break;

                case PV_UINT:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->uval );
                    break;

                case PV_DOUBLE:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->dval );
                    break;
                }
                break;
            default:
                break;
            }
        }
        catch(...)
        {
            // Don't really care if a client has problems - just don't want to
            // propagate exception beyond this method
        }
    }
}

}}
