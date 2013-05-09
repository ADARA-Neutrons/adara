#include <stdint.h>

#include "MainWindow.h"
#include "ui_MainWindow.h"
//#include "ConfigureSMSConnectionDlg.h"
#include <iostream>
#include <algorithm>
#include <QMetaObject>
#include <QStringList>
#include <QSettings>
#include <QMessageBox>
#include <QMutexLocker>
//#include <log4cxx/logger.h>

#include "ADARA.h"
#include "AMQConfigDialog.h"
#include "RuleConfigDialog.h"
#include "DASMonMessages.h"
#include "STSMessages.h"

using namespace std;

#define PV_HIGHLIGH_DURATION    30 // poll iterations
#define TIMEOUT_UNRESPONSIVE 10
#define TIMEOUT_DEAD 20
#define TIMEOUT_DEAD_CLEANUP 5



MainWindow::MainWindow(const std::string &a_broker_uri, const std::string &a_broker_user, const std::string &a_broker_pass, bool a_kiosk )
    : QMainWindow(0), ui(new Ui::MainWindow),
    m_init(true), m_kiosk(a_kiosk),
    m_refresh_proc_table(false), m_refresh_signal_table(false),
    m_refresh_log_table(false), m_refresh_monitor_table(false), m_refresh_pv_table(false),
    m_recording(false), m_run_number(0), m_prev_run_number(0), m_paused(false), m_scanning(false), m_scan_index(0),
    m_signalled(false), m_highest_level(ADARA::TRACE), m_event_scrollback(5),
    m_combus(0),
    m_broker_uri( a_broker_uri ), m_broker_user( a_broker_user ), m_broker_pass( a_broker_pass )
{
    ui->setupUi(this);

    QCoreApplication::setOrganizationDomain("ornl.gov");
    QCoreApplication::setOrganizationName("sns");
    QCoreApplication::setApplicationName("dasmon-gui");

    QSettings settings;

    settings.beginGroup( "ActiveMQ" );
    m_broker_uri = settings.value( "broker_uri" ).toString().toStdString();
    m_broker_user = settings.value( "username" ).toString().toStdString();
    m_broker_pass = settings.value( "password" ).toString().toStdString();
    settings.endGroup();

    m_style_unlit = "QLabel { background: grey }";
    m_style_info = "QLabel { background: rgb(150,150,255) }";
    m_style_good = "QLabel { background: rgb(100,255,100) }";
    m_style_warn = "QLabel { background: rgb(255,255,100) }";;
    m_style_error = "QLabel { background: rgb(255,160,70) }";;
    m_style_fatal = "QLabel { color: white; background: rgb(255,0,0) }";;
    m_style_disabled = "QLabel { background: gray; color: rgb(160,160,160)  }";

    m_combus = new ADARA::ComBus::Connection( "DASMON-GUI", getpid(), m_broker_uri, m_broker_user, m_broker_pass );

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
    ui->monitorTable->horizontalHeader()->show();

    headers.clear();
    headers << "Name" << "Source" << "Level" << "Message";
    ui->alertTable->setHorizontalHeaderLabels( headers );
    ui->alertTable->horizontalHeader()->show();

    headers.clear();
    headers << "Process" << "Status";
    ui->procStatusTable->setHorizontalHeaderLabels( headers );
    ui->procStatusTable->horizontalHeader()->show();

    headers.clear();
    headers << "PV" << "Value" << "Status" << "Timestamp";
    ui->pvTable->setHorizontalHeaderLabels( headers );
    ui->pvTable->horizontalHeader()->show();

    QPalette pal = this->palette();
    m_default_bg_color = pal.color( QPalette::Base );

    int br = m_default_bg_color.red();
    int bg = m_default_bg_color.green();
    int bb = m_default_bg_color.blue();
    double dr = (255.0 - br)/PV_HIGHLIGH_DURATION;
    double dg = (255.0 - bg)/PV_HIGHLIGH_DURATION;
    double db = (200.0 - bb)/PV_HIGHLIGH_DURATION;

    m_hl_color.resize(PV_HIGHLIGH_DURATION+1);
    for ( int i = 0; i <= PV_HIGHLIGH_DURATION; ++i )
    {
        m_hl_color[i] = QColor( br + dr*i, bg + dg*i, bb + db*i );
    }

    m_combus->attach( *this );   // Listen to ComBus connection status
    m_combus->attach( *this, "ADARA.STATUS.>" ); // Listen to ADARA process health status (display only)
    m_combus->attach( *this, "ADARA.SIGNAL.>" ); // Listen to ADARA signals
    m_combus->attach( *this, "ADARA.APP.DASMON.0" );
    m_combus->setControlListener( *this );

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
    m_combus->detach( (IStatusListener&)*this );

    delete m_combus;
    delete ui;
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
        updateComBusStatusIndicator();

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
        clearRunDisplay();
        clearSignals();
        clearMonitors();

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
        updateDASMonStatusIndicator();

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
        clearRunDisplay();
        clearSignals();
        clearMonitors();

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
        updateSMSConnStatusIndicator();

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
        clearRunDisplay();
        clearMonitors();

        updateAllStatusIndicators();

        writeLog( ADARA::ERROR, "SMS connection lost." );
    }
}


void
MainWindow::onProcTimer()
{
    m_combus->sendStatus( ADARA::ComBus::STATUS_RUNNING );

    QMutexLocker lock( &m_mutex );
    unsigned long t = time(0);
    bool kill_dasmon = false;

    // Scan for and remove stopped or stalled processes, also check dasmon status
    for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); )
    {
        if ((( p->second.status == ADARA::ComBus::STATUS_STOPPING ) && ( t > p->second.last_updated + TIMEOUT_DEAD_CLEANUP )) ||
            ( t > p->second.last_updated + TIMEOUT_DEAD ))
        {
            if ( p->first == "DASMON.0" )
                kill_dasmon = true;

            writeLog( ADARA::INFO, p->first + " is dead" );

            m_proc_status.erase( p++ );
            m_refresh_proc_table = true;
        }
        else if ( t > p->second.last_updated + TIMEOUT_UNRESPONSIVE ) // Unresponsive processes
        {
            if ( p->second.status != ADARA::ComBus::STATUS_UNRESPONSIVE )
            {
                p->second.status = ADARA::ComBus::STATUS_UNRESPONSIVE;
                p->second.label = "Unesponsive";
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

    if ( m_refresh_log_table )
    {
        QMutexLocker lock( &m_log_mutex );

        m_refresh_log_table = false;

        ui->logTable->setRowCount( m_log_entries.size());
        row = 0;
        for ( deque<QString>::iterator l = m_log_entries.begin(); l != m_log_entries.end(); ++l, ++row )
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
        for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); ++p, ++row )
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
        for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); ++p, ++row )
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
        for ( map<string,AlertInfo>::iterator ia = m_alerts.begin(); ia != m_alerts.end(); ++ia, ++row )
        {
            item[0] = new QTableWidgetItem( ia->second.name.c_str());
            item[1] = new QTableWidgetItem( ia->second.source.c_str());
            item[2] = new QTableWidgetItem( ADARA::ComBus::ComBusHelper::toText( ia->second.level ));
            item[3] = new QTableWidgetItem(ia->second.msg.c_str());

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
        for ( map<string,AlertInfo>::iterator ia = m_alerts.begin(); ia != m_alerts.end(); ++ia, ++row )
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

    if ( m_refresh_monitor_table )
    {
        if ( ui->monitorTable->rowCount() != (int)m_monitor_rate.size() )
            ui->monitorTable->setRowCount( m_monitor_rate.size());

        map<uint32_t,double>::const_iterator im = m_monitor_rate.begin();
        int i = 0;
        for ( ; im != m_monitor_rate.end(); ++im, ++i )
        {
            ui->monitorTable->setItem( i, 0, new QTableWidgetItem(QString("%1").arg( im->first )));
            ui->monitorTable->setItem( i, 1, new QTableWidgetItem(QString("%1").arg( im->second )));
        }

        m_refresh_monitor_table = false;
    }

    // Not a table, but use this timer callback to update duration field on mainwindow
    if ( m_sms_state.activeTrue() )
    {
        uint now = QDateTime::currentDateTime().toTime_t();
        if (( now + 2 ) >= m_start_time.toTime_t() ) // Allow for 2 seconds of clock skew
        {
            uint t = 0;
            if ( now > m_start_time.toTime_t() )
                t = now - m_start_time.toTime_t();

            uint hour = t / 3600;
            uint min = (t % 3600) / 60;
            uint sec = t % 60;
            ui->durationLabel->setText( QString("%1:%2.%3").arg( hour, 2, 10, QLatin1Char('0') ).arg( min, 2, 10, QLatin1Char('0') ).arg( sec, 2, 10, QLatin1Char('0') ) );
        }
        else
        {
            ui->durationLabel->setText( "[clock skew]" );
        }
    }

    if ( m_refresh_pv_table )
    {
        if ( ui->pvTable->rowCount() != (int)m_pvs.size() )
            ui->pvTable->setRowCount( m_pvs.size());

        map<string,ADARA::ComBus::DASMON::ProcessVariables::PVData>::const_iterator ipv = m_pvs.begin();
        int i = 0;
        for ( ; ipv != m_pvs.end(); ++ipv, ++i )
        {
            ui->pvTable->setItem( i, 0, new QTableWidgetItem(QString("%1").arg( ipv->first.c_str() )));
            ui->pvTable->setItem( i, 1, new QTableWidgetItem(QString("%1").arg( ipv->second.value )));
            ui->pvTable->setItem( i, 2, new QTableWidgetItem(QString("%1").arg( getStatusText(ipv->second.status) )));
            ui->pvTable->setItem( i, 3, new QTableWidgetItem( QDateTime::fromTime_t(ipv->second.timestamp).toString() ));
        }

        m_refresh_pv_table = false;
    }


#if 0
        m_monitor->getStatistics( m_stats );
        ui->statisticsTable->setRowCount( m_stats.size());
        row = 0;
        for ( map<uint32_t,PktStats>::iterator p = m_stats.begin(); p != m_stats.end(); ++p, ++row )
        {
            ui->statisticsTable->setItem( row, 0, new QTableWidgetItem(QString("0x%1").arg( p->first >> 8, 0, 16 )));
            ui->statisticsTable->setItem( row, 1, new QTableWidgetItem(QString("%1").arg( m_monitor->getPktName( p->first ))));
            ui->statisticsTable->setItem( row, 2, new QTableWidgetItem(QString("%1").arg( p->second.count )));
            ui->statisticsTable->setItem( row, 3, new QTableWidgetItem(QString("%1").arg( p->second.min_pkt_size )));
            ui->statisticsTable->setItem( row, 4, new QTableWidgetItem(QString("%1").arg( p->second.max_pkt_size )));
            ui->statisticsTable->setItem( row, 5, new QTableWidgetItem(QString("%1").arg( p->second.total_size )));
        }

#endif
}


void
MainWindow::onPvTimer()
{
    string id;
    ADARA::ComBus::DASMON::GetProcessVariables cmd;
    m_combus->sendControl( cmd, "DASMON.0", id );
}


void
MainWindow::configActiveMQ()
{
    AMQConfigDialog  dlg( this, m_broker_uri, m_broker_user, m_broker_pass );

    if ( dlg.exec() == QDialog::Accepted )
    {
        m_broker_uri = dlg.getBrokerURI();
        m_broker_user = dlg.getUsername();
        m_broker_pass = dlg.getPassword();

        QSettings settings;

        settings.beginGroup( "ActiveMQ" );
        settings.setValue( "broker_uri", m_broker_uri.c_str() );
        settings.setValue( "username", m_broker_user.c_str() );
        settings.setValue( "password", m_broker_pass.c_str() );
        settings.endGroup();

        // Reset m_init to send an "emit status" message to dasmond when we connect
        m_init = true;
        ADARA::ComBus::Connection::getInst().setBroker( m_broker_uri, m_broker_user, m_broker_pass );
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
    QMessageBox::about( this, "About DAS Monitor", QString( "SNS Data Acquisition System Monitor\nVersion: %1" ).arg( DASMON_GUI_VERSION ));
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
            QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("(prev: %1)").arg(m_prev_run_number)));
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
    QMetaObject::invokeMethod( ui->countRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_count_rate )));
    QMetaObject::invokeMethod( ui->pchargeLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_charge )));
    QMetaObject::invokeMethod( ui->pfreqLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_freq )));
    QMetaObject::invokeMethod( ui->bitRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_stream_bps/1024 )));

    m_monitor_rate = a_metrics.m_monitor_count_rate;
    m_refresh_monitor_table = true;

    // TODO add pix error rate
    //double                  m_pixel_error_rate;

}


void
MainWindow::updateRunMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    QMetaObject::invokeMethod( ui->totalCountsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_count )));
    QMetaObject::invokeMethod( ui->totalChargeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_charge )));
    QMetaObject::invokeMethod( ui->pixErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pixel_error_count )));
    QMetaObject::invokeMethod( ui->dupPulseLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_dup_pulse_count )));
    QMetaObject::invokeMethod( ui->mapErrorLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_mapping_error_count )));
    QMetaObject::invokeMethod( ui->pulseVetoLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_veto_count )));
    QMetaObject::invokeMethod( ui->missRTDLLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_missing_rtdl_count )));
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
    QMetaObject::invokeMethod( ui->durationLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
}


void
MainWindow::clearRunDisplay()
{
    //QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
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
MainWindow::clearMonitors()
{
    m_monitor_rate.clear();
    m_refresh_monitor_table = true;
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
    if ( m_init && a_connected )
    {
        string id;
        ADARA::ComBus::EmitStateCommand cmd;
        m_combus->sendControl( cmd, "DASMON.0", id );
        m_init = false;
    }
}


///////////////////////////////////////////////////////////
// IMessageListener methods


void
MainWindow::comBusMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    //const string &source = a_msg.getSourceName() + "." + boost::lexical_cast<string>(a_msg.getSourceInstance());

    QMutexLocker lock( &m_mutex );

    setComBusActive( true );
    if ( a_msg.getAppCategory() == ADARA::ComBus::APP_DASMON )
        setDASMonActive( true );

    switch( a_msg.getMessageType() )
    {
    case ADARA::ComBus::MSG_STATUS:
        {

            map<string,ProcInfo>::iterator ip = m_proc_status.find( a_msg.getSourceName() );
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
                ProcInfo &info = m_proc_status[a_msg.getSourceName()];
                info.name = QString( a_msg.getSourceName().c_str() );
                info.status = ((ADARA::ComBus::StatusMessage&)a_msg).m_status;
                info.label = QString( ADARA::ComBus::ComBusHelper::toText( info.status ));
                info.hl_count = PV_HIGHLIGH_DURATION;
                info.last_updated = time(0);
                m_refresh_proc_table = true;
            }
        }
        break;

    case ADARA::ComBus::MSG_LOG:
        {
            const ADARA::ComBus::LogMessage &logmsg = (const ADARA::ComBus::LogMessage&)a_msg;

            if ( m_log_entries.size() == 1000 )
                m_log_entries.pop_front();
            m_log_entries.push_back( QString("%1 [%2:%3] %4 ").arg(logmsg.getTimestamp()).arg(logmsg.getSourceName().c_str())
                                     .arg(logmsg.getLevelText().c_str()).arg(logmsg.m_msg.c_str()));
            m_refresh_log_table = true;
        }
        break;

    case ADARA::ComBus::MSG_STS_TRANS_COMPLETE:
        {
            // Leave this out until tested
            //string txt = string("STS translation completed for run ") + boost::lexical_cast<string>( ((ADARA::ComBus::STS::TranslationCompleteMessage&)a_msg).m_run_num );
            //writeLog( ADARA::INFO, txt );
        }
        break;

    case ADARA::ComBus::MSG_SIGNAL_ASSERT:
        {
            const ADARA::ComBus::SignalAssertMessage &msg = dynamic_cast<const ADARA::ComBus::SignalAssertMessage&>(a_msg);

            m_alerts[msg.m_sig_name] = AlertInfo( msg.m_sig_name, msg.m_sig_source, msg.m_sig_level,
                                                     msg.m_sig_message, PV_HIGHLIGH_DURATION );

            updateHighestSignal();
            m_refresh_signal_table = true;

            writeLog( ADARA::INFO, string("Signal Asserted: ") + msg.m_sig_name + " (" + msg.m_sig_message + ")");
        }
        break;

    case ADARA::ComBus::MSG_SIGNAL_RETRACT:
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

                clearRunDisplay();
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


bool
MainWindow::comBusControlMessage( const ADARA::ComBus::ControlMessage &a_msg )
{
    QMutexLocker lock( &m_mutex );

    setComBusActive( true );
    if ( a_msg.getAppCategory() == ADARA::ComBus::APP_DASMON )
        setDASMonActive( true );

    //cout << "Control Msg: " << a_msg.getMessageType() << endl;

    // See if this message belogs to a sub client (by correlation id)

    map<string,SubClient*>::iterator iRoute = m_client_cids.find( a_msg.m_correlation_id );
    if ( iRoute != m_client_cids.end() )
    {
        if ( !iRoute->second->comBusControlMessage( a_msg ))
        {
            // If sub client is done talking, release CID routing entry
            m_client_cids.erase( iRoute );
        }

        return true;
    }

    // Process main thread commands here
    if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_PVS )
    {
        m_pvs = ((const ADARA::ComBus::DASMON::ProcessVariables &)a_msg).m_pvs;
        m_refresh_pv_table = true;
        return true;
    }

    return false;
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
MainWindow::createRoute( SubClient &a_sub_client, ADARA::ComBus::ControlMessage &a_msg, const std::string &a_dest_proc, std::string &a_correlation_id )
{
    QMutexLocker lock( &m_mutex );
    bool res = m_combus->sendControl( a_msg, a_dest_proc, a_correlation_id );

    if ( res )
    {
        // If send worked, add routing entry for CID
        m_client_cids[a_correlation_id] = &a_sub_client;
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
