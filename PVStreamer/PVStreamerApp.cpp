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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


using namespace SNS::PVS::LDAS;
using namespace SNS::PVS::ADARA;
using namespace std;

class CmdLinParser : public CCommandLineInfo
{
public:
    CmdLinParser() : m_port(31416),m_sat_config_file("c:/SatelliteComputer.xml"), m_log_file("c:/pvslog.txt") {}
    
    unsigned short  m_port;
    string          m_sat_config_file;
    string          m_log_file;

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
    // TODO How do we NOT have a window?
    CPVStreamerDlg dlg;
    m_pMainWnd = &dlg;

    dlg.Create(IDD_PVSTREAMER_DIALOG,0);

    // MFC provides command-line arguments concatenatied into a single string
    // Bust this string back into an array of strings so we can parse them

    // Create PVStreamer facilities and start them

    // Init Legacy DAS classes

    PVStreamer pvs;

    pvs.attachConfigListener( dlg );
    pvs.attachStreamListener( dlg );

    PVStreamLogger      logger(cmdline.m_log_file);
    pvs.attachConfigListener( logger );
    pvs.attachStreamListener( logger );

    ADARA_PVWriter      adara_writer(pvs,cmdline.m_port);
    adara_writer.attachListener( dlg );

    LDAS_PVReader       ldas_reader(pvs);
    LDAS_PVConfigMgr    ldas_cfg(pvs);


    ldas_cfg.connectSatCompFile(cmdline.m_sat_config_file);

    // Show and run main window (this runs message loop, doesn't return until window closes)
    //dlg.DoModal();

    dlg.ShowWindow(SW_SHOW);
    MSG msg;
    while( GetMessage( &msg, 0, 0, 0 ) > 0 )
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    // Return false to prevent CWinApp::Run() from being called
    return FALSE;
}
