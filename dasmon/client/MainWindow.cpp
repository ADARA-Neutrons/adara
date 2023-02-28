#include <stdint.h>

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <iostream>
#include <algorithm>
#include <QMetaObject>
#include <QStringList>
#include <QSettings>
#include <QMessageBox>
#include <QMutexLocker>

#include "ADARA.h"
#include "AMQConfigDialog.h"
#include "RuleConfigDialog.h"
#include "ComBusMessages.h"
#include "DASMonMessages.h"
#include "STCMessages.h"
#include "style.h"

using namespace std;

#define PV_HIGHLIGH_DURATION        30 // poll iterations
#define TIMEOUT_PROC_UNRESPONSIVE   10
#define TIMEOUT_PROC_DEAD           30
#define TIMEOUT_PROC_DEAD_CLEANUP   30
#define TIMEOUT_STC_INACTIVE        30
#define TIMEOUT_STC_UNRESPONSIVE    10
#define MONITOR_ID_INACTIVE_TIMEOUT 60
#define MONITOR_ID_DATA_TIMEOUT     10
#define TOPIC_STATUS                "STATUS.>"
#define TOPIC_SIGNALS               "SIGNAL.>"
#define TOPIC_DASMON                "APP.DASMON"
#define TOPIC_STC                   "APP.STC"


MainWindow::MainWindow( const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_broker_user,
                        const std::string &a_broker_pass, const std::string &a_config_label, bool a_kiosk, bool a_master )
    : QMainWindow(0), ui(new Ui::MainWindow),
    m_init(true), m_kiosk(a_kiosk),
    m_refresh_proc_table(false), m_refresh_signal_table(false),
    m_refresh_log_table(false), m_refresh_trans_table(false), m_refresh_pv_table(false),
    m_recording(false), m_run_number(0), m_prev_run_number(0), m_paused(false), m_scanning(false), m_scan_index(0),
    m_signalled(false), m_highest_level(ADARA::TRACE), m_event_scrollback(5),
    m_combus(0), m_domain( a_domain ), m_broker_uri( a_broker_uri ), m_broker_user( a_broker_user ),
    m_broker_pass( a_broker_pass )
{
    ui->setupUi(this);

    QString m_app_name = "dasmon-gui";
    if ( !a_config_label.empty() )
        m_app_name += "-" + QString::fromStdString(a_config_label);

    QCoreApplication::setOrganizationDomain("ornl.gov");
    QCoreApplication::setOrganizationName("sns");
    QCoreApplication::setApplicationName(m_app_name);

    QSettings settings;

    settings.beginGroup( "ActiveMQ" );
    if ( m_domain.empty() )
        m_domain = settings.value( "domain" ).toString().toStdString();

    if ( m_broker_uri.empty() )
    {
        m_broker_uri = settings.value( "broker_uri" ).toString().toStdString();
        m_broker_user = settings.value( "username" ).toString().toStdString();
        m_broker_pass = settings.value( "password" ).toString().toStdString();
    }
    settings.endGroup();

    m_style_unlit    = TEXT_STYLE_UNLIT;
    m_style_info     = TEXT_STYLE_INFO;
    m_style_good     = TEXT_STYLE_OK;
    m_style_warn     = TEXT_STYLE_WARN;
    m_style_error    = TEXT_STYLE_ERROR;
    m_style_fatal    = TEXT_STYLE_FATAL;
    m_style_disabled = TEXT_STYLE_DISABLED;

    // Make sure domain has a "." as last character
    if ( !m_domain.empty() && *m_domain.rbegin() != '.' )
        m_domain += ".";

    m_combus = new ADARA::ComBus::Connection( m_domain, "DASMON-GUI",
        a_master ? 0 : getpid(),
        m_broker_uri, m_broker_user, m_broker_pass, "", "" );

    updateMainWindowTitle();

    updateComBusStatusIndicator();
    updateDASMonStatusIndicator();
    updateSMSConnStatusIndicator();
    updateRunStatusIndicator();
    updatePauseStatusIndicator();
    updateScanStatusIndicator();
    updateSignalStatusIndicator();

    // This code is required due to a bug in Qt designer
    QStringList headers;
    headers << "Monitor ID" << "Count Rate";
    ui->monitorTable->setHorizontalHeaderLabels( headers );
    ui->monitorTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->monitorTable->horizontalHeader()->show();

    headers.clear();
    headers << "Run No." << "Process" << "Status";
    ui->transTable->setHorizontalHeaderLabels( headers );
    ui->transTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->transTable->horizontalHeader()->show();

    headers.clear();
    headers << "Name" << "Source" << "Level" << "Message";
    ui->alertTable->setHorizontalHeaderLabels( headers );
    ui->alertTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->alertTable->horizontalHeader()->show();

    headers.clear();
    headers << "Process" << "Status";
    ui->procStatusTable->setHorizontalHeaderLabels( headers );
    ui->procStatusTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->procStatusTable->horizontalHeader()->show();

    headers.clear();
    headers << "PV" << "Value" << "Status" << "Timestamp";
    ui->pvTable->setHorizontalHeaderLabels( headers );
    ui->pvTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->pvTable->horizontalHeader()->show();

    initHighlightColors();

    m_combus->attach( *this );   // Listen to ComBus connection status
    m_combus->setInputListener( *this );
    m_combus->attach( *this, TOPIC_STATUS ); // Listen to ADARA process health status (display only)
    m_combus->attach( *this, TOPIC_SIGNALS ); // Listen to ADARA signals
    m_combus->attach( *this, TOPIC_DASMON ); // Listen to dasmon service
    m_combus->attach( *this, TOPIC_STC ); // Listen to (local) STC translation messages

    m_start_time = QDateTime::currentDateTime();

    connect( &m_proc_timer, SIGNAL(timeout()), this, SLOT(onProcTimer()));
    m_proc_timer.start(5200);

    connect( &m_table_timer, SIGNAL(timeout()), this, SLOT(onTableTimer()));
    m_table_timer.start(1000);

    connect( &m_pv_timer, SIGNAL(timeout()), this, SLOT(onPvTimer()));
    m_pv_timer.start(5000);

    emit kioskMode( m_kiosk );
}


MainWindow::~MainWindow()
{
    m_combus->detach( (ITopicListener&)*this );
    m_combus->detach( (IConnectionListener&)*this );

    delete m_combus;
    delete ui;
}


/**
 * @brief Custom event handler for main window
 * @param a_event - Event object
 * @return Returns value from QMainWindow::event() method
 *
 * This method is used to hook into the event stream for the main window. This
 * enables detection of system-wide changes (such as to the system palette)
 * such that the application can adjust as needed.
 */
bool
MainWindow::event( QEvent *a_event )
{
    // Re-init highlight colors on app palette change
    if ( a_event->type() == QEvent::ApplicationPaletteChange )
        initHighlightColors();

    return QMainWindow::event( a_event );
}

/**
 * @brief Initializes highlight-fade colors used in certain tables
 *
 * This method generates a set of foreground and background colors to create a
 * gradual transition from the color-scheme-define highlight and normal colors.
 * These colors are used to gradually fade highlights of new/changed items in
 * certain tables. The algorithm attemps to produce a simple linear
 * interpolation between the two color end-points; however, it also checks for
 * reasonable contrast between the fg/bg colors and will auto-adjust the fg
 * color if the contrast drops too low.
 */
void
MainWindow::initHighlightColors()
{
    m_fg_color.resize(PV_HIGHLIGH_DURATION+1);
    m_bg_color.resize(PV_HIGHLIGH_DURATION+1);

    QPalette palette;
    QColor fg_hl = palette.color( QPalette::HighlightedText );
    QColor bg_hl = palette.color( QPalette::Highlight );
    QColor fg_nm = palette.color( QPalette::Text );
    QColor bg_nm = palette.color( QPalette::Base );

    qreal hl[3], nm[3], d[3];

    bg_hl.getRgbF( &hl[0], &hl[1], &hl[2] );
    bg_nm.getRgbF( &nm[0], &nm[1], &nm[2] );

    for ( int c = 0; c < 3; ++c )
        d[c] = (hl[c] - nm[c])/PV_HIGHLIGH_DURATION;

    int j = PV_HIGHLIGH_DURATION/2;

    for ( int i = 0; i <= PV_HIGHLIGH_DURATION; ++i )
    {
        m_bg_color[i] = QColor::fromRgbF( nm[0] + d[0]*i, nm[1] + d[1]*i, nm[2] + d[2]*i );
        if ( i < j )
            m_fg_color[i] = fg_nm;
        else
            m_fg_color[i] = fg_hl;
    }
}


void
MainWindow::attach( SubClient &a_sub_client )
{
    QMutexLocker lock( &m_mutex );

    if ( ::find( m_sub_clients.begin(), m_sub_clients.end(), &a_sub_client ) == m_sub_clients.end())
    {
        m_sub_clients.push_back( &a_sub_client );

        a_sub_client.dasmonStatus( m_dasmon_state.activeTrue() );
    }
}


void
MainWindow::setComBusActive( bool a_active )
{
    if ( a_active && !m_combus_state.activeTrue() )
    {
        m_combus_state.set( a_active, true );
        updateAllStatusIndicators();

        writeLog( ADARA::INFO, "ComBus connection established." );
    }
    else if ( !a_active && !m_combus_state.activeFalse() )
    {
        m_combus_state.set( a_active, true );
        m_dasmon_state.setActive( false );
        m_sms_state.setActive( false );

        m_recording = false;
        m_run_number = 0;
        m_scanning = false;
        m_paused = false;
        m_scan_index = 0;
        m_signalled = false;

        clearBeamDisplay();
        clearRunDisplay( true );
        clearSignals();

        updateAllStatusIndicators();

        // TODO This is WRONG - need to encapsulate calbacks in one place (setDASMonActive)
        for ( vector<SubClient*>::iterator c = m_sub_clients.begin(); c != m_sub_clients.end(); ++c )
            (*c)->dasmonStatus( false );

        writeLog( ADARA::ERROR, "ComBus connection lost." );
    }
}


void
MainWindow::setDASMonActive( bool a_active )
{
    if ( a_active && !m_dasmon_state.activeTrue() )
    {
        m_dasmon_state.set( a_active, true );
        updateAllStatusIndicators();

        for ( vector<SubClient*>::iterator c = m_sub_clients.begin(); c != m_sub_clients.end(); ++c )
            (*c)->dasmonStatus( a_active );

        writeLog( ADARA::INFO, "DASMON server connection established." );
    }
    else if ( !a_active && !m_dasmon_state.activeFalse() )
    {
        m_dasmon_state.set( a_active, true );
        m_sms_state.setActive( false );

        m_recording = false;
        m_run_number = 0;
        m_scanning = false;
        m_paused = false;
        m_scan_index = 0;
        m_signalled = false;

        clearBeamDisplay();
        clearRunDisplay( true );
        clearSignals();

        updateAllStatusIndicators();

        for ( vector<SubClient*>::iterator c = m_sub_clients.begin(); c != m_sub_clients.end(); ++c )
            (*c)->dasmonStatus( a_active );

        writeLog( ADARA::ERROR, "DASMON server connection lost." );
    }
}


void
MainWindow::setSMSActive( bool a_active )
{
    if ( a_active && !m_sms_state.activeTrue() )
    {
        m_sms_state.set( a_active, true );
        updateAllStatusIndicators();

        writeLog( ADARA::INFO, "SMS connection established." );
    }
    else if ( !a_active && !m_sms_state.activeFalse() )
    {
        m_sms_state.set( a_active, true );

        m_recording = false;
        m_run_number = 0;
        m_scanning = false;
        m_paused = false;
        m_scan_index = 0;

        clearBeamDisplay();
        clearRunDisplay( true );

        updateAllStatusIndicators();

        writeLog( ADARA::ERROR, "SMS connection lost." );
    }
}


void
MainWindow::onProcTimer()
{
    m_combus->status( ADARA::ComBus::STATUS_OK );

    QMutexLocker lock( &m_mutex );
    unsigned long t = time(0);
    bool kill_dasmon = false;

    // Scan for and remove stopped or stalled processes, also check dasmon status
    for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); )
    {
        if ((( p->second.status == ADARA::ComBus::STATUS_INACTIVE ) && ( t > p->second.last_updated + TIMEOUT_PROC_DEAD_CLEANUP )) ||
            ( t > p->second.last_updated + TIMEOUT_PROC_DEAD ))
        {
            if ( p->first == "DASMON_0" )
                kill_dasmon = true;

            writeLog( ADARA::INFO, p->first + " is inactive" );

            m_proc_status.erase( p++ );
            m_refresh_proc_table = true;
        }
        else if ( t > p->second.last_updated + TIMEOUT_PROC_UNRESPONSIVE ) // Unresponsive processes
        {
            if ( p->second.status != ADARA::ComBus::STATUS_UNRESPONSIVE )
            {
                p->second.status = ADARA::ComBus::STATUS_UNRESPONSIVE;
                p->second.label = "Unresponsive";
                p->second.hl_count = PV_HIGHLIGH_DURATION;
                m_refresh_proc_table = true;
                writeLog( ADARA::INFO, p->first + " has become unresponsive" );
            }
            ++p;
        }
        else // Properly running processes
        {
            ++p;
        }
    }

    if ( kill_dasmon )
    {
        if ( m_combus_state.activeTrue())
            setDASMonActive( false );
        else
        {
            m_dasmon_state.setValue( false );
            updateDASMonStatusIndicator();
        }
    }
}


void
MainWindow::onTableTimer()
{
    int row;
    QTableWidgetItem *item[4];
    unsigned long now = time(0);

    if ( m_refresh_log_table )
    {
        QMutexLocker lock( &m_log_mutex );

        m_refresh_log_table = false;

        ui->logTable->setRowCount( m_log_entries.size());
        row = 0;
        for ( deque<QString>::iterator l = m_log_entries.begin();
                l != m_log_entries.end(); ++l, ++row )
        {
            ui->logTable->setItem( row, 0, new QTableWidgetItem( *l ) );
        }
        ui->logTable->scrollToBottom();
    }

    QMutexLocker lock( &m_mutex );

    if ( m_refresh_proc_table )
    {
        m_refresh_proc_table = false;

        ui->procStatusTable->setRowCount( m_proc_status.size());
        row = 0;
        for ( map<string, ProcInfo>::iterator p = m_proc_status.begin();
                p != m_proc_status.end(); ++p, ++row )
        {
            item[0] = new QTableWidgetItem( p->second.name );
            item[1] = new QTableWidgetItem( p->second.label );

            ui->procStatusTable->setItem( row, 0, item[0] );
            ui->procStatusTable->setItem( row, 1, item[1] );

            testSetBkgnd( p->second.hl_count, item, 2 );
        }
    }
    else
    {
        row = 0;
        for ( map<string, ProcInfo>::iterator p = m_proc_status.begin();
                p != m_proc_status.end(); ++p, ++row )
        {
            if ( p->second.hl_count )
            {
                item[0] = ui->procStatusTable->item( row, 0 );
                item[1] = ui->procStatusTable->item( row, 1 );

                testSetBkgnd( p->second.hl_count, item, 2 );
            }
        }
    }

    if ( m_refresh_signal_table )
    {
        m_refresh_signal_table = false;

        ui->alertTable->setRowCount( m_alerts.size());
        int row = 0;
        for ( map<string, AlertInfo>::iterator ia = m_alerts.begin();
                ia != m_alerts.end(); ++ia, ++row )
        {
            item[0] = new QTableWidgetItem( ia->second.name.c_str() );
            item[1] = new QTableWidgetItem( ia->second.source.c_str() );
            item[2] = new QTableWidgetItem(
                ADARA::ComBus::ComBusHelper::toText( ia->second.level ) );
            item[3] = new QTableWidgetItem(ia->second.msg.c_str() );

            ui->alertTable->setItem( row, 0, item[0] );
            ui->alertTable->setItem( row, 1, item[1] );
            ui->alertTable->setItem( row, 2, item[2] );
            ui->alertTable->setItem( row, 3, item[3] );

            testSetBkgnd( ia->second.hl_count, item, 4 );
        }
    }
    else
    {
        row = 0;
        for ( map<string, AlertInfo>::iterator ia = m_alerts.begin();
                ia != m_alerts.end(); ++ia, ++row )
        {
            if ( ia->second.hl_count )
            {
                item[0] = ui->alertTable->item( row, 0 );
                item[1] = ui->alertTable->item( row, 1 );
                item[2] = ui->alertTable->item( row, 2 );
                item[3] = ui->alertTable->item( row, 3 );

                testSetBkgnd( ia->second.hl_count, item, 4 );
            }
        }
    }


    // Update beam monitor display
    if ( ui->monitorTable->rowCount() != (int)m_monitor_rate.size() )
    {
        ui->monitorTable->setRowCount( m_monitor_rate.size());
    }

    row = 0;
    for ( map<uint32_t, MonitorInfo>::iterator im = m_monitor_rate.begin();
            im != m_monitor_rate.end(); )
    {
        if ( ( im->second.last_updated + MONITOR_ID_INACTIVE_TIMEOUT )
                < now ) // After timeout, remove monitor ID entry
        {
            m_monitor_rate.erase( im++ );
            ui->monitorTable->removeRow( row );
        }
        else
        {
            if ( ( im->second.last_updated + MONITOR_ID_DATA_TIMEOUT )
                    < now ) // After no data timeout, zero rate
            {
                im->second.rate = 0;
            }

            ui->monitorTable->setItem( row, 0,
                new QTableWidgetItem( QString("%1").arg( im->first ) ) );
            ui->monitorTable->setItem( row, 1,
                new QTableWidgetItem(
                    QString("%1").arg( im->second.rate ) ) );
            ++row;
            ++im;
        }
    }

    if ( m_refresh_pv_table )
    {
        if ( ui->pvTable->rowCount() != (int)m_pvs.size() )
            ui->pvTable->setRowCount( m_pvs.size() );

        map<string, ADARA::ComBus::DASMON::ProcessVariables::PVData>
            ::const_iterator ipv = m_pvs.begin();
        int i = 0;
        for ( ; ipv != m_pvs.end(); ++ipv, ++i )
        {
            ui->pvTable->setItem( i, 0,
                new QTableWidgetItem(
                    QString("%1").arg( ipv->first.c_str() ) ) );

            std::string array_str;

            switch ( ipv->second.pv_type )
            {
                case ADARA::ComBus::DASMON::PVDT_UINT:
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            ipv->second.uint_val ) ) );
                    break;
                case ADARA::ComBus::DASMON::PVDT_DOUBLE:
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            ipv->second.dbl_val ) ) );
                    break;
                case ADARA::ComBus::DASMON::PVDT_STRING:
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            ipv->second.str_val.c_str() ) ) );
                    break;
                case ADARA::ComBus::DASMON::PVDT_UINT_ARRAY:
                    Utils::printArrayString( ipv->second.uint_array,
                        array_str );
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            array_str.c_str() ) ) );
                    break;
                case ADARA::ComBus::DASMON::PVDT_DOUBLE_ARRAY:
                    Utils::printArrayString( ipv->second.dbl_array,
                        array_str );
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            array_str.c_str() ) ) );
                    break;
                // Unknown Data Type, Dang...
                // - Just Default to Ugly Double... ;-b
                default:
                    ui->pvTable->setItem( i, 1,
                        new QTableWidgetItem( QString("%1").arg(
                            (double) -999.999999 ) ) );
                    break;
            }

            ui->pvTable->setItem( i, 2,
                new QTableWidgetItem( QString("%1").arg(
                    getStatusText(ipv->second.status) ) ) );
            ui->pvTable->setItem( i, 2,
                new QTableWidgetItem( QString("%1").arg(
                    getStatusText(ipv->second.status) ) ) );
            ui->pvTable->setItem( i, 3,
                new QTableWidgetItem( QDateTime::fromTime_t(
                    ipv->second.timestamp).toString() ) );
        }

        m_refresh_pv_table = false;
    }

    // Examine translation status for unresponsive or stale information
    for ( map<unsigned long, TransStatus>::iterator ts
                = m_trans_status.begin();
            ts != m_trans_status.end();  )
    {
        // Maybe leave finished info up for a diff time than unresponsive?
        if ( ts->second.last_updated + TIMEOUT_STC_INACTIVE < now )
        {
            m_trans_status.erase( ts++ );
            m_refresh_trans_table = true;
        }
        else
        {
            if ( ts->second.last_updated + TIMEOUT_STC_UNRESPONSIVE < now )
                m_refresh_trans_table = true;

            ++ts;
        }
    }

    if ( m_refresh_trans_table )
    {
        m_refresh_trans_table = false;

        if ( ui->transTable->rowCount() != (int)m_trans_status.size() )
            ui->transTable->setRowCount( m_trans_status.size() );

        map<unsigned long, TransStatus>::reverse_iterator ts =
            m_trans_status.rbegin();
        row = 0;
        for ( ; ts != m_trans_status.rend(); ++ts, ++row )
        {
            ui->transTable->setItem( row, 0,
                new QTableWidgetItem( QString("%1").arg( ts->first ) ) );
            ui->transTable->setItem( row, 1,
                new QTableWidgetItem( QString("%1 (%2)").arg(
                    ts->second.stc_pid.c_str() ).arg(
                        ts->second.stc_host.c_str() ) ) );

            if ( ts->second.running )
            {
                if ( ts->second.last_updated + TIMEOUT_STC_UNRESPONSIVE
                        < now )
                {
                    ui->transTable->setItem( row, 2,
                        new QTableWidgetItem( "Unresponsive" ) );
                }
                else
                {
                    ui->transTable->setItem( row, 2,
                        new QTableWidgetItem( QString("Running - %1").arg(
                            getStatusText( ts->second.run_status ) ) ) );
                }
            }
            else
            {
                ui->transTable->setItem( row, 2,
                    new QTableWidgetItem( QString("%1").arg(
                        getTransStatusText(
                            ts->second.trans_status ) ) ) );
            }
        }
    }

#if 0
        m_monitor->getStatistics( m_stats );
        ui->statisticsTable->setRowCount( m_stats.size() );
        row = 0;
        for ( map<uint32_t, PktStats>::iterator p = m_stats.begin();
                p != m_stats.end(); ++p, ++row )
        {
            ui->statisticsTable->setItem( row, 0,
                new QTableWidgetItem( QString("0x%1").arg(
                    p->first >> 8, 0, 16 ) ) );
            ui->statisticsTable->setItem( row, 1,
                new QTableWidgetItem( QString("%1").arg(
                    m_monitor->getPktName( p->first ) ) ) );
            ui->statisticsTable->setItem( row, 2,
                new QTableWidgetItem( QString("%1").arg(
                    p->second.count ) ) );
            ui->statisticsTable->setItem( row, 3,
                new QTableWidgetItem( QString("%1").arg(
                    p->second.min_pkt_size ) ) );
            ui->statisticsTable->setItem( row, 4,
                new QTableWidgetItem( QString("%1").arg(
                    p->second.max_pkt_size ) ) );
            ui->statisticsTable->setItem( row, 5,
                new QTableWidgetItem( QString("%1").arg(
                    p->second.total_size ) ) );
        }
#endif
}


void
MainWindow::onPvTimer()
{
    ADARA::ComBus::DASMON::GetProcessVariables cmd;
    m_combus->send( cmd, "DASMON_0" );
}


void
MainWindow::configActiveMQ()
{
    AMQConfigDialog  dlg( this, m_domain, m_broker_uri, m_broker_user, m_broker_pass );

    if ( dlg.exec() == QDialog::Accepted )
    {
        m_domain = dlg.getDomain();
        m_broker_uri = dlg.getBrokerURI();
        m_broker_user = dlg.getUsername();
        m_broker_pass = dlg.getPassword();

        QSettings settings;

        settings.beginGroup( "ActiveMQ" );
        settings.setValue( "domain", m_domain.c_str() );
        settings.setValue( "broker_uri", m_broker_uri.c_str() );
        settings.setValue( "username", m_broker_user.c_str() );
        settings.setValue( "password", m_broker_pass.c_str() );
        settings.endGroup();

        // Reset m_init to send an "emit status" message to dasmond when we connect
        m_init = true;
        ADARA::ComBus::Connection::getInst().setConnection( m_domain, m_broker_uri, m_broker_user, m_broker_pass );

        m_combus->attach( *this, TOPIC_STATUS );
        m_combus->attach( *this, TOPIC_SIGNALS );
        m_combus->attach( *this, TOPIC_DASMON );
        m_combus->attach( *this, TOPIC_STC ); // Listen to (local) STC translation messages

        updateMainWindowTitle();
    }
}

void
MainWindow::configRules()
{
    if ( !m_dasmon_state.activeTrue() )
    {
        QMessageBox::information( this, "DAS Monitor", QString( "Must be connected to DASMON service\nbefore rules can be configured." ));
    }
    else
    {
        RuleConfigDialog  dlg( *this );

        attach( dlg );

        if ( dlg.exec() == QDialog::Accepted )
        {
        }
    }
}


void
MainWindow::about()
{
    string version = DASMON_GUI_VERSION
        + string( " (ADARA Common " ) + ADARA::VERSION
        + string( ", ComBus " ) + ADARA::ComBus::VERSION + string( ")" );
    QMessageBox::about( this, "About DAS Monitor",
        QString( "SNS Data Acquisition System Monitor\nVersion: %1" ).arg(
            version.c_str() ) );
}


// Update the main window title to reflect the latest domain selection
void
MainWindow::updateMainWindowTitle()
{
    // Get the current MainWindow Title string
    QString windowTitle = this->property("windowTitle").toString();

    // Trim off any existing domain string
    int idx = windowTitle.lastIndexOf(" [");
    if ( idx >= 0 )
    {
        windowTitle = windowTitle.left( idx );
    }

    // Add back in any new domain string
    if ( !m_domain.empty() )
    {
        // If domain includes trailing ".", remove it for title display
        if ( m_domain[ m_domain.length() - 1 ] == '.' )
        {
            windowTitle += QString::fromStdString( " ["
                + m_domain.substr( 0, m_domain.length() - 1 ) + "]" );
        }
        else
        {
            windowTitle += QString::fromStdString( " [" + m_domain + "]" );
        }
    }

    // Set the new MainWindow Title
    setWindowTitle(QApplication::translate("MainWindow",
        windowTitle.toLatin1().data(), 0, QApplication::UnicodeUTF8));
}


void
MainWindow::updateAllStatusIndicators()
{
    updateComBusStatusIndicator();
    updateDASMonStatusIndicator();
    updateSMSConnStatusIndicator();
    updateSignalStatusIndicator();
    updateRunStatusIndicator();
    updatePauseStatusIndicator();
    updateScanStatusIndicator();
}

void
MainWindow::updateComBusStatusIndicator()
{
    QString text;
    QString style;

    if ( m_combus_state.value() )
    {
        text = "ComBus";
        style = m_style_good;
    }
    else
    {
        text = "ComBus ?";
        style = m_style_fatal;
    }

    QMetaObject::invokeMethod( ui->combusStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->combusStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::updateDASMonStatusIndicator()
{
    QString text;
    QString style;

    if ( m_dasmon_state.value() )
    {
        text = "DASMON";
        style = m_style_good;
    }
    else
    {
        text = "DASMON ?";
        style = m_style_fatal;
    }

    if ( !m_combus_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->dasmonStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->dasmonStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}

void
MainWindow::updateSMSConnStatusIndicator()
{
    QString text;
    QString style;

    if ( m_sms_state.value() )
    {
        text = "SMS";
        style = m_style_good;
    }
    else
    {
        text = "SMS ?";
        style = m_style_fatal;
    }

    if ( !m_dasmon_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->smsStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->smsStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::updateRunStatusIndicator()
{
    QString text;
    QString style;

    if( m_recording )
    {
        text = "Recording";
        style = m_style_good;

        QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_run_number)));
    }
    else
    {
        text = "Idle";
        style = m_style_unlit;

        if ( m_prev_run_number > 0 )
            QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("(%1)").arg(m_prev_run_number)));
        else
            QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    }

    if ( !m_sms_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->runStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));

    QMetaObject::invokeMethod( ui->runStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::updatePauseStatusIndicator()
{
    QString text;
    QString style;

    if ( m_paused )
    {
        text = "Paused";
        style = m_style_warn;
    }
    else
    {
        text = "------";
        style = m_style_unlit;
    }

    if ( !m_sms_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->pauseStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->pauseStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}



void
MainWindow::updateScanStatusIndicator()
{
    QString text;
    QString style;

    if ( m_scanning )
    {
        text = "Scanning";
        style = m_style_good;

        QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_scan_index)));
    }
    else
    {
        text = "No Scan";
        style = m_style_unlit;

        QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    }

    if ( !m_sms_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->scanStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->scanStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::updateSignalStatusIndicator()
{
    QString text;
    QString style;

    if ( m_signalled )
    {
        switch( m_highest_level )
        {
        case ADARA::FATAL:
            text = "Fatal";
            style = m_style_fatal;
            break;
        case ADARA::ERROR:
            text = "Error";
            style = m_style_error;
            break;
        case ADARA::WARN:
            text = "Warning";
            style = m_style_warn;
            break;
        case ADARA::INFO:
            text = "Info";
            style = m_style_info;
            break;
        case ADARA::DEBUG:
        case ADARA::TRACE:
            text = "Debug";
            style = m_style_unlit;
            break;
        }
    }
    else
    {
        text = "No Signals";
        style = m_style_unlit;
    }

    if ( !m_dasmon_state.activeTrue() )
        style = m_style_disabled;

    QMetaObject::invokeMethod( ui->signalStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->signalStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}



void
MainWindow::updateBeamInfo( const ADARA::DASMON::BeamInfo &a_beam_info )
{
    QMetaObject::invokeMethod( ui->beamIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_beam_info.m_beam_id.c_str()));
    QMetaObject::invokeMethod( ui->beamNameShortEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_beam_info.m_beam_sname.c_str()));
    QMetaObject::invokeMethod( ui->beamNameLongEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_beam_info.m_beam_lname.c_str()));

    // Note: BeamInfo now contains "Target Station Number",
    // m_target_station_number.

    QMetaObject::invokeMethod( ui->facilityNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_beam_info.m_facility.c_str()));
}


void
MainWindow::updateRunInfo( const ADARA::DASMON::RunInfo &a_run_info )
{
    QMetaObject::invokeMethod( ui->propIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_proposal_id.c_str()));
    QMetaObject::invokeMethod( ui->runTitleEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_run_title.c_str()));
    QMetaObject::invokeMethod( ui->sampleIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_sample_id.c_str()));
    QMetaObject::invokeMethod( ui->sampleNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_sample_name.c_str()));
    QMetaObject::invokeMethod( ui->sampleNatureEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_sample_nature.c_str()));
    QMetaObject::invokeMethod( ui->sampleFormulaEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_sample_formula.c_str()));
    QMetaObject::invokeMethod( ui->sampleEnvironmentEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_info.m_sample_environ.c_str()));

    // TODO User info & run num?
    //unsigned long           m_run_num;
    //std::vector<UserInfo>   m_user_info;
}


void
MainWindow::updateBeamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics )
{
    QMetaObject::invokeMethod( ui->countRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,m_locale.toString( a_metrics.m_count_rate )));
    QMetaObject::invokeMethod( ui->pchargeLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_charge )));
    QMetaObject::invokeMethod( ui->pfreqLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_freq )));
    QMetaObject::invokeMethod( ui->bitRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,m_locale.toString( (uint)a_metrics.m_stream_bps )));

    MonitorInfo info;
    info.last_updated = time(0);

    for ( map<uint32_t,double>::const_iterator im = a_metrics.m_monitor_count_rate.begin(); im != a_metrics.m_monitor_count_rate.end(); ++im )
    {
        info.rate = im->second;
        m_monitor_rate[im->first] = info;
    }
}


void
MainWindow::updateRunMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    uint hour = a_metrics.m_time / 3600;
    uint min = ((uint32_t)(a_metrics.m_time) % 3600) / 60;
    uint sec = (uint32_t)(a_metrics.m_time) % 60;

    QMetaObject::invokeMethod( ui->durationLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, QString("%1:%2.%3").arg( hour, 2, 10, QLatin1Char('0') ).arg( min, 2, 10, QLatin1Char('0') ).arg( sec, 2, 10, QLatin1Char('0') ) ));
    QMetaObject::invokeMethod( ui->totalCountsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_total_counts )));
    QMetaObject::invokeMethod( ui->totalChargeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_total_charge )));
    QMetaObject::invokeMethod( ui->pixErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_error_count )));
    QMetaObject::invokeMethod( ui->dupPulseLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_dup_pulse_count )));
    QMetaObject::invokeMethod( ui->mapErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_mapping_error_count )));
    QMetaObject::invokeMethod( ui->pulseVetoLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pulse_veto_count )));
    QMetaObject::invokeMethod( ui->missRTDLLabel, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_missing_rtdl_count )));
    QMetaObject::invokeMethod( ui->pulsePchgUncorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_pcharge_uncorrected )));
    QMetaObject::invokeMethod( ui->gotMetadataEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_got_metadata_count )));
    QMetaObject::invokeMethod( ui->gotNeutronsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_got_neutrons_count )));
    QMetaObject::invokeMethod( ui->hasStatesEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_has_states_count )));
    QMetaObject::invokeMethod( ui->totalPulsesEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_total_pulses_count )));
}


void
MainWindow::updateStreamMetrics( const ADARA::DASMON::StreamMetrics &a_metrics )
{
    QMetaObject::invokeMethod( ui->invPktEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_invalid_pkt )));
    QMetaObject::invokeMethod( ui->invPktTypeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_invalid_pkt_type )));
    QMetaObject::invokeMethod( ui->invPktTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_invalid_pkt_time )));
    QMetaObject::invokeMethod( ui->dupEventPktEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_duplicate_packet )));
    QMetaObject::invokeMethod( ui->freqTolEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pulse_freq_tol )));
    QMetaObject::invokeMethod( ui->cycleErrEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_cycle_err )));
    QMetaObject::invokeMethod( ui->invBankEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_invalid_bank_id )));
    QMetaObject::invokeMethod( ui->bankSrcMisEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_bank_source_mismatch )));
    QMetaObject::invokeMethod( ui->dupSrcEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_duplicate_source )));
    QMetaObject::invokeMethod( ui->dupBankEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_duplicate_bank )));
    QMetaObject::invokeMethod( ui->repPixMapEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_map_err )));
    QMetaObject::invokeMethod( ui->pixBankMisEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_bank_mismatch )));
    QMetaObject::invokeMethod( ui->invTofEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_invalid_tof )));
    QMetaObject::invokeMethod( ui->invPixEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_unknown_id )));
    QMetaObject::invokeMethod( ui->repPixErrEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_pixel_errors )));
    QMetaObject::invokeMethod( ui->invDDPXMLEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_bad_ddp_xml )));
    QMetaObject::invokeMethod( ui->invRunXMLEdit, "setText", Qt::QueuedConnection, Q_ARG(QString, m_locale.toString( (uint) a_metrics.m_bad_runinfo_xml )));
}


void
MainWindow::clearBeamDisplay()
{
    QMetaObject::invokeMethod( ui->beamIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->beamNameShortEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->beamNameLongEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->facilityNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));

    QMetaObject::invokeMethod( ui->countRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->pchargeLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->pfreqLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->bitRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
}


void
MainWindow::setStaleText( QLineEdit *a_edit )
{
    QString temp = a_edit->text();
    if ( !temp.isEmpty() && !temp.startsWith("(") )
    {
        temp = QString("(") + temp + QString(")");
        QMetaObject::invokeMethod( a_edit, "setText", Qt::QueuedConnection, Q_ARG(QString,temp));
    }
}


void
MainWindow::clearRunDisplay( bool a_lost_comm )
{
    if ( a_lost_comm )
    {
        QMetaObject::invokeMethod( ui->propIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->runTitleEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->sampleIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->sampleNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->sampleNatureEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->sampleFormulaEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->sampleEnvironmentEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));

        QMetaObject::invokeMethod( ui->totalCountsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->totalChargeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->pixErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->dupPulseLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->mapErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->pulseVetoLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->missRTDLLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->pulsePchgUncorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->gotMetadataEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->gotNeutronsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->hasStatesEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
        QMetaObject::invokeMethod( ui->totalPulsesEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    }
    else
    {
        setStaleText( ui->propIdEdit );
        setStaleText( ui->propIdEdit );
        setStaleText( ui->runTitleEdit );
        setStaleText( ui->sampleIdEdit );
        setStaleText( ui->sampleNameEdit );
        setStaleText( ui->sampleNatureEdit );
        setStaleText( ui->sampleFormulaEdit );
        setStaleText( ui->sampleEnvironmentEdit );
        setStaleText( ui->totalCountsEdit );
        setStaleText( ui->totalChargeEdit );
        setStaleText( ui->pixErrorLabel );
        setStaleText( ui->dupPulseLabel );
        setStaleText( ui->mapErrorLabel );
        setStaleText( ui->pulseVetoLabel );
        setStaleText( ui->missRTDLLabel );
        setStaleText( ui->pulsePchgUncorLabel );
        setStaleText( ui->gotMetadataEdit );
        setStaleText( ui->gotNeutronsEdit );
        setStaleText( ui->hasStatesEdit );
        setStaleText( ui->totalPulsesEdit );
    }
}


void
MainWindow::clearSignals()
{
    m_alerts.clear();
    m_signalled = false;
    m_highest_level = ADARA::TRACE;

    m_refresh_signal_table = true;
}


void
MainWindow::writeLog( ADARA::Level a_level, const std::string &a_msg )
{
    QMutexLocker lock( &m_log_mutex );

    if ( m_log_entries.size() == 1000 )
        m_log_entries.pop_front();

    m_log_entries.push_back( QString("%1 [%2] %3").arg( QDateTime::currentDateTime().toString("MMM d, hh:mm:ss") )
                             .arg(ADARA::ComBus::ComBusHelper::toText( a_level ))
                             .arg(a_msg.c_str()));

    m_refresh_log_table = true;
}


///////////////////////////////////////////////////////////
// IStatusListener methods


void
MainWindow::comBusConnectionStatus( bool a_connected )
{
    QMutexLocker lock( &m_mutex );

    setComBusActive( a_connected );

    // If we're connecting for the first time, ask DASMON service to rebroadcast it's full state
    //if ( m_init && a_connected )
    if ( a_connected )
    {
        ADARA::ComBus::EmitStateCommand cmd;
        m_combus->send( cmd, "DASMON_0" );
        m_init = false;
    }
}


///////////////////////////////////////////////////////////
// IMessageListener methods


void
MainWindow::comBusMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    QMutexLocker lock( &m_mutex );

    setComBusActive( true );
    if ( a_msg.getAppCategory() == ADARA::ComBus::APP_DASMON )
        setDASMonActive( true );

    switch( a_msg.getMessageType() )
    {
    case ADARA::ComBus::MSG_STATUS:
        {
            map<string,ProcInfo>::iterator ip = m_proc_status.find( a_msg.getSourceID() );
            if ( ip != m_proc_status.end())
            {
                if ( ip->second.status != ((ADARA::ComBus::StatusMessage&)a_msg).m_status )
                {
                    ip->second.status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;
                    ip->second.label = QString( ADARA::ComBus::ComBusHelper::toText( ip->second.status ));
                    ip->second.hl_count = PV_HIGHLIGH_DURATION;
                    m_refresh_proc_table = true;
                }
                ip->second.last_updated = time(0);
            }
            else
            {
                ProcInfo &info = m_proc_status[a_msg.getSourceID()];
                info.name = QString( a_msg.getSourceID().c_str() );
                info.status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;
                info.label = QString( ADARA::ComBus::ComBusHelper::toText( info.status ));
                info.hl_count = PV_HIGHLIGH_DURATION;
                info.last_updated = time(0);
                m_refresh_proc_table = true;
            }

            if ( a_msg.getSourceID().compare( 0, 3, "STC" ) == 0 )
            {
                map<unsigned long,TransStatus>::iterator ts;
                for ( ts = m_trans_status.begin(); ts != m_trans_status.end(); ++ts )
                {
                    if ( ts->second.stc_pid == a_msg.getSourceID() )
                    {
                        if ( ts->second.run_status != ((ADARA::ComBus::StatusMessage&)a_msg).m_status )
                        {
                            ts->second.run_status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;
                            m_refresh_trans_table = true;
                        }
                        ts->second.last_updated = time(0);
                        break;
                    }
                }

                if ( ts == m_trans_status.end())
                {
                    TransStatus status;
                    status.running = true;
                    status.run_num = 0;
                    status.stc_pid = a_msg.getSourceID();
                    status.stc_host = "?";
                    status.run_status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;
                    status.last_updated = time(0);
                    m_trans_status[status.run_num] = status;
                    m_refresh_trans_table = true;
                }
            }

        }
        break;

    case ADARA::ComBus::MSG_LOG:
        {
            const ADARA::ComBus::LogMessage &logmsg = (const ADARA::ComBus::LogMessage&)a_msg;

            if ( m_log_entries.size() == 1000 )
                m_log_entries.pop_front();
            m_log_entries.push_back( QString("%1 [%2:%3] %4 ").arg(logmsg.getTimestamp()).arg(logmsg.getSourceID().c_str())
                                     .arg(logmsg.getLevelText().c_str()).arg(logmsg.m_msg.c_str()));
            m_refresh_log_table = true;
        }
        break;

    case ADARA::ComBus::MSG_SIGNAL_ASSERTED:
        {
            const ADARA::ComBus::SignalAssertMessage &msg = dynamic_cast<const ADARA::ComBus::SignalAssertMessage&>(a_msg);

            m_alerts[msg.m_sig_name] = AlertInfo( msg.m_sig_name, msg.m_sig_source, msg.m_sig_level,
                                                     msg.m_sig_message, PV_HIGHLIGH_DURATION );

            updateHighestSignal();
            m_refresh_signal_table = true;

            writeLog( ADARA::INFO, string("Signal Asserted: ") + msg.m_sig_name + " (" + msg.m_sig_message + ")");
        }
        break;

    case ADARA::ComBus::MSG_SIGNAL_RETRACTED:
        {
            const ADARA::ComBus::SignalRetractMessage &msg = dynamic_cast<const ADARA::ComBus::SignalRetractMessage&>(a_msg);

            map<string,AlertInfo>::iterator ia = m_alerts.find( msg.m_sig_name);
            if ( ia != m_alerts.end() )
            {
                m_alerts.erase( ia );

                updateHighestSignal();
                m_refresh_signal_table = true;

                writeLog( ADARA::INFO, string("Signal Retracted: ") + msg.m_sig_name );
            }
        }
        break;

    case ADARA::ComBus::MSG_DASMON_SMS_CONN_STATUS:
        {
            const ADARA::ComBus::DASMON::ConnectionStatusMessage &msg = (const ADARA::ComBus::DASMON::ConnectionStatusMessage&)a_msg;
            setSMSActive( msg.m_connected );
        }
        break;
    case ADARA::ComBus::MSG_DASMON_RUN_STATUS:
        {
            const ADARA::ComBus::DASMON::RunStatusMessage &msg = (const ADARA::ComBus::DASMON::RunStatusMessage&)a_msg;

            if ( msg.m_recording && ( !m_recording || ( msg.m_run_number != m_run_number )))
            {
                m_recording = msg.m_recording;
                m_run_number = msg.m_run_number;
                m_prev_run_number = m_run_number;
                m_start_time = QDateTime::fromTime_t( msg.m_timestamp );
                updateRunStatusIndicator();

                QMetaObject::invokeMethod( ui->startTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_start_time.toString("M/d/yy h:mm:ss"))));

                writeLog( ADARA::INFO, string("Run started. Run number = ") + boost::lexical_cast<string>(m_run_number) );
            }
            else if ( !msg.m_recording && m_recording )
            {
                m_recording = msg.m_recording;
                m_run_number = msg.m_run_number;
                m_start_time = QDateTime::fromTime_t( msg.m_timestamp );

                QMetaObject::invokeMethod( ui->startTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_start_time.toString("M/d/yy h:mm:ss"))));
                updateRunStatusIndicator();

                clearRunDisplay( false );
                writeLog( ADARA::INFO, "Run stopped." );
            }
        }
        break;
    case ADARA::ComBus::MSG_DASMON_PAUSE_STATUS:
        {
            const ADARA::ComBus::DASMON::PauseStatusMessage &msg = (const ADARA::ComBus::DASMON::PauseStatusMessage&)a_msg;

            if ( !m_paused && msg.m_paused )
                writeLog( ADARA::INFO, "System paused." );
            else if ( m_paused && !msg.m_paused )
                writeLog( ADARA::INFO, "System resumed." );

            m_paused = msg.m_paused;
            updatePauseStatusIndicator();
        }
        break;
    case ADARA::ComBus::MSG_DASMON_SCAN_STATUS:
        {
            const ADARA::ComBus::DASMON::ScanStatusMessage &msg = (const ADARA::ComBus::DASMON::ScanStatusMessage&)a_msg;

            if ( !m_scanning && msg.m_scaning )
                writeLog( ADARA::INFO, string("Scan started. Scan index = ") + boost::lexical_cast<string>(msg.m_scan_index) );
            else if ( m_scanning && !msg.m_scaning )
                writeLog( ADARA::INFO, "Scanning stopped." );

            m_scanning = msg.m_scaning;
            m_scan_index = msg.m_scan_index;
            updateScanStatusIndicator();
        }
        break;
    case ADARA::ComBus::MSG_DASMON_BEAM_INFO:
        {
            const ADARA::ComBus::DASMON::BeamInfoMessage &msg = (const ADARA::ComBus::DASMON::BeamInfoMessage&)a_msg;
            updateBeamInfo( msg );
        }
        break;
    case ADARA::ComBus::MSG_DASMON_RUN_INFO:
        {
            const ADARA::ComBus::DASMON::RunInfoMessage &msg = (const ADARA::ComBus::DASMON::RunInfoMessage&)a_msg;
            updateRunInfo( msg );
        }
        break;
    case ADARA::ComBus::MSG_DASMON_BEAM_METRICS:
        {
            const ADARA::ComBus::DASMON::BeamMetricsMessage &msg = (const ADARA::ComBus::DASMON::BeamMetricsMessage&)a_msg;
            updateBeamMetrics( msg );
        }
        break;
    case ADARA::ComBus::MSG_DASMON_RUN_METRICS:
        {
            const ADARA::ComBus::DASMON::RunMetricsMessage &msg = (const ADARA::ComBus::DASMON::RunMetricsMessage&)a_msg;
            updateRunMetrics( msg );
        }
        break;
    case ADARA::ComBus::MSG_DASMON_STREAM_METRICS:
        {
            const ADARA::ComBus::DASMON::StreamMetricsMessage &msg = (const ADARA::ComBus::DASMON::StreamMetricsMessage&)a_msg;
            updateStreamMetrics( msg );
        }
        break;

    case ADARA::ComBus::MSG_STC_TRANS_STARTED:
        {
            const ADARA::ComBus::STC::TranslationStartedMsg &msg = (const ADARA::ComBus::STC::TranslationStartedMsg&)a_msg;

            TransStatus status;
            status.running = true;
            status.run_num = msg.m_run_num;
            status.stc_pid = msg.getSourceID();
            status.stc_host = msg.m_host;
            status.run_status = ADARA::ComBus::STATUS_OK;
            status.last_updated = time(0);
            m_trans_status[status.run_num] = status;
            m_refresh_trans_table = true;
        }
        break;

    case ADARA::ComBus::MSG_STC_TRANS_FINISHED:
        {
            const ADARA::ComBus::STC::TranslationFinishedMsg &msg = (const ADARA::ComBus::STC::TranslationFinishedMsg&)a_msg;

            TransStatus status;
            status.running = false;
            status.run_num = msg.m_run_num;
            status.stc_pid = msg.getSourceID();
            status.stc_host = msg.m_host;
            status.trans_status = STC::TS_SUCCESS;
            status.last_updated = time(0);
            m_trans_status[status.run_num] = status;
            m_refresh_trans_table = true;
        }
        break;

    case ADARA::ComBus::MSG_STC_TRANS_FAILED:
        {
            const ADARA::ComBus::STC::TranslationFailedMsg &msg = (const ADARA::ComBus::STC::TranslationFailedMsg&)a_msg;

            TransStatus status;
            status.running = false;
            status.run_num = msg.m_run_num;
            status.stc_pid = msg.getSourceID();
            status.stc_host = msg.m_host;
            status.trans_status = msg.m_code;
            status.last_updated = time(0);
            m_trans_status[status.run_num] = status;
            m_refresh_trans_table = true;
        }
        break;

    default:
        break;
    }
}


void
MainWindow::updateHighestSignal()
{
    if ( m_alerts.empty() )
    {
        m_signalled = false;
    }
    else
    {
        m_signalled = true;
        m_highest_level = ADARA::TRACE;
        for ( map<string,AlertInfo>::iterator ia = m_alerts.begin(); ia != m_alerts.end(); ++ia )
        {
            if ( ia->second.level > m_highest_level )
                m_highest_level = ia->second.level;
        }
    }

    updateSignalStatusIndicator();
}

///////////////////////////////////////////////////////////
// IControlListener methods


void
MainWindow::comBusInputMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    QMutexLocker lock( &m_mutex );

    setComBusActive( true );
    if ( a_msg.getAppCategory() == ADARA::ComBus::APP_DASMON )
        setDASMonActive( true );

    // See if this message belongs to a sub client (by correlation id)

    map<string, SubClient*>::iterator iRoute =
        m_client_cids.find( a_msg.getCorrelationID() );
    if ( iRoute != m_client_cids.end() )
    {
        if ( !iRoute->second->comBusControlMessage( a_msg ))
        {
            // If sub client is done talking, release CID routing entry
            m_client_cids.erase( iRoute );
        }

        return;
    }

    // Process main thread commands here
    if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_PVS )
    {
        m_pvs =
            ((const ADARA::ComBus::DASMON::ProcessVariables &)a_msg).m_pvs;
        m_refresh_pv_table = true;
    }
}

///////////////////////////////////////////////////////////
// SubClient support methods

void
MainWindow::detach( SubClient &a_sub_client )
{
    QMutexLocker lock( &m_mutex );

    clearCIDs_nolock( a_sub_client );

    vector<SubClient*>::iterator i = ::find( m_sub_clients.begin(), m_sub_clients.end(), &a_sub_client );
    if ( i != m_sub_clients.end())
        m_sub_clients.erase( i );
}


void
MainWindow::clearCIDs_nolock( SubClient &a_sub_client )
{
    for ( map<string,SubClient*>::iterator c = m_client_cids.begin(); c != m_client_cids.end(); )
    {
        if ( c->second == &a_sub_client )
            m_client_cids.erase( c++ );
        else
            ++c;
    }
}

bool
MainWindow::createRoute( SubClient &a_sub_client, ADARA::ComBus::MessageBase &a_msg, const std::string &a_dest_proc )
{
    QMutexLocker lock( &m_mutex );
    bool res = m_combus->send( a_msg, a_dest_proc );

    if ( res )
    {
        // If send worked, add routing entry for CID
        m_client_cids[a_msg.getCorrelationID()] = &a_sub_client;
    }

    return res;
}


void
MainWindow::removeRoute( SubClient &a_sub_client, std::string &a_correlation_id )
{
    QMutexLocker lock( &m_mutex );

    map<string,SubClient*>::iterator iRoute = m_client_cids.find( a_correlation_id );
    if ( iRoute != m_client_cids.end() && iRoute->second == &a_sub_client )
        m_client_cids.erase( iRoute );
}


const char *
MainWindow::getStatusText( int a_status )
{
    switch( a_status )
    {
    case ADARA::VariableStatus::OK: return "OK";
    case ADARA::VariableStatus::HIHI_LIMIT: return "High-High";
    case ADARA::VariableStatus::HIGH_LIMIT: return "High";
    case ADARA::VariableStatus::LOLO_LIMIT: return "Low-Low";
    case ADARA::VariableStatus::LOW_LIMIT: return "Low";
    case ADARA::VariableStatus::NO_COMMUNICATION: return "Lost Comm";
    case ADARA::VariableStatus::UPSTREAM_DISCONNECTED: return "Disconnected";
    default: return "ERROR";
    }
}


const char *
MainWindow::getTransStatusText( STC::TranslationStatusCode &a_status )
{
    switch( a_status )
    {
    case STC::TS_SUCCESS: return "Completed Successfully";
    case STC::TS_PERM_ERROR: return "Critical Error";
    case STC::TS_TRANSIENT_ERROR: return "Transient Error";
    default: return "ERROR";
    }
}

// vim: expandtab

