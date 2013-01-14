#include "ConfigureSMSConnectionDlg.h"
#include "ui_ConfigureSMSConnectionDlg.h"
#include <QMessageBox>


ConfigureSMSConnectionDlg::ConfigureSMSConnectionDlg( QWidget *parent, std::string a_hostname, unsigned short a_port )
 : QDialog(parent), ui(new Ui::ConfigureSMSConnectionDlg), m_hostname(a_hostname), m_port(a_port)
{
    ui->setupUi(this);
    ui->hostEdit->setText(m_hostname.c_str());
    ui->portEdit->setText(QString("%1").arg(a_port));
}


ConfigureSMSConnectionDlg::~ConfigureSMSConnectionDlg()
{
    delete ui;
}


void
ConfigureSMSConnectionDlg::accept()
{
    bool conv_ok;

    m_hostname = ui->hostEdit->text().toStdString();

    m_port = ui->portEdit->text().toUShort(&conv_ok);
    if ( !conv_ok )
    {
        QMessageBox::warning( this, "DASMON Message", "Port number contains invalid data." );
        return;
    }

    QDialog::accept();
}
