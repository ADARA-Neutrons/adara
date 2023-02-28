#include <set>
#include <boost/lexical_cast.hpp>
#include "ConfigManager.h"
#include "StreamService.h"
#include "ADARA.h" // Needed for PV status bits
#include <syslog.h>
#include <unistd.h>

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
ConfigManager::defineDevice( DeviceDescriptor &a_descriptor,
        bool &a_device_changed )
{
    // syslog( LOG_DEBUG, "ConfigMgr::defineDevice(): [%s] %s/%lu",
        // a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(),
        // (unsigned long)a_descriptor.m_protocol );
    // usleep(33333); // give syslog a chance...

    boost::lock_guard<boost::mutex> lock(m_mutex);
    DeviceRecordPtr record;

    a_device_changed = false;

    // Compare to the latest configuration records
    string key = makeDeviceKey( a_descriptor.m_name, a_descriptor.m_source,
        a_descriptor.m_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );

    // This is an existing device
    if ( idev != m_devices.end() )
    {
        // Note: DeviceDescriptor's Operator== Does _Not_ Compare
        // Device ID (m_id), Active Status PV or Ready Status (m_ready)!
        if ( a_descriptor == *idev->second )
        {
            // Check for Device ID Re-Numbered...
            if ( a_descriptor.m_id != idev->second->m_id )
            {
                syslog( LOG_ERR, "%s %s: %s: [%s] %s/%lu, (%s %d -> %d)",
                    "PVSD ERROR:", "ConfigManager::defineDevice()",
                    "Device Definition Unchanged",
                    a_descriptor.m_name.c_str(),
                    a_descriptor.m_source.c_str(),
                    (unsigned long) a_descriptor.m_protocol,
                    "*** Device ID Re-Numbered",
                    idev->second->m_id, a_descriptor.m_id );
                usleep(33333); // give syslog a chance...

                DeviceDescriptor *new_desc = new DeviceDescriptor(
                    *idev->second ); // Deep Copy, Keep ID!

                new_desc->m_id = a_descriptor.m_id;

                // Device ID Changed, Use New Descriptor...
                record = DeviceRecordPtr(new_desc);
                sendDeviceRedefined( record, idev->second );

                idev->second = record;

                a_device_changed = true;
            }
            else
            {
                syslog( LOG_INFO, "%s: %s: [%s] %s/%lu (Device ID=%d)",
                    "ConfigManager::defineDevice()",
                    "Device Definition Unchanged",
                    a_descriptor.m_name.c_str(),
                    a_descriptor.m_source.c_str(),
                    (unsigned long) a_descriptor.m_protocol,
                    a_descriptor.m_id );
                usleep(33333); // give syslog a chance...

                // Descriptor has not changed, just return existing record
                record = idev->second;
            }

            // Make Sure Any Updated Active Status PV is Used...
            if ( !a_descriptor.m_active_pv_conn.empty() )
            {
                // If the Active Status PV Changed, Re-Create It...
                if ( a_descriptor.m_active_pv_conn.compare(
                    record->m_active_pv_conn ) )
                {
                    syslog( LOG_ERR,
                        "%s %s: %s: [%s] (%s: [%s] -> [%s], %s = %d (%s))",
                        "PVSD ERROR:", "ConfigManager::defineDevice()",
                        "Re-Creating Active Status PV",
                        a_descriptor.m_name.c_str(),
                        "*** Active Status PV Connection Changed",
                        record->m_active_pv_conn.c_str(),
                        a_descriptor.m_active_pv_conn.c_str(),
                        "active", a_descriptor.m_active,
                        ( a_descriptor.m_active ) ? "true" : "false" );
                    usleep(33333); // give syslog a chance...

                    delete record->m_active_pv;

                    record->m_active_pv_conn =
                        a_descriptor.m_active_pv_conn;

                    record->m_active_pv = new PVDescriptor(
                        record.get(), *(a_descriptor.m_active_pv) );

                    record->m_active = a_descriptor.m_active;
                }
            }

            // Otherwise, Clear Out Any Active Status Meta-Data...
            else if ( !record->m_active_pv_conn.empty() )
            {
                syslog( LOG_ERR, "%s %s: %s: [%s] (%s: [%s] -> [])",
                    "PVSD ERROR:", "ConfigManager::defineDevice()",
                    "Removing Active Status PV",
                    a_descriptor.m_name.c_str(),
                    "New Descriptor No Longer Has Active Status PV",
                    record->m_active_pv_conn.c_str() );
                usleep(33333); // give syslog a chance...

                delete record->m_active_pv;
                record->m_active_pv_conn.clear();
                record->m_active = true;
            }
        }
        else
        {
            // The descriptor is not identical to existing record,
            // analyze differences

            if ( a_descriptor.m_id != idev->second->m_id )
            {
                syslog( LOG_ERR,
                    "%s %s: Re-defining Device: [%s] %s/%lu (%s %d -> %d)",
                    "PVSD ERROR:", "ConfigManager::defineDevice()",
                    a_descriptor.m_name.c_str(),
                    a_descriptor.m_source.c_str(),
                    (unsigned long) a_descriptor.m_protocol,
                    "*** Device ID Re-Numbered",
                    idev->second->m_id, a_descriptor.m_id );
                usleep(33333); // give syslog a chance...
            }
            else
            {
                syslog( LOG_ERR,
                    "%s %s: Re-defining Device: [%s] %s/%lu (Device ID=%d)",
                    "PVSD ERROR:", "ConfigManager::defineDevice()",
                    a_descriptor.m_name.c_str(),
                    a_descriptor.m_source.c_str(),
                    (unsigned long) a_descriptor.m_protocol,
                    idev->second->m_id );
                usleep(33333); // give syslog a chance...
            }

            // Record is different, must make new record but
            // try to re-use identifiers
            DeviceDescriptor *new_desc = new DeviceDescriptor(
                a_descriptor ); // Deep Copy, Keep ID!

            // Initialize New Descriptor PV IDs to Zero (just to be sure!)
            if ( new_desc->m_active_pv
                    && !(new_desc->m_active_pv->m_ignore) )
            {
                new_desc->m_active_pv->m_id = 0;
            }
            vector<PVDescriptor*>::iterator ipv;
            for ( ipv = new_desc->m_pvs.begin();
                    ipv != new_desc->m_pvs.end(); ++ipv )
            {
                (*ipv)->m_id = 0;
            }

            // Create PV IDs Set: Sorted Unique Element List
            // - Use for Generating Guaranteed-Unique *New* PV Ids...! ;-D
            std::set<Identifier> pv_ids;

            // Clean Up Any Old Active Status PV that Subsumed a Device PV
            if ( idev->second->m_active_pv
                    && !(idev->second->m_active_pv->m_ignore) )
            {
                bool used_id = false;

                // Does the New Device's Active Status PV Also Subsume a PV?
                if ( new_desc->m_active_pv
                    && !(new_desc->m_active_pv->m_ignore) )
                {
                    // Re-Use Active Status PV ID, They're the Same PV...
                    // (Consider Diff Alias Name as Diff PV for Bookkeeping)
                    if ( !(new_desc->m_active_pv->m_name.compare(
                            idev->second->m_active_pv->m_name ))
                        && !(new_desc->m_active_pv->m_connection.compare(
                            idev->second->m_active_pv->m_connection )) )
                    {
                        new_desc->m_active_pv->m_id =
                            idev->second->m_active_pv->m_id;

                        pv_ids.insert( new_desc->m_active_pv->m_id );

                        used_id = true;

                        syslog( LOG_INFO,
                        "%s: %s [%s] (%s=%d) %s <%s> (%s) - %s PV ID=%d",
                            "ConfigManager::defineDevice()",
                            "Device", new_desc->m_name.c_str(),
                            "Device ID", new_desc->m_id,
                            "Active Status PV Persists",
                            new_desc->m_active_pv->m_name.c_str(),
                            new_desc->m_active_pv->m_connection.c_str(),
                            "Keep", new_desc->m_active_pv->m_id );
                        usleep(33333); // give syslog a chance...
                    }
                }

                // If Old Active Status PV ID Not Used,
                // Just Clean Up the Old Active Statue PV...
                if ( !used_id )
                {
                    syslog( LOG_ERR,
                    "%s %s: %s [%s] (%s=%d) %s <%s> (%s) (PV ID=%d)",
                        "PVSD ERROR:", "ConfigManager::defineDevice()",
                        "Device", new_desc->m_name.c_str(),
                        "Device ID", new_desc->m_id,
                        "Undefining Old Active Status PV",
                        idev->second->m_active_pv->m_name.c_str(),
                        idev->second->m_active_pv->m_connection.c_str(),
                        idev->second->m_active_pv->m_id );
                    usleep(33333); // give syslog a chance...

                    sendPvUndefined( idev->second,
                        idev->second->m_active_pv );
                }
            }

            // Check Old Descriptor PVs against New Descriptor's PVs...
            // - If Found, Re-Use PV ID...
            // - Else, Send PV Disconnected Packet...
            for ( ipv = idev->second->m_pvs.begin();
                    ipv != idev->second->m_pvs.end(); ++ipv )
            {
                PVDescriptor *new_pv;

                // Send PV Disconnected Packet for Old PVs that are
                // Not in New Descriptor
                if ( !(new_pv = new_desc->getPvByName( (*ipv)->m_name )) )
                {
                    syslog( LOG_ERR,
                    "%s %s: %s [%s] (Device ID=%d) %s <%s> (%s) (PV ID=%d)",
                        "PVSD ERROR:", "ConfigManager::defineDevice()",
                        "Device", new_desc->m_name.c_str(), new_desc->m_id,
                        "Undefining Old PV",
                        (*ipv)->m_name.c_str(),
                        (*ipv)->m_connection.c_str(),
                        (*ipv)->m_id );
                    usleep(33333); // give syslog a chance...

                    sendPvUndefined( idev->second, *ipv );
                }

                // Otherwise, Steal the PV ID from the Old Descriptor
                // (for consistency, as amenable... :-)
                else
                {
                    new_pv->m_id = (*ipv)->m_id;

                    pv_ids.insert( new_pv->m_id );

                    syslog( LOG_INFO,
                    "%s: %s [%s] (Device ID=%d) %s <%s> (%s) - %s PV ID=%d",
                        "ConfigManager::defineDevice()", "Device",
                        new_desc->m_name.c_str(), new_desc->m_id,
                        "PV Persists",
                        new_pv->m_name.c_str(),
                        new_pv->m_connection.c_str(),
                        "Keep", new_pv->m_id );
                    usleep(33333); // give syslog a chance...
                }
            }

            // Now Generate Any (Guaranteed-Unique) *New* PV Ids...! ;-D

            Identifier next_id = 1;

            // Check If the New Active Status PV Subsumed a Device PV
            // And Needs a PV ID... ;-D
            if ( new_desc->m_active_pv
                    && !(new_desc->m_active_pv->m_ignore)
                    && new_desc->m_active_pv->m_id == 0 )
            {
                // Find a Free ID
                // (Not Re-Used from Old IDs or a Recent New...)
                std::set<Identifier>::iterator idi;
                while ( (idi = pv_ids.find( next_id )) != pv_ids.end() )
                    next_id++;

                new_desc->m_active_pv->m_id = next_id++;

                // Mark this PV ID, to be sure for the next guy... ;-D
                pv_ids.insert( new_desc->m_active_pv->m_id );

                syslog( LOG_ERR,
                    "%s %s: %s [%s] (%s=%d) %s <%s> (%s) - %s PV ID=%d",
                    "PVSD ERROR:", "ConfigManager::defineDevice()",
                    "Device", new_desc->m_name.c_str(),
                    "Device ID", new_desc->m_id,
                    "Found New Active Status PV",
                    new_desc->m_active_pv->m_name.c_str(),
                    new_desc->m_active_pv->m_connection.c_str(),
                    "Assign", new_desc->m_active_pv->m_id );
                usleep(33333); // give syslog a chance...
            }

            for ( ipv = new_desc->m_pvs.begin();
                    ipv != new_desc->m_pvs.end(); ++ipv )
            {
                // Need a New PV Id...
                if ( (*ipv)->m_id == 0 )
                {
                    // New PV, Find a Free ID
                    // (Not Re-Used from Old IDs or a Recent New...)
                    std::set<Identifier>::iterator idi;
                    while ( (idi = pv_ids.find( next_id )) != pv_ids.end() )
                        next_id++;

                    (*ipv)->m_id = next_id++;

                    // Mark this PV ID, to be sure for the next guy... ;-D
                    pv_ids.insert( (*ipv)->m_id );

                    syslog( LOG_ERR,
                        "%s %s: %s [%s] (%s=%d) %s <%s> (%s) - %s PV ID=%d",
                        "PVSD ERROR:", "ConfigManager::defineDevice()",
                        "Device", new_desc->m_name.c_str(),
                        "Device ID", new_desc->m_id,
                        "Found New PV",
                        (*ipv)->m_name.c_str(),
                        (*ipv)->m_connection.c_str(),
                        "Assign", (*ipv)->m_id );
                    usleep(33333); // give syslog a chance...
                }
            }

            // Now Ensure All New PV Names are Unique
            makePvNamesUnique( key, *new_desc );

            // Move old record to trash, and save new record
            record = DeviceRecordPtr(new_desc);
            sendDeviceRedefined( record, idev->second );

            idev->second = record;

            a_device_changed = true;
        }
    }

    // This is a NEW device
    else
    {
        // No existing record, create a new one copied from the
        // passed-in descriptor
        DeviceDescriptor *new_desc =
            new DeviceDescriptor( a_descriptor ); // Deep Copy, Keep ID!

        syslog( LOG_ERR,
            "%s %s: Defining New Device: [%s] %s/%lu (Device ID=%d)",
            "PVSD ERROR:", "ConfigManager::defineDevice()",
            a_descriptor.m_name.c_str(), a_descriptor.m_source.c_str(),
            (unsigned long) a_descriptor.m_protocol,
            new_desc->m_id );
        usleep(33333); // give syslog a chance...

        // PV IDs can be assigned arbitrarily for new devices

        Identifier id = 1;

        if ( new_desc->m_active_pv
                && !(new_desc->m_active_pv->m_ignore) )
        {
            new_desc->m_active_pv->m_id = id++;

            syslog( LOG_ERR,
                "%s %s: %s [%s] (%s=%d) %s <%s> (%s) - Assign PV ID=%d",
                "PVSD ERROR:", "ConfigManager::defineDevice()",
                "Device", new_desc->m_name.c_str(),
                "Device ID", new_desc->m_id,
                "New Active Status PV",
                new_desc->m_active_pv->m_name.c_str(),
                new_desc->m_active_pv->m_connection.c_str(),
                new_desc->m_active_pv->m_id );
            usleep(33333); // give syslog a chance...
        }

        for ( vector<PVDescriptor*>::iterator ipv = new_desc->m_pvs.begin();
                ipv != new_desc->m_pvs.end(); ++ipv )
        {
            (*ipv)->m_id = id++;

            syslog( LOG_ERR,
                "%s %s: %s [%s] (%s=%d) New PV <%s> (%s) - Assign PV ID=%d",
                "PVSD ERROR:", "ConfigManager::defineDevice()",
                "Device", new_desc->m_name.c_str(),
                "Device ID", new_desc->m_id,
                (*ipv)->m_name.c_str(),
                (*ipv)->m_connection.c_str(),
                (*ipv)->m_id );
            usleep(33333); // give syslog a chance...
        }

        // Ensure all new PV names are unique
        makePvNamesUnique( key, *new_desc );

        // Store new record in devices vector
        record = DeviceRecordPtr(new_desc);
        m_devices[key] = record;

        sendDeviceDefined( record );

        a_device_changed = true;
    }

    return record;
}


/**
 * @brief Removes a device from configuration database
 * @param a_record - Device to undefine
 *
 * This method undefines a device (removes from configuration database).
 * This method results in stream packets being emitted to set the
 * undefined device PVs to "disconnected" state, followed by a
 * "device undefined" packet. Note that the device pointers in these
 * messages are shared pointer, so the device record will persist
 * until the emitted packets are consumed and cleared (even if device
 * is actually deleted).
 */
void
ConfigManager::undefineDevice( DeviceRecordPtr &a_record,
        bool a_delete_device )
{
    syslog( LOG_ERR,
        "%s %s: Un-defining device: [%s] %s/%lu (Device ID=%d) %s=%u",
        "PVSD ERROR:", "ConfigManager::undefineDevice()",
        a_record->m_name.c_str(), a_record->m_source.c_str(),
        (unsigned long)a_record->m_protocol, a_record->m_id,
        "delete_device", a_delete_device );
    usleep(33333); // give syslog a chance...

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Compare to the latest configuration records
    // (those in trash don't matter)
    string key = makeDeviceKey(
        a_record->m_name, a_record->m_source, a_record->m_protocol );

    map<string,DeviceRecordPtr>::iterator idev = m_devices.find( key );
    if ( idev != m_devices.end())
    {
        // Send PV disconnected packets for old PVs
        // (that are not in new descriptor, if being re-defined...)
        for ( vector<PVDescriptor*>::iterator ipv =
                    idev->second->m_pvs.begin();
                ipv != idev->second->m_pvs.end(); ++ipv )
        {
            sendPvUndefined( idev->second, *ipv );
        }

        // Actually Delete the Device (All the Way Down the Pipeline...)
        if ( a_delete_device )
        {
            sendDeviceUndefined( idev->second );

            m_devices.erase( idev );
        }
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


/**
 * @brief This method ensures PV names on device are unique
 * @param a_key - Device map key for device being checked
 * @param a_descriptor - Descriptor for device being checked
 *
 * This method examines the PV names of the specified descriptor against
 * all currently configured PVs except for a descriptor being redefined
 * (i.e. with the same key). If a conflict is found the new conflicting PV
 * is renamed by appending a "_dupX" suffix (where X is a conflict counter)
 * and checked again. This continues until all conflicts have been resolved.
 *
 * This step tries to ensure that ADARA clients will receive PVs with
 * unique names. PV names are required to be unique, and this routine is
 * a "just-in-case" measure to prevent editing errors from causing serious
 * client issues. This method cannot prevent other data sources from
 * injecting Device Descriptor packets with conflicting names. This
 * condition must be trapped by the SMS.
 *
 * Addendum: As of the end of 2018, the ADARA STC has gotten much smarter
 * about recognizing "Duplicate PVs" and Collapsing any redundant values
 * into a single Merged PV Log. So we can be more cavalier now too,
 * and only look for PV Name/Alias Clashes that are Distinct from the
 * EPICS PV Connection Strings. It's actually "Ok" now to have the
 * Same PV requested in Multiple Devices, just no Name/Alias Clashes...! ;-D
 */
void
ConfigManager::makePvNamesUnique( const string &a_key,
        DeviceDescriptor &a_descriptor )
{
    vector<PVDescriptor*>::const_iterator ipv;
    set<string> names;
    set<string>::iterator inames;

    for ( map<string,DeviceRecordPtr>::iterator idev = m_devices.begin();
            idev != m_devices.end(); ++idev )
    {
        // Skip device being redefined
        if ( idev->first == a_key )
            continue;

        for ( ipv = idev->second->m_pvs.begin();
                ipv != idev->second->m_pvs.end(); ++ipv )
        {
            // Only Consider Names/Aliases that are Distinct from
            // the Given PV's Connection String... ;-D
            if ( (*ipv)->m_name.compare( (*ipv)->m_connection ) )
                names.insert( (*ipv)->m_name );
        }
    }

    string new_name;
    unsigned short count;

    for ( ipv = a_descriptor.m_pvs.begin();
            ipv != a_descriptor.m_pvs.end(); ++ipv )
    {
        // Only Check Names/Aliases that are Distinct from
        // the Given PV's Connection String... ;-D
        if ( !(*ipv)->m_name.compare( (*ipv)->m_connection ) )
            continue;

        count = 0;
        new_name = (*ipv)->m_name;

        while ( 1 )
        {
            if ( count )
            {
                new_name = (*ipv)->m_name + "_dup"
                    + boost::lexical_cast<string>(count);
            }

            if ( names.find( new_name ) != names.end() )
                ++count;
            else
                break;
        }

        // Only Change the Name as Needed... ;-D
        if ( count )
        {
            syslog( LOG_ERR,
                "%s %s: %s [%s] (%s=%d): %s <%s> to <%s> (%s) (PV ID=%d)!",
                "PVSD ERROR:", "ConfigManager::makePvNamesUnique()",
                "Device", a_descriptor.m_name.c_str(),
                "Device ID", a_descriptor.m_id,
                "Renaming Name-Clash PV from",
                (*ipv)->m_name.c_str(), new_name.c_str(),
                (*ipv)->m_connection.c_str(), (*ipv)->m_id );
            usleep(33333); // give syslog a chance...

            (*ipv)->m_name = new_name;
        }
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
    else
    {
        if ( m_stream_api->getFreeQueueActive() )
        {
            syslog( LOG_ERR,
                "%s %s: %s! Device [%s] (Device ID=%d) %s! [%s = %lu]",
                "PVSD ERROR:", "ConfigManager::sendDeviceDefined()",
                "No Free Packets",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id,
                "Descriptor Lost",
                "Filled Queue Size",
                (unsigned long) m_stream_api->getFilledQueueSize() );
            usleep(33333); // give syslog a chance...
        }
        else
        {
            syslog( LOG_ERR,
                "%s %s: %s, Ignore Define Device [%s] (Device ID=%d)",
                "PVSD ERROR:", "ConfigManager::sendDeviceDefined()",
                "Queue Deactivated",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id );
            usleep(33333); // give syslog a chance...
        }
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
    else
    {
        if ( m_stream_api->getFreeQueueActive() )
        {
            syslog( LOG_ERR,
                "%s %s: %s! Device [%s] (Device ID=%d) %s! [%s = %lu]",
                "PVSD ERROR:", "ConfigManager::sendDeviceUndefined()",
                "No Free Packets",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id,
                "Undefined Lost",
                "Filled Queue Size",
                (unsigned long) m_stream_api->getFilledQueueSize() );
            usleep(33333); // give syslog a chance...
        }
        else
        {
            syslog( LOG_ERR,
                "%s %s: %s, Ignore Undefine Device [%s] (Device ID=%d)",
                "PVSD ERROR:", "ConfigManager::sendDeviceUndefined()",
                "Queue Deactivated",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id );
            usleep(33333); // give syslog a chance...
        }
    }
}


void
ConfigManager::sendDeviceRedefined( DeviceRecordPtr a_dev_desc,
        DeviceRecordPtr a_old_dev_desc )
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
    else
    {
        if ( m_stream_api->getFreeQueueActive() )
        {
            syslog( LOG_ERR,
                "%s %s: %s! %s [%s] (%s [%s]) (%s=%d) %s! [%s = %lu]",
                "PVSD ERROR:", "ConfigManager::sendDeviceRedefined()",
                "No Free Packets", "Device", a_dev_desc->m_name.c_str(),
                "Old", a_old_dev_desc->m_name.c_str(),
                "Device ID", a_dev_desc->m_id, "Descriptor Update Lost",
                "Filled Queue Size",
                (unsigned long) m_stream_api->getFilledQueueSize() );
            usleep(33333); // give syslog a chance...
        }
        else
        {
            syslog( LOG_ERR,
                "%s %s: %s, Ignore Redefine Device [%s] (Device ID=%d)",
                "PVSD ERROR:", "ConfigManager::sendDeviceRedefined()",
                "Queue Deactivated",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id );
            usleep(33333); // give syslog a chance...
        }
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
        pkt->state = PVState(
            ::ADARA::VariableStatus::UPSTREAM_DISCONNECTED,
            ::ADARA::VariableSeverity::INVALID );
        pkt->state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;
        pkt->state.m_time.nsec = 0;

        m_stream_api->putFilledPacket( pkt );
    }
    else
    {
        if ( m_stream_api->getFreeQueueActive() )
        {
            syslog( LOG_ERR,
        "%s %s: %s! %s [%s] (%s=%d) PV <%s> (%s) (PV ID=%d) %s! [%s = %lu]",
                "PVSD ERROR:", "ConfigManager::sendPvUndefined()",
                "No Free Packets", "Device", a_dev_desc->m_name.c_str(),
                "Device ID", a_dev_desc->m_id,
                a_pv_desc->m_name.c_str(),
                a_pv_desc->m_connection.c_str(),
                a_pv_desc->m_id, "Undefined Lost",
                "Filled Queue Size",
                (unsigned long) m_stream_api->getFilledQueueSize() );
            usleep(33333); // give syslog a chance...
        }
        else
        {
            syslog( LOG_ERR,
            "%s %s: %s Device [%s] (Device ID=%d) PV <%s> (%s) (PV ID=%d)",
                "PVSD ERROR:", "ConfigManager::sendPvUndefined()",
                "Queue Deactivated, Ignore PV Undefine",
                a_dev_desc->m_name.c_str(), a_dev_desc->m_id,
                a_pv_desc->m_name.c_str(),
                a_pv_desc->m_connection.c_str(),
                a_pv_desc->m_id );
            usleep(33333); // give syslog a chance...
        }
    }
}

}

// vim: expandtab

