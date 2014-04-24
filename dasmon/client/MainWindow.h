#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ComBus.h"
#include "DASMonDefs.h"
#include "SubClient.h"
#include "DASMonMessages.h"
#include "STSMessages.h"

#include <map>
#include <list>
#include <deque>
#include <QMainWindow>
#include <QTimer>
#include <QTableWidgetItem>
#include <QDateTime>
#include <QMutex>
#include <QLineEdit>

#define DASMON_GUI_VERSION "1.2.1"


namespace Ui {
class MainWindow;
}


class MainWindow : public QMainWindow, public ADARA::ComBus::ITopicListener, public ADARA::ComBus::IConnectionListener,
        public ADARA::ComBus::IInputListener
{
    Q_OBJECT

public:
    MainWindow( const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_broker_user, const std::string &a_broker_pass, const std::string &a_config_label, bool a_kiosk, bool a_master );
    ~MainWindow();

signals:
    void kioskMode( bool a_enabled );

private slots:

    void onProcTimer();
    void onTableTimer();
    void onPvTimer();
    void configActiveMQ();
    void configRules();
    void about();

private:
    struct AlertInfo
    {
        AlertInfo()
        {}

        AlertInfo( const std::string &a_name, const std::string &a_source, ADARA::Level a_level, const std::string &a_msg, unsigned short a_hl_count )
            : name(a_name), source(a_source), level(a_level), msg(a_msg), hl_count(a_hl_count)
        {}

        std::string                 name;
        std::string                 source;
        ADARA::Level                level;
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

    struct MonitorInfo
    {
        double                      rate;
        unsigned long               last_updated;
    };

    struct TransStatus
    {
        unsigned long               run_num;
        std::string                 sts_pid;
        bool                        running;
        ADARA::ComBus::StatusCode   run_status;
        STS::TranslationStatusCode  trans_status;
        unsigned long               last_updated;
    };

    class Tristate
    {
    public:
        Tristate() : m_flags(0) {}
        Tristate( bool a_value, bool a_active ) : m_flags(0)
        { set( a_value, a_active ); }

        void set( bool a_value, bool a_active )
        {
            m_flags = 0;

            if ( a_value )
                m_flags |= 1;

            if ( a_active )
                m_flags |= 2;
        }

        inline bool value() const { return (m_flags & 1) > 0; }
        inline bool active() const { return (m_flags & 2) > 0; }
        inline bool inactive() const { return (m_flags & 2) == 0; }
        inline void setValue( bool a_value ) { a_value?m_flags |= 1:m_flags &= 2; }
        inline void setActive( bool a_active ) { a_active?m_flags |= 2:m_flags &= 1; }
        inline bool activeTrue() const { return m_flags == 3; }
        inline bool activeFalse() const { return m_flags == 2; }

    private:
        unsigned char   m_flags;
    };


    void        clearRunDisplay( bool a_lost_comm );
    void        clearBeamDisplay();
    void        clearSignals();
    void        comBusMessage( const ADARA::ComBus::MessageBase &a_msg );
    void        comBusConnectionStatus( bool a_connected );
    void        comBusInputMessage( const ADARA::ComBus::MessageBase &a_cmd );

    void        updateMainWindowTitle();

    void        updateAllStatusIndicators();
    void        updateComBusStatusIndicator();
    void        updateDASMonStatusIndicator();
    void        updateSMSConnStatusIndicator();
    void        updateSignalStatusIndicator();
    void        updateRunStatusIndicator();
    void        updatePauseStatusIndicator();
    void        updateScanStatusIndicator();
    void        updateBeamInfo( const ADARA::DASMON::BeamInfo &a_beam_info );
    void        updateRunInfo( const ADARA::DASMON::RunInfo &a_run_info );
    void        updateBeamMetrics( const ADARA::DASMON::BeamMetrics &a_metrics );
    void        updateRunMetrics( const ADARA::DASMON::RunMetrics &a_metrics );
    void        updateStreamMetrics( const ADARA::DASMON::StreamMetrics &a_metrics );
    void        setStaleText( QLineEdit *a_edit );

    void        setComBusActive( bool a_active );
    void        setDASMonActive( bool a_active );
    void        setSMSActive( bool a_active );

    void        writeLog( ADARA::Level a_level, const std::string &a_msg );
    void        updateHighestSignal();
    const char *getStatusText( int a_status );
    const char *getTransStatusText( STS::TranslationStatusCode &a_status );

    // Methods to support SubClient command routing
    void        attach( SubClient &a_sub_client );
    void        detach( SubClient &a_sub_client );
    bool        createRoute( SubClient &a_sub_client, ADARA::ComBus::MessageBase &a_msg, const std::string &a_dest_proc );
    void        removeRoute( SubClient &a_sub_client, std::string &a_correlation_id );
    void        clearCIDs_nolock( SubClient &a_sub_client );
    void        initHighlightColors();
    bool        event( QEvent *a_event );

    inline void testSetBkgnd( unsigned short &hlcnt, QTableWidgetItem* items[], int icnt )
    {
        if ( hlcnt )
        {
            --hlcnt;
            for ( int i = 0; i < icnt; ++i )
            {
                items[i]->setTextColor( m_fg_color[hlcnt] );
                items[i]->setBackgroundColor( m_bg_color[hlcnt] );
            }
        }
    }

    Ui::MainWindow *ui;

    bool                            m_init;
    bool                            m_kiosk;
    QTimer                          m_table_timer;
    QTimer                          m_proc_timer;
    QTimer                          m_pv_timer;
    std::map<uint32_t,MonitorInfo>  m_monitor_rate;
    std::map<std::string,ProcInfo>  m_proc_status;
    std::map<unsigned long,TransStatus> m_trans_status;
    bool                            m_refresh_proc_table;
    bool                            m_refresh_signal_table;
    bool                            m_refresh_log_table;
    bool                            m_refresh_trans_table;
    bool                            m_refresh_pv_table;
    QString                         m_style_unlit;
    QString                         m_style_info;
    QString                         m_style_good;
    QString                         m_style_warn;
    QString                         m_style_error;
    QString                         m_style_fatal;
    QString                         m_style_disabled;
    QLocale                         m_locale;
    Tristate                        m_combus_state;
    Tristate                        m_dasmon_state;
    Tristate                        m_sms_state;
    bool                            m_recording;
    unsigned long                   m_run_number;
    unsigned long                   m_prev_run_number;
    bool                            m_paused;
    bool                            m_scanning;
    unsigned long                   m_scan_index;
    bool                            m_signalled;
    ADARA::Level                    m_highest_level;
    std::deque<QString>             m_log_entries;
    unsigned short                  m_event_scrollback;
    std::map<std::string,AlertInfo> m_alerts;
    std::list<AlertInfo>            m_events;
    //QColor                          m_default_bg_color;
    std::vector<QColor>             m_fg_color;
    std::vector<QColor>             m_bg_color;
    QDateTime                       m_start_time;
    QMutex                          m_mutex;
    QMutex                          m_log_mutex;
    ADARA::ComBus::Connection      *m_combus;
    std::string                     m_domain;
    std::string                     m_broker_uri;
    std::string                     m_broker_user;
    std::string                     m_broker_pass;
    std::vector<SubClient*>         m_sub_clients;
    std::map<std::string,SubClient*>    m_client_cids;
    std::map<std::string,ADARA::ComBus::DASMON::ProcessVariables::PVData> m_pvs;

    friend class SubClient;
};

#endif // MAINWINDOW_H
