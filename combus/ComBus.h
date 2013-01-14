#ifndef COMBUS_H
#define COMBUS_H

#include <map>
#include <vector>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <activemq/core/ActiveMQConnection.h>
#include <activemq/library/ActiveMQCPP.h>
#include <cms/Connection.h>
#include <cms/Session.h>
#include <activemq/transport/TransportListener.h>
#include "ComBusMessages.h"



namespace ADARA {
namespace ComBus {

/*
JMS/ActiveMQ Topic Structure

ADARA.LOG.PID - [ouput] information, warning, and error messages from a PID
ADARA.STATUS.PID - [output] status/state of a PID
ADARA.COMMAND.PID - [input] command to a server PID

ADARA.* - monitors ALL ADARA message traffic
ADARA.*.PID - monitors all message traffic to/from a given process
ADARA.LOG.* - monitor all ADARA log messages

PID is a composite string consisting of base process name plus an optional unique
numeric identifier. For example: SMS, PREPROC.2, STS.123, PVSTREAMER. The unique identifier
is determined by processing context and can be either static for configured instances (i.e.
preprocessors), or dynamic for transient processes (i.e. STS uses run number?)

All topics use same message header structure as follows:

string[20] PID      - source process (Topic encodes destination, so isn't needed)
integer msg_type    - (log, status, command)
integer msg_subtype - (info,warn,error,starting,stopping,etc.)

LOG and STATUS message structure:
string msg_text     - human readable description of event/state change

COMMAND messahe structure:
(defined by each specific server process - see SMSComBusCommands.h, PVSComBusCommands.h, etc.)

*/

class Message;
class Command;

class IStatusListener
{
public:
    virtual void    comBusConnectionStatus( bool a_connected ) = 0;
};

class ITopicListener
{
public:
    virtual void    comBusMessage( const Message &a_msg ) = 0;
};

class IControllable
{
public:
    virtual bool    comBusCommand( const Command &a_cmd ) = 0;
};

class Connection
{
public:
    Connection(  const std::string &a_proc_name, unsigned long a_inst_num,
                 const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                 const std::string &a_log_dir = "/tmp", IControllable *a_cmd_handler = 0 );

    ~Connection() throw();

    static Connection&  getInst();
    bool                sendStatus( StatusCode a_status );
    bool                sendLog( const std::string &a_msg, LogLevel a_level, const char *a_file = "", unsigned long a_line = 0, unsigned long a_tid = 0 );
    bool                sendMessage( Message &a_msg );
    //void              sendCommand( Command &a_cmd, bool a_wait_ack = true, unsigned long a_timeout = 0 );
    //void              sendCommandAsync( Command &a_cmd, unsigned long a_timeout = 0 );
    void                attach( IStatusListener  &a_subscriber );
    void                detach( IStatusListener  &a_subscriber );
    void                attach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber );

private:

    class Translator : public cms::MessageListener
    {
    public:
        Translator( ITopicListener &a_listener );
        Translator( IControllable &a_handler );
        virtual ~Translator() throw();

        void                attach( const std::string &a_topic );
        void                detach( const std::string &a_topic );
        inline bool         haveTopics() { return !m_topics.empty(); }

    private:
        void                connect_all();
        void                disconnect_all();
        void                onMessage( const cms::Message *a_msg ) throw();

        ITopicListener     *m_listener;
        IControllable      *m_handler;
        std::map<std::string,std::pair<cms::Topic*,cms::MessageConsumer*> > m_topics;

        friend class Connection;
    };

    void                    reconnectThread();
    void                    connectionStatusNotifyThread();
    void                    disconnect();
    void                    createTopicConsumer( const std::string &a_topic_name, cms::Topic **a_topic, cms::MessageConsumer **a_consumer );

    // Message factory methods/attribs
    static Message*         makeMessage( const cms::Message &a_msg );

    bool                                    m_running;
    bool                                    m_connected;
    std::string                             m_proc_name;
    unsigned long                           m_inst_num;
    Translator                             *m_cmd_translator;
    IControllable                          *m_cmd_handler;
    cms::Destination                       *m_cmd_destination;
    cms::MessageConsumer                   *m_cmd_consumer;
    std::string                             m_broker_uri;
    std::string                             m_broker_user;
    std::string                             m_broker_pass;
    std::string                             m_log_file;
    activemq::core::ActiveMQConnection     *m_connection;
    cms::Session                           *m_session;
    std::vector<IStatusListener*>           m_status_listeners;
    std::map<std::string,std::pair<cms::Topic*,cms::MessageProducer*> > m_producer_topics;
    std::map<ITopicListener*,Translator*>   m_listeners;
    boost::thread                          *m_reconnect_thread;
    boost::thread                          *m_status_thread;
    boost::mutex                            m_status_mutex;
    boost::condition_variable               m_status_cond;
    boost::mutex                            m_mutex;

    static Connection                      *g_inst;

    friend class Translator;
};

} // End ComBus namespace
} // End ADARA namespace

#endif // COMBUS_H

