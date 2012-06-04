#include "stdafx.h"
#include "PVStreamer.h"
#include "LDAS_DeviceMonitor.h"
#include "LDAS_PVReader.h"
#include "LDAS_XmlParser.h"

using namespace std;
using namespace NI;

namespace SNS { namespace PVS { namespace LDAS {



LDAS_DeviceMonitor::LDAS_DeviceMonitor( LDAS_IDevMonitorMgr &a_mgr, PVStreamer &a_streamer, const std::string &a_hostname )
: m_mgr(a_mgr), m_streamer(a_streamer), m_hostname(a_hostname)
{
    m_notify_socket.InstallEventHandler( *this, &LDAS_DeviceMonitor::notifySocketData );

    string uri = string("dstp://") + m_hostname + "/notifymsg";
    m_notify_socket.Connect( uri.c_str(), CNiDataSocket::ReadAutoUpdate );
}

LDAS_DeviceMonitor::~LDAS_DeviceMonitor()
{
}


void
LDAS_DeviceMonitor::notifySocketData( CNiDataSocketData &a_data )
{
    /*
    This code is a hack to access only app started and app stopped notification messages.
    Any changes made to the DAS datasocket notify protocol will probably break this code.
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
            }
        }
    }


}

/*
<localhost>
<Notify Type="200" TimeStamp="1337807790" FromProcessID="1580" ToProcessID="0">
1031
</Notify>
</localhost>

<localhost>
<Notify Type="201" TimeStamp="1337814870" FromProcessID="1580" ToProcessID="0">
1031
</Notify>
</localhost>
*/





}}}
