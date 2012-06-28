// PVStreamerDlg.cpp : implementation file
//

#include "stdafx.h"

#include <sstream>

#include "Resource.h"
#include "PVStreamer.h"
#include "PVStreamerDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace std;

// CPVStreamerDlg dialog


CPVStreamerDlg::CPVStreamerDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CPVStreamerDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_start_time = (unsigned long)time(0);
}

void CPVStreamerDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LOG_EDIT, m_log_edit);
    DDX_Control(pDX, IDC_STATUS_EDIT, m_status_edit);
}

BEGIN_MESSAGE_MAP(CPVStreamerDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
//    ON_BN_CLICKED(IDOK, &CPVStreamerDlg::OnBnClickedOk)
//ON_EN_CHANGE(IDC_LOG_EDIT, &CPVStreamerDlg::OnEnChangeLogEdit)
//ON_BN_CLICKED(IDOK, &CPVStreamerDlg::OnBnClickedOk)
END_MESSAGE_MAP()


// CPVStreamerDlg message handlers

BOOL CPVStreamerDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // Set the icon for this dialog.  The framework does this automatically
    //  when the application's main window is not a dialog
    SetIcon(m_hIcon, TRUE);			// Set big icon
    SetIcon(m_hIcon, FALSE);		// Set small icon

    m_status_edit.SetWindowText( "ADARA: Not listening" );

    return TRUE;  // return TRUE  unless you set the focus to a control
}

void CPVStreamerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    CDialog::OnSysCommand(nID, lParam);
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CPVStreamerDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // Center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // Draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialog::OnPaint();
    }
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CPVStreamerDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void
CPVStreamerDlg::print( const std::string & a_msg )
{
    if ( m_log_entries.size() > 500 )
        m_log_entries.pop_front();

    m_log_entries.push_back( a_msg );
    m_log_text.clear();

    for ( list<string>::iterator e = m_log_entries.begin(); e != m_log_entries.end(); ++e )
    {
        m_log_text += *e + "\r\n";
    }

    int visline = m_log_edit.GetFirstVisibleLine();
    m_log_edit.SetWindowText( m_log_text.c_str() );
    if ( m_log_entries.size() > 30 ) // Estimate of how many lines fit on screen
        m_log_edit.LineScroll((visline + 1));
}

void
CPVStreamerDlg::listening( const std::string & a_address, unsigned short a_port )
{
    stringstream   sstr;
    sstr << "ADARA: Listening on " << a_address << ":" << a_port;
    m_status_edit.SetWindowText( sstr.str().c_str() );
}

void
CPVStreamerDlg::connected( string &a_address )
{
    stringstream   sstr;
    sstr << "ADARA: Client connected from " << a_address;
    m_status_edit.SetWindowText( sstr.str().c_str() );
}

void
CPVStreamerDlg::disconnected( string &a_address )
{
    stringstream   sstr;
    sstr << "ADARA: Client disconnected from " << a_address;
    m_status_edit.SetWindowText( sstr.str().c_str() );
}

void
CPVStreamerDlg::configurationLoaded( Protocol a_protocol, const std::string &a_source )
{
    stringstream   sstr;
    sstr << "Loaded Config: source = " << a_source << ", protocol = " << a_protocol;
    addLogEntry( 0, sstr.str() );
}

void
CPVStreamerDlg::configurationInvalid( Protocol a_protocol, const std::string &a_source )
{
    stringstream   sstr;
    sstr << "ERROR: Invalid Configuration. source = " << a_source << ", protocol = " << a_protocol;
    addLogEntry( 0, sstr.str() );
}

void
CPVStreamerDlg::deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    stringstream   sstr;
    sstr << "Device " << a_name << " (ID " << a_dev_id << ") ACTIVE";
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    stringstream   sstr;
    sstr << "Device " << a_name << " (ID " << a_dev_id << ") INACTIVE";
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvActive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name <<  " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") ACTIVE";
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvInactive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") INACTIVE";
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms);
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = ";
    if ( a_enum )
        sstr << a_enum->getName(a_value) << " ";
    sstr << "{"<< a_value << "}" << alarmText(a_pv_info.m_alarms);
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms);
    addLogEntry( &a_time, sstr.str() );
}

void
CPVStreamerDlg::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value )
{
    stringstream   sstr;
    sstr << "PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms);
    addLogEntry( &a_time, sstr.str() );
}

string
CPVStreamerDlg::timeString( Timestamp *ts ) const
{
    char buf[25];
    time_t t;

    if (ts)
        t = ts->sec;
    else
        t = time(0);

    strftime(buf,50,"%Y.%m.%d %H:%M.%S ", localtime(&t));
    return string(buf);
}

void
CPVStreamerDlg::addLogEntry( Timestamp *ts, string &entry )
{
    if ( m_log_entries.size() > 500 )
        m_log_entries.pop_front();

    //char ts[20];
    //sprintf_s( ts, "[%06i] ", 20, sec - m_start_time );
    //m_log_entries.push_back( string(ts) + entry );

    m_log_entries.push_back( timeString(ts) + entry );
    m_log_text.clear();

    for ( list<string>::iterator e = m_log_entries.begin(); e != m_log_entries.end(); ++e )
    {
        m_log_text += *e + "\r\n";
    }

    int visline = m_log_edit.GetFirstVisibleLine();
    m_log_edit.SetWindowText( m_log_text.c_str() );
    if ( m_log_entries.size() > 30 ) // Estimate of how many lines fit on screen
        m_log_edit.LineScroll((visline + 1));
}

void
CPVStreamerDlg::unhandledException( const TraceException &e )
{
    AfxMessageBox( e.toString().c_str(), MB_OK | MB_ICONEXCLAMATION, 0 );
    PostQuitMessage(0);
}

void
CPVStreamerDlg::OnCancel()
{
    if ( MessageBox( "PVStreamer is a required component of the SNS beamline data acquisition system.\n"
        "Are you sure you want to close this application?", "SNS Process Variable Streamer", MB_YESNO | MB_ICONWARNING ) == IDYES )
    {
        CDialog::OnCancel();
        PostQuitMessage(0);
    }
}
