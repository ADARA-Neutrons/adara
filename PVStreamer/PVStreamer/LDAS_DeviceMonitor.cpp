/**
 * \file LDAS_DeviceMonitor.cpp
 * \brief Source file for LDAS_DeviceMonitor class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"
#include "PVStreamer.h"
#include "LDAS_DeviceMonitor.h"
#include "LDAS_PVReader.h"
#include "LDAS_XmlParser.h"

#include "PVStreamerApp.h"
#include "PVStreamerDlg.h"

using namespace std;
using namespace NI;

#define MAX_APP_QUIET_TIME 120

namespace SNS { namespace PVS { namespace LDAS {

bool                        LDAS_DeviceMonitor::g_init = true;
set<LDAS_DeviceMonitor*>    LDAS_DeviceMonitor::g_inst;
boost::mutex                LDAS_DeviceMonitor::g_mutex;

/**
 * \brief Constructor for LDAS_DeviceMonitor class.
 * \param a_mgr - The owning LDAS_IDevMonitorMgr instance.
 * \param a_streamer - The associate PVStreamer instance.
 * \param a_hostname - The name of the system that hosts devices to be monitored.
 */
LDAS_DeviceMonitor::LDAS_DeviceMonitor( LDAS_IDevMonitorMgr &a_mgr, PVStreamer &a_streamer, const std::string &a_hostname )
: m_mgr(a_mgr), m_streamer(a_streamer), m_hostname(a_hostname), m_appmon_thread(0), m_running(true)
{
    // Install DataSocket event handlers
    m_notify_socket.InstallEventHandler( *this, &LDAS_DeviceMonitor::notifySocketData );
    m_reqreply_socket.InstallEventHandler( *this, &LDAS_DeviceMonitor::reqreplySocketData );

    // Start app monitoring thread
    //m_appmon_thread = new boost::thread( boost::bind(&LDAS_DeviceMonitor::appMonThread, this));

    boost::lock_guard<boost::mutex> lock(g_mutex);

    if ( g_init )
    {
        SetTimer( 0, 0, 1000, appMonTimer );
        g_init = false;
    }

    g_inst.insert(this);
}

/**
 * \brief Destructor for LDAS_DeviceMonitor class.
 */
LDAS_DeviceMonitor::~LDAS_DeviceMonitor()
{
    boost::lock_guard<boost::mutex> lock(g_mutex);

    g_inst.erase( this );

    //m_running = false;
    disconnect();
    //m_appmon_thread->join();
    //delete m_appmon_thread;
}


void
LDAS_DeviceMonitor::connect()
{
    // Connect to the "notifymsg" variable on the specified host
    string uri = string("dstp://") + m_hostname + "/notifymsg";
    m_notify_socket.Connect( uri.c_str(), CNiDataSocket::ReadAutoUpdate );

    // Connect to the "reqreply" variable on the specified host
    uri = string("dstp://") + m_hostname + "/requestreply";
    m_reqreply_socket.Connect( uri.c_str(), CNiDataSocket::ReadAutoUpdate );
}


void
LDAS_DeviceMonitor::disconnect()
{
    m_notify_socket.SyncDisconnect(5000);
    m_reqreply_socket.SyncDisconnect(5000);
}


/**
 * \brief This is the callback method that receives data from the notify DataSocket.
 * \param a_data - The NI DataSocket data packet.
 */
void
LDAS_DeviceMonitor::notifySocketData( CNiDataSocketData &a_data )
{
    if ( !m_running )
        return;

    /*
    This code is a quick-fix to access only app started and app stopped
    notification messages. Any changes made to the DAS datasocket notify
    protocol in ListenerLib will need to be reflected here.
    */

    CString         xml = CNiString(a_data);
    CStringParser   myParser;
    CString         csElement1;
	CString         csTemp;

    csTemp = myParser.StripHeader(a_data);  
	csTemp = myParser.StripFirstTag(csTemp);
	csTemp = myParser.StripLastTag(csTemp);

	csElement1=myParser.GetElement(csTemp,1);
	if ( !csElement1.IsEmpty())
	{
	    ELE_STRUCT eStruct = myParser.GetElementStructure(csElement1);

        if ( eStruct.csName == "Notify" )
        {
            int msg_type = 0;
            Timestamp time;

            for ( unsigned int i = 0; i < eStruct.uiNumberOfAttributes; i++ )
            {
	            if (eStruct.sAttribute[i].csName=="Type")
		            msg_type = atoi(eStruct.sAttribute[i].csValue);
	            else if (eStruct.sAttribute[i].csName=="TimeStamp")
		            time.sec = atoi(eStruct.sAttribute[i].csValue);
            }

            // ListnerLib message types of interest:
            // 201 : App Stopped

            if ( msg_type == 201 )
            {
                // Get device ID
                int app_id = atoi(eStruct.csValue);

                if ( app_id > 0 )
                {
                    if ( m_streamer.isAppDefined( app_id ))
                    {
                        if ( m_streamer.isAppActive( app_id ))
                        {
                            boost::lock_guard<boost::mutex> lock(m_mutex);

                            m_streamer.appInactive( app_id );
                            m_app_activity[app_id] = -1;

                            // Get list of devices owned by this application
                            const vector<Identifier> &devices = m_streamer.getAppDevices( app_id );
                            for ( vector<Identifier>::const_iterator d = devices.begin(); d != devices.end(); ++d )
                            {
                                m_mgr.deviceInactive( *d, time );
                            }
                        }
                    }
                    else
                    {
                        LOG_WARNING( ">>> DevMon(" << m_hostname << ") - NOTIFY from UNKOWN App ID: " << app_id );

                        // This REALLY needs to be put in the GUI log window
                        ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** Received activation notice from UNKNOWN APPLICATION *****" );
                        ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** PVStreamer must be restarted when configuration is changed *****" );
                    }
                }
            }
        }
        else
        {
            LOG_WARNING( ">>> DevMon(" << m_hostname << ") - unknown msg element: " << eStruct.csName );
        }
    }
}

/**
 * \brief This is the callback method that receives data from the request reply (echo) DataSocket.
 * \param a_data - The NI DataSocket data packet.
 */
void
LDAS_DeviceMonitor::reqreplySocketData( CNiDataSocketData &a_data )
{
    if ( !m_running )
        return;

    CString         xml = CNiString(a_data);
    CStringParser   myParser;
    CString         csElement1;
	CString         csTemp;

    csTemp = myParser.StripHeader(a_data);  
	csTemp = myParser.StripFirstTag(csTemp);
	csTemp = myParser.StripLastTag(csTemp);

	csElement1=myParser.GetElement(csTemp,1);
	if ( !csElement1.IsEmpty())
	{
	    ELE_STRUCT eStruct = myParser.GetElementStructure(csElement1);

        if ( eStruct.csName == "Reply" )
        {
            int msg_type = 0;
            Timestamp time;
            Timestamp time_now;

            time_now.sec = (unsigned long)::time(0);

            for ( unsigned int i = 0; i < eStruct.uiNumberOfAttributes; i++ )
	        {
		        if (eStruct.sAttribute[i].csName=="Type")
			        msg_type = atoi(eStruct.sAttribute[i].csValue);
		        else if (eStruct.sAttribute[i].csName=="TimeStamp")
			        time.sec = atoi(eStruct.sAttribute[i].csValue);
	        }

            // 230 : Echo request
            if ( msg_type == 230 )
            {
                int app_id = atoi(eStruct.csValue);

                if ( m_streamer.isAppDefined( app_id ))
                {
                    if ( m_streamer.isAppActive( app_id ))
                    {
                        boost::lock_guard<boost::mutex> lock(m_mutex);

                        m_app_activity[app_id] = 0;
                    }
                    else
                    {
                        boost::lock_guard<boost::mutex> lock(m_mutex);
                        if ( m_app_activity.find( app_id ) == m_app_activity.end())
                        {
                            // ignore stale echos
                            if ( abs( (long)time.sec - (long)time_now.sec ) > 15 )
                            {
                                m_app_activity[app_id] = -1;
                                return;
                            }
                        }

                        // Received an echo from an inactive app - activate it!
                        m_streamer.appActive( app_id );
                        m_app_activity[app_id] = 0;

                        // Get list of devices owned by this application
                        const vector<Identifier> &devices = m_streamer.getAppDevices( app_id );
                        for ( vector<Identifier>::const_iterator d = devices.begin(); d != devices.end(); ++d )
                            m_mgr.deviceActive( *d, time );

                        LOG_INFO( "DevMon(" << m_hostname << ") - app " << app_id << " started from echo" );
                    }
                }
                else
                {
                    LOG_WARNING( ">>> DevMon(" << m_hostname << ") - ECHO from UNKOWN App ID: " << app_id );

                    // This REALLY needs to be put in the GUI log window
                    ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** Received echo repl from UNKNOWN APPLICATION *****" );
                    ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** PVStreamer must be restarted when configuration is changed *****" );
                }
            }
        }
    }
}


/**
 * \brief This is a timer callback that monitors activitiy on all active apps.
 *
 * It was decided (on 8/30/2013) that the echo request/reply traffic between legacy CPA and
 * satellite apps should be used as an additional test for the activity status of a device.
 * This was decided for two reasons: 1) occasionally PVStreamer does not receive app started
 * or stopped messages from the notify datasockey, causing it to think that a device is 
 * inactive when it really isn't, and 2) it was decided by scientists at Hyspec and Sequoia
 * that if a CPA is shutdown, then PVs for that app should stop streaming (currently so long
 * as the satellite app is emitting PVs, they will be streamed). To implement this behavior,
 * the echo messages flowing between CPA and app will be monitored by this thread and if they
 * stop for a specified amount of time, then the app will be assumed to be disconnected.
 *
 * Due to limitations of the DataSockets library, the implementation must use a timer callback
 * such that calls into the datasocket library are on the same thread as the GUI. This function
 * is static and monitors apps for all instances of LDAS_DeviceMonitor.
 */
VOID CALLBACK
LDAS_DeviceMonitor::appMonTimer( HWND a_hwnd, UINT a_msg, UINT_PTR a_id, DWORD a_time )
{
    boost::lock_guard<boost::mutex> lock(g_mutex);

    std::map<unsigned long,long>::iterator ia;
    Timestamp ts;

    for ( set<LDAS_DeviceMonitor*>::iterator inst = g_inst.begin(); inst != g_inst.end(); ++inst )
    {
        boost::lock_guard<boost::mutex> lock((*inst)->m_mutex);

        for ( ia = (*inst)->m_app_activity.begin(); ia != (*inst)->m_app_activity.end(); ++ia )
        {
            // Apps that have stopped have a time of -1; ignore these
            if ( ia->second > -1 )
            {
                // If app has not echoed or updated a variable within MAX_APP_QUIET_TIME, then assume it stopped
                if ( ++ia->second > MAX_APP_QUIET_TIME )
                {
                    LOG_WARNING( "DevMon(" << (*inst)->m_hostname << ") - no echo from app " << ia->first );

                    // Set app as inactive
                    (*inst)->m_streamer.appInactive( ia->first );
                    ia->second = -1;

                    ts.sec = (unsigned long)time(0);

                    // Get list of devices owned by this application and make them inactive
                    const vector<Identifier> &devices = (*inst)->m_streamer.getAppDevices( ia->first );
                    for ( vector<Identifier>::const_iterator d = devices.begin(); d != devices.end(); ++d )
                        (*inst)->m_mgr.deviceInactive( *d, ts );
                }
            }
        }
    }
}

}}}
