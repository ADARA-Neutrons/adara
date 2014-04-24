/**
 * \file LDAS_PVReader.cpp
 * \brief Source file for LDAS_PVReader class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"
#include "LDAS_PVReader.h"
#include "LDAS_PVReaderAgent.h"
#include "LDAS_DeviceMonitor.h"

using namespace std;

namespace SNS { namespace PVS { namespace LDAS {

/**
 * \brief Constructor for LDAS_PVReader class.
 * \param a_streamer - Owning PVStreamer instance
 */
LDAS_PVReader::LDAS_PVReader( PVStreamer &a_streamer )
: PVReader( a_streamer, LDAS_PROTOCOL )
{
    m_streamer.attachConfigListener( *this );
}

/**
 * \brief Destructor for LDAS_PVReader class.
 */
LDAS_PVReader::~LDAS_PVReader()
{
    m_streamer.detachConfigListener( *this );

    // Delete devce monitors
    for ( map<std::string,LDAS_DeviceMonitor*>::iterator im = m_monitors.begin(); im != m_monitors.end(); ++im )
        delete im->second;
    
    // Delete free agents
    for ( list<LDAS_PVReaderAgent*>::iterator ia = m_free_agents.begin(); ia != m_free_agents.end(); ++ia )
        delete *ia;

    // Delete active agents
    for ( map<Identifier,list<LDAS_PVReaderAgent*> >::iterator idev = m_active_devices.begin(); idev != m_active_devices.end(); ++idev )
        for ( list<LDAS_PVReaderAgent*>::iterator ia = idev->second.begin(); ia != idev->second.end(); ++ia )
            delete *ia;
}

// ---------- IPVConfigListener Methods ---------------------------------------

/**
 * \brief Callback from IPVConfigListener API used to initiate device monitoring on host.
 * \param a_protocol - Protocol of configured host.
 * \param a_source - Source (host) that has been configured.
 */
void
LDAS_PVReader::configurationLoaded( Protocol a_protocol, const std::string &a_source )
{
    // Ignore if not our portocol
    if ( a_protocol != LDAS_PROTOCOL )
        return;

    LDAS_DeviceMonitor *dev_mon = 0;

    boost::unique_lock<boost::mutex> lock(m_mutex);

    // Launch device monitor for this host if not already running
    if ( m_monitors.find(a_source) == m_monitors.end())
    {
        dev_mon = new LDAS_DeviceMonitor( *this, m_streamer, a_source );
        m_monitors[a_source] = dev_mon;
    }

    lock.unlock();

    // Must connect dev mon outside of lock as it can (does) cause synchronous callbacks into
    // other protected regions of the PVReader object.
    if ( dev_mon )
        dev_mon->connect();
}

/**
 * \brief Callback from IPVConfigListener API used to indicate host has bad configuration.
 * \param a_protocol - Protocol of ost.
 * \param a_source - Source (host) that has bad configuration.
 */
void
LDAS_PVReader::configurationInvalid( Protocol a_protocol, const std::string &a_source )
{
    // Do nothing
}

// ---------- LDAS_IPVReaderAgentMgr Methods ------------------------------

/**
 * \brief Callback that indicate that a reader agent has successfully connected.
 * \param a_agent - The LDAS_PVReaderAgent instance that has connected.
 */
void
LDAS_PVReader::socketConnected( LDAS_PVReaderAgent &a_agent )
{
    PVInfo *pv = a_agent.getPV();

    Timestamp ts;
    ts.sec = (unsigned long)time(0);
    sendPVActive( pv, ts );
}

/**
 * \brief Callback that indicate that a reader agent has disconnected.
 * \param a_agent - The LDAS_PVReaderAgent instance that has disconnected.
 */
void
LDAS_PVReader::socketDisconnected( LDAS_PVReaderAgent &a_agent )
{
    // This is an expected callback that is triggered when a device disconnects in an orderly fashion
    handleInactiveAgent( a_agent );
}

#if 0
/**
 * \brief Callback that indicate that a reader agent has encountered a general socket error.
 * \param a_agent - The LDAS_PVReaderAgent instance that has the error.
 * \param a_err_code - An error code obtained from the DataSockets layer.
 */
void
LDAS_PVReader::socketError( LDAS_PVReaderAgent &a_agent, long a_err_code )
{
    // This is an unexpected callback - something has gone away when it shouldn't have
    LOG_ERROR( "Unknown data socket error for PV " << a_agent.getPV()->m_name );

    PVInfo *pv = a_agent.getPV();

    handleInactiveAgent( a_agent );

    // Need to retry connecting to this PV
}
#endif

/**
 * \brief Disconnect agent and move to free pool
 * \param a_agent - Agent to disconnect
 */
void
LDAS_PVReader::handleInactiveAgent( LDAS_PVReaderAgent &a_agent )
{
    PVInfo *pv = a_agent.getPV();

    // It's possible that we received multiple callbacks for an agent having connection issues
    if ( !pv )
        return;

    Timestamp ts;
    ts.sec = (unsigned long)time(0);
    sendPVInactive( pv, ts );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    // See if this was an unexpected disconnect (we should not receive 
    // disconnects from agents that still have active devices)
    map<Identifier,list<LDAS_PVReaderAgent*> >::iterator idev = m_active_devices.find( pv->m_device_id );
    if ( idev != m_active_devices.end())
    {
        // Device is stil active
        list<LDAS_PVReaderAgent*>::iterator ia = find( idev->second.begin(), idev->second.end(), &a_agent );
        if ( ia != idev->second.end() )
        {
            // Remove disconnected agent from device list
            idev->second.erase(ia);
        }
    }

    m_free_agents.push_back( &a_agent );
}


// ---------- LDAS_IDevMonitorMgr Methods -------------------------------------

/**
 * \brief Callback that indicate that a device has become active.
 * \param a_dev_id - The ID of the device that has become active.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::deviceActive( Identifier a_dev_id, Timestamp a_time )
{
    // See if this device is defined (might get notified about apps that we're not interested in?)
    if ( m_streamer.isDeviceDefined( a_dev_id ))
    {
        boost::unique_lock<boost::mutex> lock(m_mutex);

        if ( m_active_devices.find( a_dev_id ) != m_active_devices.end() )
        {
            LOG_WARNING( "Received unexpected/redundant activation notice for device ID " << a_dev_id );
        }
        else
        {
            // First send a device active stream packet
            sendDeviceActive( a_dev_id, a_time );

            // Assign and/or allocate agents for each PV that belongs to this device
            // Do not connect yet as deadlock would result

            vector<PVInfo*> & pvs = m_reader_services->getWriteableDevicePVs( a_dev_id );
            list<LDAS_PVReaderAgent*> agents;

            for ( vector<PVInfo*>::iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
            {
                if ( (*ipv)->m_access != PV_READ )
                    continue;

                if ( m_free_agents.size())
                {
                    agents.push_back(m_free_agents.front());
                    m_free_agents.pop_front();
                }
                else
                    agents.push_back(new LDAS_PVReaderAgent( *this, *m_reader_services ));
            }

            if ( agents.size())
                m_active_devices[a_dev_id] = agents;

            lock.unlock();

            // Now connect agents to PV sources (will generate synchronous callbacks)
            list<LDAS_PVReaderAgent*>::iterator ia = agents.begin();
            for ( vector<PVInfo*>::iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
            {
                if ( (*ipv)->m_access != PV_READ )
                    continue;

                (*ia)->connect( **ipv );
                ++ia;
            }
        }
    }
}


/**
 * \brief Callback that indicate that a device has become inactive.
 * \param a_dev_id - The ID of the device that has become inactive.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::deviceInactive( Identifier a_dev_id, Timestamp a_time )
{
    // See if this device is defined (might get notified about apps that we're not interested in?)
    if ( m_streamer.isDeviceDefined( a_dev_id ))
    {
        list<LDAS_PVReaderAgent*> disc_agents;
        list<LDAS_PVReaderAgent*>::iterator ia;

        boost::unique_lock<boost::mutex> lock(m_mutex);

        // Now disconnect the associated LDAS_PVReaderAgent from each PV
        map<Identifier,list<LDAS_PVReaderAgent*> >::iterator idev = m_active_devices.find( a_dev_id );
        if ( idev == m_active_devices.end() )
        {
            LOG_WARNING( "Received unexpected/redundant deactivation notice for device ID " << a_dev_id );
        }
        else
        {
            // First send a device inactive stream packet
            sendDeviceInactive( a_dev_id, a_time );

            // Retain soon-to-be disconnected agents
            disc_agents = idev->second;

            // Remove active device entry
            m_active_devices.erase(idev);

            // Tell inactive agents to disconnect
            // Must do this outside of the mutex as disconnect() causes an immediate callback (causing deadlock)
            // The disconnect call triggers a socketDisconnected which moves the associated agent to the free pool
            lock.unlock();

            for ( ia = disc_agents.begin(); ia != disc_agents.end(); ++ia )
                (*ia)->disconnect();
        }
    }
}

// ---------- Internal Methods ------------------------------------------------

/**
 * \brief This method transmits a DeviceActive PV stream packet for the specified device.
 * \param a_dev_id - The ID of the device that is active.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::sendDeviceActive( Identifier a_dev_id, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    if ( pkt )
    {
        pkt->pkt_type = DeviceActive;
        pkt->device_id = a_dev_id;
        pkt->time = a_time;

        m_reader_services->putFilledPacket(pkt);
    }
}

/**
 * \brief This method transmits a DeviceInactive PV stream packet for the specified device.
 * \param a_dev_id - The ID of the device that is inactive.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::sendDeviceInactive( Identifier a_dev_id, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    if ( pkt )
    {
        pkt->pkt_type = DeviceInactive;
        pkt->device_id = a_dev_id;
        pkt->time = a_time;

        m_reader_services->putFilledPacket(pkt);
    }
}

/**
 * \brief This method transmits a VarActive PV stream packet for the specified PV.
 * \param a_pv_info - The PV info object for the var that is active.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::sendPVActive( PVInfo *a_pv_info, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    if ( pkt )
    {
        pkt->pkt_type = VarActive;
        pkt->device_id = a_pv_info->m_device_id;
        pkt->time = a_time;
        pkt->pv_info = a_pv_info;

        m_reader_services->putFilledPacket(pkt);
    }
}

/**
 * \brief This method transmits a VarInactive PV stream packet for the specified PV.
 * \param a_pv_info - The PV info object for the var that is inactive.
 * \param a_time - The time when the activity occurred.
 */
void
LDAS_PVReader::sendPVInactive( PVInfo *a_pv_info, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();
    if ( pkt )
    {
        pkt->pkt_type = VarInactive;
        pkt->device_id = a_pv_info->m_device_id;
        pkt->time = a_time;
        pkt->pv_info = a_pv_info;

        m_reader_services->putFilledPacket(pkt);
    }
}


}}}