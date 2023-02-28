#ifndef COMBUSTRANSMON_H
#define COMBUSTRANSMON_H

#include "stcdefs.h"
#include "combus/ComBus.h"
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

namespace STC
{
    class StreamParser;
}

class ComBusTransMon
{
public:
    ComBusTransMon();
    ~ComBusTransMon();

    void start( STC::StreamParser &a_stream_parser,
        const std::string &a_broker_uri,
        const std::string &a_broker_user,
        const std::string &a_broker_pass,
        const std::string &a_domain );

    void success( bool a_moved, const std::string &a_nexus_file );

    void failure( STC::TranslationStatusCode a_code,
        const std::string a_reason );

private:
    void commThread();

    STC::StreamParser          *m_stream_parser;
    ADARA::ComBus::Connection  *m_combus;
    std::string                 m_domain;
    std::string                 m_broker_uri;
    std::string                 m_broker_user;
    std::string                 m_broker_pass;
    boost::thread              *m_comm_thread;
    bool                        m_stop;
    bool                        m_send_to_workflow;
    ADARA::ComBus::MessageBase *m_terminal_msg;
    boost::mutex                m_api_mutex;
    std::string                 m_host;
};

#endif // COMBUSTRANSMON_H

// vim: expandtab

