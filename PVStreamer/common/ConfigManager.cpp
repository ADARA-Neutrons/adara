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
{}


ConfigManager::~ConfigManager()
{}


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
    //syslog( LOG_DEBUG, "ConfigMgr::defineDevice: %s, %s, %lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

    boost::lock_guard<boost::mutex> lock(m_mutex);
    DeviceRecordPtr record;

    // Compare to the latest configuration records
    string key = makeDeviceKey( a_descriptor.m_name, a_descriptor.m_source, a_descriptor.m_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
        // Existing record found, compare to provided descriptor
        if ( a_descriptor == *idev->second )
        {
            // Records are same, just return existing record
            record = idev->second;
        }
        else
        {
            syslog( LOG_DEBUG, "Re-defining device: %s:%s:%lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

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
            sendDeviceRedefined( record, idev->second );

            idev->second = record;
        }
    }
    else
    {
        syslog( LOG_DEBUG, "Defining device: %s:%s:%lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

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


/** \brief Undefine specified devices
  *
  */
void
ConfigManager::undefineDevice( DeviceRecordPtr &a_record )
{
    syslog( LOG_DEBUG, "Un-defining device: %s:%s:%lu", a_record->m_name.c_str(), a_record->m_source.c_str(), (unsigned long)a_record->m_protocol );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Compare to the latest configuration records (those in trash don't matter)
    string key = makeDeviceKey( a_record->m_name, a_record->m_source, a_record->m_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
        sendDeviceUndefined( idev->second );

        m_devices.erase( idev );
    }
}


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

}

