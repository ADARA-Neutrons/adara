#include "stdafx.h"


#include "LegacyDAS.h"
#include "LDAS_PVReader.h"
#include "LDAS_PVReaderAgent.h"
#include "LDAS_DeviceMonitor.h"

using namespace std;

namespace SNS { namespace PVS { namespace LDAS {


LDAS_PVReader::LDAS_PVReader( PVStreamer &a_streamer )
: PVReader( a_streamer, LDAS_PROTOCOL )
{
    m_streamer.attachConfigListener( *this );
}

LDAS_PVReader::~LDAS_PVReader()
{
    m_streamer.detachConfigListener( *this );

    // Delete devce monitors
    for ( map<std::string,LDAS_DeviceMonitor*>::iterator im = m_monitors.begin(); im != m_monitors.end(); ++im )
        delete im->second;
    
    // TODO Disconnect & delete connected agents

    // Delete free agents
    for ( list<LDAS_PVReaderAgent*>::iterator ia = m_free_agents.begin(); ia != m_free_agents.end(); ++ia )
        delete *ia;
}

void
LDAS_PVReader::configurationLoaded( Protocol a_protocol, const std::string &a_source )
{
    if ( a_protocol != LDAS_PROTOCOL )
        return;

    boost::lock_guard<boost::mutex> lock(m_cs);

    if ( m_monitors.find(a_source) == m_monitors.end())
    {
        m_monitors[a_source] = new LDAS_DeviceMonitor( *this, m_streamer, a_source );
    }
}

// ---------- LDAS_IPVReaderAgentMgr Methods ------------------------------


void
LDAS_PVReader::socketConnected( LDAS_PVReaderAgent &a_agent )
{
//    boost::lock_guard<boost::mutex> lock(m_cs);

    PVInfo *pv = a_agent.getPV();

    Timestamp ts;
    ts.sec = (unsigned long)time(0);
    sendPVActive( pv, ts );
}

void
LDAS_PVReader::socketDisconnected( LDAS_PVReaderAgent &a_agent )
{

    PVInfo *pv = a_agent.getPV();

    Timestamp ts;
    ts.sec = (unsigned long)time(0);
    sendPVInactive( pv, ts );

    boost::lock_guard<boost::mutex> lock(m_cs);

    // Unexpected disconnect? - this is a potentially serious condition
    map<Identifier,list<LDAS_PVReaderAgent*> >::iterator idev = m_active_devices.find( pv->m_device_id );
    if ( idev != m_active_devices.end())
    {
        list<LDAS_PVReaderAgent*>::iterator ia = find( idev->second.begin(), idev->second.end(), &a_agent );
        if ( ia != idev->second.end() )
        {
            // TODO Maybe have a reconnect re-try before killing agent?
            // Move disconnected agent to free list
            m_free_agents.push_back(*ia);
            idev->second.erase(ia);
        }
    }
}

void
LDAS_PVReader::socketConnectionError( LDAS_PVReaderAgent &a_gent, long a_err_code )
{
    // TODO What should we do with socket connection errors?
}

void
LDAS_PVReader::socketError( LDAS_PVReaderAgent &a_gent, long a_err_code )
{
    // TODO What should we do with socket errors that don't cause a disconnect? Log?
}


void
LDAS_PVReader::deviceActive( Identifier a_dev_id, Timestamp a_time )
{
    // See if this device is defined (might get notified about apps that we're not interested in?)
    if ( m_streamer.isDeviceDefined( a_dev_id ))
    {
        boost::lock_guard<boost::mutex> lock(m_cs);

        if ( m_active_devices.find( a_dev_id ) != m_active_devices.end() )
        {
            // Error - should not have received a "dev running" notice if device is already active
            //TODO Log error and continue
        }
        else
        {
            // First send a device active stream packet
            sendDeviceActive( a_dev_id, a_time );

            vector<PVInfo*> & pvs = m_reader_services->getWriteableDevicePVs( a_dev_id );
            list<LDAS_PVReaderAgent*> agents;

            // The following code will call connect() on agents which will trigger callbacks
            // Ensure that these callbacks do not deadlocks on the m_cs mutex

            for ( vector<PVInfo*>::iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
            {
                if ( (*ipv)->m_access == PV_READ )
                {
                    if ( m_free_agents.size())
                    {
                        agents.push_back(m_free_agents.front());
                        m_free_agents.pop_front();
                        agents.back()->connect( **ipv );
                    }
                    else
                        agents.push_back(new LDAS_PVReaderAgent(*this,m_reader_services,*ipv));
                }
            }

            if ( agents.size() )
                m_active_devices[a_dev_id] = agents;
        }
    }
}

void
LDAS_PVReader::deviceInactive( Identifier a_dev_id, Timestamp a_time )
{
    // See if this device is defined (might get notified about apps that we're not interested in?)
    if ( m_streamer.isDeviceDefined( a_dev_id ))
    {
        list<LDAS_PVReaderAgent*> disc_agents;
        list<LDAS_PVReaderAgent*>::iterator ia;

        boost::unique_lock<boost::mutex> lock(m_cs);

        // Now disconnect the associated LDAS_PVReaderAgent from each PV
        map<Identifier,list<LDAS_PVReaderAgent*> >::iterator idev = m_active_devices.find( a_dev_id );
        if ( idev == m_active_devices.end() )
        {
            // Error - should not have received a "dev stopped" notice if no agents are active
            //TODO Log error and continue
        }
        else
        {
            // First send a device active stream packet
            sendDeviceInactive( a_dev_id, a_time );

            // Move active agents to "retirement bin" and tell them to terminate
            //for ( ia = idev->second.begin(); ia != idev->second.end(); ++ia )
            //    m_inactive_agents.push_back(*ia);

            disc_agents = idev->second;

            // Remove active device entry
            m_active_devices.erase(idev);

            // Tell agents to disconnect
            // Must do this outside of the mutex as disconnect() causes an immediate callback...
            lock.unlock();
            for ( ia = disc_agents.begin(); ia != disc_agents.end(); ++ia )
                (*ia)->disconnect();
        }
    }
}

void
LDAS_PVReader::sendDeviceActive( Identifier a_dev_id, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    pkt->pkt_type = DeviceActive;
    pkt->device_id = a_dev_id;
    pkt->time = a_time;

    m_reader_services->putFilledPacket(pkt);
}

void
LDAS_PVReader::sendDeviceInactive( Identifier a_dev_id, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    pkt->pkt_type = DeviceInactive;
    pkt->device_id = a_dev_id;
    pkt->time = a_time;

    m_reader_services->putFilledPacket(pkt);
}

void
LDAS_PVReader::sendPVActive( PVInfo *a_pv_info, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    pkt->pkt_type = VarActive;
    pkt->device_id = a_pv_info->m_device_id;
    pkt->time = a_time;
    pkt->pv_info = a_pv_info;

    m_reader_services->putFilledPacket(pkt);
}

void
LDAS_PVReader::sendPVInactive( PVInfo *a_pv_info, Timestamp a_time )
{
    PVStreamPacket *pkt = m_reader_services->getFreePacket();

    pkt->pkt_type = VarInactive;
    pkt->device_id = a_pv_info->m_device_id;
    pkt->time = a_time;
    pkt->pv_info = a_pv_info;

    m_reader_services->putFilledPacket(pkt);
}



}}}