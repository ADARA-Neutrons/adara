#ifndef CONFIGURESMSCONNECTIONDLG_H
#define CONFIGURESMSCONNECTIONDLG_H

#include <QDialog>

namespace Ui {
class ConfigureSMSConnectionDlg;
}

class ConfigureSMSConnectionDlg : public QDialog
{
    Q_OBJECT
    
public:
//    explicit ConfigureSMSConnectionDlg(QWidget *parent = 0);
    ConfigureSMSConnectionDlg(QWidget *parent, std::string a_hostname, unsigned short a_port );
    ~ConfigureSMSConnectionDlg();
    
    std::string     getHostname() const { return m_hostname; }
    unsigned short  getPort() const { return m_port; }

public slots:
    void accept();

private:
    Ui::ConfigureSMSConnectionDlg *ui;

    std::string     m_hostname;
    unsigned short  m_port;
};

#endif // CONFIGURESMSCONNECTIONDLG_H
