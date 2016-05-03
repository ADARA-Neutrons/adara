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
    : m_stream_api(a_stream_api), m_dev_desc(0), m_defined(false),
      m_state_changed(false), m_active(true),
      m_epics_context(a_epics_context),
      m_device_init_timeout(a_device_init_timeout)
{
    m_monitor_thread = new boost::thread(
        boost::bind( &DeviceAgent::monitorThread, this ));
    m_ctrl_thread = new boost::thread(
        boost::bind( &DeviceAgent::controlThread, this ));

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
 * This method is used to establish or update EPICS connections when a new device is defined or the
 * current device is updated. If a current device is defined, the passed-in device descriptor is
 * compared to the existing descriptor and connections are updated based on any detected differences.
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
            if ( !a_device->getPvByConnection( (*ipv)->m_connection ))
                disconnectPV( *ipv );
        }

        // If a device is already defined, reuse any shared PV connections
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
                        // Update PV to new (temporary) PV
                        ich->second.m_pv = *ipv;
                        // Old device record is no longer valid - reset
                        ich->second.m_device.reset();

                        // Re-aqcuire metadata just in case it changed (only if connected)
                        if ( ich->second.m_chan_state != UNINITIALIZED )
                            ich->second.m_chan_state = INFO_NEEDED;
                    }
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
        for ( ipv = a_device->m_pvs.begin(); ipv != a_device->m_pvs.end(); ++ipv )
            connectPV( *ipv );
    }

    m_dev_desc = a_device;
    ca_flush_io();

    // If no new PV channels were created, state machine will not progress on it's own
    // Wake state machine in this case (doesn't matter if its notified more than once)
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
        ich->second.m_pv = m_dev_desc->getPvByConnection(
            ich->second.m_pv->m_connection );

        if ( ich->second.m_chan_state == INFO_AVAILABLE )
        {
            syslog( LOG_INFO,
                "%s: Device [%s] - Updating metadata for PV <%s> (%s)",
                "DeviceAgent::metadataUpdated()",
                m_dev_desc->m_name.c_str(),
                ich->second.m_pv->m_name.c_str(),
                ich->second.m_pv->m_connection.c_str() );
            usleep(30000); // give syslog a chance...

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
            usleep(30000); // give syslog a chance...

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
    usleep(30000); // give syslog a chance...

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
        usleep(30000); // give syslog a chance...
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
DeviceAgent::disconnectPV( PVDescriptor *a_pv )
{
    std::string deviceStr = "";
    if ( a_pv->m_device != NULL && !a_pv->m_device->m_name.empty() )
        deviceStr = "Device [" + a_pv->m_device->m_name + "] - ";

    map<std::string,chid>::iterator ipv =
        m_pv_index.find( a_pv->m_connection );

    if ( ipv != m_pv_index.end())
    {
        syslog( LOG_INFO, "%s: %sDisconnecting channel for PV <%s> (%s)",
            "DeviceAgent::disconnectPV()", deviceStr.c_str(),
            a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
        usleep(30000); // give syslog a chance...

        map<chid,ChanInfo>::iterator ich = m_chan_info.find( ipv->second );

        if ( ich != m_chan_info.end() )
        {
            if ( ich->second.m_subscribed )
                ca_clear_subscription( ich->second.m_evid );

            ca_clear_channel( ich->second.m_chid );

            // Update channel info index structures
            m_chan_info.erase( ich );

            // Don't flush I/O here - update() method will call it
        }
        else
        {
            syslog( LOG_WARNING,
                "%s: %sWarning: No Channel Info Found for PV <%s> (%s)",
                "DeviceAgent::disconnectPV()", deviceStr.c_str(),
                a_pv->m_name.c_str(), a_pv->m_connection.c_str() );
            usleep(30000); // give syslog a chance...
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
        usleep(30000); // give syslog a chance...
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
            if ( !m_active )
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
                         // << ich->second.m_ca_elem_count'
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
                        usleep(30000); // give syslog a chance...
                    }
                    pendingPVs.push_back( ich->second.m_pv->m_connection );
                    break;
                case INFO_AVAILABLE:
                    if ( m_defined )
                    {
                        // cout <<  "IA, chan: " << ich->first
                             // << " ca_type = " << ich->second.m_ca_type
                             // << " ca_element_count = "
                             // << ich->second.m_ca_elem_count'
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
                if ( ready == m_dev_desc->m_pvs.size() )
                {
                    device_changed = false;

                    // Defined device with ConfigManager - this will return
                    // the "real" DeviceDescriptor record
                    DeviceRecordPtr new_rec =
                        m_stream_api.getCfgMgr().defineDevice(
                            *m_dev_desc );

                    // If new record is the same as existing record,
                    // then nothing has changed
                    if ( new_rec != m_dev_record )
                        device_changed = true;

                    // Save new device record
                    m_dev_record = new_rec;

                    // Update channel info objects with new device & pv
                    // pointers (replaces m_dev_desc pointers)
                    for ( idx = m_pv_index.begin();
                            idx != m_pv_index.end(); ++idx )
                    {
                        // Careful! PV names can be changed in new_rec
                        // due to name conflicts!!!
                        pv = m_dev_record->getPvByConnection( idx->first );
                        ich = m_chan_info.find( idx->second );
                        if ( pv && ich != m_chan_info.end() )
                        {
                            // Replace temporary device and PV records
                            // with new managed records
                            ich->second.m_device = m_dev_record;
                            ich->second.m_pv = pv;
                        }
                    }

                    // Send cached PV values only if the device
                    // actually changed
                    if ( device_changed )
                        sendCurrentValues();

                    m_defined = true;
                    device_changed = false;

                    // Delete temporary device descriptor
                    delete m_dev_desc;
                    m_dev_desc = 0;
                }

                // Device Not Ready to Define, Some PVs Still Pending...
                else
                {
                    std::string deviceStr = "";
                    if ( !m_dev_desc->m_name.empty() )
                        deviceStr = "Device ["
                            + m_dev_desc->m_name + "] - ";

                    syslog( LOG_INFO, "%s: %sWaiting for %ld Pending PVs",
                        "DeviceAgent::controlThread()", deviceStr.c_str(),
                        m_dev_desc->m_pvs.size() - ready );
                    usleep(30000); // give syslog a chance...

                    for ( uint32_t i=0 ; i < pendingPVs.size() ; i++ )
                    {
                        syslog( LOG_INFO,
                            "%s: %sPending PV Connection <%s>",
                            "DeviceAgent::controlThread()",
                            deviceStr.c_str(), pendingPVs[i].c_str() );
                        usleep(30000); // give syslog a chance...
                    }

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
    memset( &timer_cfg, 0, sizeof(struct sigevent));
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
    while ( m_active )
    {
        sleep(1);

        boost::unique_lock<boost::mutex> lock(m_mutex);

        switch( state )
        {
            case DSS_INITIALIZING: // Timer running - device not ready yet
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
                            - m_dev_desc->m_ready );
                        ss << " PVs Pending!";
                        ADARA::ComBus::SignalAssertMessage sigmsg( sid,
                            "EPICS", ss.str(), ADARA::ERROR );
                        ADARA::ComBus::Connection::getInst().broadcast(
                            sigmsg );

                        // Also log the error
                        syslog( LOG_ERR, ss.str().c_str() );
                        usleep(30000); // give syslog a chance...

                        if ( m_dev_record.get() )
                        {
                            m_stream_api.getCfgMgr().undefineDevice(
                                m_dev_record );
                            m_dev_record.reset();
                        }

                        // Change state
                        state = DSS_FAULT;
                    }
                }
                break;
            case DSS_FAULT: // Timer stopped - device not ready
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
                    usleep(30000); // give syslog a chance...

                    // Change state
                    state = DSS_READY;
                }
                break;
            case DSS_READY: // Timer stopped - device ready
                // Did device get updated
                if ( !m_defined )
                {
                    // Start timer
                    memset( &timeout, 0, sizeof(struct itimerspec));
                    timeout.it_value.tv_sec = m_device_init_timeout;
                    timer_settime( tid, 0, &timeout, 0 );

                    // Change state to INIT
                    state = DSS_INITIALIZING;
                }
                break;
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
                if ( VALID_DB_FIELD( type ))
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
                        syslog( LOG_INFO, "%s: Subscription created%s%s",
                            "DeviceAgent::epicsConnectionHandler()",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(30000); // give syslog a chance...

                        ich->second.m_subscribed = true;
                    }
                    else
                    {
                        syslog( LOG_ERR,
                            "%s %s: Failed to create subscription%s%s",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(30000); // give syslog a chance...
                    }

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
            boost::lock_guard<boost::mutex> lock(m_mutex);

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
                    usleep(30000); // give syslog a chance...

                    ca_clear_subscription( ich->second.m_evid );
                    ich->second.m_subscribed = false;
                }

                // Set var state to disconnected
                ich->second.m_pv_state.m_status = epicsAlarmComm;
                ich->second.m_pv_state.m_severity = epicsSevMajor;

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
                        usleep(30000); // give syslog a chance...
                    }
                    else
                    {
                        syslog( LOG_ERR, "%s %s: %s %s%s%s",
                            "PVSD ERROR:",
                            "DeviceAgent::epicsConnectionHandler()",
                            "Queue Deactivated,", "Ignore VariableUpdate",
                            deviceStr.c_str(), pvStr.c_str() );
                        usleep(30000); // give syslog a chance...
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

/// Macro for common state parsing below
/*
#define SET_STATE( state, type, src ) \
    state.m_time.sec = ((struct type *)src)->stamp.secPastEpoch; \
    state.m_time.nsec = ((struct type *)src)->stamp.nsec; \
    state.m_status = ((struct type *)src)->status; \
    state.m_severity = ((struct type *)src)->severity;
*/

template<typename T>
void
DeviceAgent::updateState( const void *a_src, PVState &a_state )
{
    if ( ((T*)a_src)->stamp.secPastEpoch == 0 )
    {
        // Use a local timestamp if a valid timestamp has not yet been received
        if ( a_state.m_time.sec == 0 )
        {
            a_state.m_time.sec = (uint32_t)time(0) - EPICS_TIME_OFFSET;;
            a_state.m_time.nsec = 0;
        }
    }
    else
    {
        // Make sure timestamp does not go backwards
        if ( ((T*)a_src)->stamp.secPastEpoch > a_state.m_time.sec ||
             (((T*)a_src)->stamp.secPastEpoch == a_state.m_time.sec && ((T*)a_src)->stamp.nsec > a_state.m_time.nsec ))
        {
            a_state.m_time.sec = ((T*)a_src)->stamp.secPastEpoch;
            a_state.m_time.nsec = ((T*)a_src)->stamp.nsec;
        }
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
        if ( epicsIsTimeRecordType( a_args.type ))
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Valid EPICS channel?
            map<chid,ChanInfo>::iterator ich =
                m_chan_info.find( a_args.chid );

            if ( ich != m_chan_info.end())
            {
                // Extract PV state from type-specific data structure
                PVState &state = ich->second.m_pv_state;

                switch ( a_args.type )
                {
                case DBR_TIME_STRING:
                    state.m_str_val =
                        ((struct dbr_time_string *)a_args.dbr)->value;
                    updateState<struct dbr_time_string>(
                        a_args.dbr, state );
                    state.m_elem_count = a_args.count;
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
                    break;
                case DBR_TIME_ENUM:
                    state.m_uint_val = (uint32_t)
                        ((struct dbr_time_enum *)a_args.dbr)->value;
                    state.m_elem_count = a_args.count;
                    updateState<struct dbr_time_enum>(
                        a_args.dbr, state );
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
                    break;
                default:
                    break;
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
                            usleep(30000); // give syslog a chance...
                        }
                        else
                        {
                            syslog( LOG_ERR, "%s %s: %s %s%s",
                                "PVSD ERROR:",
                                "DeviceAgent::epicsEventHandler()",
                                "Queue Deactivated, Ignore VariableUpdate",
                                deviceStr.c_str(), pvStr.c_str() );
                            usleep(30000); // give syslog a chance...
                        }
                    }
                }
            }
        }
        // Metadata event?
        else if ( epicsIsCtrlRecordType( a_args.type ))
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);

            // Valid EPICS channel?
            map<chid,ChanInfo>::iterator ich = m_chan_info.find( a_args.chid );
            if ( ich != m_chan_info.end())
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
    for ( map<chid,ChanInfo>::iterator ich = m_chan_info.begin(); ich != m_chan_info.end(); ++ich )
    {
        bool timeout;
        StreamPacket *pkt = m_stream_api.getFreePacket( 5000, timeout );
        if ( pkt )
        {
            pkt->type = VariableUpdate;
            pkt->device = ich->second.m_device;
            pkt->pv = ich->second.m_pv;
            pkt->state = ich->second.m_pv_state;

            //cout << "Sending PV " << ich->second.m_device->m_id
                //<< "." << ich->second.m_pv->m_id
                //<< " on chid " << ich->first
                //<< ", stat = " <<  pkt->state.m_status
                //<< ", sev: " <<  pkt->state.m_severity << endl;

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
                    "PVSD ERROR:", "DeviceAgent::sendCurrentValues()",
                    "No Free Packets! VariableUpdate Lost for",
                    deviceStr.c_str(), pvStr.c_str(),
                    "Filled Queue Size",
                    (unsigned long) m_stream_api.getFilledQueueSize() );
                usleep(30000); // give syslog a chance...
            }
            else
            {
                syslog( LOG_ERR, "%s %s: %s %s%s",
                    "PVSD ERROR:", "DeviceAgent::sendCurrentValues()",
                    "Queue Deactivated, Ignore VariableUpdate for",
                    deviceStr.c_str(), pvStr.c_str() );
                usleep(30000); // give syslog a chance...
            }
        }
    }
}


/**
 * @brief Static connection callback for EPICS C-style API
 * @param a_args - EPICS callback args
 */
void
DeviceAgent::epicsConnectionCallback( struct connection_handler_args a_args )
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

