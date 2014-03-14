#include <set>
#include <boost/lexical_cast.hpp>
#include "ConfigManager.h"
#include "StreamService.h"
#include "ADARA.h" // Needed for PV status bits
#include <syslog.h>

using namespace std;

namespace PVS {


//=============================================================================
//===== PUBLIC METHODS ========================================================


ConfigManager::ConfigManager( uint32_t a_offset )
    : m_stream_api(0), m_offset(a_offset)
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
        // This is an existing device

        if ( a_descriptor == *idev->second )
        {
            // Descriptor has not changed, so just return existing record
            record = idev->second;
        }
        else
        {
            // The descriptor is not identical to existing record, analyze differences

            syslog( LOG_DEBUG, "Re-defining device: %s:%s:%lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

            // Record is different, must make new record but try to re-use identifiers
            //const DeviceDescriptor *old_desc = idev->second.get();
            DeviceDescriptor *new_desc = new DeviceDescriptor( a_descriptor ); // Deep copy

            // Now assign/reassign IDs
            new_desc->m_id = idev->second->m_id;

            Identifier next_id = 1;
            vector<PVDescriptor*>::const_iterator ipv2;
            const PVDescriptor *old_pv;

            vector<PVDescriptor*>::iterator ipv = new_desc->m_pvs.begin();
            for ( ; ipv != new_desc->m_pvs.end(); ++ipv )
            {
                // See if new PV is in old PV list - by "friendly name"
                old_pv = idev->second->getPvByName( (*ipv)->m_name );

                // If PV exists in both devices, reuse the old PV ID
                if ( old_pv )
                    (*ipv)->m_id = old_pv->m_id;
                else
                {
                    // New PV, find a free ID (not in old IDs)
                    for ( ipv2 = idev->second->m_pvs.begin(); ipv2 != idev->second->m_pvs.end(); ++ipv2 )
                    {
                        if ( next_id < (*ipv2)->m_id )
                            break;
                        else if ( next_id == (*ipv2)->m_id )
                            next_id++;
                    }

                    (*ipv)->m_id = next_id++;
                }
            }

            // Send PV disconnected packets for old PVs that are not in new descriptor
            for ( ipv = idev->second->m_pvs.begin(); ipv != idev->second->m_pvs.end(); ++ipv )
            {
                if ( !new_desc->getPvByName( (*ipv)->m_name ))
                    sendPvUndefined( idev->second, *ipv );
            }

            // Ensure all new PV names are unique
            makePvNamesUnique( key, *new_desc );

            // Move old record to trash, and save new record
            record = DeviceRecordPtr(new_desc);
            sendDeviceRedefined( record, idev->second );

            idev->second = record;
        }
    }
    else
    {
        // This is a NEW device

        syslog( LOG_DEBUG, "Defining device: %s:%s:%lu", a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(), (unsigned long)a_descriptor.m_protocol );

        // No existing record, create a new one copied from the passed-in descriptor
        DeviceDescriptor *new_desc = new DeviceDescriptor( a_descriptor ); // Deep copy

        // Assign an ID to the device and PVs
        new_desc->m_id = getNextDeviceID();

        // PV IDs can be assigned arbitrarily for new devices
        Identifier id = 1;
        for ( vector<PVDescriptor*>::iterator ipv = new_desc->m_pvs.begin(); ipv != new_desc->m_pvs.end(); ++ipv )
            (*ipv)->m_id = id++;

        // Ensure all new PV names are unique
        makePvNamesUnique( key, *new_desc );

        // Store new record in devices vector
        record = DeviceRecordPtr(new_desc);
        m_devices[key] = record;

        sendDeviceDefined( record );
    }

    return record;
}


/**
 * @brief Removes a device from configuration database
 * @param a_record - Device to undefine
 *
 * This method undefines a device (removes from configuration database). This method results in stream
 * packets being emitted to set the undefined device PVs to "disconnected" state, followed by a
 * "device undefined" packet. Note that the device pointers in these messages are shared pointer, so
 * the device record will persist until the emitted packets are consumed and cleared.
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
        // Send PV disconnected packets for old PVs that are not in new descriptor
        for ( vector<PVDescriptor*>::iterator ipv = idev->second->m_pvs.begin(); ipv != idev->second->m_pvs.end(); ++ipv )
            sendPvUndefined( idev->second, *ipv );

        sendDeviceUndefined( idev->second );

        m_devices.erase( idev );
    }
}


//=============================================================================
//===== PRIVATE METHODS =======================================================

/**
 * @brief Generates a locally unique string identifier for device
 * @param a_device_name - Name of device
 * @param a_source - Name of source (or host)
 * @param a_protocol - Protocal
 * @return A string identifier (key) for device
 */
string
ConfigManager::makeDeviceKey( const string &a_device_name, const string &a_source, Protocol a_protocol ) const
{
    return a_device_name + boost::lexical_cast<string>(a_protocol) + a_source;
}


/** \brief Gets next available device identifier
  * \return New identifier
  *
  * This method finds and returns the lowest-valued free device identifier,
  * starting at 1 + offset. This new identifier is only reserved while m_mutex
  * is locked (i.e. must configure new device before releasing mutex).
  */
Identifier
ConfigManager::getNextDeviceID() const
{
    set<Identifier> ids;
    for (  map<std::string,DeviceRecordPtr>::const_iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
        ids.insert(idev->second->m_id);

    Identifier id = 1 + m_offset;

    for (  set<Identifier>::const_iterator i = ids.begin(); i != ids.end(); ++i )
    {
        if ( id < *i )
            break;
        else
            id = (*i) + 1;
    }

    return id;
}


/**
 * @brief This method ensures PV names on device are unique
 * @param a_key - Device map key for device being checked
 * @param a_descriptor - Descriptor for device being checked
 *
 * This method examines the PV names of the specfied descriptor agains all currently configure
 * PVs except for a descriptor being redefined (i.e. with the same key). If a conflict is found
 * the new conflicting PV is renamed by appeding a "_dupX" suffix (where X is a conflict
 * counter) and checked again. This continues until all conflicts have been resolved.
 *
 * This step tries to ensure that ADARA clients will receive PVs with unique names. PV names are
 * required to be unique, and this routine is a "just-in-case" measure to prevent editing errors
 * from causing serious client issues. This method can not prevent other data sources from
 * injecting Device Descriptor packets with conflicting names. This condition must be trapped by
 * the SMS.
 */
void
ConfigManager::makePvNamesUnique( const string &a_key, DeviceDescriptor &a_descriptor )
{
    vector<PVDescriptor*>::const_iterator ipv;
    set<string> names;
    set<string>::iterator inames;

    for ( map<string,DeviceRecordPtr>::iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
    {
        // Skip device being redefined
        if ( idev->first == a_key )
            continue;

        for ( ipv = idev->second->m_pvs.begin(); ipv != idev->second->m_pvs.end(); ++ipv )
            names.insert( (*ipv)->m_name );
    }

    string          new_name;
    unsigned short  count;

    for ( ipv = a_descriptor.m_pvs.begin(); ipv != a_descriptor.m_pvs.end(); ++ipv )
    {
        count = 0;
        new_name = (*ipv)->m_name;

        while ( 1 )
        {
            if ( count )
                new_name = (*ipv)->m_name + "_dup" + boost::lexical_cast<string>(count);

            if ( names.find( new_name ) != names.end() )
                ++count;
            else
                break;
        }

        (*ipv)->m_name = new_name;
    }
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


void
ConfigManager::sendPvUndefined( DeviceRecordPtr a_dev_desc, PVDescriptor *a_pv_desc )
{
    bool timeout;
    StreamPacket *pkt = m_stream_api->getFreePacket( 5000, timeout );
    if ( pkt )
    {
        pkt->type = VariableUpdate;
        pkt->device = a_dev_desc;
        pkt->pv = a_pv_desc;
        pkt->state = PVState( ::ADARA::VariableStatus::UPSTREAM_DISCONNECTED, ::ADARA::VariableSeverity::INVALID );

        m_stream_api->putFilledPacket( pkt );
    }
}

}

