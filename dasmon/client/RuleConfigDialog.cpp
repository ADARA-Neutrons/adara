#include <QMessageBox>
#include <QLineEdit>
#include <QCompleter>
#include <QMessageBox>
#include <boost/algorithm/string.hpp>
#include "ComBus.h"
#include "ComBusMessages.h"
#include "DASMonMessages.h"
#include "RuleConfigDialog.h"
#include "ui_RuleConfigDialog.h"

using namespace std;

RuleConfigDialog::RuleConfigDialog( MainWindow &a_parent) :
    QDialog( &a_parent), SubClient(a_parent), ui(new Ui::RuleConfigDialog), m_mainwin(a_parent),
    m_status(Disconnected), m_dirty(false), m_quit_on_set(false)
{
    ui->setupUi(this);

    QStringList headers;
    headers << "Stat" << "Rule ID" << "Rule Expression";
    ui->ruleTable->setHorizontalHeaderLabels( headers );
    ui->ruleTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->ruleTable->horizontalHeader()->show();

    headers.clear();
    headers << "Stat" << "Signal ID" << "Rule ID" << "Source" << "Level" << "Message";
    ui->signalTable->setHorizontalHeaderLabels( headers );
    ui->signalTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->signalTable->horizontalHeader()->show();

    ui->splitter->setStretchFactor( 0, 1 );
    ui->splitter->setStretchFactor( 1, 0 );

    connect( &m_load_timer, SIGNAL(timeout()), this, SLOT(getRules()));
    connect( &m_load_timer, SIGNAL(timeout()), this, SLOT(getFacts()));
    connect( &m_com_timer, SIGNAL(timeout()), this, SLOT(commTimeout()));
    connect( ui->ruleTable, SIGNAL(cellChanged(int,int)), this, SLOT(ruleCellChanged(int,int)));
    connect( ui->signalTable, SIGNAL(cellChanged(int,int)), this, SLOT(signalCellChanged(int,int)));

    m_load_timer.setSingleShot( true );
    m_load_timer.start( 10 );
}


RuleConfigDialog::~RuleConfigDialog()
{
    delete ui;
}


void
RuleConfigDialog::updateStatusIndicator()
{
    QString text;
    QString style = "QLabel { background: green }";

    switch ( m_status )
    {
    case Disconnected: text = "Disconnected"; style = "QLabel { background: red }"; break;
    case Idle: text = "Idle"; style = "QLabel { background: green }"; break;
    case Setting: text = "Sending"; style = "QLabel { background: yellow }"; break;
    case Getting: text = "Receiving"; style = "QLabel { background: yellow }"; break;
    }

    QMetaObject::invokeMethod( ui->statusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->statusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}

void
RuleConfigDialog::dasmonStatus( bool active )
{
    if ( m_status == Disconnected && active )
        m_status = Idle;
    else if ( !active )
    {
        if ( !m_last_cid.empty())
            removeRoute( m_last_cid );

        m_status = Disconnected;
    }

    updateStatusIndicator();
}

/**
 * @return True to continue coversation, False to end and release routing entry
 *
 */
bool
RuleConfigDialog::comBusControlMessage( const ADARA::ComBus::ControlMessage &a_msg )
{
    if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_RULE_DEFINITIONS )
    {
        if ( m_status == Setting )
        {
            // Response to set command, compare what was sent (current data) to what was
            // received. Highlight differences (syntax errors will cause items to be
            // missing from received list.

            m_com_timer.stop();

            using namespace ADARA::ComBus::DASMON;
            const RuleDefinitions *defs = dynamic_cast<const RuleDefinitions *>( &a_msg );
            if ( defs )
            {
                bool err = false;

                // Compare received with curent and mark differences in diif-vectors

                // Analyze rules
                vector<RuleEngine::RuleInfo>::iterator rule_cur;
                vector<RuleEngine::RuleInfo>::const_iterator rule_recv;
                m_rules_status.clear();
                for ( rule_cur = m_rules.begin(); rule_cur != m_rules.end(); ++rule_cur )
                {
                    for ( rule_recv = defs->m_rules.begin(); rule_recv != defs->m_rules.end(); ++rule_recv )
                    {
                        if ( boost::iequals( rule_recv->fact, rule_cur->fact ))
                        {
                            if ( boost::iequals( rule_recv->expr, rule_cur->expr ))
                                m_rules_status.push_back( ItemOK );
                            else
                            {
                                err = true;
                                m_rules_status.push_back( ItemMissing );
                            }
                            break;
                        }
                    }

                    if ( rule_recv == defs->m_rules.end() )
                    {
                        err = true;
                        m_rules_status.push_back( ItemMissing );
                    }
                }

                // Analyze signals
                vector<ADARA::DASMON::SignalInfo>::iterator sig_cur;
                vector<ADARA::DASMON::SignalInfo>::const_iterator sig_recv;
                m_signal_status.clear();
                for ( sig_cur = m_signals.begin(); sig_cur != m_signals.end(); ++sig_cur )
                {
                    for ( sig_recv = defs->m_signals.begin(); sig_recv != defs->m_signals.end(); ++sig_recv )
                    {
                        if ( boost::iequals( sig_recv->name, sig_cur->name ))
                        {
                            if ( boost::iequals( sig_recv->fact, sig_cur->fact ) &&
                                 boost::iequals( sig_recv->source, sig_cur->source ) &&
                                 sig_recv->level == sig_cur->level &&
                                 boost::iequals( sig_recv->msg, sig_cur->msg ))
                                m_signal_status.push_back( ItemOK );
                            else
                            {
                                err = true;
                                m_signal_status.push_back( ItemMissing );
                            }
                            break;
                        }
                    }

                    if ( sig_recv == defs->m_signals.end() )
                    {
                        err = true;
                        m_signal_status.push_back( ItemMissing );
                    }
                }

                QMetaObject::invokeMethod( this, "updateRuleTables", Qt::QueuedConnection );

                // If no errors, then reset dirty/apply
                if ( !err )
                {
                    m_dirty = false;
                    emit configDirty( m_dirty );

                    if ( m_quit_on_set )
                        QMetaObject::invokeMethod( this, "accept", Qt::QueuedConnection );
                }
                else
                {
                    m_quit_on_set = false;
                }
            }

            m_last_cid.clear();
            m_status = Idle;
            emit busy(false);
            updateStatusIndicator();
        }
        else if ( m_status == Getting )
        {
            // Response to get command, just copy received data into object and refresh tables

            m_com_timer.stop();

            using namespace ADARA::ComBus::DASMON;
            const RuleDefinitions *defs = dynamic_cast<const RuleDefinitions *>( &a_msg );
            if ( defs )
            {
                // Make copy of data, then trigger UI refresh
                m_rules = defs->m_rules;
                m_rules_status.clear();

                m_signals = defs->m_signals;
                m_signal_status.clear();

                QMetaObject::invokeMethod( this, "updateRuleTables", Qt::QueuedConnection );
            }
            m_last_cid.clear();
            m_status = Idle;
            m_dirty = false;
            emit configDirty( m_dirty );
            emit busy(false);

            updateStatusIndicator();
        }
    }
    else if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_INPUT_FACTS )
    {
        using namespace ADARA::ComBus::DASMON;
        const InputFacts *in_facts = dynamic_cast<const InputFacts *>( &a_msg );
        if ( in_facts )
        {
            m_fact_list = in_facts->m_facts;
            QMetaObject::invokeMethod( this, "updateFactList", Qt::QueuedConnection );
        }
    }

    // Returning false here will remove the sub route created by the client
    return false;
}


void
RuleConfigDialog::commTimeout()
{
    if ( m_status > Idle )
    {
        removeRoute( m_last_cid );
        m_last_cid.clear();
        m_status = Idle;
        emit busy( false );
        updateStatusIndicator();
    }
}


void
RuleConfigDialog::updateRuleTables()
{
    static bool first_time = true;

    QTableWidgetItem *item;

    ui->ruleTable->disconnect( this );
    ui->signalTable->disconnect( this );

    int row = 0;
    int cur_count = ui->ruleTable->rowCount();
    ui->ruleTable->setRowCount( m_rules.size() );

    for ( row = cur_count; row < ui->ruleTable->rowCount(); ++row )
    {
        item = new QTableWidgetItem( "" );

        // This is rediculous, but there is not an easy way to determine the default text color of a table
        // cell (style sheets are not supported in tables)
        if ( first_time )
        {
            first_time = false;
            m_def_color = item->textColor();
        }

        ui->ruleTable->setItem( row, 0, item );
        ui->ruleTable->setItem( row, 1, new QTableWidgetItem(""));
        ui->ruleTable->setItem( row, 2, new QTableWidgetItem(""));
    }

    row = 0;
    for ( vector<RuleEngine::RuleInfo>::const_iterator r = m_rules.begin(); r != m_rules.end(); ++r, ++row )
    {
        ui->ruleTable->item( row, 1 )->setText( r->fact.c_str() );
        ui->ruleTable->item( row, 2 )->setText( r->expr.c_str() );

        if ( row < (int)m_rules_status.size() && m_rules_status[row] == ItemMissing )
            setupRuleTableRow( row, true );
        else
            setupRuleTableRow( row, false );
    }

    cur_count = ui->signalTable->rowCount();
    ui->signalTable->setRowCount( m_signals.size() );

    for ( row = cur_count; row < ui->signalTable->rowCount(); ++row )
    {
        ui->signalTable->setItem( row, 0, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 1, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 2, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 3, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 4, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 5, new QTableWidgetItem(""));
    }

    ui->signalTable->setRowCount( m_signals.size() );
    row = 0;
    for ( vector<ADARA::DASMON::SignalInfo>::const_iterator s = m_signals.begin(); s != m_signals.end(); ++s, ++row )
    {
        ui->signalTable->item( row, 1 )->setText( s->name.c_str() );
        ui->signalTable->item( row, 2 )->setText( s->fact.c_str() );
        ui->signalTable->item( row, 3 )->setText( s->source.c_str() );
        ui->signalTable->item( row, 4 )->setText( ADARA::ComBus::ComBusHelper::toText( s->level ) );
        ui->signalTable->item( row, 5 )->setText( s->msg.c_str() );

        if ( row < (int)m_signal_status.size() && m_signal_status[row] == ItemMissing )
            setupSignalTableRow( row, true );
        else
            setupSignalTableRow( row, false );
    }

    m_rules_status.clear();
    m_signal_status.clear();

    connect( ui->ruleTable, SIGNAL(cellChanged(int,int)), this, SLOT(ruleCellChanged(int,int)));
    connect( ui->signalTable, SIGNAL(cellChanged(int,int)), this, SLOT(signalCellChanged(int,int)));
}


void
RuleConfigDialog::updateFactList()
{
    ui->factList->clear();
    for ( map<string,string>::iterator fact = m_fact_list.begin(); fact != m_fact_list.end(); ++fact )
    {
        ui->factList->addItem( fact->first.c_str() );
    }
}


void
RuleConfigDialog::accept()
{
    if ( m_dirty )
    {
        if ( m_status != Idle )
        {
            if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                        QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
                return;
        }

        m_quit_on_set = true;
        setRules();
    }
    else
    {
        QDialog::accept();
    }
}


void
RuleConfigDialog::reject()
{
    if ( m_dirty )
    {
        if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                    QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
            return;
    }

    QDialog::reject();
}


void
RuleConfigDialog::ruleCellChanged( int row, int col )
{
    m_dirty = true;
    emit configDirty( m_dirty );

    QTableWidgetItem *item = ui->ruleTable->item( row, col );
    item->setTextColor( Qt::darkBlue );
}

void
RuleConfigDialog::signalCellChanged( int row, int col )
{
    m_dirty = true;
    emit configDirty( m_dirty );

    QTableWidgetItem *item = ui->signalTable->item( row, col );
    item->setTextColor( Qt::darkBlue );
}


void
RuleConfigDialog::getFacts()
{
    if ( m_status != Disconnected )
    {
        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::GetInputFacts cmd;
        string cid;

        createRoute( cmd, "DASMON.0", cid );
    }
}


void
RuleConfigDialog::getRules()
{
    if ( m_status == Idle )
    {
        if ( m_dirty )
        {
            if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                        QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
                return;
        }

        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::GetRuleDefinitions cmd;

        m_status = Getting;
        if ( createRoute( cmd, "DASMON.0", m_last_cid ))
        {
            m_com_timer.start( 10000 );
            emit busy(true);
        }
        else
            m_status = Disconnected;

        updateStatusIndicator();
    }
}

void
RuleConfigDialog::setRules()
{
    setRules( false );
}

void
RuleConfigDialog::setDefaultRules()
{
    setRules( true );
}

void
RuleConfigDialog::setRules( bool a_set_default )
{
    if ( m_status == Idle )
    {
        try
        {
            // Save data in tables to object data
            m_rules.clear();
            RuleEngine::RuleInfo    rule;
            int row;
            int count = ui->ruleTable->rowCount();
            for ( row = 0; row < count; ++row )
            {
                rule.fact = ui->ruleTable->item( row, 1 )->text().toStdString();
                rule.expr = ui->ruleTable->item( row, 2 )->text().toStdString();
                m_rules.push_back( rule );
            }

            m_signals.clear();
            ADARA::DASMON::SignalInfo sig;
            count = ui->signalTable->rowCount();
            for ( row = 0; row < count; ++row )
            {
                sig.name = ui->signalTable->item( row, 1 )->text().toStdString();
                sig.fact = ui->signalTable->item( row, 2 )->text().toStdString();
                sig.source = ui->signalTable->item( row, 3 )->text().toStdString();
                try
                {
                    sig.level = ADARA::ComBus::ComBusHelper::toLevel( ui->signalTable->item( row, 4 )->text().toStdString() );
                }
                catch ( ... )
                {
                    QMessageBox::critical( this, "DAS Monitor", QString("Invalid signal level specified for signal %1.").arg( sig.name.c_str() ));
                    return;
                }

                sig.msg = ui->signalTable->item( row, 5 )->text().toStdString();

                m_signals.push_back( sig );
            }

            // Send SetRuleDefinitions message to DASMON service
            ADARA::ComBus::DASMON::SetRuleDefinitions cmd;

            cmd.m_set_default = a_set_default;
            cmd.m_rules = m_rules;
            cmd.m_signals = m_signals;

            if ( createRoute( cmd, "DASMON.0", m_last_cid ))
            {
                m_status = Setting;
                updateStatusIndicator();
                m_com_timer.start( 10000 );
                emit busy(true);
            }
            else
            {
                m_status = Disconnected;
                updateStatusIndicator();
                throw -1; // Trigger generic error message
            }
        }
        catch ( ... )
        {
            QMessageBox::critical( this, "DAS Monitor", "Error encountered processing rule configuration." );
        }
    }
}


void
RuleConfigDialog::getDefaultRules()
{
    if ( m_status == Idle )
    {
        if ( m_dirty )
        {
            if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                        QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
                return;
        }

        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::RestoreDefaultRuleDefinitions cmd;

        m_status = Getting;
        if ( createRoute( cmd, "DASMON.0", m_last_cid ))
        {
            m_com_timer.start( 10000 );
            emit busy(true);
        }
        else
            m_status = Disconnected;

        updateStatusIndicator();
    }
}


void
RuleConfigDialog::addRule()
{
    int row = ui->ruleTable->rowCount();
    ui->ruleTable->insertRow( row );
    ui->ruleTable->setItem( row, 0, new QTableWidgetItem(""));
    ui->ruleTable->setItem( row, 1, new QTableWidgetItem(""));
    ui->ruleTable->setItem( row, 2, new QTableWidgetItem(""));
    setupRuleTableRow( ui->ruleTable->rowCount() - 1 );

    ui->ruleTable->scrollToBottom();
    ui->ruleTable->setCurrentCell( ui->ruleTable->rowCount() - 1, 1 );
    QModelIndex ind = ui->ruleTable->model()->index( ui->ruleTable->rowCount() - 1, 1 );
    ui->ruleTable->edit( ind );

    m_dirty = true;
    emit configDirty( m_dirty );
}

void
RuleConfigDialog::removeSelectedRule()
{
    ui->ruleTable->removeRow( ui->ruleTable->currentRow());

    m_dirty = true;
    emit configDirty( m_dirty );
}

void
RuleConfigDialog::addSignal()
{
    int row = ui->signalTable->rowCount();
    ui->signalTable->insertRow( row );
    ui->signalTable->setItem( row, 0, new QTableWidgetItem(""));
    ui->signalTable->setItem( row, 1, new QTableWidgetItem(""));
    ui->signalTable->setItem( row, 2, new QTableWidgetItem(""));
    ui->signalTable->setItem( row, 3, new QTableWidgetItem(""));
    ui->signalTable->setItem( row, 4, new QTableWidgetItem(""));
    ui->signalTable->setItem( row, 5, new QTableWidgetItem(""));
    setupSignalTableRow( ui->signalTable->rowCount() - 1 );

    ui->signalTable->scrollToBottom();
    ui->signalTable->setCurrentCell( ui->signalTable->rowCount() - 1, 1 );
    QModelIndex ind = ui->signalTable->model()->index( ui->signalTable->rowCount() - 1, 1 );
    ui->signalTable->edit( ind );

    m_dirty = true;
    emit configDirty( m_dirty );
}

void
RuleConfigDialog::removeSelectedSignal()
{
    ui->signalTable->removeRow( ui->signalTable->currentRow());

    m_dirty = true;
    emit configDirty( m_dirty );
}


void
RuleConfigDialog::setupRuleTableRow( int a_row, bool err )
{
    QTableWidgetItem *item = ui->ruleTable->item( a_row, 0 );
    if ( err )
    {
        item->setText( "Err" );
        item->setTextColor( Qt::red );
    }
    else
    {
        item->setText( "" );
    }

    item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled );

    if ( err )
    {
        ui->ruleTable->item( a_row, 1 )->setTextColor( Qt::red );
        ui->ruleTable->item( a_row, 2 )->setTextColor( Qt::red );
    }
    else
    {
        ui->ruleTable->item( a_row, 1 )->setTextColor( m_def_color );
        ui->ruleTable->item( a_row, 2 )->setTextColor( m_def_color );
    }
}


void
RuleConfigDialog::setupSignalTableRow( int a_row, bool err )
{
    QTableWidgetItem *item = ui->signalTable->item( a_row, 0 );
    if ( err )
    {
        item->setText( "Err" );
        item->setTextColor( Qt::red );
    }
    else
    {
        item->setText( "" );
    }

    item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled );

    QColor c = m_def_color;
    if ( err )
        c = Qt::red;

    ui->signalTable->item( a_row, 1 )->setTextColor( c );
    ui->signalTable->item( a_row, 2 )->setTextColor( c );
    ui->signalTable->item( a_row, 3 )->setTextColor( c );
    ui->signalTable->item( a_row, 4 )->setTextColor( c );
    ui->signalTable->item( a_row, 5 )->setTextColor( c );
}


void
RuleConfigDialog::showHelp()
{
    string help_msg =
            "Rule expression syntax:\n\n"
            "    FACT_ID OP [VALUE] [OP FACT_ID OP [VALUE] ...]\n\n"
            "where OP is one of:\n\n"
            "    DEF (unary - fact is defined)\n"
            "    UNDEF (unary - fact is undefined)\n"
            "    <, <=, =, !=, >=, >  (numeric comparisons)\n"
            "    |    (boolean OR)\n"
            "    &    (boolean AND)\n"
            "    !|   (boolean NOR)\n"
            "    !&   (boolean NAND)\n"
            "    ^    (boolean XOR)\n\n"
            "For non-unary operators, a value must be specified. The value "
            "can be a numberic constant, or it may be a FACT_ID. For compound "
            "expressions, additional fact-value clauses must be preceeded by "
            "a boolean operator.";

    QMessageBox::information( this, "DAS Monitor Rule Configuration Help", help_msg.c_str(), QMessageBox::Ok );
}




