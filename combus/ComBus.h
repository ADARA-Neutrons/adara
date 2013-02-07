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

class MessageBase;
class Command;

class IStatusListener
{
public:
    virtual void    comBusConnectionStatus( bool a_connected ) = 0;
};

class ITopicListener
{
public:
    virtual void    comBusMessage( const MessageBase &a_msg ) = 0;
};

class IControlListener
{
public:
    virtual bool    comBusCommand( const Command &a_cmd ) = 0;
    virtual void    comBusReply( const Reply &a_reply ) = 0;
};

class Connection
{
public:
    Connection(  const std::string &a_proc_name, unsigned long a_inst_num,
                 const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                 const std::string &a_log_dir = "/tmp" );

    ~Connection() throw();

    static Connection&  getInst();
    bool                waitForConnect( unsigned short a_timeout ) const;
    bool                sendStatus( StatusCode a_status );
    bool                sendLog( const std::string &a_msg, Level a_level, const char *a_file = "", unsigned long a_line = 0, unsigned long a_tid = 0 );
    bool                sendMessage( MessageBase &a_msg );
    bool                sendCommand( Command &a_cmd, const std::string &a_dest_proc );
    //bool                sendCommand( Command &a_cmd, const std::string &a_dest_proc, Reply *&a_reply, unsigned long a_timeout = 0 );
    bool                reply( Reply &a_reply, const std::string &a_dest_proc );
    void                setControlListener( IControlListener &a_ctrl_listener );
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
        Translator( IControlListener &a_handler );
        virtual ~Translator() throw();

        void                attach( const std::string &a_topic );
        void                detach( const std::string &a_topic );
        inline bool         haveTopics() { return !m_topics.empty(); }

    private:
        void                connect_all();
        void                disconnect_all();
        void                onMessage( const cms::Message *a_msg ) throw();

        ITopicListener     *m_listener;
        IControlListener   *m_handler;
        std::map<std::string,std::pair<cms::Topic*,cms::MessageConsumer*> > m_topics;

        friend class Connection;
    };

    void                    reconnectThread();
    void                    connectionStatusNotifyThread();
    void                    disconnect();
    void                    createTopicConsumer( const std::string &a_topic_name, cms::Topic **a_topic, cms::MessageConsumer **a_consumer );
    bool                    sendCommandReply( MessageBase &a_msg, const std::string &a_dest_proc, bool a_command );

    // Message factory methods/attribs
    static MessageBase*     makeMessage( const cms::Message &a_msg );

    bool                                    m_running;
    bool                                    m_connected;
    std::string                             m_proc_name;
    unsigned long                           m_inst_num;
    IControlListener                       *m_ctrl_listener;
    Translator                             *m_ctrl_translator;
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

