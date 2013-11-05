#include <boost/lexical_cast.hpp>
#include "ConfigManager.h"
#include "StreamService.h"
#include <syslog.h>

using namespace std;

namespace PVS {


//=============================================================================
//===== PUBLIC METHODS ========================================================


ConfigManager::ConfigManager()
    : m_stream_api(0)
{
#ifdef USE_GC
    m_running = true;
    m_gc_thread = new boost::thread(boost::bind( &ConfigManager::gcThread, this ));
#endif
}


ConfigManager::~ConfigManager()
{
#ifdef USE_GC
    m_running = false;
    m_gc_thread->join();
    delete m_gc_thread;
#endif
}


void
ConfigManager::attach( IInputAdapterAPI *a_stream_api )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_stream_api = a_stream_api;
}


DeviceRecordPtr
ConfigManager::getDeviceConfig( const string &a_device_name, const string &a_source, Protocol a_protocol )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    string key = makeDeviceKey( a_device_name, a_source, a_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
        DeviceRecordPtr dev( idev->second );
        return dev;
    }

    return DeviceRecordPtr();
}


/**
  * This method compares the provided device descriptor with any current
  * configuration records and will either define a new device, redefine an
  * existing device, or take no action if none is required.
  */
DeviceRecordPtr
ConfigManager::defineDevice( DeviceDescriptor &a_descriptor )
{
    syslog( LOG_DEBUG, "ConfigMgr::defineDevice: %s, %s, %lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

    boost::lock_guard<boost::mutex> lock(m_mutex);
    DeviceRecordPtr record;

    // Compare to the latest configuration records (those in trash don't matter)
    string key = makeDeviceKey( a_descriptor.m_name, a_descriptor.m_source, a_descriptor.m_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
        // Existing record found, compare to provided descriptor
        if ( a_descriptor == *idev->second )
        {
            syslog( LOG_DEBUG, "ConfigMgr: device already defined" );

            // Records are same, just return existing record
            record = idev->second;
        }
        else
        {
            syslog( LOG_DEBUG, "ConfigMgr: device being redefined" );

            // Record is different, must make new record but try to re-use identifiers
            const DeviceDescriptor *old_desc = idev->second.get();
            DeviceDescriptor *new_desc = new DeviceDescriptor( a_descriptor ); // Deep copy

            // Now assign/reassign IDs
            new_desc->m_id = old_desc->m_id;

            Identifier next_id = 1;
            vector<PVDescriptor*>::const_iterator ipv;
            const PVDescriptor *old_pv;

            for ( vector<PVDescriptor*>::iterator new_pv = new_desc->m_pvs.begin(); new_pv != new_desc->m_pvs.end(); ++new_pv )
            {
                // See if new PV is in old PV list - by "friendly name", if so, reuse ID
                old_pv = old_desc->getPV( (*new_pv)->m_name );

                if ( old_pv )
                    (*new_pv)->m_id = old_pv->m_id;
                else
                {
                    // New PV, find a free ID (not in old IDs)
                    for ( ipv = old_desc->m_pvs.begin(); ipv != old_desc->m_pvs.end(); ++ipv )
                    {
                        if ( next_id < (*ipv)->m_id )
                            break;
                        else if ( next_id == (*ipv)->m_id )
                            next_id++;
                    }

                    (*new_pv)->m_id = next_id++;
                }
            }

            // Move old record to trash, and save new record
            record = DeviceRecordPtr(new_desc);
#ifdef USE_GC
            m_garbage.insert( idev->second );
#endif
            sendDeviceRedefined( record, idev->second );

            idev->second = record;
        }
    }
    else
    {
        syslog( LOG_DEBUG, "ConfigMgr: device being defined" );

        // No existing record, create a new one copied from the passed-in descriptor
        DeviceDescriptor *new_desc = new DeviceDescriptor( a_descriptor ); // Deep copy

        // Assign an ID to the device and PVs
        new_desc->m_id = getNextDeviceID();

        // PV IDs can be assigned arbitrarily for new devices
        Identifier id = 1;
        for ( vector<PVDescriptor*>::iterator ipv = new_desc->m_pvs.begin(); ipv != new_desc->m_pvs.end(); ++ipv )
            (*ipv)->m_id = id++;

        // Store new record in devices vector
        record = DeviceRecordPtr(new_desc);
        m_devices[key] = record;

        sendDeviceDefined( record );
    }

    return record;
}

#if 0
/** \brief Undefine all devices of specified protocol on specified host
  *
  */
void
ConfigManager::undefineDevice( const std::string &a_source, Protocol a_protocol )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    for ( map<string,DeviceRecordPtr>::iterator idev = m_devices.begin(); idev != m_devices.end();  )
    {
        if ( idev->second->m_protocol == a_protocol && idev->second->m_source == a_source )
        {
#ifdef USE_GC
            cout << "Moving " << idev->second.get() << " to garbage" << endl;
            m_garbage.insert( idev->second );
#endif
            m_devices.erase( idev++ );
        }
        else
            ++idev;
    }
}
#endif


/** \brief Undefine specified devices
  *
  */
void
ConfigManager::undefineDevice( const std::string &a_name, const std::string &a_source, Protocol a_protocol )
{
    syslog( LOG_DEBUG, "ConfigMgr::undefineDevice: %s, %s, %lu", a_name.c_str(), a_source.c_str(), (unsigned long)a_protocol );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Compare to the latest configuration records (those in trash don't matter)
    string key = makeDeviceKey( a_name, a_source, a_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
#ifdef USE_GC
        m_garbage.insert( idev->second );
#endif
        sendDeviceUndefined( idev->second );

        m_devices.erase( idev );
    }
}

#if 0
void
ConfigManager::attachListener( IConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    vector<IConfigListener *>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l == m_listeners.end())
        m_listeners.push_back( &a_listener );
}


void
ConfigManager::detachListener( IConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    vector<IConfigListener *>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end())
        m_listeners.erase( l );
}
#endif


//=============================================================================
//===== PRIVATE METHODS =======================================================

string
ConfigManager::makeDeviceKey( const string &a_device_name, const string &a_source, Protocol a_protocol ) const
{
    return a_device_name + boost::lexical_cast<string>(a_protocol) + a_source;
}


Identifier
ConfigManager::getNextDeviceID() const
{
    Identifier id = 1;
    for (  map<std::string,DeviceRecordPtr>::const_iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
    {
        if ( id < idev->second->m_id )
            break;
        else
            id = idev->second->m_id + 1;
    }

    return id;
}


void
ConfigManager::sendDeviceDefined( DeviceRecordPtr a_dev_desc )
{
    bool timeout;
    StreamPacket *pkt = m_stream_api->getFreePacket( 5000, timeout );
    if ( pkt )
    {
        pkt->type = DeviceDefined;
        pkt->device = a_dev_desc;

        m_stream_api->putFilledPacket( pkt );
    }
}


void
ConfigManager::sendDeviceUndefined( DeviceRecordPtr a_dev_desc )
{
    bool timeout;
    StreamPacket *pkt = m_stream_api->getFreePacket( 5000, timeout );
    if ( pkt )
    {
        pkt->type = DeviceUndefined;
        pkt->device = a_dev_desc;

        m_stream_api->putFilledPacket( pkt );
    }
}


void
ConfigManager::sendDeviceRedefined( DeviceRecordPtr a_dev_desc, DeviceRecordPtr a_old_dev_desc )
{
    bool timeout;
    StreamPacket *pkt = m_stream_api->getFreePacket( 5000, timeout );
    if ( pkt )
    {
        pkt->type = DeviceRedefined;
        pkt->device = a_dev_desc;
        pkt->old_device = a_old_dev_desc;

        m_stream_api->putFilledPacket( pkt );
    }
}



#ifdef USE_GC

void
ConfigManager::gcThread()
{
    //cout << "GC thread started" << endl;

    set<DeviceRecordPtr>::iterator i;
    uint32_t    count = 0;

    while( m_running )
    {
        sleep(1);
        count++;

        if ( !(count % 2 ))
        {
            cout << "GC running" << endl;

            boost::lock_guard<boost::mutex> lock(m_mutex);
            for ( i = m_garbage.begin(); i != m_garbage.end(); )
            {
                if ( (*i).unique() )
                {
                    cout << "deleting device " << (*i).get() << "," << (*i)->m_id << "," << (*i)->m_name << endl;
                    m_garbage.erase( i++ );
                }
                else
                    ++i;
            }
        }
    }
    cout << "GC thread exiting" << endl;
}

#endif

}

