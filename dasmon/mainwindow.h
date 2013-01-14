#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <map>
#include <list>
#include <deque>
#include <QMainWindow>
#include <QTimer>

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

#include "StreamMonitor.h"
#include "ruleengine.h"
#include "ComBus.h"

namespace Ui {
class MainWindow;
}

class StreamMonitor;


class MainWindow : public QMainWindow, public IStreamListener, public RuleEng::IRuleListener,
        public ADARA::ComBus::ITopicListener, public ADARA::ComBus::IStatusListener
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setStreamMonitor( StreamMonitor *a_monitor );

    void runStatus( bool a_recording, unsigned long a_run_number );
    void pauseStatus( bool a_paused );
    void scanStart( unsigned long a_scan_number );
    void scanStop();
    void runTitle( const std::string &a_run_title );
    void facilityName( const std::string &a_facility_name );
    void beamInfo( const std::string &a_id, const std::string &a_short_name, const std::string &a_long_name );
    void proposalID( const std::string &a_proposal_id );
    void sampleID( const std::string &a_sample_id );
    void sampleName( const std::string &a_sample_name );
    void sampleNature( const std::string &a_sample_nature );
    void sampleFormula( const std::string &a_sample_formula );
    void sampleEnvironment( const std::string &a_sample_environment );
    void userInfo( const std::string &a_uid, const std::string &a_uname, const std::string &a_urole );
    void pvDefined( const std::string &a_name );
    void pvValue( const std::string &a_name, double a_value );
    void connectionStatus( bool a_connected );
    void communicationTimeout();

    void reportEvent( const RuleEng::Rule &a_rule );
    void assertFinding( const std::string &a_id, const RuleEng::Rule &a_rule );
    void retractFinding( const std::string &a_id );

public slots:

    void onPollTimer();
    void onProcTimer();
    void configureSMSConnection();

private slots:

    void onPvDefined( QString a_name );
    void onPvValue( QString a_name, double a_value );

private:
    struct AlertInfo
    {
        AlertInfo()
        {}

        AlertInfo( RuleEng::RuleType a_type, RuleEng::RuleSeverity a_severity, const std::string &a_msg, unsigned short a_hl_count )
            : type(a_type), severity(a_severity), msg(a_msg), hl_count(a_hl_count)
        {}

        RuleEng::RuleType           type;
        RuleEng::RuleSeverity       severity;
        std::string                 msg;
        unsigned short              hl_count;
    };

    struct ProcInfo
    {
        ADARA::ComBus::StatusCode   status;
        QString                     name;
        QString                     label;
        unsigned long               last_updated;
        unsigned short              hl_count;
    };


    void                            clearRunInfo();
    void                            combusStatus( bool a_connected );
    void                            comBusMessage( const ADARA::ComBus::Message &a_msg );
    void                            comBusConnectionStatus( bool a_connected );

    Ui::MainWindow *ui;

    StreamMonitor                  *m_monitor;
    QTimer                          m_poll_timer;
    QTimer                          m_proc_timer;
    std::map<uint32_t,uint64_t>     m_monitor_rate;
    std::map<QString,std::pair<double,unsigned short> > m_pvs;
    std::map<std::string,ProcInfo>  m_proc_status;
    std::deque<QString>             m_log_entries;
    std::map<uint32_t,PktStats>     m_stats;
    bool                            m_refresh_pv_table;
    bool                            m_refresh_alert_table;
    bool                            m_refresh_event_table;
    bool                            m_refresh_proc_table;
    bool                            m_refresh_log_table;
    unsigned short                  m_event_scrollback;
    std::map<std::string,AlertInfo> m_alerts;
    std::list<AlertInfo>            m_events;
    QColor                          m_default_bg_color;
    std::vector<QColor>             m_hl_color;
    boost::mutex                    m_mutex;
    ADARA::ComBus::Connection      &m_combus;
};

#endif // MAINWINDOW_H
