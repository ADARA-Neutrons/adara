#include <iostream>
#include <string>
#include <sstream>
#include <syslog.h>
#include <unistd.h>
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
DeviceAgent::DeviceAgent( IInputAdapterAPI &a_stream_api,
        DeviceDescriptor *a_device,
        struct ca_client_context *a_epics_context,
        time_t a_device_init_timeout )
    : m_stream_api(a_stream_api), m_dev_desc(0),
      m_defined(false), m_hung(false),
      m_state_changed(false), m_agent_active(true),
      m_epics_context(a_epics_context),
      m_device_init_timeout(a_device_init_timeout)
{
    m_monitor_thread = new boost::thread(
        boost::bind( &DeviceAgent::monitorThread, this ) );
    m_ctrl_thread = new boost::thread(
        boost::bind( &DeviceAgent::controlThread, this ) );

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
    m_agent_active = false;
    stop();

    // Wake up ctrl thread and wait for it to exit
    m_state_cond.notify_one();
    m_ctrl_thread->join();
    m_monitor_thread->join();

    // Get mutex in case timer callback is currently active
    boost::unique_lock<boost::mutex> lock(m_mutex);
    if ( m_dev_desc )
    {
        delete m_dev_desc;
        m_dev_desc = 0;
    }
    lock.unlock();
}


/**
 * @brief Handles new or updated device descriptors
 * @param a_device - DeviceDescriptor for new/updated device
 *
 * This method is used to establish or update EPICS connections when a
 * new device is defined or the current device is updated. If a current
 * device is defined, the passed-in device descriptor is compared to the
 * existing descriptor and connections are updated based on any detected
 * differences.
 * This method does NOT inject any packets into the internal data stream.
 */
void
DeviceAgent::update( DeviceDescriptor *a_device )
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    PVDescriptor*                   old_pv;
    vector<PVDescriptor*>::iterator ipv;
    map<chid,ChanInfo>::iterator    ich;
    map<string,chid>::iterator      idx;

    std::string deviceStr = "";
    if ( !a_device->m_name.empty() )
    {
        deviceStr = "Device [" + a_device->m_name + "] - ";
    }

    // Resetting m_defined will cause control thread to watch PVs and
    // perform post-connection duties - including defining device
    // with config manager
    m_defined = false;

    // Use transient descriptor first, then configured device second
    DeviceDescriptor *old_desc =
        m_dev_desc ? m_dev_desc : m_dev_record.get();

    if ( old_desc )
    {
        // Disconnect any existing connections that are no longer needed

        syslog( LOG_INFO, "%s: %sDisconnecting Old PV Channels...",
            "DeviceAgent::update()", deviceStr.c_str() );
        usleep(33333); // give syslog a chance...

        // If Active Status PV Connection Changed,
        // Delete Old PV Subscription.
        if ( old_desc->m_active_pv
                && old_desc->m_active_pv_conn.compare(
                    a_device->m_active_pv_conn ) )
        {
            disconnectPV( old_desc->m_active_pv, lock );
        }

        for ( ipv = old_desc->m_pvs.begin();
                ipv != old_desc->m_pvs.end(); ++ipv )
        {
            if ( !a_device->getPvByConnection( (*ipv)->m_connection ) )
            {
                disconnectPV( *ipv, lock );
            }
        }

        // If a device is already defined, reuse any shared PV connections
        // Make connections for new PVs in updated device

        syslog( LOG_INFO, "%s: %sReuse/Create New PV Channels...",
            "DeviceAgent::update()", deviceStr.c_str() );
        usleep(33333); // give syslog a chance...

        // Handle Any Active Status PV Channel...
        if ( a_device->m_active_pv )
        {
            // Create New Active Status PV Channel
            if ( old_desc->m_active_pv == NULL
                    || old_desc->m_active_pv_conn.compare(
                        a_device->m_active_pv_conn ) )
            {
                connectPV( a_device->m_active_pv );
            }

            // Active Status PV channel is shared between
            // Old and New Device, Reuse Any Existing
            // Active Status PV Channel...
            else
            {
                std::string pvStr = "";
                if ( old_desc->m_active_pv != NULL )
                {
                    pvStr = " from Old Active Status PV <"
                        + old_desc->m_active_pv->m_name + "> ("
                        + old_desc->m_active_pv->m_connection + ")";
                }
                else
                {
                    pvStr = " from Old Active Status PV ("
                        + old_desc->m_active_pv_conn + ")";
                }

                syslog( LOG_INFO, "%s: %sReusing Channel%s",
                    "DeviceAgent::update()", deviceStr.c_str(),
                    pvStr.c_str() );
                usleep(33333); // give syslog a chance...

                // Inherit the Active Status from Old Active Status PV
                a_device->m_active_pv->m_is_active =
                    old_desc->m_active_pv->m_is_active;

                // Update Active PV pointer on channel info
                idx = m_pv_index.find(
                    old_desc->m_active_pv->m_connection );
                if ( idx != m_pv_index.end() )
                {
                    ich = m_chan_info.find( idx->second );
                    if ( ich != m_chan_info.end() )
                    {
                        // Update PV to new Active Status PV
                        ich->second.m_pv = a_device->m_active_pv;
                        // Old device record is no longer valid - reset
                        ich->second.m_device.reset();

                        // Re-acquire metadata just in case it changed
                        // (only if connected)
                        if ( ich->second.m_chan_state != UNINITIALIZED )
                            ich->second.m_chan_state = INFO_NEEDED;
                    }
                    else
                    {
                        syslog( LOG_ERR,
                            "%s %s: %s%s%s - %s!",
                            "PVSD ERROR:", "DeviceAgent::update()",
                            deviceStr.c_str(), "Channel Info Not Found",
                            pvStr.c_str(),
                            "Internal Bookkeeping Linkage Not Updated" );
                        usleep(33333); // give syslog a chance...
                    }
                }
                else
                {
                    syslog( LOG_ERR,
                        "%s %s: %s%s%s - %s!",
                        "PVSD ERROR:", "DeviceAgent::update()",
                        deviceStr.c_str(), "PV Index/Channel ID Not Found",
                        pvStr.c_str(),
                        "Internal Bookkeeping Linkage Not Updated" );
                    usleep(33333); // give syslog a chance...
                }
            }
        }

        for ( ipv = a_device->m_pvs.begin();
                ipv != a_device->m_pvs.end(); ++ipv )
        {
            old_pv = old_desc->getPvByConnection( (*ipv)->m_connection );
            if ( !old_pv )
            {
                connectPV( *ipv );
            }
            else
            {
                // PV channel is shared between old and new device,
                // reuse connection

                deviceStr = "";
                if ( (*ipv)->m_device != NULL
                        && !(*ipv)->m_device->m_name.empty() )
                {
                    deviceStr = "Device ["
                        + (*ipv)->m_device->m_name + "] - ";
                }

                syslog( LOG_INFO,
                    "%s: %sReusing Channel from Old PV <%s> (%s)",
                    "DeviceAgent::update()", deviceStr.c_str(),
                    (*ipv)->m_name.c_str(), (*ipv)->m_connection.c_str() );
                usleep(33333); // give syslog a chance...

                // Update PV pointer on channel info
                idx = m_pv_index.find( old_pv->m_connection );
                if ( idx != m_pv_index.end() )
                {
                    ich = m_chan_info.find( idx->second );
                    if ( ich != m_chan_info.end() )
                    {
                        // Update PV to new (temporary) PV
                        ich->second.m_pv = *ipv;
                        // Old device record is no longer valid - reset
                        ich->second.m_device.reset();

                        // Re-aqcuire metadata just in case it changed
                        // (only if connected)
                        if ( ich->second.m_chan_state != UNINITIALIZED )
                            ich->second.m_chan_state = INFO_NEEDED;
                    }
                    else
                    {
                        syslog( LOG_ERR,
                            "%s %s: %s%s for PV <%s> - %s!",
                            "PVSD ERROR:", "DeviceAgent::update()",
                            deviceStr.c_str(), "Channel Info Not Found",
                            old_pv->m_connection.c_str(),
                            "Internal Bookkeeping Linkage Not Updated" );
                        usleep(33333); // give syslog a chance...
                    }
                }
                else
                {
                    syslog( LOG_ERR,
                        "%s %s: %s%s for PV <%s> - %s!",
                        "PVSD ERROR:", "DeviceAgent::update()",
                        deviceStr.c_str(), "PV Index/Channel ID Not Found",
                        old_pv->m_connection.c_str(),
                        "Internal Bookkeeping Linkage Not Updated" );
                    usleep(33333); // give syslog a chance...
                }
            }
        }

        // Delete old temporary descriptor if set
        if ( m_dev_desc )
        {
            delete m_dev_desc;
        }
    }
    else
    {
        syslog( LOG_INFO, "%s: %sCreate New PV Channels...",
            "DeviceAgent::update()", deviceStr.c_str() );
        usleep(33333); // give syslog a chance...

        if ( a_device->m_active_pv )
            connectPV( a_device->m_active_pv );

        for ( ipv = a_device->m_pvs.begin();
                ipv != a_device->m_pvs.end(); ++ipv )
        {
            connectPV( *ipv );
        }
    }

    m_dev_desc = a_device;
    ca_flush_io();

    // If no new PV channels were created, state machine will not progress
    // on it's own
    // Wake state machine in this case (doesn't matter if its notified
    // more than once)
    m_state_changed = true;
    m_state_cond.notify_one();
}


/**
 * @brief Handles metadata updates after device configuration
 *
 * This method is called by the state machine when metadata changes are
 * detected after configuration for at least one connected PV. Because
 * metadata (PV type, units, enum) are part of the device descriptor,
 * the existing device must be redefined with the new metadata and a
 * new device descriptor emitted. This method copies the current device
 * descriptor, updates the changed metadata, and redefines the device
 * through the configuration manager.
 */
void
DeviceAgent::metadataUpdated()
{
    // No lock is required since this method is only called
    // from state machine that holds the lock.

    // Copy current device descriptor
    DeviceDescriptor *new_dev = new DeviceDescriptor( *m_dev_record );
    m_defined = false;
    m_dev_desc = new_dev;

    // Update channel info and channel state
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin();
            ich != m_chan_info.end(); ++ich )
    {
        ich->second.m_device.reset();

        // Find PV in New Device Instance...

        PVDescriptor *pv = NULL;

        // Check for Active Status PV...
        if ( !ich->second.m_pv->m_connection.compare(
                m_dev_desc->m_active_pv_conn ) )
        {
            pv = m_dev_desc->m_active_pv;
        }
        else
        {
            pv = m_dev_desc->getPvByConnection(
                ich->second.m_pv->m_connection );
        }

        // Failsafe...! ;-o
        if ( pv )
        {
            ich->second.m_pv = pv;
        }
        else
        {
            syslog( LOG_ERR,
                "%s %s: Device [%s] - %s for PV <%s> (%s), %s",
                "PVSD ERROR:", "DeviceAgent::metadataUpdated()",
                m_dev_desc->m_name.c_str(),
                "Error Updating Metadata",
                ich->second.m_pv->m_name.c_str(),
                ich->second.m_pv->m_connection.c_str(),
                "PV Not Found in Device - Skipping!" );
            usleep(33333); // give syslog a chance...

            continue;
        }

        if ( ich->second.m_chan_state == INFO_AVAILABLE )
        {
            syslog( LOG_INFO,
                "%s: Device [%s] - Updating metadata for PV <%s> (%s)",
                "DeviceAgent::metadataUpdated()",
                m_dev_desc->m_name.c_str(),
                ich->second.m_pv->m_name.c_str(),
                ich->second.m_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            ich->second.m_pv->setMetadata(
                epicsToPVType( ich->second.m_ca_type,
                    ich->second.m_ca_elem_count ),
                ich->second.m_ca_elem_count,
                ich->second.m_ca_units, ich->second.m_ca_enum_vals );
            ich->second.m_chan_state = READY;
        }
        else if ( ich->second.m_chan_state == READY )
        {
            syslog( LOG_INFO,
            "%s: Device [%s] - Re-requesting metadata for PV <%s> (%s)",
                "DeviceAgent::metadataUpdated()",
                m_dev_desc->m_name.c_str(),
                ich->second.m_pv->m_name.c_str(),
                ich->second.m_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            // Re-request metadata from all other PV
            // (they may have changed too)
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
DeviceAgent::stop(void)
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    std::string deviceStr = "";
    if ( m_dev_desc != NULL && !m_dev_desc->m_name.empty() )
        deviceStr = "Device [" + m_dev_desc->m_name + "] - ";

    // If a device is currently configured, undefine it
    if ( m_dev_record.get() )
    {
        m_stream_api.getCfgMgr().undefineDevice( m_dev_record );
        m_dev_record.reset();
    }

    // Clear CA channels and unsubscribe
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin();
            ich != m_chan_info.end(); ++ich )
    {
        syslog( LOG_INFO,
            "%s: %sStopping PV <%s> (%s)",
            "DeviceAgent::stop()", deviceStr.c_str(),
            ich->second.m_pv->m_name.c_str(),
            ich->second.m_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...

        if ( ich->second.m_subscribed )
        {
            syslog( LOG_INFO,
                "%s: %sUnsubscribing Channel for PV <%s> (%s)",
                "DeviceAgent::stop()", deviceStr.c_str(),
                ich->second.m_pv->m_name.c_str(),
                ich->second.m_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            // *** Prevent Deadlock with New EPICS Callback Guard...!!
            lock.unlock();
            ca_clear_subscription( ich->second.m_evid );
            lock.lock();
        }

        syslog( LOG_INFO,
            "%s: %sClearing Channel for PV <%s> (%s)",
            "DeviceAgent::stop()", deviceStr.c_str(),
            ich->second.m_pv->m_name.c_str(),
            ich->second.m_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...

        // *** Prevent Deadlock with New EPICS Callback Guard...!!
        lock.unlock();
        ca_clear_channel( ich->second.m_chid );
        lock.lock();

        syslog( LOG_INFO,
            "%s: %sDone with PV <%s> (%s)",
            "DeviceAgent::stop()", deviceStr.c_str(),
            ich->second.m_pv->m_name.c_str(),
            ich->second.m_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...

        ca_flush_io();
    }

    m_chan_info.clear();
    m_pv_index.clear();
}


/**
 * @brief Marks DeviceAgent as "Undefined" when Device ID goes Inactive.
 *
 * This method _Only_ Undefines the Device Descriptor for an
 * Old Device ID which has gone inactive; the DeviceAgent is
 * _Still_ Alive and Ok to Proceed, it has _Only_ been "Re-Numbered"...!
 */
void
DeviceAgent::undefine(void)
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    DeviceDescriptor *dev = m_dev_desc ? m_dev_desc : m_dev_record.get();

    std::string deviceStr = "";
    if ( dev != NULL && !dev->m_name.empty() )
        deviceStr = "Device [" + dev->m_name + "] - ";

    // If a device is currently configured, undefine it
    // so we make sure Old Inactive Device IDs get cleared.
    if ( m_dev_record.get() )
    {
        syslog( LOG_INFO,
            "%s: Undefining %sInactive Device ID %d",
            "DeviceAgent::undefine()", deviceStr.c_str(),
            dev ? dev->m_id : -1 );
        usleep(33333); // give syslog a chance...

        m_stream_api.getCfgMgr().undefineDevice( m_dev_record );
        // *Don't* Reset the Descriptor Here, Device May Still Exist
        // But Only Be Re-Numbered...! ;-D
    }

    // Device Record Not Found, Maybe Already Stopped...?
    else
    {
        syslog( LOG_INFO,
            "%s: %sInactive Device ID %d Not Found - Already Stopped?",
            "DeviceAgent::undefine()", deviceStr.c_str(),
            ( m_dev_desc != NULL ) ? m_dev_desc->m_id : -1 );
        usleep(33333); // give syslog a chance...
    }
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
DeviceAgent::stopped(void)
{
    boost::unique_lock<boost::mutex> lock(m_mutex);
    return m_chan_info.size() == 0;
}


/**
 * @brief Method returns DeviceAgent status (Ready/Total PVs & Hung State)
 * @param a_ready_pvs - Output number of "Ready" PVs for this Device
 * @param a_total_pvs - Output Total number of PVs for this Device
 * @param a_hung - Output whether this Device is Hung in Initialization
 */
void
DeviceAgent::deviceStatus( uint32_t &a_ready_pvs, uint32_t &a_total_pvs,
        bool &a_hung, bool &a_active )
{
    // Return Number of "Ready" PVs and Total PVs Defined for Device...
    DeviceDescriptor *dev = m_dev_desc ? m_dev_desc : m_dev_record.get();
    a_ready_pvs = dev->m_ready;
    a_total_pvs = dev->m_pvs.size()
        + ( ( dev->m_active_pv ) ? 1 : 0 );

    // Return Device Hung Status
    a_hung = m_hung;

    // Return Device Active Status
    a_active = dev->m_active;
}


//-------------------------------------------------------------------------------------------------
// PRIVATE DeviceAgent Methods
//-------------------------------------------------------------------------------------------------

/**
 * @brief Creates data connections to specified process variable
 * @param a_pv - Process variable descriptor for connection
 *
 * This method makes EPICS calls to connect to the specified process
 * variable. This call is asynchronous. Once the connections have been
 * established, the epicsConnectionCallback method will be called and
 * the DeviceAgent state machine will be stimulated.
 */
void
DeviceAgent::connectPV( PVDescriptor *a_pv )
{
    std::string deviceStr = "";
    if ( a_pv->m_device != NULL && !a_pv->m_device->m_name.empty() )
        deviceStr = "Device [" + a_pv->m_device->m_name + "] - ";

    syslog( LOG_INFO, "%s: %sCreating channel for PV <%s> (%s)",
        "DeviceAgent::connectPV()", deviceStr.c_str(),
        a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
    usleep(33333); // give syslog a chance...

    ChanInfo info;
    info.m_pv = a_pv;

    // Create a CA channel
    if ( ca_create_channel( a_pv->m_connection.c_str(),
                &epicsConnectionCallback, this, 0, &info.m_chid )
            == ECA_NORMAL )
    {
        // Note: don't flush I/O here - update() method will call it

        // Update channel info and PV name index structures
        m_chan_info[info.m_chid] = info;
        m_pv_index[a_pv->m_connection] = info.m_chid;
        //cout << "connected chid: " << info.m_chid
            //<< " for PV: " << a_pv->m_connection << endl;
    }
    else
    {
        syslog( LOG_ERR, "%s %s: %sFailed to create channel for PV <%s>",
            "PVSD ERROR:", "DeviceAgent::connectPV()", deviceStr.c_str(),
            a_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...
    }
}


/**
 * @brief Disconnects the specified process variable
 * @param a_pv - Process variable descriptor for disconnection
 *
 * This method makes EPICS calls to disconnect to the specified process
 * variable. This call is synchronous (for now).
 */
void
DeviceAgent::disconnectPV( PVDescriptor *a_pv,
    boost::unique_lock<boost::mutex> & lock )
{
    std::string deviceStr = "";
    if ( a_pv->m_device != NULL && !a_pv->m_device->m_name.empty() )
        deviceStr = "Device [" + a_pv->m_device->m_name + "] - ";

    map<std::string,chid>::iterator ipv =
        m_pv_index.find( a_pv->m_connection );

    if ( ipv != m_pv_index.end() )
    {
        syslog( LOG_INFO, "%s: %sDisconnecting channel for PV <%s> (%s)",
            "DeviceAgent::disconnectPV()", deviceStr.c_str(),
            a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...

        map<chid,ChanInfo>::iterator ich = m_chan_info.find( ipv->second );

        if ( ich != m_chan_info.end() )
        {
            syslog( LOG_INFO,
                "%s: %sFound Existing Channel for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            if ( ich->second.m_subscribed )
            {
                syslog( LOG_INFO,
                    "%s: %sClearing Subscription for PV <%s> (%s)",
                    "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                    a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
                usleep(33333); // give syslog a chance...

                // *** Prevent Deadlock with New EPICS Callback Guard...!!
                lock.unlock();
                ca_clear_subscription( ich->second.m_evid );
                lock.lock();
            }

            syslog( LOG_INFO,
                "%s: %sClearing Channel for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            // *** Prevent Deadlock with New EPICS Callback Guard...!!
            lock.unlock();
            ca_clear_channel( ich->second.m_chid );
            lock.lock();

            // Update channel info index structures

            syslog( LOG_INFO,
                "%s: %sErasing Channel Info for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            m_chan_info.erase( ich );

            syslog( LOG_INFO,
                "%s: %sDone Disconnecting Channel Info for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...

            // Don't flush I/O here - update() method will call it
        }
        else
        {
            syslog( LOG_WARNING,
                "%s: %sWarning: No Channel Info Found for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(33333); // give syslog a chance...
        }

        // Update name index structures
        m_pv_index.erase( ipv );
    }
    else
    {
        syslog( LOG_ERR,
            "%s %s: %s%s <%s> (%s) - Not Found",
            "PVSD ERROR:", "DeviceAgent::disconnectPV()",
            deviceStr.c_str(), "Failed to disconnect channel for PV",
            a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
        usleep(33333); // give syslog a chance...
    }
}


/**
 * @brief This method is control thread for the DeviceAgent state machine
 *
 * This method runs the state machine thread for a given DeviceAgent
 * instance. The state machine governs the process of EPICS channel
 * management and also finalizes device definition. There are two stimuli
 * that can trigger device definition (or redefinition): a call to the
 * update() method, or a detected change in process variable metadata
 * (type, units, enum). The state machine is able to detect changes
 * to metadata when a PV progresses to INFO_AVAILABLE state.
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

    while ( 1 )
    {
        try
        {
            boost::unique_lock<boost::mutex> lock(m_mutex);

            // Only run when there is a state change
            if ( !m_state_changed )
                m_state_cond.wait( lock );

            // Exit loop if DeviceAgent instance is shutting down
            if ( !m_agent_active )
                break;

            // Might have been woken spuriously
            if ( !m_state_changed )
                continue;

            m_state_changed = false;

            // Check channel state of member process variables
            std::vector<std::string> pendingPVs;
            ready = 0;
            for ( ich = m_chan_info.begin();
                    ich != m_chan_info.end() && !m_state_changed; ++ich )
            {
                switch ( ich->second.m_chan_state )
                {
                case INFO_NEEDED:
                    // cout <<  "IN, chan: " << ich->first
                         // << " ca_type = " << ich->second.m_ca_type
                         // << " ca_element_count = "
                         // << ich->second.m_ca_elem_count
                         // << " for PV: "
                         // << ich->second.m_pv->m_connection << endl;

                    if ( ca_get_callback(
                            epicsToCtrlRecordType( ich->second.m_ca_type ),
                            ich->first, epicsEventCallback,
                            this ) == ECA_NORMAL )
                    {
                        ich->second.m_chan_state = INFO_PENDING;
                    }
                    else
                    {
                        std::string deviceStr = "";
                        if ( m_dev_desc != NULL
                                && !m_dev_desc->m_name.empty() )
                        {
                            deviceStr = "Device ["
                                + m_dev_desc->m_name + "] - ";
                        }

                        syslog( LOG_ERR, "%s %s: %s%s <%s> (%s)",
                            "PVSD ERROR:",
                            "DeviceAgent::controlThread(INFO_NEEDED)",
                            deviceStr.c_str(),
                            "Failed to get channel info for PV",
                            ich->second.m_pv->m_name.c_str(),
                            ich->second.m_pv->m_connection.c_str() );
                        usleep(33333); // give syslog a chance...
                    }
                    pendingPVs.push_back( ich->second.m_pv->m_connection );
                    break;
                case INFO_AVAILABLE:
                    if ( m_defined )
                    {
                        // cout <<  "IA, chan: " << ich->first
                             // << " ca_type = " << ich->second.m_ca_type
                             // << " ca_element_count = "
                             // << ich->second.m_ca_elem_count
                             // << " for PV: "
                             // << ich->second.m_pv->m_connection << endl;

                        if ( !ich->second.m_pv->equalMetadata(
                                epicsToPVType( ich->second.m_ca_type,
                                    ich->second.m_ca_elem_count ),
                                ich->second.m_ca_elem_count,
                                ich->second.m_ca_units,
                                ich->second.m_ca_enum_vals ) )
                        {
                            metadataUpdated();
                            // Setup for another state machine pass
                            ready = 0;
                            m_state_changed = true;
                            break;
                        }
                    }

                    // cout <<  "IN(2), chan: " << ich->first
                         // << " ca_type = " << ich->second.m_ca_type
                         // << " ca_element_count = "
                         // << ich->second.m_ca_elem_count'
                         // << " for PV: "
                         // << ich->second.m_pv->m_connection << endl;

                    ich->second.m_pv->setMetadata(
                        epicsToPVType( ich->second.m_ca_type,
                            ich->second.m_ca_elem_count ),
                        ich->second.m_ca_elem_count,
                        ich->second.m_ca_units,
                        ich->second.m_ca_enum_vals );
                    ich->second.m_chan_state = READY;
                    ++ready;
                    break;
                case READY:
                    ++ready;
                    break;
                default:
                    pendingPVs.push_back( ich->second.m_pv->m_connection );
                    break;
                }
            }

            // Flush any pending EPICS requests
            ca_flush_io();

            // IF a temporary device descriptor is set (m_dev_desc) and
            // all member PVs are READY, then define the device
            // NOTE: Even if all PVs are READY, they may still be in
            // an invalid state (i.e. connected but not processed)
            // This is OK as it tells us that the channel is working,
            // and the SMS should handle the PV state appropriately.
            if ( m_dev_desc && !m_defined && ready > 0 )
            {
                if ( ready ==
                        ( m_dev_desc->m_pvs.size()
                            + ( ( m_dev_desc->m_active_pv ) ? 1 : 0 ) ) )
                {
                    // Define Device with ConfigManager - this will return
                    // the "real" DeviceDescriptor record
                    device_changed = false;
                    DeviceRecordPtr new_rec =
                        m_stream_api.getCfgMgr().defineDevice(
                            *m_dev_desc, device_changed );

                    // Save new device record
                    m_dev_record = new_rec;

                    std::string deviceStr = "";
                    if ( m_dev_record != NULL )
                    {
                        deviceStr = " Device ["
                            + m_dev_record->m_name + "]";
                    }

                    // Update channel info objects with new device & pv
                    // pointers (replaces m_dev_desc pointers)
                    for ( idx = m_pv_index.begin();
                            idx != m_pv_index.end(); ++idx )
                    {
                        ich = m_chan_info.find( idx->second );
                        if ( ich != m_chan_info.end() )
                        {
                            // Careful! PV names can be changed in new_rec
                            // due to name conflicts!!!
                            if ( (pv = m_dev_record->getPvByConnection(
                                    idx->first )) )
                            {
                                // Replace temporary device and PV records
                                // with new managed records
                                ich->second.m_device = m_dev_record;
                                ich->second.m_pv = pv;
                            }
                            // Handle Any Active Status PV...
                            else if ( m_dev_record->m_active_pv
                                    && !m_dev_record->m_active_pv_conn
                                        .compare( idx->first ) )
                            {
                                // Replace temporary device and PV records
                                // with new managed records
                                ich->second.m_device = m_dev_record;
                                ich->second.m_pv =
                                    m_dev_record->m_active_pv;
                            }
                            else
                            {
                                syslog( LOG_ERR, "%s %s: %s%s %s=%s - %s",
                                    "PVSD ERROR:",
                                    "DeviceAgent::controlThread()",
                                    "Missing PV for", deviceStr.c_str(),
                                    "connection", idx->first.c_str(),
                                    "Ignoring..." );
                                usleep(33333); // give syslog a chance...
                            }
                        }
                        else
                        {
                            syslog( LOG_ERR, "%s %s: %s %lx %s %s - %s",
                                "PVSD ERROR:",
                                "DeviceAgent::controlThread()",
                                "Missing Channel Info", (long) idx->second,
                                "for PV", idx->first.c_str(),
                                "Ignoring..." );
                            usleep(33333); // give syslog a chance...
                        }
                    }

                    // Send cached PV values only if the device
                    // actually changed
                    if ( device_changed )
                        sendCurrentValues();

                    // Start with a Full House, Then Track for Status
                    m_dev_record->m_ready = ready;

                    m_defined = true;

                    // Delete temporary device descriptor
                    delete m_dev_desc;
                    m_dev_desc = 0;

                    // Now, Check Any Saved Device "Active Status"...
                    if ( m_dev_record->m_active_pv )
                    {
                        std::string pvStr = " for PV <"
                            + m_dev_record->m_active_pv->m_name + "> ("
                            + m_dev_record->m_active_pv->m_connection
                            + ")";

                        // If Device is Active, Make Sure Device Status Set
                        // (And Send Current Values as Needed...)
                        if ( m_dev_record->m_active_pv->m_is_active
                                == DEVICE_IS_ACTIVE )
                        {
                            // Only Set/Log if Active Status Changed...
                            if ( !(m_dev_record->m_active) )
                            {
                                m_dev_record->m_active = true;

                                syslog( LOG_INFO,
                                    "%s: %s %s -%s%s [%s = %u (%s)]",
                                    "DeviceAgent::controlThread()",
                                    "Mark Device Active",
                                    "from Active Status PV",
                                    deviceStr.c_str(), pvStr.c_str(),
                                    "active", 1, "true" );
                                usleep(33333); // give syslog a chance...
                            }

                            // If We Didn't Just Send the PV Values
                            // (Because the Device Didn't Change)
                            // Then Send Them Now! ;-D
                            if ( !device_changed )
                                sendCurrentValues();
                        }

                        // If Device Status is Unknown, Leave Device As Is!
                        // (Just Send Current Values as Needed...)
                        else if ( m_dev_record->m_active_pv->m_is_active
                                == DEVICE_IS_UNKNOWN )
                        {
                            syslog( LOG_ERR,
                                "%s %s: %s, %s %s -%s%s [%s = %u (%s)]",
                                "PVSD ERROR:",
                                "DeviceAgent::controlThread()",
                                "Device Active Status Unknown",
                                "Active Status PV Disconnected!",
                                "Leave Device Status As Is",
                                deviceStr.c_str(), pvStr.c_str(),
                                "active", m_dev_record->m_active,
                                ( ( m_dev_record->m_active )
                                    ? "true" : "false" ) );
                            usleep(33333); // give syslog a chance...

                            // If Device is Active,
                            // And We Didn't Just Send the PV Values
                            // (Because the Device Didn't Change)
                            // Then Send Them Now! ;-D
                            if ( m_dev_record->m_active
                                    && !device_changed )
                            {
                                sendCurrentValues();
                            }
                        }

                        // If Device is Inactive, "Soft-Delete" Device
                        // (i.e. Don't _Actually_ Delete It, Just Pretend!)
                        else if ( m_dev_record->m_active_pv->m_is_active
                                == DEVICE_IS_INACTIVE )
                        {
                            // Only Set/Log if Active Status Changed...
                            if ( m_dev_record->m_active )
                            {
                                m_dev_record->m_active = false;

                                syslog( LOG_INFO,
                                    "%s: %s %s -%s%s [%s = %u (%s)]",
                                    "DeviceAgent::controlThread()",
                                    "Mark Device Inactive",
                                    "from Active Status PV",
                                    deviceStr.c_str(), pvStr.c_str(),
                                    "active", 0, "false" );
                                usleep(33333); // give syslog a chance...
                            }

                            if ( m_dev_record.get() )
                            {
                                m_stream_api.getCfgMgr().undefineDevice(
                                    m_dev_record,
                                    /* Don't Delete Device! */ false );
                            }
                            else
                            {
                                syslog( LOG_ERR,
                                    "%s %s: %s%s%s [%s = %u (%s)]",
                                    "PVSD ERROR:",
                                    "DeviceAgent::controlThread()",
                                    "Couldn't Soft-Undefine Now-Inactive",
                                    deviceStr.c_str(), pvStr.c_str(),
                                    "active", 0, "false" );
                                usleep(33333); // give syslog a chance...
                            }
                        }
                    }
                }

                // Device Not Ready to Define, Some PVs Still Pending...
                else
                {
                    std::string deviceStr = "";
                    if ( !m_dev_desc->m_name.empty() )
                        deviceStr = "Device ["
                            + m_dev_desc->m_name + "] - ";

                    // Collect Pending PV Names for _Single_ Log Message!
                    stringstream ss;
                    for ( uint32_t i=0 ; i < pendingPVs.size() ; i++ )
                    {
                        if ( i ) ss << " ";
                        ss << "<" << pendingPVs[i] << ">";
                    }

                    syslog( LOG_INFO,
                        "%s: %sWaiting for %ld Pending PV Connections: %s",
                        "DeviceAgent::controlThread()", deviceStr.c_str(),
                        m_dev_desc->m_pvs.size()
                            + ( ( m_dev_desc->m_active_pv ) ? 1 : 0 )
                            - ready,
                        ss.str().c_str() );
                    usleep(33333); // give syslog a chance...

                    // Save Number of "Ready" PVs, in case Device Hangs
                    // on Initialization (include pending count in signal)
                    m_dev_desc->m_ready = ready;
                }
            }
        }
        catch ( TraceException &e )
        {
            syslog( LOG_ERR, "%s %s: TraceException thrown!",
                "PVSD ERROR:", "DeviceAgent::controlThread()" );
            syslog( LOG_ERR, "content: %s", e.toString( true ).c_str() );
        }
        catch ( exception &e )
        {
            syslog( LOG_ERR, "%s %s: std::exception thrown!",
                "PVSD ERROR:", "DeviceAgent::controlThread()" );
            syslog( LOG_ERR, "content: %s", e.what() );
        }
        catch(...)
        {
            syslog( LOG_ERR, "%s %s: Unknown exception thrown!",
                "PVSD ERROR:", "DeviceAgent::controlThread()" );
        }
    }
}

/**
 * @brief Monitors device start-up and generates alarm if needed
 *
 * A device descriptor packet is only injected into the output stream when a
 * device becomes fully active. For EPICS, this means all configured process
 * variables are connected and any required metadata has been receieved;
 * however, PV error codes do not impact the "readiness" of a device. If a
 * device does not become ready within a configured timeout, this thread
 * will generate a ComBus signal alerting any monitoring processes that the
 * device is in a hung state. This signal will persist until the device
 * becomes ready, or the DeviceAgent instance is destroyed (i.e. pvsd is
 * reconfigured or shutdown).
 */
void
DeviceAgent::monitorThread()
{
    ca_attach_context( m_epics_context );

    enum DeviceStartupState
    {
        DSS_INITIALIZING,
        DSS_FAULT,
        DSS_READY
    };

    timer_t             tid;
    struct sigevent     timer_cfg;
    struct itimerspec   timeout;
    string              sid;
    string              dev_name;
    DeviceStartupState  state = DSS_READY;

    // Create but don't start timer
    memset( &timer_cfg, 0, sizeof(struct sigevent) );
    timer_cfg.sigev_notify = SIGEV_NONE;
    timer_create( CLOCK_REALTIME, &timer_cfg, &tid );

    /* Note: it is possible for devices to change state quickly without this
     * monitor loop from ever noticing. This is fine since the only purpose of
     * the loop is to detect slow/hung devices. It's also possible for an
     * initializing device to be reconfigured before is becomes "ready" which
     * is also OK - but if the total initialization time takes too long it
     * could cause a signal to be asserted briefly (this is unlikely). If a
     * hung device is reconfigured (which is likely), then the asserted signal
     * will be properly retracted when it is finally ready.
     */

    // Run until DeviceAgent is shutdown/destroyed
    while ( m_agent_active )
    {
        sleep(1);

        boost::unique_lock<boost::mutex> lock(m_mutex);

        switch( state )
        {
            case DSS_INITIALIZING: // Timer running - device not ready yet
            {
                // Is device ready? If so, stop timer and go to READY
                if ( m_defined )
                {
                    // Change state
                    state = DSS_READY;
                }
                else
                {
                    // Else did timer expire?
                    // If so, assert signal and go to FAULT state
                    timer_gettime( tid, &timeout );
                    if ( timeout.it_value.tv_sec == 0
                            && timeout.it_value.tv_nsec == 0 )
                    {
                        dev_name = m_dev_desc->m_name;
                        sid = string("SID_EPICS_DEV_") + dev_name;
                        stringstream ss;
                        ss << "PVSD ERROR: ";
                        ss << "DeviceAgent::monitorThread(): ";
                        ss << string("Device [") + dev_name;
                        ss << "] INIT HUNG, Check beamline.xml - ";
                        ss << ( m_dev_desc->m_pvs.size()
                            + ( ( m_dev_desc->m_active_pv ) ? 1 : 0 )
                            - m_dev_desc->m_ready );
                        ss << " PVs Pending!";
                        ADARA::ComBus::SignalAssertMessage sigmsg( sid,
                            "EPICS", ss.str(), ADARA::ERROR );
                        ADARA::ComBus::Connection::getInst().broadcast(
                            sigmsg );

                        // Also log the error
                        syslog( LOG_ERR, ss.str().c_str() );
                        usleep(33333); // give syslog a chance...

                        // Be Sure to Set Device Active Status If Known!
                        if ( m_dev_desc->m_active_pv )
                        {
                            string pvStr = "PV <"
                                + m_dev_desc->m_active_pv->m_name + "> ("
                                + m_dev_desc->m_active_pv->m_connection
                                    + ")";

                            if ( m_dev_desc->m_active_pv->m_is_active
                                == DEVICE_IS_ACTIVE )
                            {
                                syslog( LOG_ERR,
                                    "%s: %s [%s] %s to %s, %s %s",
                                    "DeviceAgent::monitorThread()",
                                    "Setting HUNG Device",
                                    dev_name.c_str(),
                                    "Active Status", "Active",
                                    "Based on Active Status",
                                    pvStr.c_str() );
                                m_dev_desc->m_active = true;
                            }
                            else if ( m_dev_desc->m_active_pv->m_is_active
                                == DEVICE_IS_INACTIVE )
                            {
                                syslog( LOG_ERR,
                                    "%s: %s [%s] %s to %s, %s %s",
                                    "DeviceAgent::monitorThread()",
                                    "Setting HUNG Device",
                                    dev_name.c_str(),
                                    "Active Status", "Inactive",
                                    "Based on Active Status",
                                    pvStr.c_str() );
                                m_dev_desc->m_active = false;
                            }
                            else
                            {
                                syslog( LOG_ERR,
                                    "%s: %s [%s] %s, %s %s",
                                    "DeviceAgent::monitorThread()",
                                    "Cannot Set HUNG Device",
                                    dev_name.c_str(),
                                    "Active Status",
                                    "Active Status Unknown for ",
                                    pvStr.c_str() );
                            }
                        }

                        if ( m_dev_record.get() )
                        {
                            m_stream_api.getCfgMgr().undefineDevice(
                                m_dev_record );
                            m_dev_record.reset();
                        }

                        m_hung = true;

                        // Change state
                        state = DSS_FAULT;
                    }
                }
                break;
            }
            case DSS_FAULT: // Timer stopped - device not ready
            {
                // Is device ready?
                // If so, retract signal and go to state READY
                if ( m_defined )
                {
                    // Retract signal
                    ADARA::ComBus::SignalRetractMessage sigmsg( sid );
                    ADARA::ComBus::Connection::getInst().broadcast(
                        sigmsg );

                    // Log the recovery
                    string message =
                        "PVSD ERROR: DeviceAgent::monitorThread(): "
                        + string("Device [") + dev_name
                        + "] has recovered from hung state.";
                    syslog( LOG_ERR, message.c_str() );
                    usleep(33333); // give syslog a chance...

                    m_hung = false;

                    // Change state
                    state = DSS_READY;
                }
                break;
            }
            case DSS_READY: // Timer stopped - device ready
            {
                // Did device get updated
                if ( !m_defined )
                {
                    // Start timer
                    memset( &timeout, 0, sizeof(struct itimerspec) );
                    timeout.it_value.tv_sec = m_device_init_timeout;
                    timer_settime( tid, 0, &timeout, 0 );

                    // Change state to INIT
                    state = DSS_INITIALIZING;
                }
                break;
            }
        }
    }

    // Exited while in fault state - retract signal
    if ( state == DSS_FAULT )
    {
        ADARA::ComBus::SignalRetractMessage msg( sid );
        ADARA::ComBus::Connection::getInst().broadcast( msg );
    }

    // Clean-up timer
    timer_delete( tid );
}

/**
 * @brief Handles EPICS connection status callbacks
 * @param a_args - EPICS callback arguments
 *
 * This method handles EPICS channel connection Up/Down events. When a
 * connection is established, a subscription is created for time-value
 * events, and the state machine is triggered to ask for channel metadata
 * (INFO_NEEDED). When a connection is lost, the event subscription is
 * dropped and a PV disconnect packet is emitted.
 */
void
DeviceAgent::epicsConnectionHandler(
        struct connection_handler_args a_args )
{
    try
    {
        // Connection established?
        if ( a_args.op == CA_OP_CONN_UP )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich =
                m_chan_info.find( a_args.chid );

            if ( ich != m_chan_info.end() )
            {
                ich->second.m_connected = true;

                chtype type = ca_field_type( a_args.chid );
                unsigned long elem_count = ca_element_count( a_args.chid );
                if ( VALID_DB_FIELD( type ) )
                {
                    // Save native type
                    ich->second.m_ca_type = type;
                    ich->second.m_ca_elem_count = elem_count;
                    //cout <<  "chan: " << ich->first
                        // << " ca_type = " << type << " for PV: "
                        // << ich->second.m_pv->m_connection << endl;

                    std::string deviceStr = "";
                    if ( ich->second.m_device != NULL )
                    {
                        deviceStr = " - Device ["
                            + ich->second.m_device->m_name + "]";
                    }

                    std::string pvStr = "";
                    if ( ich->second.m_pv != NULL )
                    {
                        pvStr = " for PV <"
                            + ich->second.m_pv->m_name + "> ("
                            + ich->second.m_pv->m_connection + ")";
                    }

                    if ( ca_create_subscription(
                            epicsToTimeRecordType( type ), 0,
                            ich->second.m_chid,
                            DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
                            &epicsEventCallback, this,
                            &ich->second.m_evid ) == ECA_NORMAL )
                    {
                        // Log as "Error" to Trigger Email Notification
                        // (as an Accompaniment to Subscription Down...)
                        syslog( LOG_ERR, "%s %s: Subscription created%s%s",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(33333); // give syslog a chance...

                        ich->second.m_subscribed = true;
                    }
                    else
                    {
                        syslog( LOG_ERR,
                            "%s %s: Failed to create subscription%s%s",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(33333); // give syslog a chance...
                    }

                    // Update Pseudo "Ready" Counter for Status Logging...
                    if ( ich->second.m_device != NULL )
                        (ich->second.m_device->m_ready)++;

                    ca_flush_io();

                    // Note: there is no way to know if the metadata
                    // on this channel has or hasn't changed,
                    // so assume it has changed

                    // There is NO ctrl record for EPICS string types
                    if ( ich->second.m_pv->m_type == PV_STR )
                        ich->second.m_chan_state = INFO_AVAILABLE;
                    else
                        ich->second.m_chan_state = INFO_NEEDED;

                    m_state_changed = true;
                    m_state_cond.notify_one();
                }
            }
        }

        // Connection lost?
        else if ( a_args.op == CA_OP_CONN_DOWN )
        {
            boost::unique_lock<boost::mutex> lock(m_mutex);

            map<chid,ChanInfo>::iterator ich =
                m_chan_info.find( a_args.chid );

            if ( ich != m_chan_info.end() )
            {
                ich->second.m_connected = false;

                std::string deviceStr = "";
                if ( ich->second.m_device != NULL )
                {
                    deviceStr = " - Device ["
                        + ich->second.m_device->m_name + "]";
                }

                std::string pvStr = "";
                if ( ich->second.m_pv != NULL )
                {
                    pvStr = " for PV <"
                        + ich->second.m_pv->m_name + "> ("
                        + ich->second.m_pv->m_connection + ")";
                }

                if ( ich->second.m_subscribed )
                {
                    syslog( LOG_ERR, "%s %s: %s%s%s",
                        "PVSD ERROR:",
                        "DeviceAgent::epicsConnectionHandler()",
                        "Clearing subscription (Down?)",
                        deviceStr.c_str(), pvStr.c_str() );
                    usleep(33333); // give syslog a chance...

                    // *** Prevent Deadlock with New EPICS Callback Guard!
                    lock.unlock();
                    ca_clear_subscription( ich->second.m_evid );
                    lock.lock();

                    ich->second.m_subscribed = false;
                }

                // Update Pseudo "Ready" Counter for Status Logging...
                if ( ich->second.m_device != NULL )
                    (ich->second.m_device->m_ready)--;

                // Set var state to disconnected
                ich->second.m_pv_state.m_status = epicsAlarmComm;
                ich->second.m_pv_state.m_severity = epicsSevMajor;

                // Lost Connection for PV, Clean Things Up...
                if ( ich->second.m_pv != NULL )
                {
                    // Always Save Active Status to PV Descriptor...
                    // (This PV Could Become an Active Status PV... ;-D)
                    ich->second.m_pv->m_is_active = DEVICE_IS_UNKNOWN;

                    // Lost Connection for Active Status PV...!
                    if ( ich->second.m_pv->m_is_active_pv )
                    {
                        // NOTE: Just Because Active Status PV Goes Away,
                        // *Don't* Set Device to "Inactive"...
                        // Leave Device Status As Is! ;-D (8/1/2019)
                        // if ( ich->second.m_device != NULL )
                            // ich->second.m_device->m_active = false;

                        std::string statusStr = "unknown";
                        bool active_state = false;

                        std::string deviceStr = " *** NO DEVICE ***";
                        if ( ich->second.m_device != NULL )
                        {
                            deviceStr = " Device ["
                                + ich->second.m_device->m_name + "]";
                            active_state = ich->second.m_device->m_active;
                            statusStr = active_state ? "true" : "false";
                        }

                        std::string pvStr = "";
                        if ( ich->second.m_pv != NULL )
                        {
                            pvStr = " Active Status PV <"
                                + ich->second.m_pv->m_name + "> ("
                                + ich->second.m_pv->m_connection + ")";
                        }

                        syslog( LOG_ERR,
                            "%s %s: %s%s%s - %s [%s = %u (%s)]",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            "Lost Active Status PV Connection for",
                            deviceStr.c_str(), pvStr.c_str(),
                            "Device Active Status Remains Unchanged",
                            "active", active_state, statusStr.c_str() );
                        usleep(33333); // give syslog a chance...

                        // NOTE: *Don't* "Soft-Delete" Device! Leave As Is!
                        // Device has Gone Inactive, "Soft-Delete" Device
                        // (i.e. Don't _Actually_ Delete It, Just Pretend!)
                        // if ( m_dev_record.get() )
                        // {
                            // m_stream_api.getCfgMgr().undefineDevice(
                                // m_dev_record,
                                // /* Don't Delete Device! */ false );
                        // }
                        // else
                        // {
                            // syslog( LOG_ERR,
                                // "%s %s: %s%s%s [%s = %u (%s)]",
                                // "PVSD ERROR:",
                                // "DeviceAgent::epicsConnectionHandler()",
                                // "Couldn't Soft-Undefine Now-Inactive",
                                // deviceStr.c_str(), pvStr.c_str(),
                                // "active", 0, "false" );
                            // usleep(33333); // give syslog a chance...
                        // }

                        // Don't Send Variable Value Updates
                        // for Active Status PVs...!
                        // (Unless Active Status PV is Marked
                        // to _Not_ Ignore, Because it Subsumed
                        // a Regular Device PV...)
                        if ( ich->second.m_pv->m_ignore )
                            return;
                    }
                }

                // Do not try to send value/alarm data unless
                // device is fully defined
                if ( !m_defined )
                    return;

                bool timeout;
                StreamPacket *pkt =
                    m_stream_api.getFreePacket( 5000, timeout );
                if ( pkt )
                {
                    pkt->type = VariableUpdate;
                    pkt->device = ich->second.m_device;
                    pkt->pv = ich->second.m_pv;
                    pkt->state = ich->second.m_pv_state;

                    m_stream_api.putFilledPacket( pkt );
                }
                else
                {
                    if ( m_stream_api.getFreeQueueActive() )
                    {
                        syslog( LOG_ERR, "%s %s: %s %s%s%s [%s = %lu]",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            "No Free Packets!", "VariableUpdate Lost",
                            deviceStr.c_str(), pvStr.c_str(),
                            "Filled Queue Size",
                            (unsigned long)
                                m_stream_api.getFilledQueueSize() );
                        usleep(33333); // give syslog a chance...
                    }
                    else
                    {
                        syslog( LOG_ERR, "%s %s: %s %s%s%s",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            "Queue Deactivated,", "Ignore VariableUpdate",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(33333); // give syslog a chance...
                    }
                }
            }
        }
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, "%s %s: TraceException!",
            "PVSD ERROR:", "DeviceAgent::epicsConnectionHandler()" );
        syslog( LOG_ERR, e.toString().c_str() );
    }
    catch( std::exception &e )
    {
        syslog( LOG_ERR, "%s %s: Exception!",
            "PVSD ERROR:", "DeviceAgent::epicsConnectionHandler()" );
        syslog( LOG_ERR, e.what() );
    }
    catch(...)
    {
        syslog( LOG_ERR, "%s %s: Unknown exception!",
            "PVSD ERROR:", "DeviceAgent::epicsConnectionHandler()" );
    }
}


template<typename T>
void
DeviceAgent::updateState( const void *a_src, PVState &a_state )
{
    if ( ((T*)a_src)->stamp.secPastEpoch == 0 )
    {
        // Use a local timestamp if a valid timestamp has
        // not yet been received
        if ( a_state.m_time.sec == 0 )
        {
            a_state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;;
            a_state.m_time.nsec = 0;
        }
    }
    else
    {
        // It's *OK*" if the timestamp goes backwards, Let It Be! ;-D
        // (always just pass the data through; maybe something else
        // later on can sort it out... ;-)
        a_state.m_time.sec = ((T*)a_src)->stamp.secPastEpoch;
        a_state.m_time.nsec = ((T*)a_src)->stamp.nsec;
    }

    a_state.m_status = ((T*)a_src)->status;
    a_state.m_severity = ((T*)a_src)->severity;
}


/**
 * @brief Handles EPICS channel events
 * @param a_args - EPICS callback arguments
 *
 * This method handles EPICS data and metadata channel events.
 */
void
DeviceAgent::epicsEventHandler( struct event_handler_args a_args )
{
    try
    {
        // Valid status / db record?
        if ( a_args.status != ECA_NORMAL || a_args.dbr == 0 )
            return;

        // Data event?
        if ( epicsIsTimeRecordType( a_args.type ) )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Valid EPICS channel?
            map<chid,ChanInfo>::iterator ich =
                m_chan_info.find( a_args.chid );

            if ( ich != m_chan_info.end() )
            {
                // Extract PV state from type-specific data structure
                PVState &state = ich->second.m_pv_state;

                bool active_state = false;

                // For Logging...
                std::string ckDeviceStr = "";
                if ( ich->second.m_device != NULL )
                {
                    ckDeviceStr = " for Device ["
                        + ich->second.m_device->m_name + "]";
                }
                std::string ckPvStr = "";
                if ( ich->second.m_pv != NULL )
                {
                    ckPvStr = " PV <"
                        + ich->second.m_pv->m_name + "> ("
                        + ich->second.m_pv->m_connection + ")";
                }

                switch ( a_args.type )
                {
                case DBR_TIME_STRING:
                    state.m_str_val =
                        ((struct dbr_time_string *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count; // Always 1 String?
                    updateState<struct dbr_time_string>(
                        a_args.dbr, state );
                    if ( !state.m_str_val.compare("true") )
                        active_state = true;
                    syslog( LOG_DEBUG, "%s: %s%s%s %s=[%s] %s=%u %s=%lu",
                        "DeviceAgent::epicsEventHandler()",
                        "Got String PV Value Update",
                        ckDeviceStr.c_str(), ckPvStr.c_str(),
                        "state.m_str_val", state.m_str_val.c_str(),
                        "state.m_elem_count", state.m_elem_count,
                        "state.m_str_val.size()", state.m_str_val.size() );
                    usleep(33333); // give syslog a chance...
                    break;
                case DBR_TIME_SHORT:
                    // Could be Scalar Numerical Value
                    // -OR- Variable Length Numerical Array...!
                    //    -> Therefore, Set *Both* Value Fields...! ;-D
                    state.m_int_val = (int32_t)
                        ((struct dbr_time_short *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    if ( a_args.count > 1 ) // Minimum Array Size is 2...
                    {
                        state.m_short_array = new int16_t[a_args.count];
                        int16_t *values = (int16_t *)
                            &((struct dbr_time_short *)a_args.dbr)->value;
                        for ( uint32_t i=0 ; i < a_args.count ; i++ )
                        {
                            state.m_short_array[i] = values[i];
                        }
                    }
                    updateState<struct dbr_time_short>(
                        a_args.dbr, state );
                    if ( state.m_int_val )
                        active_state = true;
                    break;
                case DBR_TIME_FLOAT:
                    // Could be Scalar Numerical Value
                    // -OR- Variable Length Numerical Array...!
                    //    -> Therefore, Set *Both* Value Fields...! ;-D
                    state.m_double_val = (double)
                        ((struct dbr_time_float *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    if ( a_args.count > 1 ) // Minimum Array Size is 2...
                    {
                        state.m_float_array = new float[a_args.count];
                        float *values = (float *)
                            &((struct dbr_time_float *)a_args.dbr)->value;
                        for ( uint32_t i=0 ; i < a_args.count ; i++ )
                        {
                            state.m_float_array[i] = values[i];
                        }
                    }
                    updateState<struct dbr_time_float>(
                        a_args.dbr, state );
                    if ( state.m_double_val )
                        active_state = true;
                    break;
                case DBR_TIME_ENUM:
                    state.m_uint_val = (uint32_t)
                        ((struct dbr_time_enum *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    updateState<struct dbr_time_enum>(
                        a_args.dbr, state );
                    if ( state.m_uint_val )
                        active_state = true;
                    break;
                case DBR_TIME_CHAR:
                    // Could be (Scalar Numerical) Character
                    // -OR- Variable Length Character String...!
                    //    -> Therefore, Set *Both* Value Fields...! ;-D
                    state.m_uint_val = (uint32_t)
                        ((struct dbr_time_char *)a_args.dbr)->value;
                    state.m_str_val = (char *)
                        &((struct dbr_time_char *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    updateState<struct dbr_time_char>( a_args.dbr, state );
                    // *** Trim String Value to Specified Length!!
                    if ( state.m_elem_count < state.m_str_val.size() )
                    {
                        syslog( LOG_ERR,
                            "%s %s: %s%s%s ORIG %s=[%s] %s=%u %s=%lu",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsEventHandler()",
                            "Got Char PV Value Update TRIMMING LENGTH",
                            ckDeviceStr.c_str(), ckPvStr.c_str(),
                            "state.m_str_val", state.m_str_val.c_str(),
                            "state.m_elem_count", state.m_elem_count,
                            "state.m_str_val.size()",
                            state.m_str_val.size() );
                        usleep(33333); // give syslog a chance...
                        state.m_str_val.erase( state.m_elem_count );
                    }
                    // Check/Set Any Active State Value
                    if ( state.m_uint_val )
                        active_state = true;
                    syslog( LOG_DEBUG, "%s: %s%s%s %s=[%s] %s=%u %s=%lu",
                        "DeviceAgent::epicsEventHandler()",
                        "Got Char PV Value Update",
                        ckDeviceStr.c_str(), ckPvStr.c_str(),
                        "state.m_str_val", state.m_str_val.c_str(),
                        "state.m_elem_count", state.m_elem_count,
                        "state.m_str_val.size()", state.m_str_val.size() );
                    usleep(33333); // give syslog a chance...
                    break;
                case DBR_TIME_LONG:
                    // Could be Scalar Numerical Value
                    // -OR- Variable Length Numerical Array...!
                    //    -> Therefore, Set *Both* Value Fields...! ;-D
                    state.m_int_val = (int32_t)
                        ((struct dbr_time_long *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    if ( a_args.count > 1 ) // Minimum Array Size is 2...
                    {
                        state.m_long_array = new int32_t[a_args.count];
                        int32_t *values = (int32_t *)
                            &((struct dbr_time_long *)a_args.dbr)->value;
                        for ( uint32_t i=0 ; i < a_args.count ; i++ )
                        {
                            state.m_long_array[i] = values[i];
                        }
                    }
                    updateState<struct dbr_time_long>(
                        a_args.dbr, state );
                    if ( state.m_int_val )
                        active_state = true;
                    break;
                case DBR_TIME_DOUBLE:
                    // Could be Scalar Numerical Value
                    // -OR- Variable Length Numerical Array...!
                    //    -> Therefore, Set *Both* Value Fields...! ;-D
                    state.m_double_val = (double)
                        ((struct dbr_time_double *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    if ( a_args.count > 1 ) // Minimum Array Size is 2...
                    {
                        state.m_double_array = new double[a_args.count];
                        double *values = (double *)
                            &((struct dbr_time_double *)a_args.dbr)->value;
                        for ( uint32_t i=0 ; i < a_args.count ; i++ )
                        {
                            state.m_double_array[i] = values[i];
                        }
                    }
                    updateState<struct dbr_time_double>(
                        a_args.dbr, state );
                    if ( state.m_double_val )
                        active_state = true;
                    break;
                default:
                    break;
                }

                // Set PV/Device Status as Needed from Value Update...
                if ( ich->second.m_pv != NULL )
                {
                    // Always Save Active Status to PV Descriptor...
                    // (This PV Could Become an Active Status PV... ;-D)
                    ich->second.m_pv->m_is_active =
                        ( active_state )
                            ? DEVICE_IS_ACTIVE : DEVICE_IS_INACTIVE;

                    // Set Device Active Status
                    // If This PV is an Active Status PV...
                    if ( ich->second.m_pv->m_is_active_pv )
                    {
                        std::string deviceStr = " *** NO DEVICE ***";
                        if ( ich->second.m_device != NULL )
                        {
                            deviceStr = " Device ["
                                + ich->second.m_device->m_name + "]";
                        }

                        std::string pvStr = "";
                        if ( ich->second.m_pv != NULL )
                        {
                            pvStr = " from Active Status PV <"
                                + ich->second.m_pv->m_name + "> ("
                                + ich->second.m_pv->m_connection + ")";
                        }

                        if ( ich->second.m_device != NULL )
                        {
                            // Only Set/Log if Active Status Changed...
                            if ( ich->second.m_device->m_active
                                    != active_state )
                            {
                                syslog( LOG_ERR,
                                "%s %s: %s%s%s [%s = %u (%s) -> %u (%s)]",
                                    "PVSD ERROR:",
                                    "DeviceAgent::epicsEventHandler()",
                                    "Setting Active Status for",
                                    deviceStr.c_str(), pvStr.c_str(),
                                    "active",
                                    ich->second.m_device->m_active,
                                    ( (ich->second.m_device->m_active)
                                        ? "true" : "false" ),
                                    active_state,
                                    ( active_state ? "true" : "false" ) );
                                usleep(33333); // give syslog a chance...

                                ich->second.m_device->m_active =
                                    active_state;
                            }

                            // Setting Device Active, Send PV Values...
                            if ( active_state )
                            {
                                sendCurrentValues();
                            }

                            // Device has Gone Inactive,
                            // "Soft-Delete" Device
                            // (i.e. Don't _Actually_ Delete It,
                            // Just Pretend!)
                            else
                            {
                                if ( m_dev_record.get() )
                                {
                                    m_stream_api.getCfgMgr()
                                        .undefineDevice(
                                            m_dev_record,
                                            /* Don't Delete Device! */
                                            false );
                                }
                                else
                                {
                                    syslog( LOG_ERR,
                                        "%s %s: %s %s%s%s [%s = %u (%s)]",
                                        "PVSD ERROR:",
                                        "DeviceAgent::epicsEventHandler()",
                                        "Couldn't Soft-Undefine",
                                        "Now-Inactive",
                                        deviceStr.c_str(), pvStr.c_str(),
                                        "active", active_state,
                                        ( active_state
                                            ? "true" : "false" ) );
                                    usleep(33333);
                                    // give syslog a chance...
                                }
                            }
                        }
                        else
                        {
                            syslog( LOG_ERR,
                                "%s %s: %s%s%s [%s = %u (%s)]",
                                "PVSD ERROR:",
                                "DeviceAgent::epicsEventHandler()",
                                "FAILED to Set Active Status for",
                                deviceStr.c_str(), pvStr.c_str(),
                                "active", active_state,
                                ( active_state ? "true" : "false" ) );
                            usleep(33333); // give syslog a chance...
                        }

                        // Don't Send Variable Value Updates
                        // for Active Status PVs...!
                        // (Unless Active Status PV is Marked
                        // to _Not_ Ignore, Because it Subsumed
                        // a Regular Device PV...)
                        if ( ich->second.m_pv->m_ignore )
                            return;
                    }
                }

                // Send value/alarm data if device is fully defined
                if ( m_defined )
                {
                    bool timeout;
                    StreamPacket *pkt =
                        m_stream_api.getFreePacket( 5000, timeout );
                    if ( pkt )
                    {
                        pkt->type = VariableUpdate;
                        pkt->device = ich->second.m_device;
                        pkt->pv = ich->second.m_pv;
                        pkt->state = state;

                        m_stream_api.putFilledPacket( pkt );
                    }
                    else
                    {
                        std::string deviceStr = "";
                        if ( ich->second.m_device != NULL )
                        {
                            deviceStr = " Device ["
                                + ich->second.m_device->m_name + "]";
                        }

                        std::string pvStr = "";
                        if ( ich->second.m_pv != NULL )
                        {
                            pvStr = " for PV <"
                                + ich->second.m_pv->m_name + "> ("
                                + ich->second.m_pv->m_connection + ")";
                        }

                        if ( m_stream_api.getFreeQueueActive() )
                        {
                            syslog( LOG_ERR, "%s %s: %s %s%s [%s = %lu]",
                                "PVSD ERROR:",
                                "DeviceAgent::epicsEventHandler()",
                                "No Free Packets! VariableUpdate Lost",
                                deviceStr.c_str(), pvStr.c_str(),
                                "Filled Queue Size",
                                (unsigned long)
                                    m_stream_api.getFilledQueueSize() );
                            usleep(33333); // give syslog a chance...
                        }
                        else
                        {
                            syslog( LOG_ERR, "%s %s: %s%s%s",
                                "PVSD ERROR:",
                                "DeviceAgent::epicsEventHandler()",
                                "Queue Deactivated, Ignore VariableUpdate",
                                deviceStr.c_str(), pvStr.c_str() );
                            usleep(33333); // give syslog a chance...
                        }
                    }
                }

                // Device Not Yet Defined, Ignoring Value Update...!
                else
                {
                    std::string deviceStr = "";
                    if ( ich->second.m_device != NULL )
                    {
                        deviceStr = " Device ["
                            + ich->second.m_device->m_name + "]";
                    }

                    std::string pvStr = "";
                    if ( ich->second.m_pv != NULL )
                    {
                        pvStr = " for PV <"
                            + ich->second.m_pv->m_name + "> ("
                            + ich->second.m_pv->m_connection + ")";
                    }

                    syslog( LOG_ERR, "%s %s: %s%s%s",
                        "PVSD ERROR:",
                        "DeviceAgent::epicsEventHandler()",
                        "Device Not Yet Defined, Ignore VariableUpdate",
                        deviceStr.c_str(), pvStr.c_str() );
                    usleep(33333); // give syslog a chance...
                }
            }

            // Invalid EPICS Channel/Not Found...!
            else
            {
                 syslog( LOG_ERR, "%s %s: %s Unknown Channel Id...",
                    "PVSD ERROR:",
                    "DeviceAgent::epicsEventHandler()",
                    "Error Looking Up EPICS Data Event, " );
                usleep(33333); // give syslog a chance...
            }
        }
        // Metadata event?
        else if ( epicsIsCtrlRecordType( a_args.type ) )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Valid EPICS channel?
            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end() )
            {
                // Note EPICS does not define ctrl structs (or units) for string types
                // Extract units and/or enumeration values
                switch ( a_args.type )
                {
                case DBR_CTRL_SHORT:
                    ich->second.m_ca_units = ((struct dbr_ctrl_short *)a_args.dbr)->units;
                    break;
                case DBR_CTRL_FLOAT:
                    ich->second.m_ca_units = ((struct dbr_ctrl_float *)a_args.dbr)->units;
                    break;
                case DBR_CTRL_CHAR:
                    ich->second.m_ca_units = ((struct dbr_ctrl_char *)a_args.dbr)->units;
                    break;
                case DBR_CTRL_LONG:
                    ich->second.m_ca_units = ((struct dbr_ctrl_long *)a_args.dbr)->units;
                    break;
                case DBR_CTRL_DOUBLE:
                    ich->second.m_ca_units = ((struct dbr_ctrl_double *)a_args.dbr)->units;
                    break;
                case DBR_CTRL_ENUM:
                    {
                    ich->second.m_ca_enum_vals.clear();
                    for ( int i = 0; i < ((struct dbr_ctrl_enum *)a_args.dbr)->no_str; ++i )
                        ich->second.m_ca_enum_vals[i] = ((struct dbr_ctrl_enum *)a_args.dbr)->strs[i];
                    break;
                    }
                default:
                    break;
                }

                // Bump state to INFO_AVAILABLE if it was pending
                if ( ich->second.m_chan_state == INFO_PENDING )
                {
                    ich->second.m_chan_state = INFO_AVAILABLE;
                    // Wake state machine
                    m_state_changed = true;
                    m_state_cond.notify_one();
                }
            }

            // Invalid EPICS Channel/Not Found...!
            else
            {
                 syslog( LOG_ERR, "%s %s: %s Unknown Channel Id...",
                    "PVSD ERROR:",
                    "DeviceAgent::epicsEventHandler()",
                    "Error Looking Up EPICS Metadata Event, " );
                usleep(33333); // give syslog a chance...
            }
        }
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, "%s %s: TraceException!",
            "PVSD ERROR:", "DeviceAgent::epicsEventHandler()" );
        syslog( LOG_ERR, e.toString().c_str() );
    }
    catch( std::exception &e )
    {
        syslog( LOG_ERR, "%s %s: Exception!",
            "PVSD ERROR:", "DeviceAgent::epicsEventHandler()" );
        syslog( LOG_ERR, e.what() );
    }
    catch(...)
    {
        syslog( LOG_ERR, "%s %s: Unknown exception!",
            "PVSD ERROR:", "DeviceAgent::epicsEventHandler()" );
    }
}


/**
 * @brief Send current PV values for all channels
 */
void
DeviceAgent::sendCurrentValues()
{
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin();
            ich != m_chan_info.end(); ++ich )
    {
        std::string deviceStr = "";
        if ( ich->second.m_device != NULL )
        {
            deviceStr = " Device ["
                + ich->second.m_device->m_name + "]";
        }

        std::string pvStr = "";
        if ( ich->second.m_pv != NULL )
        {
            pvStr = " for PV <"
                + ich->second.m_pv->m_name + "> ("
                + ich->second.m_pv->m_connection + ")";
        }

        // Don't Send Variable Value Updates for Active Status PVs...!
        if ( ich->second.m_pv != NULL
                && ich->second.m_pv->m_is_active_pv
                && ich->second.m_pv->m_ignore )
        {
            syslog( LOG_INFO, "%s: %s%s%s",
                "DeviceAgent::sendCurrentValues()",
                "Don't Send Active Status PV State for",
                deviceStr.c_str(), pvStr.c_str() );
            usleep(33333); // give syslog a chance...

            continue;
        }

        bool timeout;
        StreamPacket *pkt = m_stream_api.getFreePacket( 5000, timeout );
        if ( pkt )
        {
            pkt->type = VariableUpdate;
            pkt->device = ich->second.m_device;
            pkt->pv = ich->second.m_pv;
            pkt->state = ich->second.m_pv_state;

            //syslog( LOG_DEBUG, "%s: %s%s%s %s=%u %s=%u",
                //"DeviceAgent::sendCurrentValues()",
                //"Sending PV State for",
                //deviceStr.c_str(), pvStr.c_str(),
                //"status", ich->second.m_pv_state.m_status,
                //"severity", ich->second.m_pv_state.m_severity );
            //usleep(33333); // give syslog a chance...

            m_stream_api.putFilledPacket( pkt );
        }
        else
        {
            if ( m_stream_api.getFreeQueueActive() )
            {
                syslog( LOG_ERR, "%s %s: %s%s%s [%s = %lu]",
                    "PVSD ERROR:", "DeviceAgent::sendCurrentValues()",
                    "No Free Packets! VariableUpdate Lost for",
                    deviceStr.c_str(), pvStr.c_str(),
                    "Filled Queue Size",
                    (unsigned long) m_stream_api.getFilledQueueSize() );
                usleep(33333); // give syslog a chance...
            }
            else
            {
                syslog( LOG_ERR, "%s %s: %s%s%s",
                    "PVSD ERROR:", "DeviceAgent::sendCurrentValues()",
                    "Queue Deactivated, Ignore VariableUpdate for",
                    deviceStr.c_str(), pvStr.c_str() );
                usleep(33333); // give syslog a chance...
            }
        }
    }
}


/**
 * @brief Static connection callback for EPICS C-style API
 * @param a_args - EPICS callback args
 */
void
DeviceAgent::epicsConnectionCallback(
        struct connection_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent*)ca_puser( a_args.chid );

    if ( agent )
        agent->epicsConnectionHandler( a_args );
}


/**
 * @brief Static event callback for EPICS C-style API
 * @param a_args - EPICS callback args
 */
void
DeviceAgent::epicsEventCallback( struct event_handler_args a_args )
{
    DeviceAgent *agent = (DeviceAgent *)ca_puser( a_args.chid );

    if ( agent )
        agent->epicsEventHandler( a_args );
}


/**
 * @brief Checks if argument is a valid EPICS time record
 * @param a_rec_type - Input value to check
 * @return True if value is valid; false otherwise
 */
bool
DeviceAgent::epicsIsTimeRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_TIME_STRING && a_rec_type <= DBR_TIME_DOUBLE )
        return true;
    else
        return false;
}


/**
 * @brief Checks if argument is a valid EPICS control record
 * @param a_rec_type - Input value to check
 * @return True if value is valid; false otherwise
 */
bool
DeviceAgent::epicsIsCtrlRecordType( uint32_t a_rec_type )
{
    if ( a_rec_type >= DBR_CTRL_STRING && a_rec_type <= DBR_CTRL_DOUBLE )
        return true;
    else
        return false;
}


/**
 * @brief Converts EPICS record type to PVSD PV type
 * @param a_rec_type - Input value to convert
 * @return PVType if value is valid; throws otherwise
 */
PVType
DeviceAgent::epicsToPVType( uint32_t a_rec_type, uint32_t a_elem_count )
{
    switch ( a_rec_type )
    {
        case DBR_STRING:    return PV_STR;

        case DBR_ENUM:      return PV_ENUM;

        case DBR_SHORT:
        case DBR_LONG:
        {
            // Just a (Scalar) Integer...
            if ( a_elem_count <= 1 )
                return PV_INT;
            // Actually a Variable Length Integer Array...!
            else 
                return PV_INT_ARRAY;
        }

        case DBR_FLOAT:
        case DBR_DOUBLE:
        {
            // Just a (Scalar) Float...
            if ( a_elem_count <= 1 )
                return PV_REAL;
            // Actually a Variable Length Float Array...!
            else 
                return PV_REAL_ARRAY;
        }

        case DBR_CHAR:
        {
            // Just a (Scalar) Character...
            if ( a_elem_count <= 1 )
                return PV_UINT;
            // Actually a Variable Length Character String...!
            else 
                return PV_STR;
        }

        default:
            EXCEPT_PARAM( EC_INVALID_PARAM,
                "Invalid PV type: " << a_rec_type );
    }
}


/**
 * @brief Converts EPICS DB record type to EPICS time record type
 * @param a_rec_type - Input value to convert
 * @return EPICS time type if value is valid; throws otherwise
 */
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
    default:
        EXCEPT_PARAM( EC_INVALID_PARAM, "Invalid EPICS DB record type: " << a_rec_type );
    }
}


/**
 * @brief Converts EPICS DB record type to EPICS control record type
 * @param a_rec_type - Input value to convert
 * @return EPICS control type if value is valid; throws otherwise
 */
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
    default:
        EXCEPT_PARAM( EC_INVALID_PARAM, "Invalid EPICS DB record type: " << a_rec_type );
    }
}


}}

// vim: expandtab

