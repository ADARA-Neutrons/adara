#ifndef RULECONFIGDIALOG_H
#define RULECONFIGDIALOG_H

#include <vector>
#include <set>
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
    void readAllowed( bool a_allowed );
    void writeAllowed( bool a_allowed );

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
    void setFactFilter( int a_index );

private:
    enum CommStatus
    {
        Disconnected,
        Idle,
        Getting,
        Setting
    };

    enum FactFilter
    {
        FilterAll = 0,
        FilterBuiltIn,
        FilterPV,
        FilterPVERR,
        FilterPVLIM
    };

    void updateStatusIndicator();
    void dasmonStatus( bool active );
    bool comBusControlMessage( const ADARA::ComBus::MessageBase &a_msg );
    void setupRuleTableRow( int a_row, bool a_error );
    void setupSignalTableRow( int a_row, bool a_error );
    void setRules( bool a_set_default );
    void updateGUIState();

    Ui::RuleConfigDialog *ui;

    MainWindow     &m_mainwin;
    CommStatus      m_comm_status;
    QTimer          m_load_timer;
    QTimer          m_com_timer;
    std::string     m_last_cid;
    bool            m_dirty;
    bool            m_quit_on_set;
    FactFilter      m_fact_filter;
    std::vector<RuleEngine::RuleInfo>       m_rules;
    std::vector<ADARA::DASMON::SignalInfo>  m_signals;
    std::set<std::string>                   m_fact_list;
    std::map<std::string,std::string>       m_errors;
};

#endif // RULECONFIGDIALOG_H
