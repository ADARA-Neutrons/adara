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

#define VERSION "1.2.1"

using namespace std;

#define MAX_LOG_SIZE 500

CPVStreamerDlg::CPVStreamerDlg
(
    const std::string &a_topic_path,
    const std::string &a_broker_uri,
    unsigned short a_broker_port,
    const std::string &a_broker_user,
    const std::string &a_broker_pass
):
    CDialog(CPVStreamerDlg::IDD, 0),
    m_update_log(false),
    m_topic_path(a_topic_path),
    m_broker_uri(a_broker_uri),
    m_broker_port(a_broker_port),
    m_broker_user(a_broker_user),
    m_broker_pass(a_broker_pass),
    m_combus(0),
    m_running(true)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDI_MAINICON);
    m_start_time = (unsigned long)time(0);

    // If ComBusLite ctor fails, no exception will be thrown, just wont work
	if ( !a_broker_uri.empty() && !a_topic_path.empty() )
	{
        // Create a thread to emit ComBus status
        m_status_thread = new boost::thread( boost::bind(&CPVStreamerDlg::statusThread, this));
	}
}


CPVStreamerDlg::~CPVStreamerDlg()
{
    m_running = false;

    if ( m_combus )
    {
        m_status_thread->join();

        delete m_status_thread;
    }
}


void CPVStreamerDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LOG_EDIT, m_log_edit);
    DDX_Control(pDX, IDC_STATUS_EDIT, m_status_edit);
    DDX_Control(pDX, IDC_VERSION_EDIT, m_version_edit);
}

BEGIN_MESSAGE_MAP(CPVStreamerDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
    ON_WM_TIMER()
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
    stringstream tmp;
    tmp << "Version " << VERSION;
    m_version_edit.SetWindowText( tmp.str().c_str() );

    updateLogText();
    m_update_log = false;
    m_log_edit.SetWindowText( m_log_text.c_str() );
    m_log_edit.LineScroll( MAX_LOG_SIZE, 0 );

    SetTimer( 1, 1000, 0 );

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
CPVStreamerDlg::OnTimer( UINT a_timer_id )
{
    if ( a_timer_id == 1 && m_update_log )
    {
        // Timer ID 1 is triggered once per second, used to updated log window
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_log_edit.SetRedraw( false );
        int visline = m_log_edit.GetFirstVisibleLine();
        m_log_edit.SetWindowText( m_log_text.c_str() );
        m_log_edit.LineScroll( visline );
        m_log_edit.SetRedraw( true );
        m_log_edit.Invalidate();
        m_log_edit.UpdateWindow();

        m_update_log = false;
    }
}

void
CPVStreamerDlg::statusThread()
{
    unsigned long count = 0;
    time_t error = 0;
    time_t now;

    m_combus = new ComBusLite( m_topic_path, "PVSTREAMER", 0, m_broker_uri, m_broker_port, m_broker_user, m_broker_pass );

    while( m_running )
    {
		if ( m_combus && !( count % 4 ))
        {
			if ( m_combus->sendStatus( STATUS_OK ))
            {
                if ( ! m_running )
                    break;

                now = time(0);

                if ( error == 0 )
                {
                    // Print a message in log window (first error)
                    print( "Warning! ComBus::sendStatus() failed. Check broker settings." );
                    LOG_WARNING( "ComBus::sendStatus() failed." );

                    error = now + 30; // Mask send errors for some time
                }
                else if ( error <= now )
                {
                    // Subsequent errors (max one per 30 seconds)
                    print( "Warning! ComBus::sendStatus() failed." );
                    LOG_WARNING( "ComBus::sendStatus() failed." );

                    error = now + 30; // Mask send errors for some time
                }
            }
        }

        Sleep( 1000 );
        ++count;
    }

    delete m_combus;
    m_combus = 0;
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

	struct tm loctime;
	localtime_s( &loctime, &t );
    strftime(buf,50,"%Y.%m.%d %H:%M.%S ", &loctime);

    return string(buf);
}

void
CPVStreamerDlg::addLogEntry( Timestamp *ts, string &entry )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_log_entries.size() >= MAX_LOG_SIZE )
        m_log_entries.pop_front();

    m_log_entries.push_back( timeString(ts) + " - " + entry );

    updateLogText();
}


void
CPVStreamerDlg::print( const std::string & a_msg )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_log_entries.size() >= MAX_LOG_SIZE )
        m_log_entries.pop_front();

    m_log_entries.push_back( a_msg );

    updateLogText();
}


void
CPVStreamerDlg::updateLogText()
{
    m_log_text.clear();

    // Back-fill with empty lines
    for ( size_t i = 0; i < (MAX_LOG_SIZE - m_log_entries.size()); ++i )
        m_log_text += "\r\n";

    for ( list<string>::iterator e = m_log_entries.begin(); e != m_log_entries.end(); ++e )
        m_log_text += *e + "\r\n";

    m_update_log = true;
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
        "Are you sure you want to close this application?", "ADARA Process Variable Streamer", MB_YESNO | MB_ICONWARNING ) == IDYES )
    {
        CDialog::OnCancel();
        PostQuitMessage(0);
    }
}
