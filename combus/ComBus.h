#ifndef COMBUS_H
#define COMBUS_H

#pragma GCC diagnostic ignored "-Woverloaded-virtual"

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
    virtual void    comBusInputMessage( const MessageBase &a_msg ) = 0;
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
 * by the client control listener object (see dasmon-gui rule config dialog for an example).
 *
 * Connection parameters may be specified in the Connection constructor, or supplied/changed later
 * using the setConnection() method. Note that if the connection parameters are changed, all
 * subscribed topics are dropped and must be re-established. This does not apply if the connection
 * is lost and re-acquired; in this case, all subscriptions are automatically reconnected when
 * the connection is re-established.
 */
class Connection
{
public:
    Connection(  const std::string &a_base_path, const std::string &a_proc_name, uint32_t a_inst_num,
                 const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                 const std::string &a_log_dir = "/tmp" );

    ~Connection() throw();

    static Connection&  getInst();
    void                setConnection( const std::string &a_domain, const std::string &a_broker_uri,
                                       const std::string &a_user, const std::string &a_pass );
    bool                waitForConnect( unsigned short a_timeout ) const;
    void                setInputListener( IInputListener &a_ctrl_listener );
    void                attach( IConnectionListener  &a_subscriber );
    void                detach( IConnectionListener  &a_subscriber );
    void                attach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber, const std::string &a_topic );
    void                detach( ITopicListener &a_subscriber );
    bool                status( StatusCode a_status );
    bool                log( const std::string &a_msg, Level a_level, const char *a_file = "",
                             uint32_t a_line = 0, uint32_t a_tid = 0 );
    bool                broadcast( MessageBase &a_msg );
    bool                send( MessageBase &a_msg, const std::string &a_dest_proc,
                              const std::string *a_correlation_id = 0 );
    bool                postWorkflow( MessageBase &a_msg );

private:

    typedef std::map<std::string,std::pair<cms::Topic*,cms::MessageProducer*> > ProducerMap;
    typedef std::map<std::string,std::pair<cms::Topic*,cms::MessageConsumer*> > ConsumerMap;

    /** \brief Provides inbound connection/topic message handling
      *
      * The Translator class is a private class that provides handling for inbound
      * AMQ messages on a per-listener basis. Receieved messages are inspected and
      * translated to ComBus messages, then dispatched via client interfaces. One
      * Translator instance manages all topic sucbscrpiptions for a given client/
      * listener; however, only one topic should be attached for input handlers
      * (the [domain].INPUT.[proc_name] topic); otherwise, non-input messages
      * would be routed to the input/comand handler of the client.
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
        void                connect_all();
        void                disconnect_all();

    private:
        void                onMessage( const cms::Message *a_msg ) throw();

        ITopicListener     *m_listener; ///< Topic listener instance (for topic mode)
        IInputListener     *m_handler;  ///< Input listener instance (for command mode)
        ConsumerMap         m_topics;   ///< AMQP topic-consumer map
        std::string         m_proc_id;  ///< Proc ID of associated listener/handler
    };

    void                    reconnectThread();
    void                    connectionStatusNotifyThread();
    void                    disconnect();
    void                    createTopicConsumer( const std::string &a_topic_name, cms::Topic **a_topic,
                                                 cms::MessageConsumer **a_consumer );
    static MessageBase*     makeMessage( const cms::TextMessage &a_msg );

    bool                                    m_running;          ///< Flag indicating ComBus lib is active/running
    bool                                    m_connected;        ///< Flag indicating ComBus is connected
    std::string                             m_domain;           ///< Domain prefix for topics
    std::string                             m_proc_name;        ///< Name of owning process
    std::string                             m_proc_id;          ///< Identifier of owning process (name.pid)
    uint32_t                                m_proc_inst;        ///< Instance number (pid) of owning process
    IInputListener                         *m_input_listener;   ///< Object to receive command inputs
    Translator                             *m_input_translator; ///< Translator dedicated to command inputs
    std::string                             m_broker_uri;       ///< AMQP broker URI
    std::string                             m_broker_user;      ///< AMQP broker user name
    std::string                             m_broker_pass;      ///< AMQP broker password
    std::string                             m_log_file;         ///< Alternative log ouput file
    activemq::core::ActiveMQConnection     *m_connection;       ///< AMQP connction instance
    cms::Session                           *m_session;          ///< AMQP session instance
    std::vector<IConnectionListener*>       m_status_listeners; ///< Connection status listeners
    ProducerMap                             m_producer_topics;  ///< AMQP topic-producer map
    std::map<ITopicListener*,Translator*>   m_listeners;        ///< Topic listeners (subscribers)
    boost::thread                          *m_reconnect_thread; ///< AMQP connection maintenance thread
    boost::thread                          *m_status_thread;    ///< Status / watchdog thread
    boost::mutex                            m_status_mutex;     ///< Connection status/maint mutex
    boost::condition_variable               m_status_cond;      ///< Connection status cond var
    boost::mutex                            m_mutex;            ///< Mutex for listeners
    static Connection                      *g_inst;             ///< Global Connection instance
};

} // End ComBus namespace
} // End ADARA namespace

#endif // COMBUS_H

