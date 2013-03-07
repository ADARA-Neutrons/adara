#ifndef RULECONFIGDIALOG_H
#define RULECONFIGDIALOG_H

#include <vector>
#include <QDialog>
#include <QTimer>
#include "MainWindow.h"
#include "SubClient.h"
#include "RuleEngine.h"

namespace Ui {
class RuleConfigDialog;
}


class RuleConfigDialog : public QDialog, public SubClient
{
    Q_OBJECT

public:
    explicit RuleConfigDialog( MainWindow &a_parent );
    ~RuleConfigDialog();

signals:
    void busy( bool a_busy );
    void configDirty( bool a_dirty );

public slots:

    void accept();
    void reject();
    void getFacts();
    void getRules();
    void setRules();
    void getDefaultRules();
    void setDefaultRules();
    void addRule();
    void removeSelectedRule();
    void addSignal();
    void removeSelectedSignal();
    void showHelp();

private slots:
    void commTimeout();
    void ruleCellChanged( int row, int col );
    void signalCellChanged( int row, int col );
    void updateRuleTables();
    void updateFactList();

private:
    enum DataStatus
    {
        Disconnected,
        Idle,
        Getting,
        Setting
    };

    enum ItemStatus
    {
        ItemOK,
        ItemMissing
    };

    void updateStatusIndicator();
    void dasmonStatus( bool active );
    bool comBusControlMessage( const ADARA::ComBus::ControlMessage &a_msg );
    void setupRuleTableRow( int a_row, bool a_err = false );
    void setupSignalTableRow( int a_row, bool a_err = false );
    void setRules( bool a_set_default );

    Ui::RuleConfigDialog *ui;

    MainWindow     &m_mainwin;
    DataStatus      m_status;
    QTimer          m_load_timer;
    QTimer          m_com_timer;
    std::string     m_last_cid;
    bool            m_dirty;
    bool            m_quit_on_set;
    QColor          m_def_color;
    std::vector<RuleEngine::RuleInfo>       m_rules;
    std::vector<ItemStatus>                 m_rules_status;
    std::vector<ADARA::DASMON::SignalInfo>  m_signals;
    std::vector<ItemStatus>                 m_signal_status;
    std::map<std::string,std::string>       m_fact_list;
};

#endif // RULECONFIGDIALOG_H
