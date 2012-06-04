#ifndef LDAS_DEVICEMONITOR
#define LDAS_DEVICEMONITOR

#include <string>


#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"

namespace SNS { namespace PVS {
    
class PVStreamer;

namespace LDAS {

class LDAS_IDevMonitorMgr;

class LDAS_DeviceMonitor
{
public:
    LDAS_DeviceMonitor( LDAS_IDevMonitorMgr &a_reader, PVStreamer &a_streamer, const std::string &a_hostname );
    ~LDAS_DeviceMonitor();


private:
    void        notifySocketData( NI::CNiDataSocketData &a_data );

    LDAS_IDevMonitorMgr    &m_mgr;
    PVStreamer             &m_streamer;
    std::string             m_hostname;
    NI::CNiDataSocket       m_notify_socket;
};

}}}

#endif
