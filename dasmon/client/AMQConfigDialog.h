#ifndef AMQCONFIGDIALOG_H
#define AMQCONFIGDIALOG_H

#include <QDialog>
#include <string>

namespace Ui {
class AMQConfigDialog;
}

class AMQConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AMQConfigDialog(QWidget *parent = 0);
    AMQConfigDialog( QWidget *parent, const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass );
    ~AMQConfigDialog();

    const std::string &getDomain()
    { return m_domain; }

    const std::string &getBrokerURI()
    { return m_broker_uri; }

    const std::string &getUsername()
    { return m_username; }

    const std::string &getPassword()
    { return m_password; }

private slots:
    void accept();

private:
    Ui::AMQConfigDialog    *ui;
    std::string             m_domain;
    std::string             m_broker_uri;
    std::string             m_username;
    std::string             m_password;
};

#endif // CONFIGDIALOG_H
