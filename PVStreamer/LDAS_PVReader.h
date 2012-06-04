#ifndef LDAS_PVREADER
#define LDAS_PVREADER

#include <map>
#include <vector>
#include <list>

#include "PVReader.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"


namespace SNS { namespace PVS { namespace LDAS {

class LDAS_PVReaderAgent;
class LDAS_DeviceMonitor;

class LDAS_IPVReaderAgentMgr
{
public:
    virtual void    socketConnected( LDAS_PVReaderAgent &a_gent ) = 0;
    virtual void    socketDisconnected( LDAS_PVReaderAgent &a_gent ) = 0;
    virtual void    socketConnectionError( LDAS_PVReaderAgent &a_gent, long a_err_code ) = 0;
    virtual void    socketError( LDAS_PVReaderAgent &a_gent, long a_err_code ) = 0;
};

class LDAS_IDevMonitorMgr
{
public:
    virtual void    deviceActive( Identifier a_dev_id, Timestamp a_time ) = 0;
    virtual void    deviceInactive( Identifier a_dev_id, Timestamp a_time ) = 0;
};

class LDAS_PVReader : public IPVConfigListener, public PVReader, protected LDAS_IPVReaderAgentMgr, protected LDAS_IDevMonitorMgr
{
public:
    LDAS_PVReader( PVStreamer &a_streamer );
    ~LDAS_PVReader();

private:

    // ---------- LDAS_IPVReaderAgentMgr Methods ------------------------------

    void            socketConnected( LDAS_PVReaderAgent &a_agent );
    void            socketDisconnected( LDAS_PVReaderAgent &a_agent );
    void            socketConnectionError( LDAS_PVReaderAgent &a_gent, long a_err_code );
    void            socketError( LDAS_PVReaderAgent &a_gent, long a_err_code );

    // ---------- LDAS_IDevMonitorMgr Methods ---------------------------------

    void            deviceActive( Identifier a_dev_id, Timestamp a_time );
    void            deviceInactive( Identifier a_dev_id, Timestamp a_time );

    // ---------- Internal Methods --------------------------------------------

    void            configurationLoaded( Protocol a_protocol, const std::string &a_source );
    void            sendDeviceActive( Identifier a_dev_id, Timestamp a_time );
    void            sendDeviceInactive( Identifier a_dev_id, Timestamp a_time );
    void            sendPVActive( PVInfo *a_pv_info, Timestamp a_time );
    void            sendPVInactive( PVInfo *a_pv_info, Timestamp a_time );

    boost::mutex        m_cs;

    std::map<std::string,LDAS_DeviceMonitor*>    m_monitors;

    //! Device ID -> list of socket readers and PVs
    std::map<Identifier,std::list<LDAS_PVReaderAgent*> > m_active_devices;

    //! Readers that are inactive but available for reuse
    std::list<LDAS_PVReaderAgent*> m_free_agents;
};

}}}

#endif
