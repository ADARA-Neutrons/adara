#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <map>
#include <list>
#include <deque>
#include <QMainWindow>
#include <QTimer>
#include <QTableWidgetItem>

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

#include "ComBus.h"
#include "RuleDefs.h"
#include "DASMonDefs.h"

namespace Ui {
class MainWindow;
}


class MainWindow : public QMainWindow, public ADARA::ComBus::ITopicListener, public ADARA::ComBus::IStatusListener
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();


private slots:

    void onProcTimer();
    void onTableTimer();

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


    void        clearRunDisplay();
    void        clearBeamDisplay();
    void        clearSignals();
    void        comBusMessage( const ADARA::ComBus::MessageBase &a_msg );
    void        comBusConnectionStatus( bool a_connected );

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

    void        setComBusActive( bool a_active );
    void        setDASMonActive( bool a_active );
    void        setSMSActive( bool a_active );

    void        writeLog( ADARA::Level a_level, const std::string &a_msg );

    inline void testSetBkgnd( unsigned short &hlcnt, QTableWidgetItem* items[], int icnt )
    {
        if ( hlcnt )
        {
          --hlcnt;
          for ( int i = 0; i < icnt; ++i )
            items[i]->setBackgroundColor( m_hl_color[hlcnt] );
        }
    }

    Ui::MainWindow *ui;

    QTimer                          m_table_timer;
    QTimer                          m_proc_timer;
    std::map<uint32_t,uint64_t>     m_monitor_rate;
    std::map<QString,std::pair<double,unsigned short> > m_pvs;
    std::map<std::string,ProcInfo>  m_proc_status;
    bool                            m_refresh_proc_table;
    bool                            m_refresh_signal_table;
    bool                            m_refresh_event_table;
    bool                            m_refresh_log_table;
    //unsigned short                  m_connect_flags;
    Tristate                        m_combus_state;
    Tristate                        m_dasmon_state;
    Tristate                        m_sms_state;
    bool                            m_recording;
    unsigned long                   m_run_number;
    bool                            m_paused;
    bool                            m_scanning;
    unsigned long                   m_scan_index;
    bool                            m_signalled;
    ADARA::Level                    m_highest_level;
    std::deque<QString>             m_log_entries;
    //std::map<uint32_t,PktStats>     m_stats;
    unsigned short                  m_event_scrollback;
    std::map<std::string,AlertInfo> m_alerts;
    std::list<AlertInfo>            m_events;
    QColor                          m_default_bg_color;
    std::vector<QColor>             m_hl_color;
    boost::mutex                    m_mutex;
    boost::mutex                    m_log_mutex;
    ADARA::ComBus::Connection      &m_combus;
};

#endif // MAINWINDOW_H
