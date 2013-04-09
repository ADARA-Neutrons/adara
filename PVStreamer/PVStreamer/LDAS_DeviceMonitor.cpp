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

namespace SNS { namespace PVS { namespace LDAS {

/**
 * \brief Constructor for LDAS_DeviceMonitor class.
 * \param a_mgr - The owning LDAS_IDevMonitorMgr instance.
 * \param a_streamer - The associate PVStreamer instance.
 * \param a_hostname - The name of the system that hosts devices to be monitored.
 */
LDAS_DeviceMonitor::LDAS_DeviceMonitor( LDAS_IDevMonitorMgr &a_mgr, PVStreamer &a_streamer, const std::string &a_hostname )
: m_mgr(a_mgr), m_streamer(a_streamer), m_hostname(a_hostname)
{
    // Install DataSocket event handlers
    m_notify_socket.InstallEventHandler( *this, &LDAS_DeviceMonitor::notifySocketData );
}

/**
 * \brief Destructor for LDAS_DeviceMonitor class.
 */
LDAS_DeviceMonitor::~LDAS_DeviceMonitor()
{
}


void
LDAS_DeviceMonitor::connect()
{
    // Connect to the "notifymsg" variable on the specified host
    string uri = string("dstp://") + m_hostname + "/notifymsg";
    m_notify_socket.Connect( uri.c_str(), CNiDataSocket::ReadAutoUpdate );
}


/**
 * \brief This is the callback method that receives data from the notify DataSocket.
 * \param a_data - The NI DataSocket data packet.
 */
void
LDAS_DeviceMonitor::notifySocketData( CNiDataSocketData &a_data )
{
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
            
            if ( msg_type == 200 || msg_type == 201 )
            {
                // Get device ID
                int app_id = atoi(eStruct.csValue);

                if ( app_id > 0 )
                {
                    if ( m_streamer.isAppDefined( app_id ))
                    {
                        // Get list of devices owned by this application
                        const vector<Identifier> &devices = m_streamer.getAppDevices( app_id );
                        for ( vector<Identifier>::const_iterator d = devices.begin(); d != devices.end(); ++d )
                        {
                            if ( msg_type == 200 )
                                m_mgr.deviceActive( *d, time );
                            else
                                m_mgr.deviceInactive( *d, time );
                        }
                    }
                    else
                    {
                        LOG_WARNING( "Received activation notice for unknown application ID: " << app_id );

                        // This REALLY needs to be put in the GUI log window
                        ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** Received activation notice from UNKNOWN APPLICATION *****" );
                        ((CPVStreamerDlg*)theApp.m_pMainWnd)->print( "***** PVStreamer must be restarted when configuration is changed *****" );
                    }
                }
            }
        }
    }
}


}}}
