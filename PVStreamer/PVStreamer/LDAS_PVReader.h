/**
 * \file LDAS_PVReader.h
 * \brief Header file for LDAS_PVReader class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef LDAS_PVREADER
#define LDAS_PVREADER

#include <map>
#include <vector>
#include <list>

#include "LegacyDAS.h"
#include "PVReader.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"


namespace SNS { namespace PVS { namespace LDAS {

class LDAS_PVReaderAgent;
class LDAS_DeviceMonitor;

/// Interface used by LDAS_ReaderAgent instances to communicate with the LDAS_PVReader instance
class LDAS_IPVReaderAgentMgr
{
public:
    virtual void    socketConnected( LDAS_PVReaderAgent &a_gent ) = 0;
    virtual void    socketDisconnected( LDAS_PVReaderAgent &a_gent ) = 0;
    //virtual void    socketConnectionError( LDAS_PVReaderAgent &a_gent, long a_err_code ) = 0;
    //virtual void    socketError( LDAS_PVReaderAgent &a_gent, long a_err_code ) = 0;
};

/// Interface used by LDAS_DeviceMonitor instances to communicate with the LDAS_PVReader instance
class LDAS_IDevMonitorMgr
{
public:
    virtual void    deviceActive( Identifier a_dev_id, Timestamp a_time ) = 0;
    virtual void    deviceInactive( Identifier a_dev_id, Timestamp a_time ) = 0;

    // TODO Need to check for connection errors & disconnects
};

/**
 * \class LDAS_PVReader
 * \brief Provides input processing of legacy DAS process variables.
 *
 * The LDAS_PVReader class reads, translates, and transmits legacy DAS process
 * variable and device data into an PVStreamer internal data stream for eventual
 * processing by other PVStreamer components (output protocol adapters). The
 * LDAS_PVReader creates and manages LDAS_PVReaderAgents and LDAS_DeviceMonitor
 * instances to implement process variable and device processing respectively.
 * Monitoring and translation are initiated on a given host when the LDAS_PVReader
 * object receives a "configuration loaded" notification. The internal process
 * variable stream is created regardless of whether any downstream clients are
 * listening or not.
 */
class LDAS_PVReader : public IPVConfigListener, public PVReader, protected LDAS_IPVReaderAgentMgr, protected LDAS_IDevMonitorMgr
{
public:
    LDAS_PVReader( PVStreamer &a_streamer );
    ~LDAS_PVReader();

private:

    // ---------- IPVConfigListener Methods -----------------------------------

    void            configurationLoaded( Protocol a_protocol, const std::string &a_source );
    void            configurationInvalid( Protocol a_protocol, const std::string &a_source );

    // ---------- LDAS_IPVReaderAgentMgr Methods ------------------------------

    void            socketConnected( LDAS_PVReaderAgent &a_agent );
    void            socketDisconnected( LDAS_PVReaderAgent &a_agent );
    //void            socketConnectionError( LDAS_PVReaderAgent &a_gent, long a_err_code );
    void            socketError( LDAS_PVReaderAgent &a_gent, long a_err_code );

    // ---------- LDAS_IDevMonitorMgr Methods ---------------------------------

    void            deviceActive( Identifier a_dev_id, Timestamp a_time );
    void            deviceInactive( Identifier a_dev_id, Timestamp a_time );

    // ---------- Internal Methods --------------------------------------------

    void            handleInactiveAgent( LDAS_PVReaderAgent &a_agent );
    void            sendDeviceActive( Identifier a_dev_id, Timestamp a_time );
    void            sendDeviceInactive( Identifier a_dev_id, Timestamp a_time );
    void            sendPVActive( PVInfo *a_pv_info, Timestamp a_time );
    void            sendPVInactive( PVInfo *a_pv_info, Timestamp a_time );

    boost::mutex                                            m_mutex;            ///< Mutex used to protect internal containers & state
    std::map<std::string,LDAS_DeviceMonitor*>               m_monitors;         ///< Active device monitors
    std::map<Identifier,std::list<LDAS_PVReaderAgent*> >    m_active_devices;   ///< Active devices and associated reader agents
    std::list<LDAS_PVReaderAgent*>                          m_free_agents;      ///< Free (unassigned) reader agents
};

}}}

#endif
