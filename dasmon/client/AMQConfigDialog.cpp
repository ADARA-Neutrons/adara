#include "AMQConfigDialog.h"
#include "ui_AMQConfigDialog.h"

AMQConfigDialog::AMQConfigDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AMQConfigDialog)
{
    ui->setupUi(this);
}


AMQConfigDialog::AMQConfigDialog( QWidget *parent, const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass )
    : QDialog(parent), ui(new Ui::AMQConfigDialog), m_broker_uri( a_broker_uri ), m_username( a_user ), m_password( a_pass )
{
    ui->setupUi(this);
    ui->amqBrokerEdit->setText( m_broker_uri.c_str() );
    ui->amqUsernameEdit->setText( m_username.c_str() );
    ui->amqPasswordEdit->setText( m_password.c_str() );
}

AMQConfigDialog::~AMQConfigDialog()
{
    delete ui;
}


void
AMQConfigDialog::accept()
{
    m_broker_uri = ui->amqBrokerEdit->text().toStdString();
    m_username = ui->amqUsernameEdit->text().toStdString();
    m_password = ui->amqPasswordEdit->text().toStdString();

    if ( !m_broker_uri.empty() && !(!m_password.empty() && m_username.empty()) )
        QDialog::accept();
}
