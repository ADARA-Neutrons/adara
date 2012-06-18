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
#include "LDAS_PVReader.h"
#include "ADARA_PVWriter.h"


using namespace SNS::PVS::LDAS;
using namespace SNS::PVS::ADARA;
using namespace std;

class CmdLinParser : public CCommandLineInfo
{
public:
    CmdLinParser() : m_port(31416),m_sat_config_file("c:/SatelliteComputer.xml"), m_log_file("c:/pvslog.txt"), m_heartbeat(2000) {}
    
    unsigned short  m_port;
    string          m_sat_config_file;
    string          m_log_file;
    unsigned long   m_heartbeat;

    void ParseParam(const TCHAR* pszParam,BOOL bFlag,BOOL bLast)
    {
        if ( bFlag )
        {
            if ( _strnicmp( pszParam, "port=",5) == 0 )
                m_port = atoi( &pszParam[5] );
            else if ( _strnicmp( pszParam, "cfg=",4) == 0 )
                m_sat_config_file = &pszParam[4];
            else if ( _strnicmp( pszParam, "log=",4) == 0 )
                m_log_file = &pszParam[4];
            else if ( _strnicmp( pszParam, "hb=",3) == 0 )
                m_heartbeat = atoi( &pszParam[3] );
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
            if (pWndPrev = CWnd::FindWindow(0,"SNS-SMS Process Variable Streamer"))
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
    
    // Create main window
    CPVStreamerDlg dlg;
    m_pMainWnd = &dlg;
    dlg.Create(IDD_PVSTREAMER_DIALOG,0);

    dlg.print( "PVStreamer starting." );

    stringstream   sstr;
    sstr << "  config file = " << cmdline.m_sat_config_file << " (use -cfg=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  port no. = " << cmdline.m_port << " (use -port=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  log file = " << cmdline.m_log_file << " (use -log=x to change)";
    dlg.print( sstr.str());

    sstr.str("");
    sstr << "  heartbeat = " << cmdline.m_heartbeat << " msec (use -hb=x to change)";
    dlg.print( sstr.str());

    // Initialize PVStreamer objects
    PVStreamer* pvs = new PVStreamer(200,100);

    pvs->attachConfigListener( dlg );
    pvs->attachStreamListener( dlg );

    PVStreamLogger*     logger = new PVStreamLogger(cmdline.m_log_file);
    pvs->attachConfigListener( *logger );
    pvs->attachStreamListener( *logger );

    ADARA_PVWriter*     adara_writer = new ADARA_PVWriter(*pvs,cmdline.m_port,cmdline.m_heartbeat);
    adara_writer->attachListener( dlg );
    adara_writer->attachListener( *logger );

    LDAS_PVReader*      ldas_reader = new LDAS_PVReader(*pvs);
    LDAS_PVConfigMgr    ldas_cfg(*pvs);

    // Loading a LDAS satellite configuration file will initiate internal streaming
    // External streaming will commence when the SMS client connects to the ADARA port
    ldas_cfg.connectSatCompFile(cmdline.m_sat_config_file);

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

    // Return false to prevent CWinApp::Run() from being called
    return FALSE;
}
