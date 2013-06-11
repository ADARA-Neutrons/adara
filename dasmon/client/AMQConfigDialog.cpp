#include "AMQConfigDialog.h"
#include "ui_AMQConfigDialog.h"

AMQConfigDialog::AMQConfigDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AMQConfigDialog)
{
    ui->setupUi(this);
}


AMQConfigDialog::AMQConfigDialog( QWidget *parent, const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass )
    : QDialog(parent), ui(new Ui::AMQConfigDialog), m_domain(a_domain), m_broker_uri( a_broker_uri ), m_username( a_user ), m_password( a_pass )
{
    ui->setupUi(this);
    ui->domainEdit->setText( m_domain.c_str() );
    ui->brokerEdit->setText( m_broker_uri.c_str() );
    ui->usernameEdit->setText( m_username.c_str() );
    ui->passwordEdit->setText( m_password.c_str() );
}

AMQConfigDialog::~AMQConfigDialog()
{
    delete ui;
}


void
AMQConfigDialog::accept()
{
    m_domain = ui->domainEdit->text().toStdString();
    m_broker_uri = ui->brokerEdit->text().toStdString();
    m_username = ui->usernameEdit->text().toStdString();
    m_password = ui->passwordEdit->text().toStdString();

    if ( !m_broker_uri.empty() && !(!m_password.empty() && m_username.empty()) )
    {
        // Append a "." character to domain if needed
        if ( !m_domain.empty() && *m_domain.rbegin() != '.' )
            m_domain += ".";

        QDialog::accept();
    }
}
