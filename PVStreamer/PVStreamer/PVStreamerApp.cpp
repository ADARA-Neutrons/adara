// PVStreamer.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "Resource.h"
#include "PVStreamerApp.h"
#include "PVStreamerDlg.h"

#include "PVStreamer.h"
#include "PVStreamLogger.h"
#include "LegacyDAS.h"
#include "LDAS_PVConfigMgr.h"
//#include "LDAS_PVConfigAgent.h"
#include "LDAS_PVReader.h"
#include "EPICS_PVConfigMgr.h"
#include "EPICS_PVReader.h"
#include "ADARA_PVWriter.h"


using namespace SNS::PVS::EPICS;
using namespace SNS::PVS::LDAS;
using namespace SNS::PVS::ADARA;
using namespace std;

class CmdLinParser : public CCommandLineInfo
{
public:
    CmdLinParser() :
      m_port(31416),
      m_ldas_config_file("Legacy.cfg"),
      m_epics_config_file("EPICS.cfg"),
      m_heartbeat(2000),
      m_broker_port(0),
      m_topic_path("/demo/message")
      {}

    unsigned short  m_port;
    string          m_ldas_config_file;
    string          m_epics_config_file;
    string          m_log_path;
    unsigned long   m_heartbeat;
    string          m_pv_config_local_file;
    string          m_broker_host;
    unsigned short  m_broker_port;
    string          m_broker_user;
    string          m_broker_pass;
    string          m_topic_path;
    string          m_epics_prefix;

    void ParseParam(const TCHAR* pszParam,BOOL bFlag,BOOL bLast)
    {
        if ( bFlag )
        {
            if ( _strnicmp( pszParam, "port=",5) == 0 )             // Port for SMS to read from
                m_port = (unsigned short)atoi( &pszParam[5] );
            else if ( _strnicmp( pszParam, "legacy=",7) == 0 )      // Path to LDAS configuration file
                m_ldas_config_file = &pszParam[7];
            else if ( _strnicmp( pszParam, "epics=",6) == 0 )       // Path to EPICS configuration file
                m_epics_config_file = &pszParam[6];
            else if ( _strnicmp( pszParam, "log=",4) == 0 )         // Path to log files
                m_log_path = &pszParam[4];
            else if ( _strnicmp( pszParam, "hb=",3) == 0 )          // ADARA heartbeat period
                m_heartbeat = atoi( &pszParam[3] );
            //else if ( _strnicmp( pszParam, "local=",6) == 0 )       // Optional PV config file
            //    m_pv_config_local_file = &pszParam[6];
            else if ( _strnicmp( pszParam, "broker_host=",12) == 0 )// AMQ broker hostname
                m_broker_host = &pszParam[12];
            else if ( _strnicmp( pszParam, "broker_port=",12) == 0 )// AMQ broker port
                m_broker_port = (unsigned short)atoi( &pszParam[12] );
            else if ( _strnicmp( pszParam, "broker_user=",12) == 0 )// AMQ broker user name
                m_broker_user = &pszParam[12];
            else if ( _strnicmp( pszParam, "broker_pass=",12) == 0 )// AMQ broker password
                m_broker_pass = &pszParam[12];
            else if ( _strnicmp( pszParam, "topic_path=",11) == 0 ) // AMQ beam-line specific topic prefix (i.e. SNS.SEQ)
                m_topic_path = &pszParam[11];
            else if ( _strnicmp( pszParam, "epics_pre=",10) == 0 )  // EPICS beam-line specific prefix
                m_epics_prefix = &pszParam[10];
        }
    }
};

BEGIN_MESSAGE_MAP(CPVStreamerApp, CWinApp)
    ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

CPVStreamerApp::CPVStreamerApp()
{
}


// The one and only CPVStreamerApp object

CPVStreamerApp theApp;


BOOL
CPVStreamerApp::InitApplication()
{
    BOOL ret = CWinApp::InitApplication();

    // Prevent multiple instances from running on one machine
    if ( CreateSemaphore( 0, 0, 1, "PVStreamerSemaphore" ) != 0 )
    {
        if ( GetLastError() == ERROR_ALREADY_EXISTS )
        {
            ret = false;
            CWnd *pWndPrev, *pWndChild;

            // Determine if a window with the class name exists...
            if (( pWndPrev = CWnd::FindWindow(0,"ADARA Process Variable Streamer")) != 0 )
            {
                // If so, does it have any popups?
                pWndChild = pWndPrev->GetLastActivePopup();

                // If iconic, restore the main window
                if (pWndPrev->IsIconic())
                    pWndPrev->ShowWindow(SW_RESTORE);

                // Bring the main window or its popup to the foreground
                pWndChild->SetForegroundWindow();

                // and you are done activating the other application
                return FALSE;
            }
        }
    }

    return ret;
}

BOOL
CPVStreamerApp::InitInstance()
{
    CWinApp::InitInstance();

    CmdLinParser    cmdline;
    ParseCommandLine(cmdline);

    if (!AfxSocketInit())
    {
        AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
        return FALSE;
    }

    // Create main Window of the app (a dialog)
    CPVStreamerDlg dlg( cmdline.m_topic_path, cmdline.m_broker_host, cmdline.m_broker_port, cmdline.m_broker_user, cmdline.m_broker_pass );
    m_pMainWnd = &dlg;
    dlg.Create(IDD_PVSTREAMER_DIALOG,0);

    dlg.print( "PVStreamer starting..." );

    stringstream   sstr;
    sstr << "  legacy config = " << cmdline.m_ldas_config_file << " (use -legacy=x to change)";
    dlg.print( sstr.str());

    stringstream   sstr;
    sstr << "  epics config = " << cmdline.m_epics_config_file << " (use -epics=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  port no. = " << cmdline.m_port << " (use -port=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  log path = " << cmdline.m_log_path << " (use -log=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  heartbeat = " << cmdline.m_heartbeat << " msec (use -hb=x to change)";
    dlg.print( sstr.str());

    //sstr.str("");
    //sstr << "  local pv file = " << (cmdline.m_pv_config_local_file.size()?cmdline.m_pv_config_local_file: "n/a") << " (use -local=x to change)";
    //dlg.print( sstr.str());

    sstr.str("");
    sstr << "  broker host = " << cmdline.m_broker_host;
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  broker port = " << cmdline.m_broker_port;
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  broker user = " << cmdline.m_broker_user;
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  EPICS prefix = " << cmdline.m_epics_prefix << " (use -epics_pre=x to change)";;
    dlg.print( sstr.str());

    PVStreamer      *pvs = 0;
    PVStreamLogger  *logger = 0;

    try
    {
        // Initialize PVStreamer objects
        // The following code inits legacy DAS readers and an ADARA writer. If new input protocols
        // are added, the appropriate readers and configuration monitors would be added here. Any
        // number of input protocols as supported, but only one output protocol can be set.

        // Create the top-level PVStreamer object (not a singleton, but no use case for more than one)
        pvs = new PVStreamer(500,260);

        // Attach the Windows GUI dialog as a listener (it displays status and logs)
        pvs->attachConfigListener( dlg );
        pvs->attachStreamListener( dlg );
        pvs->attachStatusListener( dlg );

        // Create a logger object and attach to PVStreamer feeds
        logger = new PVStreamLogger(cmdline.m_log_path);
        pvs->attachConfigListener( *logger );
        pvs->attachStreamListener( *logger );

        // Create an ADARA PV Writer and attach to PVStreamer object
        ADARA_PVWriter*     adara_writer = new ADARA_PVWriter(*pvs,cmdline.m_port,cmdline.m_heartbeat);
        adara_writer->attachListener( dlg );
        adara_writer->attachListener( *logger );

        //TODO The local PV config file functionality has never been used; however, it
        // might be needed in the near future. The current approach only works for LEGACY
        // pvs, but may need to be expanded to epics - so it will need to be moved/refactored

        // Load the list of disabled process variables, if specified
        //if ( cmdline.m_pv_config_local_file.size())
        //    LDAS_PVConfigAgent::loadPVConfigLocal( cmdline.m_pv_config_local_file );
        //else
        //    LDAS_PVConfigAgent::loadPVConfigLocal( "pvlocal.cfg", true );



        // Create a legacy DAS configuration reader (listens to satellite app notifications)
        // This will initiaite communication with legacy DAS applications
        LDAS_PVConfigMgr*   ldas_cfg = new LDAS_PVConfigMgr( *pvs, cmdline.m_ldas_config_file );

        // Create an EPICS DAS configuration reader
        EPICS_PVConfigMgr*  epics_cfg = new EPICS_PVConfigMgr( *pvs, cmdline.m_ldas_config_file );


        // Show and run main window (this runs message loop, doesn't return until window closes)
        dlg.ShowWindow(SW_SHOW);
        MSG msg;
        while( GetMessage( &msg, 0, 0, 0 ) > 0 )
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        delete pvs;
        delete logger;
    }
    catch( TraceException &e )
    {
        AfxMessageBox( e.toString().c_str(), MB_OK | MB_ICONEXCLAMATION, 0 );

        if ( pvs )
            delete pvs;

        if ( logger )
            delete logger;
    }

    // Return false to prevent CWinApp::Run() from being called
    return FALSE;
}
