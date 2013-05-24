#include "afxinet.h"
#include <string>

// For now this enum must be manually synched with the one in ComBusMessages.h
enum ADARA_STATUS
{
    STATUS_OK = 0,
    STATUS_FAULT,
    STATUS_UNRESPONSIVE,
    STATUS_INACTIVE
};


class ComBusLite
{
public:
    ComBusLite( const std::string &a_topic_path, const std::string &a_proc_name, unsigned long a_proc_id,
        const std::string &a_broker_host, unsigned short a_port, const std::string &a_user, const std::string &a_pass );
    ~ComBusLite();

    bool sendStatus( ADARA_STATUS a_status );

private:
    std::string         m_proc_name;
    unsigned long       m_proc_id;
    std::string         m_broker_host;
    unsigned short      m_port;
    std::string         m_broker_user;
    std::string         m_broker_pass;
    std::string         m_topic;
    CInternetSession    m_session;
    CHttpConnection    *m_conn;
};