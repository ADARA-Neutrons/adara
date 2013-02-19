#include "mainwindow.h"
#include "ui_mainwindow.h"
//#include "ConfigureSMSConnectionDlg.h"
#include <iostream>
#include <QMetaObject>
#include <QStringList>
#include <QSettings>
//#include <log4cxx/logger.h>

#include "AMQConfigDialog.h"
#include "DASMonMessages.h"

using namespace std;

#define PV_HIGHLIGH_DURATION    30 // poll iterations
#define TIMEOUT_UNRESPONSIVE 10
#define TIMEOUT_DEAD 20
#define TIMEOUT_DEAD_CLEANUP 5


MainWindow::MainWindow(const std::string &a_broker_uri, const std::string &a_broker_user, const std::string &a_broker_pass )
    : QMainWindow(0), ui(new Ui::MainWindow),
    m_init(true), m_refresh_proc_table(false), m_refresh_signal_table(false), m_refresh_log_table(false),
    m_recording(false), m_run_number(0), m_paused(false), m_scanning(false), m_scan_index(0),
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
    m_combus->attach( *this, "ADARA.APP.DASMON" );
    m_combus->setControlListener( *this );

    m_start_time = QDateTime::currentDateTime();

    connect( &m_proc_timer, SIGNAL(timeout()), this, SLOT(onProcTimer()));
    m_proc_timer.start(5200);

    connect( &m_table_timer, SIGNAL(timeout()), this, SLOT(onTableTimer()));
    m_table_timer.start(1000);
}


MainWindow::~MainWindow()
{
    m_combus->detach( (ITopicListener&)*this );
    m_combus->detach( (IStatusListener&)*this );

    delete m_combus;
    delete ui;
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

        clearBeamDisplay();
        clearRunDisplay();
        clearSignals();

        updateAllStatusIndicators();

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

        writeLog( ADARA::INFO, "DASMON server connection established." );
    }
    else if ( !a_active && !m_dasmon_state.activeFalse() )
    {
        m_dasmon_state.set( a_active, true );
        m_sms_state.setActive( false );

        clearBeamDisplay();
        clearRunDisplay();
        clearSignals();

        updateAllStatusIndicators();

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

        clearBeamDisplay();
        clearRunDisplay();

        updateAllStatusIndicators();

        writeLog( ADARA::ERROR, "SMS connection lost." );
    }
}


void
MainWindow::onProcTimer()
{
    m_combus->sendStatus( ADARA::ComBus::STATUS_RUNNING );

    boost::unique_lock<boost::mutex> lock(m_mutex);
    unsigned long t = time(0);
    bool kill_dasmon = false;

    // Scan for and remove stopped or stalled processes, also check dasmon status
    for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); )
    {
        if ((( p->second.status == ADARA::ComBus::STATUS_STOPPING ) && ( t > p->second.last_updated + TIMEOUT_DEAD_CLEANUP )) ||
            ( t > p->second.last_updated + TIMEOUT_DEAD ))
        {
            if ( p->first == "DASMON" )
                kill_dasmon = true;

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
            }
            ++p;
        }
        else // Properly running processes
        {
            ++p;
        }
    }

    lock.unlock();

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
        boost::lock_guard<boost::mutex> lock(m_log_mutex);

        m_refresh_log_table = false;

        ui->logTable->setRowCount( m_log_entries.size());
        row = 0;
        for ( deque<QString>::iterator l = m_log_entries.begin(); l != m_log_entries.end(); ++l, ++row )
        {
            ui->logTable->setItem( row, 0, new QTableWidgetItem( *l ) );
        }
        ui->logTable->scrollToBottom();
    }


    boost::lock_guard<boost::mutex> lock(m_mutex);

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

    // Not a table, but use this timer callback to update duration field on mainwindow
    uint t = QDateTime::currentDateTime().toTime_t() - m_start_time.toTime_t();
    uint hour = t / 3600;
    uint min = t / 60;
    uint sec = t % 60;
    ui->durationLabel->setText( QString("%1:%2.%3").arg( hour, 2, 10, QLatin1Char('0') ).arg( min, 2, 10, QLatin1Char('0') ).arg( sec, 2, 10, QLatin1Char('0') ) );

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
MainWindow::configure()
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

        ADARA::ComBus::Connection::getInst().setBroker( m_broker_uri, m_broker_user, m_broker_pass );
    }
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
        style = "QLabel { background: green }";
    }
    else
    {
        text = "ComBus ?";
        style = "QLabel { background: red }";
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
        style = "QLabel { background: green }";
    }
    else
    {
        text = "DASMON ?";
        style = "QLabel { background: red }";
    }

    if ( !m_combus_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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
        style = "QLabel { background: green }";
    }
    else
    {
        text = "SMS ?";
        style = "QLabel { background: red }";
    }

    if ( !m_dasmon_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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
        style = "QLabel { background: green }";

        QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_run_number)));
    }
    else
    {
        text = "Idle";
        style = "QLabel { background: grey }";

        QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    }

    if ( !m_sms_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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
        style = "QLabel { background: orange }";
    }
    else
    {
        text = "------";
        style = "QLabel { background: grey }";
    }

    if ( !m_sms_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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
        style = "QLabel { background: cyan }";

        QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_scan_index)));
    }
    else
    {
        text = "No Scan";
        style = "QLabel { background: grey }";

        QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    }

    if ( !m_sms_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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
            style = "QLabel { background: red }";
            break;
        case ADARA::ERROR:
            text = "Error";
            style = "QLabel { background: orange }";
            break;
        case ADARA::WARN:
            text = "Warning";
            style = "QLabel { background: yellow }";
            break;
        case ADARA::INFO:
            text = "Information";
            style = "QLabel { background: cyan }";
            break;
        case ADARA::DEBUG:
        case ADARA::TRACE:
            text = "Debug";
            style = "QLabel { background: green }";
            break;
        }
    }
    else
    {
        text = "No Signals";
        style = "QLabel { background: green }";
    }

    if ( !m_dasmon_state.activeTrue() )
        style = "QLabel { background: gray; color: rgb(160,160,160)  }";

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


    if ( ui->monitorTable->rowCount() != a_metrics.m_num_monitors )
        ui->monitorTable->setRowCount( a_metrics.m_num_monitors );

    for ( int i = 0; i < a_metrics.m_num_monitors; ++i )
    {
        ui->monitorTable->setItem( i, 0, new QTableWidgetItem(QString("%1").arg( i + 1 )));
        ui->monitorTable->setItem( i, 1, new QTableWidgetItem(QString("%1").arg( a_metrics.m_monitor_count_rate[i] )));
    }

    // TODO add pix error rate
    //double                  m_pixel_error_rate;

}


void
MainWindow::updateRunMetrics( const ADARA::DASMON::RunMetrics &a_metrics )
{
    QMetaObject::invokeMethod( ui->totalCountsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_count )));
    QMetaObject::invokeMethod( ui->totalChargeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg( a_metrics.m_pulse_charge )));

    // TODO Add this...
    //unsigned long           m_pixel_error_count;
    //unsigned long           m_dup_pulse_count;
    //unsigned long           m_cycle_error_count;
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
MainWindow::clearRunDisplay()
{
    QMetaObject::invokeMethod( ui->propIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->runTitleEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->sampleIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->sampleNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->sampleNatureEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->sampleFormulaEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->sampleEnvironmentEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->startTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));

    QMetaObject::invokeMethod( ui->totalCountsEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
    QMetaObject::invokeMethod( ui->totalChargeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,""));
}

void
MainWindow::clearSignals()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_alerts.clear();
    m_signalled = false;
    m_highest_level = ADARA::TRACE;

    m_refresh_signal_table = true;
}


void
MainWindow::writeLog( ADARA::Level a_level, const std::string &a_msg )
{
    boost::lock_guard<boost::mutex> lock(m_log_mutex);

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
    setComBusActive( a_connected );

    // If we're connecting for the first time, ask DASMON service to rebroadcast it's full state
    if ( m_init && a_connected )
    {
        ADARA::ComBus::EmitStateCommand cmd;
        m_combus->sendCommand( cmd, "DASMON" );
        m_init = false;
    }
}


///////////////////////////////////////////////////////////
// IMessageListener methods


void
MainWindow::comBusMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    setComBusActive( true );
    if ( a_msg.getAppCategory() == ADARA::ComBus::APP_DASMON )
        setDASMonActive( true );

    const string &source = a_msg.getSource();

    switch( a_msg.getMessageType() )
    {
    case ADARA::ComBus::MSG_STATUS:
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);
            map<string,ProcInfo>::iterator ip = m_proc_status.find( source );
            if ( ip != m_proc_status.end())
            {
                if ( ip->second.status != ((ADARA::ComBus::StatusMessage&)a_msg).getStatus() )
                {
                    ip->second.status = ((ADARA::ComBus::StatusMessage&)a_msg).getStatus();
                    ip->second.label = QString( ADARA::ComBus::ComBusHelper::toText( ip->second.status ));
                    ip->second.hl_count = PV_HIGHLIGH_DURATION;
                    m_refresh_proc_table = true;
                }
                ip->second.last_updated = time(0);
            }
            else
            {
                ProcInfo &info = m_proc_status[source];
                info.name = QString( source.c_str() );
                info.status = ((ADARA::ComBus::StatusMessage&)a_msg).getStatus();
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
            boost::lock_guard<boost::mutex> lock(m_log_mutex);

            if ( m_log_entries.size() == 1000 )
                m_log_entries.pop_front();
            m_log_entries.push_back( QString("%1 [%2:%3] %4 ").arg(logmsg.getTimestamp()).arg(logmsg.getSource().c_str())
                                     .arg(logmsg.getLevelText().c_str()).arg(logmsg.getMessage().c_str()));
            m_refresh_log_table = true;
        }
        break;

    case ADARA::ComBus::MSG_STS_TRANS_COMPLETE:
        break;
#if 0
    case ADARA::ComBus::MSG_SIGNAL_EVENT:
        {
            const ADARA::ComBus::SignalEventMessage &msg = dynamic_cast<const ADARA::ComBus::SignalEventMessage&>(a_msg);

            boost::lock_guard<boost::mutex> lock(m_mutex);

            m_events.push_back( AlertInfo( msg.getSignalName(), msg.getSignalSource(), msg.getSignalLevel(),
                                           msg.getSignalMessage(), PV_HIGHLIGH_DURATION ));

            m_refresh_event_table = true;
        }
        break;
#endif
    case ADARA::ComBus::MSG_SIGNAL_ASSERT:
        {
            const ADARA::ComBus::SignalAssertMessage &msg = dynamic_cast<const ADARA::ComBus::SignalAssertMessage&>(a_msg);

            boost::lock_guard<boost::mutex> lock(m_mutex);

            m_alerts[msg.getSignalName()] = AlertInfo( msg.getSignalName(), msg.getSignalSource(), msg.getSignalLevel(),
                                                     msg.getSignalMessage(), PV_HIGHLIGH_DURATION );

            if ( msg.getSignalLevel()  > m_highest_level || m_alerts.size() == 1 )
            {
                m_highest_level = msg.getSignalLevel();
                m_signalled = true;
            }

            updateSignalStatusIndicator();
            m_refresh_signal_table = true;
        }
        break;

    case ADARA::ComBus::MSG_SIGNAL_RETRACT:
        {
            const ADARA::ComBus::SignalRetractMessage &msg = dynamic_cast<const ADARA::ComBus::SignalRetractMessage&>(a_msg);

            boost::lock_guard<boost::mutex> lock(m_mutex);

            map<string,AlertInfo>::iterator ia = m_alerts.find( msg.getSignalName());
            if ( ia != m_alerts.end() )
            {
                bool recalc = false;
                if ( ia->second.level == m_highest_level )
                    recalc = true;

                m_alerts.erase( ia );
                m_refresh_signal_table = true;

                if ( recalc )
                {
                    if ( m_alerts.empty() )
                    {
                        m_signalled = false;
                        updateSignalStatusIndicator();
                    }
                    else
                    {
                        m_highest_level = ADARA::TRACE;
                        for ( ia = m_alerts.begin(); ia != m_alerts.end(); ++ia )
                        {
                            if ( ia->second.level > m_highest_level )
                                m_highest_level = ia->second.level;
                        }
                        updateSignalStatusIndicator();
                    }

                }
            }
        }
        break;

    case ADARA::ComBus::MSG_DASMON_SMS_CONN_STATUS:
        {
            const ADARA::ComBus::DASMON::ConnectionStatusMessage &msg = (const ADARA::ComBus::DASMON::ConnectionStatusMessage&)a_msg;
            setSMSActive( msg.m_connected );
            if ( msg.m_connected )
            {
                // TODO update SMS host port GUI
                //msg.m_host, msg.m_port
            }
            else
            {
            }
        }
        break;
    case ADARA::ComBus::MSG_DASMON_RUN_STATUS:
        {
            const ADARA::ComBus::DASMON::RunStatusMessage &msg = (const ADARA::ComBus::DASMON::RunStatusMessage&)a_msg;

            if ( msg.m_recording && ( !m_recording || ( msg.m_run_number != m_run_number )))
            {
                m_recording = msg.m_recording;
                m_run_number = msg.m_run_number;
                updateRunStatusIndicator();

                m_start_time = QDateTime::currentDateTime();
                QMetaObject::invokeMethod( ui->startTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_start_time.toString())));

                writeLog( ADARA::INFO, string("Run started. Run number = ") + boost::lexical_cast<string>(m_run_number) );
            }
            else if ( !msg.m_recording && m_recording )
            {
                m_recording = msg.m_recording;
                m_run_number = msg.m_run_number;
                updateRunStatusIndicator();

                m_start_time = QDateTime::currentDateTime();
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
                writeLog( ADARA::INFO, string("Scan started. Scan index = ") + boost::lexical_cast<string>(m_scan_index) );
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


///////////////////////////////////////////////////////////
// IControlListener methods


bool
MainWindow::comBusCommand( const ADARA::ComBus::Command &a_cmd )
{
    (void)a_cmd;
    return false;
}


void
MainWindow::comBusReply( const ADARA::ComBus::Reply &a_reply )
{
    (void)a_reply;
}

