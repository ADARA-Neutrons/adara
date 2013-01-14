#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ConfigureSMSConnectionDlg.h"
#include <iostream>
#include <QMetaObject>
#include <QStringList>
#include <QDateTime>
#include <log4cxx/logger.h>


using namespace std;

#define PV_HIGHLIGH_DURATION    30 // poll iterations

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_monitor(0), m_refresh_pv_table(false), m_refresh_alert_table(false), m_refresh_event_table(false),
    m_refresh_proc_table(false), m_refresh_log_table(false), m_event_scrollback(5), m_combus(ADARA::ComBus::Connection::getInst())
{
    ui->setupUi(this);

    //m_combus =  new ADARA::ComBus::Connection( "DASMON", 0, "localhost", "", "" );
    m_combus.attach( *this );
    m_combus.attach( *this, "ADARA.>" );

    combusStatus( false );
    connectionStatus( false );
    runStatus( false, 0 );
    pauseStatus( false );
    scanStop();

    // This code is required due to a bug in Qt designer
    QStringList headers;
    headers << "Monitor ID" << "Count Rate";
    ui->monitorTable->setHorizontalHeaderLabels( headers );
    ui->monitorTable->horizontalHeader()->show();

    headers.clear();
    headers << "Pkt ID" << "Pkt Name" << "Count" << "Min Size" << "Max Size" << "Total Size";
    ui->statisticsTable->setHorizontalHeaderLabels( headers );
    ui->statisticsTable->horizontalHeader()->show();

    headers.clear();
    headers << "Name" << "Value";
    ui->pvTable->setHorizontalHeaderLabels( headers );
    ui->pvTable->horizontalHeader()->show();

    headers.clear();
    headers << "Type" << "Severity" << "Item";
    ui->alertTable->setHorizontalHeaderLabels( headers );
    ui->alertTable->horizontalHeader()->show();

    ui->eventTable->setHorizontalHeaderLabels( headers );
    ui->eventTable->horizontalHeader()->show();

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

    connect( &m_poll_timer, SIGNAL(timeout()), this, SLOT(onPollTimer()));
    m_poll_timer.start(1000);

    connect( &m_proc_timer, SIGNAL(timeout()), this, SLOT(onProcTimer()));
    m_proc_timer.start(5200);
}


MainWindow::~MainWindow()
{
    m_combus.detach( (ITopicListener&)*this );
    m_combus.detach( (IStatusListener&)*this );
//    m_combus.detach( *this, "ADARA.>" );

    delete ui;
}


void
MainWindow::setStreamMonitor( StreamMonitor *a_monitor )
{
    m_monitor = a_monitor;
    string host;
    unsigned short port;
    m_monitor->getSMSHostInfo( host, port );

    QString text = QString("%1:%2").arg(host.c_str()).arg(port);
    QMetaObject::invokeMethod( ui->smsHostLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
}


void
MainWindow::configureSMSConnection()
{
    string host;
    unsigned short port;

    m_monitor->getSMSHostInfo( host, port );

    ConfigureSMSConnectionDlg dlg( this, host, port );

    if ( dlg.exec() == QDialog::Accepted )
    {
        m_monitor->setSMSHostInfo( dlg.getHostname(), dlg.getPort());

        QString text = QString("%1:%2").arg(dlg.getHostname().c_str()).arg(dlg.getPort());
        QMetaObject::invokeMethod( ui->smsHostLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    }
}


void
MainWindow::onPollTimer()
{
    if ( m_monitor )
    {
        QMetaObject::invokeMethod( ui->countRateLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(m_monitor->getCountRate())));

        m_monitor->getMonitorCountRates( m_monitor_rate );

        ui->monitorTable->setRowCount( m_monitor_rate.size());
        int row = 0;
        for ( map<uint32_t,uint64_t>::iterator m = m_monitor_rate.begin(); m != m_monitor_rate.end(); ++m, ++row )
        {
            ui->monitorTable->setItem( row, 0, new QTableWidgetItem(QString("%1").arg( m->first )));
            ui->monitorTable->setItem( row, 1, new QTableWidgetItem(QString("%1").arg( m->second )));
        }

        ui->pchargeLabel->setText( QString("%1").arg(m_monitor->getProtonCharge()));
        ui->pfreqLabel->setText( QString("%1").arg(m_monitor->getPulseFrequency()));
        ui->totalCountsEdit->setText( QString("%1").arg(m_monitor->getRunPulseCount()));
        ui->totalChargeEdit->setText( QString("%1").arg(m_monitor->getRunPulseCharge()));

        QString tmp = QLocale(QLocale::English).toString( (qlonglong)(m_monitor->getByteRate()/1024) );
        ui->bitRateLabel->setText( tmp );

        // Process variable and alert data is updated asynchronously - gaurd the containers
        // Also statistics can be reset asynchronously
        boost::lock_guard<boost::mutex> lock(m_mutex);

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

        QTableWidgetItem *item;

        if ( m_refresh_pv_table )
        {
            m_refresh_pv_table = false;

            ui->pvTable->setRowCount( m_pvs.size());
            row = 0;
            for ( map<QString,pair<double,unsigned short> >::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv, ++row )
            {
                ui->pvTable->setItem( row, 0, new QTableWidgetItem(ipv->first));

                item = new QTableWidgetItem(QString("%1").arg( ipv->second.first ));
                ui->pvTable->setItem( row, 1, item );

                if ( ipv->second.second )
                {
                    --ipv->second.second;
                    item->setBackgroundColor( m_hl_color[ipv->second.second] );
                }
            }
        }
        else
        {
            // See if PV table should be updated for color only

            row = 0;

            for ( map<QString,pair<double,unsigned short> >::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv, ++row )
            {
                if ( ipv->second.second )
                {
                    --ipv->second.second;
                    item = ui->pvTable->item( row, 1 );
                    item->setBackgroundColor( m_hl_color[ipv->second.second] );
                }
            }
        }

        if ( m_refresh_alert_table )
        {
            m_refresh_alert_table = false;
            QTableWidgetItem *item2;
            QTableWidgetItem *item3;

            ui->alertTable->setRowCount( m_alerts.size());
            row = 0;
            for ( map<string,AlertInfo>::iterator ia = m_alerts.begin(); ia != m_alerts.end(); ++ia, ++row )
            {
                item = new QTableWidgetItem(RuleEng::RuleEngine::toString( ia->second.type ));
                ui->alertTable->setItem( row, 0, item );
                item2 = new QTableWidgetItem(RuleEng::RuleEngine::toString( ia->second.severity ));
                ui->alertTable->setItem( row, 1, item2 );
                item3 = new QTableWidgetItem(ia->second.msg.c_str());
                ui->alertTable->setItem( row, 2, item3 );

                if ( ia->second.hl_count )
                {
                    --ia->second.hl_count;
                    item->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                    item2->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                    item3->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                }
            }
        }
        else
        {
            row = 0;
            for ( map<string,AlertInfo>::iterator ia = m_alerts.begin(); ia != m_alerts.end(); ++ia, ++row )
            {
                if ( ia->second.hl_count )
                {
                    --ia->second.hl_count;
                    item = ui->alertTable->item( row, 0 );
                    item->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                    item = ui->alertTable->item( row, 1 );
                    item->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                    item = ui->alertTable->item( row, 2 );
                    item->setBackgroundColor( m_hl_color[ia->second.hl_count] );
                }
            }
        }

        if ( m_refresh_event_table )
        {
            m_refresh_event_table = false;

            int rows = ui->eventTable->rowCount();

            // Remove old messages to keep scroll back limit
            if ( m_events.size() > m_event_scrollback )
            {
                while ( m_events.size() > m_event_scrollback )
                {
                    m_events.pop_front();
                    if ( rows )
                    {
                        ui->eventTable->removeRow( 0 );
                        --rows;
                    }
                }
            }

            list<AlertInfo>::iterator ie = m_events.begin();
            advance( ie, rows );

            for ( int r = rows; r < (int)m_events.size(); ++r, ++ie )
            {
                ui->eventTable->insertRow( r );
                ui->eventTable->setItem( r, 0, new QTableWidgetItem( RuleEng::RuleEngine::toString( ie->type )));
                ui->eventTable->setItem( r, 1, new QTableWidgetItem( RuleEng::RuleEngine::toString( ie->severity )));
                ui->eventTable->setItem( r, 2, new QTableWidgetItem( ie->msg.c_str() ));
            }
            ui->eventTable->scrollToBottom();
        }

        if ( m_refresh_proc_table )
        {
            m_refresh_proc_table = false;

            ui->procStatusTable->setRowCount( m_proc_status.size());
            row = 0;
            for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); ++p, ++row )
            {
                item = new QTableWidgetItem( p->second.name );
                item->setBackgroundColor( m_hl_color[p->second.hl_count] );
                ui->procStatusTable->setItem( row, 0, item );

                item = new QTableWidgetItem( p->second.label );
                item->setBackgroundColor( m_hl_color[p->second.hl_count] );
                ui->procStatusTable->setItem( row, 1, item );
            }
        }
        else
        {
            row = 0;
            for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); ++p, ++row )
            {
                if ( p->second.hl_count )
                {
                    --p->second.hl_count;
                    item = ui->procStatusTable->item( row, 0 );
                    item->setBackgroundColor( m_hl_color[p->second.hl_count] );
                    item = ui->procStatusTable->item( row, 1 );
                    item->setBackgroundColor( m_hl_color[p->second.hl_count] );
                }
            }
        }

        if ( m_refresh_log_table )
        {
            m_refresh_log_table = false;

            ui->logTable->setRowCount( m_log_entries.size());
            row = 0;
            for ( deque<QString>::iterator l = m_log_entries.begin(); l != m_log_entries.end(); ++l, ++row )
            {
                item = new QTableWidgetItem( *l );
                ui->logTable->setItem( row, 0, item );
            }
            ui->logTable->scrollToBottom();
        }
    }
}


void
MainWindow::onProcTimer()
{
    LOG4CXX_TRACE(log4cxx::Logger::getRootLogger(),"ProcTimer TRACE" );
    LOG4CXX_DEBUG(log4cxx::Logger::getRootLogger(),"ProcTimer DEBUG" );
    LOG4CXX_INFO(log4cxx::Logger::getRootLogger(),"ProcTimer INFO" );
    LOG4CXX_WARN(log4cxx::Logger::getRootLogger(),"ProcTimer WARN " );
    LOG4CXX_ERROR(log4cxx::Logger::getRootLogger(),"ProcTimer ERROR " );
    LOG4CXX_FATAL(log4cxx::Logger::getRootLogger(),"ProcTimer FATAL " );

    //cout << "send status" << endl;
    m_combus.sendStatus( ADARA::ComBus::STATUS_RUNNING );

    boost::lock_guard<boost::mutex> lock(m_mutex);
    unsigned long t = time(0);

    // Scan for and remove stopped or stalled processes
    for ( map<string,ProcInfo>::iterator p = m_proc_status.begin(); p != m_proc_status.end(); )
    {
        if ((( p->second.status == ADARA::ComBus::STATUS_STOPPING ) && ( t > p->second.last_updated + 10 )) ||
            ( t > p->second.last_updated + 60 ))
        {
            m_proc_status.erase( p++ );
            m_refresh_proc_table = true;
        }
        else if ( t > p->second.last_updated + 30 )
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
        else
        {
            ++p;
        }
    }
}


void
MainWindow::combusStatus( bool a_connected )
{
    QString text;
    QString style;

    if( a_connected )
    {
        text = "ComBus";
        style = "QLabel { background: green }";
    }
    else
    {
        text = "ComBus?";
        style = "QLabel { background: red }";
    }

    QMetaObject::invokeMethod( ui->combusStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->combusStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::connectionStatus( bool a_connected )
{
    QString text;
    QString style;

    if( a_connected )
    {
        text = "SMS";
        style = "QLabel { background: green }";
    }
    else
    {
        text = "SMS?";
        style = "QLabel { background: red }";
    }

    QMetaObject::invokeMethod( ui->smsStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->smsStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::runStatus( bool a_recording, unsigned long a_run_number )
{
    m_combus.sendLog( "Run status callback", ADARA::ComBus::LOG_TRACE );

    QString text;
    QString style;

    if( a_recording )
    {
        text = "Recording";
        style = "QLabel { background: green }";
        clearRunInfo();

        QDateTime time = QDateTime::currentDateTime();
        QMetaObject::invokeMethod( ui->startTimeEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(time.toString())));
    }
    else
    {
        text = "Idle";
        style = "QLabel { background: grey }";

        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_pvs.clear();
        m_refresh_pv_table = true;
    }

    QMetaObject::invokeMethod( ui->runStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->runStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));

    QMetaObject::invokeMethod( ui->runNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(a_run_number)));
}


void
MainWindow::pauseStatus( bool a_paused )
{
    m_combus.sendLog( "Pause status callback", ADARA::ComBus::LOG_TRACE );

    QString text;
    QString style;

    if ( a_paused )
    {
        text = "Paused";
        style = "QLabel { background: orange }";
    }
    else
    {
        text = "------";
        style = "QLabel { background: grey }";
    }

    QMetaObject::invokeMethod( ui->pauseStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->pauseStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}

void
MainWindow::clearRunInfo()
{
    facilityName("");
    proposalID("");
    runTitle("");
    sampleID("");
    sampleName("");
    sampleNature("");
    sampleFormula("");
    sampleEnvironment("");
}


void
MainWindow::facilityName( const std::string &a_facility_name )
{
    QMetaObject::invokeMethod( ui->facilityNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_facility_name.c_str()));
}


void
MainWindow::beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name )
{
    QMetaObject::invokeMethod( ui->beamIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_id.c_str()));
    QMetaObject::invokeMethod( ui->beamNameShortEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_short_name.c_str()));
    QMetaObject::invokeMethod( ui->beamNameLongEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_long_name.c_str()));
}


void
MainWindow::proposalID( const std::string &a_proposal_id )
{
    QMetaObject::invokeMethod( ui->propIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_proposal_id.c_str()));
}


void
MainWindow::scanStart( unsigned long a_scan_number )
{
    QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,QString("%1").arg(a_scan_number)));

    QString text = "Scanning";
    QString style = "QLabel { background: cyan }";

    QMetaObject::invokeMethod( ui->scanStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->scanStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
MainWindow::scanStop()
{
    QMetaObject::invokeMethod( ui->scanNumLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,""));

    QString text = "No Scan";
    QString style = "QLabel { background: grey }";

    QMetaObject::invokeMethod( ui->scanStatusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->scanStatusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}

void
MainWindow::runTitle( const std::string & a_run_title )
{
    QMetaObject::invokeMethod( ui->runTitleEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_run_title.c_str()));
}


void
MainWindow::sampleID( const std::string &a_sample_id )
{
    QMetaObject::invokeMethod( ui->sampleIdEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_sample_id.c_str()));
}


void
MainWindow::sampleName( const std::string &a_sample_name )
{
    QMetaObject::invokeMethod( ui->sampleNameEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_sample_name.c_str()));
}


void
MainWindow::sampleNature( const std::string &a_sample_nature )
{
    QMetaObject::invokeMethod( ui->sampleNatureEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_sample_nature.c_str()));
}


void
MainWindow::sampleFormula( const std::string &a_sample_formula )
{
    QMetaObject::invokeMethod( ui->sampleFormulaEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_sample_formula.c_str()));
}


void
MainWindow::sampleEnvironment( const std::string &a_sample_environment )
{
    QMetaObject::invokeMethod( ui->sampleEnvironmentEdit, "setText", Qt::QueuedConnection, Q_ARG(QString,a_sample_environment.c_str()));
}


void
MainWindow::userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole )
{
}


void
MainWindow::onPvDefined( QString a_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_pvs.find( a_name ) == m_pvs.end())
    {
        m_pvs[a_name] = pair<double,unsigned short>(0.0,0);
        m_refresh_pv_table = true;
    }
}


void
MainWindow::onPvValue( QString a_name, double a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<QString,pair<double,unsigned short> >::iterator ipv = m_pvs.find( a_name );

    if ( ipv != m_pvs.end())
    {
        if ( a_value != m_pvs[a_name].first )
        {
            m_pvs[a_name] = pair<double,unsigned short>(a_value,PV_HIGHLIGH_DURATION);
            m_refresh_pv_table = true;
        }
    }
}

void
MainWindow::pvDefined( const std::string &a_name )
{
    QString name = a_name.c_str();
    QMetaObject::invokeMethod( this, "onPvDefined", Qt::QueuedConnection, Q_ARG(QString,name));
}


void
MainWindow::pvValue( const std::string &a_name, double a_value )
{
    QString name = a_name.c_str();
    QMetaObject::invokeMethod( this, "onPvValue", Qt::QueuedConnection, Q_ARG(QString,name), Q_ARG(double,a_value) );
}



///////////////////////////////////////////////////////////
// IRuleListener methods

void
MainWindow::reportEvent( const RuleEng::Rule &a_rule )
{
    m_combus.sendLog( "Event callback", ADARA::ComBus::LOG_TRACE );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_events.push_back( AlertInfo( a_rule.getType(), a_rule.getSeverity(), a_rule.getMessage(), PV_HIGHLIGH_DURATION ));
    m_refresh_event_table = true;
}


void
MainWindow::assertFinding( const std::string &a_id, const RuleEng::Rule &a_rule )
{
    m_combus.sendLog( "Assert finding!", ADARA::ComBus::LOG_TRACE );

    boost::lock_guard<boost::mutex> lock(m_mutex);

    m_alerts[a_id] = AlertInfo( a_rule.getType(), a_rule.getSeverity(), a_rule.getMessage(), PV_HIGHLIGH_DURATION );
    m_refresh_alert_table = true;
}


void
MainWindow::retractFinding( const std::string &a_id )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<string,AlertInfo>::iterator a = m_alerts.find( a_id );

    if ( a != m_alerts.end())
        m_alerts.erase(a);

    m_refresh_alert_table = true;
}


///////////////////////////////////////////////////////////
// IStatusListener methods

void
MainWindow::comBusConnectionStatus( bool a_connected )
{
    combusStatus( a_connected );
}


///////////////////////////////////////////////////////////
// IMessageListener methods

void
MainWindow::comBusMessage( const ADARA::ComBus::Message &a_msg )
{
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
                    ip->second.label = QString( ADARA::ComBus::StatusMessage::getStatusText( ip->second.status ).c_str() );
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
                info.label = QString( ADARA::ComBus::StatusMessage::getStatusText( info.status ).c_str() );
                info.hl_count = PV_HIGHLIGH_DURATION;
                info.last_updated = time(0);
                m_refresh_proc_table = true;
            }
        }
        break;

    case ADARA::ComBus::MSG_LOG:
        {
            const ADARA::ComBus::LogMessage &logmsg = (const ADARA::ComBus::LogMessage&)a_msg;
            boost::lock_guard<boost::mutex> lock(m_mutex);

            if ( m_log_entries.size() == 1000 )
                m_log_entries.pop_front();
            m_log_entries.push_back( QString("%1 [%2:%3] %4 ").arg(logmsg.getTimestamp()).arg(logmsg.getSource().c_str())
                                     .arg(logmsg.getLevelText().c_str()).arg(logmsg.getMessage().c_str()));
            m_refresh_log_table = true;
        }
        break;

    case ADARA::ComBus::MSG_STATUS_STS_TRANS_COMPLETE:
        break;
    }
//    QMetaObject::invokeMethod( ui->comBusEdit, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,QString(text.c_str())));
}


