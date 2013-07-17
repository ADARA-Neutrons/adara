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
#include "ComBusDefs.h"


namespace ADARA {
namespace ComBus {

class MessageBase;


/** \brief Interface for ComBus connection status listeners
  *
  */
class IConnectionListener
{
public:
    virtual void    comBusConnectionStatus( bool a_connected ) = 0;
};


/** \brief Interface for ComBus topic message listeners
  *
  */
class ITopicListener
{
public:
    virtual void    comBusMessage( const MessageBase &a_msg ) = 0;
};


/** \brief Interface for ComBus input (command) message listeners
  *
  */
class IInputListener
{
public:
    virtual bool    comBusInputMessage( const MessageBase &a_msg ) = 0;
};


/**
 * /class Connection
 *
 * The ComBus::Connection class provides access to connection management, message ouptut, and
 * topic subscription. Incoming messages are handled by an internal Translator class that then
 * forwards ComBus messages to subscriber objects via the various listener interfaces. There are
 * three distinct types of listener interfaces: status, control, and topic. An arbitrary
 * number of objects may attach to status and general topics, but only one object may be set as
 * the control listener. Internal routing of paired messages (p2p) must currently be implemented
 * by the client control listener object.
 *
 * Connection parameters may be specified in the Connection constructor, or supplied/changed later
 * using the setConnection() method. Note that if the connection parameters are changed, all
 * subscribed topics are dropped and must be re-established. This does not apply if the connection
 * is lost and re-acquired; in this case, all subscriptions are maintained.
 */
class Connection
{
public:
    Connection(  const std::string &a_base_path, const std::string &a_proc_name, unsigned long a_inst_num,
                 const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                 const std::string &a_log_dir = "/tmp" );

    ~Connection() throw();

    static Connection&  getInst();
    void                setConnection( const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass );
    bool                waitForConnect( unsigned short a_timeout ) const;
    void                setInputListener( IInputListener &a_ctrl_listener );
    void                attach( IConnectionListener  &a_subscriber );
    void                detach( IConnectionListener  &a_subscriber );
    void                attach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber );
    bool                status( StatusCode a_status );
    bool                log( const std::string &a_msg, Level a_level, const char *a_file = "", unsigned long a_line = 0, unsigned long a_tid = 0 );
    bool                broadcast( MessageBase &a_msg );
    bool                send( MessageBase &a_msg, const std::string &a_dest_proc, const std::string *a_correlation_id = 0 );

private:

    /** \brief Provides inbound connection/topic message handling
      *
      * The Translator class is a private class that provides handling for inbound
      * AMQ messages. Receieved messages are inspected and translated to ComBus
      * messages, then dispatched via client interfaces.
      */
    class Translator : public cms::MessageListener
    {
    public:
        Translator( ITopicListener &a_listener );
        Translator( IInputListener &a_handler );
        virtual ~Translator() throw();

        void                attach( const std::string &a_topic );
        void                detach( const std::string &a_topic );
        inline bool         haveTopics() { return !m_topics.empty(); }

    private:
        void                connect_all();
        void                disconnect_all();
        void                onMessage( const cms::Message *a_msg ) throw();

        ITopicListener     *m_listener;
        IInputListener     *m_handler;
        std::map<std::string,std::pair<cms::Topic*,cms::MessageConsumer*> > m_topics;

        std::string         m_proc_name;
        unsigned long       m_inst_num;
        friend class Connection;
    };

    void                    reconnectThread();
    void                    connectionStatusNotifyThread();
    void                    disconnect();
    void                    createTopicConsumer( const std::string &a_topic_name, cms::Topic **a_topic, cms::MessageConsumer **a_consumer );

    static MessageBase*     makeMessage( const cms::TextMessage &a_msg );

    bool                                    m_running;
    bool                                    m_connected;
    std::string                             m_domain;
    std::string                             m_proc_name;
    unsigned long                           m_inst_num;
    IInputListener                         *m_input_listener;
    Translator                             *m_input_translator;
    std::string                             m_broker_uri;
    std::string                             m_broker_user;
    std::string                             m_broker_pass;
    std::string                             m_log_file;
    activemq::core::ActiveMQConnection     *m_connection;
    cms::Session                           *m_session;
    std::vector<IConnectionListener*>       m_status_listeners;
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

