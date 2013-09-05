/**
 * \file LDAS_DeviceMonitor.h
 * \brief Header file for LDAS_DeviceMonitor class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef LDAS_DEVICEMONITOR
#define LDAS_DEVICEMONITOR

#include <string>
#include <set>

#include "LegacyDAS.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

namespace SNS { namespace PVS {
    
class PVStreamer;

namespace LDAS {

class LDAS_IDevMonitorMgr;

/**
 * \class LDAS_IDevMonitorMgr
 * \brief Legacy DAS device/application monitor.
 *
 * The LDAS_DeviceMonitor class provides monitoring of IOC devices (applications)
 * via a National Instruments DataSockets interface. Application activities (running
 * and stopped) are received and decoded, then sent to the owning LDAS_IDevMonitorMgr
 * instance for further processing and distribution. Each LDAS_DeviceMonitor instance
 * monitors one satellite computer for application activity. The DataSockets API used
 * for monitoring is a legacy SNS DAS protocol and was extracted from the ListenerLib
 * code base.
 */
class LDAS_DeviceMonitor
{
public:

    LDAS_DeviceMonitor( LDAS_IDevMonitorMgr &a_reader, PVStreamer &a_streamer, const std::string &a_hostname );
    ~LDAS_DeviceMonitor();

    void        connect();
    void        disconnect();

private:

    void        notifySocketData( NI::CNiDataSocketData &a_data );
    void        reqreplySocketData( NI::CNiDataSocketData &a_data );

    LDAS_IDevMonitorMgr            &m_mgr;
    PVStreamer                     &m_streamer;
    std::string                     m_hostname;
    NI::CNiDataSocket               m_notify_socket;
    NI::CNiDataSocket               m_reqreply_socket;
    boost::thread                  *m_appmon_thread;
    boost::mutex                    m_mutex;
    bool                            m_running;
    std::map<unsigned long,long>    m_app_activity;

    static VOID CALLBACK                    appMonTimer( HWND a_hwnd, UINT a_msg, UINT_PTR a_id, DWORD a_time );
    static bool                             g_init;
    static boost::mutex                     g_mutex;
    static std::set<LDAS_DeviceMonitor*>    g_inst;
};

}}}

#endif
