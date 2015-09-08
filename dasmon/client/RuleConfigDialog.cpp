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
#include "style.h"

using namespace std;

// TODO Wire-up enabled flags in rule engine and signal system
// TODO Make rule dialog remember old state and clear mod flag when no changes
// TODO Add an "Undo changes"

RuleConfigDialog::RuleConfigDialog( MainWindow &a_parent) :
    QDialog( &a_parent), SubClient(a_parent), ui(new Ui::RuleConfigDialog), m_mainwin(a_parent),
    m_comm_status(Disconnected), m_dirty(false), m_quit_on_set(false), m_fact_filter(FilterAll)
{
    ui->setupUi(this);

    QStringList headers;
    headers << "En/Stat" << "Rule ID" << "Rule Expression" << "Description";
    ui->ruleTable->setHorizontalHeaderLabels( headers );
    ui->ruleTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->ruleTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->ruleTable->horizontalHeader()->show();

    headers.clear();
    headers << "En/Stat" << "Signal ID" << "Rule ID" << "Source" << "Level" << "Message" << "Description";
    ui->signalTable->setHorizontalHeaderLabels( headers );
    ui->signalTable->horizontalHeader()->setResizeMode( QHeaderView::ResizeToContents );
    ui->signalTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->signalTable->horizontalHeader()->show();

    ui->factFilterCB->insertItem(0,"PVs at Limits");
    ui->factFilterCB->insertItem(0,"PVs with Errors");
    ui->factFilterCB->insertItem(0,"Process Variables");
    ui->factFilterCB->insertItem(0,"Built-in");
    ui->factFilterCB->insertItem(0,"All");
    ui->factFilterCB->setCurrentIndex(0);

    ui->splitter->setStretchFactor( 0, 1 );
    ui->splitter->setStretchFactor( 1, 0 );

    connect( &m_load_timer, SIGNAL(timeout()), this, SLOT(getRules()));
    connect( &m_load_timer, SIGNAL(timeout()), this, SLOT(getFacts()));
    connect( &m_com_timer, SIGNAL(timeout()), this, SLOT(commTimeout()));
    connect( ui->ruleTable, SIGNAL(cellChanged(int,int)), this, SLOT(ruleCellChanged(int,int)));
    connect( ui->signalTable, SIGNAL(cellChanged(int,int)), this, SLOT(signalCellChanged(int,int)));

    ui->okButton->setFocus();

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

    switch ( m_comm_status )
    {
    case Disconnected: text = "Disconnected"; style = TEXT_STYLE_ERROR; break;
    case Idle: text = "Idle"; style = TEXT_STYLE_OK; break;
    case Setting: text = "Sending"; style = TEXT_STYLE_INFO; break;
    case Getting: text = "Receiving"; style = TEXT_STYLE_INFO; break;
    }

    QMetaObject::invokeMethod( ui->statusLabel, "setText", Qt::QueuedConnection, Q_ARG(QString,text));
    QMetaObject::invokeMethod( ui->statusLabel, "setStyleSheet", Qt::QueuedConnection, Q_ARG(QString,style));
}


void
RuleConfigDialog::dasmonStatus( bool active )
{
    if ( m_comm_status == Disconnected && active )
        m_comm_status = Idle;
    else if ( !active )
    {
        if ( !m_last_cid.empty())
            removeRoute( m_last_cid );

        m_comm_status = Disconnected;
    }

    updateGUIState();
}


/**
 * @return True to continue coversation, False to end and release routing entry
 *
 */
bool
RuleConfigDialog::comBusControlMessage( const ADARA::ComBus::MessageBase &a_msg )
{
    if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_INPUT_FACTS )
    {
        using namespace ADARA::ComBus::DASMON;
        const InputFacts *in_facts = dynamic_cast<const InputFacts *>( &a_msg );
        if ( in_facts )
        {
            m_fact_list = in_facts->m_facts;
            QMetaObject::invokeMethod( this, "updateFactList", Qt::QueuedConnection );
        }
    }
    else if ( a_msg.getMessageType() ==  ADARA::ComBus::MSG_ACK && m_comm_status == Setting )
    {
        // Response to set command, compare what was sent (current data) to what was
        // received. Highlight differences (syntax errors will cause items to be
        // missing from received list.

        m_com_timer.stop();

        // Set was OK, no errors
        m_dirty = false;
        m_errors.clear();

        if ( m_quit_on_set )
            QMetaObject::invokeMethod( this, "accept", Qt::QueuedConnection );

        m_last_cid.clear();
        m_comm_status = Idle;

        QMetaObject::invokeMethod( this, "updateRuleTables", Qt::QueuedConnection );
        updateGUIState();
    }
    else if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_RULE_ERRORS && m_comm_status == Setting )
    {
        m_com_timer.stop();
        m_quit_on_set = false;

        const ADARA::ComBus::DASMON::RuleErrors *errs = dynamic_cast<const ADARA::ComBus::DASMON::RuleErrors *>( &a_msg );
        if ( errs )
            m_errors = errs->m_errors;
        else
            m_errors.clear();

        m_last_cid.clear();
        m_comm_status = Idle;

        QMetaObject::invokeMethod( this, "updateRuleTables", Qt::QueuedConnection );
        updateGUIState();
    }
    else if ( a_msg.getMessageType() == ADARA::ComBus::MSG_DASMON_RULE_DEFINITIONS && m_comm_status == Getting )
    {
        // Response to get command, just copy received data into object and refresh tables

        m_com_timer.stop();

        using namespace ADARA::ComBus::DASMON;
        const RuleDefinitions *defs = dynamic_cast<const RuleDefinitions *>( &a_msg );
        if ( defs )
        {
            // Make copy of data, then trigger UI refresh
            m_rules = defs->m_rules;
            m_old_rules = m_rules;
            m_signals = defs->m_signals;
            m_old_signals = m_signals;
            m_errors.clear();

            QMetaObject::invokeMethod( this, "updateRuleTables", Qt::QueuedConnection );
        }

        m_last_cid.clear();
        m_comm_status = Idle;
        m_dirty = false;
        updateGUIState();
    }

    // Returning false here will remove the sub route created by the client
    return false;
}


void
RuleConfigDialog::commTimeout()
{
    if ( m_comm_status > Idle )
        m_comm_status = Idle;

    removeRoute( m_last_cid );
    m_last_cid.clear();
    updateGUIState();
}


void
RuleConfigDialog::updateRuleTables()
{
    QTableWidgetItem *item;
    QString tip;
    map<string,string>::iterator ie;

    ui->ruleTable->disconnect( this );
    ui->signalTable->disconnect( this );

    int row = 0;
    int cur_count = ui->ruleTable->rowCount();
    ui->ruleTable->setRowCount( m_rules.size() );

    for ( row = cur_count; row < ui->ruleTable->rowCount(); ++row )
    {
        item = new QTableWidgetItem( "" );
        item->setFlags( Qt::ItemIsSelectable );
        ui->ruleTable->setItem( row, 0, item );

        ui->ruleTable->setItem( row, 1, new QTableWidgetItem(""));
        ui->ruleTable->setItem( row, 2, new QTableWidgetItem(""));
        ui->ruleTable->setItem( row, 3, new QTableWidgetItem(""));
    }

    row = 0;
    for ( vector<RuleEngine::RuleInfo>::const_iterator r = m_rules.begin(); r != m_rules.end(); ++r, ++row )
    {
        if (( ie = m_errors.find( r->fact )) != m_errors.end())
            tip = ie->second.c_str();
        else
            tip.clear();

        ui->ruleTable->item( row, 0 )->setToolTip( tip );
        ui->ruleTable->item( row, 1 )->setText( r->fact.c_str() );
        ui->ruleTable->item( row, 1 )->setToolTip( tip );
        ui->ruleTable->item( row, 2 )->setText( r->expr.c_str() );
        ui->ruleTable->item( row, 2 )->setToolTip( tip );
        ui->ruleTable->item( row, 3 )->setText( r->desc.c_str() );
        ui->ruleTable->item( row, 3 )->setToolTip( tip );

        setupRuleTableRow( row, r->enabled, ie != m_errors.end());
    }

    cur_count = ui->signalTable->rowCount();
    ui->signalTable->setRowCount( m_signals.size() );

    for ( row = cur_count; row < ui->signalTable->rowCount(); ++row )
    {
        item = new QTableWidgetItem( "" );
        item->setFlags( Qt::ItemIsSelectable );
        ui->signalTable->setItem( row, 0, item );

        ui->signalTable->setItem( row, 1, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 2, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 3, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 4, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 5, new QTableWidgetItem(""));
        ui->signalTable->setItem( row, 6, new QTableWidgetItem(""));
    }

    ui->signalTable->setRowCount( m_signals.size() );
    row = 0;
    for ( vector<ADARA::DASMON::SignalInfo>::const_iterator s = m_signals.begin(); s != m_signals.end(); ++s, ++row )
    {
        if (( ie = m_errors.find( s->name )) != m_errors.end())
            tip = ie->second.c_str();
        else
            tip.clear();

        ui->signalTable->item( row, 0 )->setToolTip( tip );
        ui->signalTable->item( row, 1 )->setText( s->name.c_str() );
        ui->signalTable->item( row, 1 )->setToolTip( tip );
        ui->signalTable->item( row, 2 )->setText( s->fact.c_str() );
        ui->signalTable->item( row, 2 )->setToolTip( tip );
        ui->signalTable->item( row, 3 )->setText( s->source.c_str() );
        ui->signalTable->item( row, 3 )->setToolTip( tip );
        ui->signalTable->item( row, 4 )->setText( ADARA::ComBus::ComBusHelper::toText( s->level ) );
        ui->signalTable->item( row, 4 )->setToolTip( tip );
        ui->signalTable->item( row, 5 )->setText( s->msg.c_str() );
        ui->signalTable->item( row, 5 )->setToolTip( tip );
        ui->signalTable->item( row, 6 )->setText( s->desc.c_str() );
        ui->signalTable->item( row, 6 )->setToolTip( tip );

        setupSignalTableRow( row, s->enabled, ie != m_errors.end());
    }

    connect( ui->ruleTable, SIGNAL(cellChanged(int,int)), this, SLOT(ruleCellChanged(int,int)));
    connect( ui->signalTable, SIGNAL(cellChanged(int,int)), this, SLOT(signalCellChanged(int,int)));
}


void
RuleConfigDialog::updateFactList()
{
    ui->factList->clear();
    for ( set<string>::iterator fact = m_fact_list.begin(); fact != m_fact_list.end(); ++fact )
    {
        switch ( m_fact_filter )
        {
        case FilterAll:
            ui->factList->addItem( fact->c_str() );
            break;
        case FilterBuiltIn:
            if ( !boost::istarts_with( *fact, "pv_" ) && !boost::istarts_with( *fact, "pverr_" ) && !boost::istarts_with( *fact, "pvlim_" ))
                ui->factList->addItem( fact->c_str() );
            break;
        case FilterPV:
            if ( boost::istarts_with( *fact, "pv_" ))
                ui->factList->addItem( fact->c_str() );
            break;
        case FilterPVERR:
            if ( boost::istarts_with( *fact, "pverr_" ))
                ui->factList->addItem( fact->c_str() );
            break;
        case FilterPVLIM:
            if ( boost::istarts_with( *fact, "pvlim_" ))
                ui->factList->addItem( fact->c_str() );
            break;
        }
    }
}


void
RuleConfigDialog::setFactFilter( int a_index )
{
    m_fact_filter = (FactFilter)a_index;
    updateFactList();
}


void
RuleConfigDialog::accept()
{
    if ( m_dirty )
    {
        if ( m_comm_status != Idle )
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
    (void)col;
    m_dirty = true;

    QTableWidgetItem *item = ui->ruleTable->item( row, 0 );
    item->setText( "(mod)" );

    updateGUIState();
}

void
RuleConfigDialog::signalCellChanged( int row, int col )
{
    (void)col;
    m_dirty = true;

    QTableWidgetItem *item = ui->signalTable->item( row, 0 );
    item->setText( "(mod)" );

    updateGUIState();
}


void
RuleConfigDialog::getFacts()
{
    if ( m_comm_status != Disconnected )
    {
        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::GetInputFacts cmd;
        createRoute( cmd, "DASMON_0" );
    }
}


void
RuleConfigDialog::getRules()
{
    if ( m_comm_status == Idle )
    {
        if ( m_dirty )
        {
            if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                        QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
                return;
        }

        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::GetRuleDefinitions cmd;

        m_comm_status = Getting;
        if ( createRoute( cmd, "DASMON_0" ))
        {
            m_last_cid = cmd.getCorrelationID();
            m_com_timer.start( 10000 );
        }
        else
        {
            m_comm_status = Disconnected;
            m_last_cid .clear();
        }

        updateGUIState();
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
    if ( m_comm_status == Idle )
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
                rule.enabled = ( ui->ruleTable->item( row, 0 )->checkState() == Qt::Checked );
                rule.fact = ui->ruleTable->item( row, 1 )->text().toUpper().toStdString();
                rule.expr = ui->ruleTable->item( row, 2 )->text().toStdString();
                rule.desc = ui->ruleTable->item( row, 3 )->text().toStdString();
                m_rules.push_back( rule );
            }

            m_signals.clear();
            ADARA::DASMON::SignalInfo sig;
            count = ui->signalTable->rowCount();
            for ( row = 0; row < count; ++row )
            {
                sig.enabled = ( ui->signalTable->item( row, 0 )->checkState() == Qt::Checked );
                sig.name = ui->signalTable->item( row, 1 )->text().toUpper().toStdString();
                sig.fact = ui->signalTable->item( row, 2 )->text().toUpper().toStdString();
                sig.source = ui->signalTable->item( row, 3 )->text().toUpper().toStdString();
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
                sig.desc = ui->signalTable->item( row, 6 )->text().toStdString();

                m_signals.push_back( sig );
            }

            // Send SetRuleDefinitions message to DASMON service
            ADARA::ComBus::DASMON::SetRuleDefinitions cmd;

            cmd.m_set_default = a_set_default;
            cmd.m_rules = m_rules;
            cmd.m_signals = m_signals;

            if ( createRoute( cmd, "DASMON_0" ))
            {
                m_last_cid = cmd.getCorrelationID();
                m_comm_status = Setting;
                m_com_timer.start( 10000 );
            }
            else
            {
                m_last_cid.clear();
                m_comm_status = Disconnected;
                throw -1; // Trigger generic error message
            }
        }
        catch ( ... )
        {
            QMessageBox::critical( this, "DAS Monitor", "Error encountered processing rule configuration." );
        }

        updateGUIState();
    }
}


void
RuleConfigDialog::getDefaultRules()
{
    if ( m_comm_status == Idle )
    {
        if ( m_dirty )
        {
            if ( QMessageBox::question( this, "DAS Monitor", "This action will discard local edits. Continue?",
                                        QMessageBox::Yes, QMessageBox::No ) == QMessageBox::No )
                return;
        }

        // Send GetRuleDefinitions message to DASMON service
        ADARA::ComBus::DASMON::RestoreDefaultRuleDefinitions cmd;

        m_comm_status = Getting;
        if ( createRoute( cmd, "DASMON_0" ))
        {
            m_last_cid = cmd.getCorrelationID();
            m_com_timer.start( 10000 );
        }
        else
        {
            m_last_cid.clear();
            m_comm_status = Disconnected;
        }

        updateGUIState();
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
    ui->ruleTable->setItem( row, 3, new QTableWidgetItem(""));

    setupRuleTableRow( row, true, false );

    ui->ruleTable->scrollToBottom();
    ui->ruleTable->setCurrentCell( ui->ruleTable->rowCount() - 1, 1 );
    QModelIndex ind = ui->ruleTable->model()->index( ui->ruleTable->rowCount() - 1, 1 );
    ui->ruleTable->edit( ind );

    m_dirty = true;
    updateGUIState();
}

void
RuleConfigDialog::removeSelectedRule()
{
    int row = ui->ruleTable->currentIndex().row();
    if ( row >= 0 )
    {
        ui->ruleTable->removeRow( row );

        m_dirty = true;
        updateGUIState();
    }
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
    ui->signalTable->setItem( row, 6, new QTableWidgetItem(""));

    setupSignalTableRow( row, true, false );

    ui->signalTable->scrollToBottom();
    ui->signalTable->setCurrentCell( ui->signalTable->rowCount() - 1, 1 );
    QModelIndex ind = ui->signalTable->model()->index( ui->signalTable->rowCount() - 1, 1 );
    ui->signalTable->edit( ind );

    m_dirty = true;
    updateGUIState();
}

void
RuleConfigDialog::removeSelectedSignal()
{
    int row = ui->signalTable->currentIndex().row();
    if ( row >= 0 )
    {
        ui->signalTable->removeRow( row );

        m_dirty = true;
        updateGUIState();
    }
}


void
RuleConfigDialog::setupRuleTableRow( int a_row, bool a_checked, bool a_error  )
{
    QTableWidgetItem *item = ui->ruleTable->item( a_row, 0 );

    if ( a_error )
        item->setText( "(err)" );
    else
        item->setText( "" );

    item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );

    if ( a_checked )
        item->setCheckState( Qt::Checked );
    else
        item->setCheckState( Qt::Unchecked );
}


void
RuleConfigDialog::setupSignalTableRow( int a_row, bool a_checked, bool a_error )
{
    QTableWidgetItem *item = ui->signalTable->item( a_row, 0 );

    if ( a_error )
        item->setText( "(err)" );
    else
        item->setText( "" );

    item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );

    if ( a_checked )
        item->setCheckState( Qt::Checked );
    else
        item->setCheckState( Qt::Unchecked );
}


void
RuleConfigDialog::showHelp()
{
    string help_msg =
            "Rule operators:\n\n"
            "    <, <=, ==, !=, >=, >  comparisons\n"
            "    =    assignment\n"
            "    ||   boolean OR)\n"
            "    &&   boolean AND)\n"
            "    ^    (boolean XOR)\n";

    QMessageBox::information( this, "DAS Monitor Rule Configuration Help", help_msg.c_str(), QMessageBox::Ok );
}


void
RuleConfigDialog::updateGUIState()
{
    if ( m_comm_status == Idle )
    {
        if ( m_dirty )
            emit writeAllowed( true );
        else
            emit writeAllowed( false );

        emit readAllowed( true );
    }
    else
    {
        emit readAllowed( false );
        emit writeAllowed( false );
    }

    updateStatusIndicator();
}



