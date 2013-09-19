#include <fstream>
#include <activemq/commands/Command.h>
#include <decaf/io/EOFException.h>
#include <boost/thread/locks.hpp>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <time.h>
#include "ComBus.h"
#include "ComBusMessages.h"

using namespace std;

namespace ADARA {
namespace ComBus {


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//================= Translator Class ==================================================================================


Connection::Translator::Translator( ITopicListener &a_listener )
    : m_listener(&a_listener), m_handler(0)
{
    m_proc_id = Connection::getInst().m_proc_id;
}


Connection::Translator::Translator( IInputListener &a_handler )
    : m_listener(0), m_handler(&a_handler)
{
    m_proc_id = Connection::getInst().m_proc_id;
}


Connection::Translator::~Translator() throw()
{
    disconnect_all();
}


/** \param a_msg - Received activemq message
  *
  * This method translates the received message into a ComBus message and
  * routes it to the appropriate interface based on mode of translator
  * (listener or input handler).
  */
void
Connection::Translator::onMessage( const cms::Message *a_msg ) throw()
{
    const cms::TextMessage *txtmsg = dynamic_cast<const cms::TextMessage*>(a_msg);


    // ALL ComBus messages are TextMessages - ignore if not
    if ( !txtmsg )
        return;

    try
    {
        MessageBase *msg = Connection::makeMessage( *txtmsg );

        // Generic ComBus messages are always broadcast, so are never directed
        // at a particular process. If filtering is desired, it must be
        // performed by the receiving process (in the comBusMessage callback).
        // Input messages are always directed at a particular process, so no
        // filtering is required.

        if ( m_listener )
            m_listener->comBusMessage( *msg );
        else if ( msg->getMessageCategory() == CAT_INPUT && msg->getDestID() == m_proc_id )
            m_handler->comBusInputMessage( *msg );

        delete msg;
    }
    catch(...)
    {
    } // TODO - Should probably report exceptions somewhere
}


void
Connection::Translator::attach( const std::string &a_topic )
{
    if ( m_topics.find( a_topic ) == m_topics.end())
    {
        Connection &conn = ComBus::Connection::getInst();
        pair<cms::Topic*,cms::MessageConsumer*> p;

        conn.createTopicConsumer( a_topic, &p.first, &p.second );

        if ( p.second )
            p.second->setMessageListener( this );

        m_topics[a_topic] = p;
    }
}


void
Connection::Translator::detach( const std::string &a_topic )
{
    map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator itop = m_topics.find( a_topic );
    if ( itop != m_topics.end())
    {
        delete itop->second.second;
        delete itop->second.first;
        m_topics.erase( itop );
    }
}


void
Connection::Translator::connect_all()
{
    Connection &conn = ComBus::Connection::getInst();
    map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator itop = m_topics.begin();
    for ( ; itop != m_topics.end(); ++itop )
    {
        conn.createTopicConsumer( itop->first, &itop->second.first, &itop->second.second );
        if ( itop->second.second )
            itop->second.second->setMessageListener( this );
    }
}


void
Connection::Translator::disconnect_all()
{
    map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator itop = m_topics.begin();
    for ( ; itop != m_topics.end(); ++itop )
    {
        delete itop->second.second;
        itop->second.second = 0;
        delete itop->second.first;
        itop->second.first = 0;
    }
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//================= Connection Class ==================================================================================


Connection * Connection::g_inst = 0;


Connection::Connection(  const std::string &a_domain, const std::string &a_proc_name, uint32_t a_proc_inst,
                         const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                         const std::string &a_log_dir )
  : m_running(true), m_connected(false), m_domain(a_domain), m_proc_name(a_proc_name), m_proc_inst(a_proc_inst),
    m_input_listener(0), m_input_translator(0),
    m_broker_uri(a_broker_uri), m_broker_user(a_user), m_broker_pass(a_pass),
    m_connection(0), m_session(0), m_reconnect_thread(0), m_status_thread(0)
{
    if ( g_inst )
        throw std::runtime_error( "Only one instance of Connection class allowed." );

    // Apply default protocol & port if not set
    if ( m_broker_uri.find("://") == string::npos )
        m_broker_uri = string("tcp://") + m_broker_uri;

    size_t pos = m_broker_uri.find_last_of(":"); // Will always return a valid pos b/c of code above
    try
    {
        boost::lexical_cast<uint32_t>( m_broker_uri.substr( pos + 1 ));
    }
    catch ( boost::bad_lexical_cast &e )
    {
        m_broker_uri += string( ":61616" );
    }

    // If base path is specified, ensure it ends with a '.' character
    if ( !m_domain.empty() && *m_domain.rbegin() != '.' )
        m_domain += ".";

    m_proc_id += m_proc_name + "." + boost::lexical_cast<string>(m_proc_inst);

    m_log_file += a_log_dir + "/ComBus.Log." + m_proc_id + ".txt";

    activemq::library::ActiveMQCPP::initializeLibrary();

    m_status_thread = new boost::thread( boost::bind( &Connection::connectionStatusNotifyThread, this ));

    g_inst = this;
}


Connection::~Connection() throw()
{
    boost::unique_lock<boost::mutex> lock(m_status_mutex);
    m_running = false;
    lock.unlock();

    m_status_cond.notify_one();
    m_status_thread->join();

    if ( m_input_translator )
        delete m_input_translator;

    map<ITopicListener*,Translator*>::iterator ilist = m_listeners.begin();
    for( ; ilist != m_listeners.end(); ++ilist )
        delete ilist->second;

    activemq::library::ActiveMQCPP::shutdownLibrary();

    g_inst = 0;
}


Connection&
Connection::getInst()
{
    if ( !g_inst )
        throw std::runtime_error("No ComBus::Connection instance present.");

    return *g_inst;
}


/**
 * /param a_domain - New AMQP domain prefix
 * /param a_broker_uri - URI of amqp broker (may specify failover)
 * /param a_user - Optional broker user name
 * /param a_pass - Optional broker password
 *
 * This method allows the AMQP connection parameters to be changed. If a connection is active, it is
 * disconnected and all topics are detached; however, status and control listeners are not detached.
 * A new connection is started asynchronously before this method returns. All status listeners will
 * be notified about the disconnect and subsequent connection events.
 */
void
Connection::setConnection( const std::string &a_domain, const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass )
{
    boost::unique_lock<boost::mutex> lock( m_status_mutex );

    // Drop all general topic subscriptions
    map<ITopicListener*,Translator*>::iterator ilist = m_listeners.begin();
    for( ; ilist != m_listeners.end(); ++ilist )
        delete ilist->second;
    m_listeners.clear();

    m_domain = a_domain;
    m_broker_uri = a_broker_uri;
    m_broker_user = a_user;
    m_broker_pass = a_pass;

    lock.unlock();

    disconnect();

    // Re-establish control listener
    if ( m_input_listener )
        setInputListener( *m_input_listener );
}


void
Connection::setInputListener( IInputListener &a_ctrl_listener )
{
    if ( m_input_translator )
    {
        delete m_input_translator;
        m_input_translator = 0;
        m_input_listener = 0;
    }

    m_input_listener = &a_ctrl_listener;
    m_input_translator = new Translator( *m_input_listener );
    m_input_translator->attach( m_domain + "INPUT." + m_proc_name );
}


bool
Connection::waitForConnect( unsigned short a_timeout ) const
{
    unsigned short t = a_timeout;
    while ( 1 )
    {
        if ( m_connected )
            return true;

        if ( a_timeout && t-- == 0 )
            return false;

        sleep( 1 );
    }
    return false;
}

void
Connection::reconnectThread()
{
    unsigned short retry_period = 2;
    boost::unique_lock<boost::mutex> lock( m_status_mutex, boost::defer_lock );

    while( 1 )
    {
        lock.lock();

        activemq::core::ActiveMQConnectionFactory factory(m_broker_uri);

        // Exit if terminating
        if ( !m_running )
            break;

        try
        {
            m_connection = dynamic_cast<activemq::core::ActiveMQConnection*>( factory.createConnection( m_broker_user, m_broker_pass ) );
            if ( !m_connection )
                throw std::runtime_error( "Failed to create ActiveMQConnection" );

            m_connection->start();
            m_session = m_connection->createSession( cms::Session::AUTO_ACKNOWLEDGE );

            m_connected = true;
            lock.unlock();

            // Reconnect all message consumers
            for ( map<ITopicListener*,Translator*>::iterator il = m_listeners.begin(); il != m_listeners.end(); ++il )
            {
                il->second->connect_all();
            }

            // Reconnect control listener, if specified
            if ( m_input_listener )
                m_input_translator->connect_all();

            // Notify connection status thread
            m_status_cond.notify_one();

            // Connected! (Retain lock)
            break;
        }
        catch(...)
        {
            delete m_session;
            m_session = 0;
            delete m_connection;
            m_connection = 0;

            // Failed to connect
            if ( retry_period < 10 )
                retry_period += 2;
        }

        lock.unlock();

        // TODO Replace with a timed cond var wait so destructor can interrupt this thread
        sleep( retry_period );
    }

    // Self-destruct!
    delete m_reconnect_thread;
    m_reconnect_thread = 0;

    // lock will unlock when method exits
}


void
Connection::connectionStatusNotifyThread()
{
    bool last_connection_state = false;

    while( 1 )
    {
        boost::unique_lock<boost::mutex> lock( m_status_mutex );

        // If Connection object is being destroyed, exit
        if ( !m_running )
            break;

        // Notify listeners of any connection status change
        if ( last_connection_state != m_connected )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);
            for ( vector<IConnectionListener*>::iterator l = m_status_listeners.begin(); l != m_status_listeners.end(); ++l )
                (*l)->comBusConnectionStatus( m_connected );

            last_connection_state = m_connected;
        }

        // If not connected, and reconnect thread is not running, start reconnect thread
        if ( !m_connected && !m_reconnect_thread )
        {
            m_reconnect_thread = new boost::thread( boost::bind( &Connection::reconnectThread, this ));
        }

        // Wait for status condition var to be signalled
        m_status_cond.wait( lock );
    }
}


void
Connection::disconnect()
{
    boost::lock_guard<boost::mutex> lock( m_status_mutex );
    if ( m_connected )
    {
        m_connected = false;

        // Disconnect all message producers
        map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator ip = m_producer_topics.begin();
        for ( ; ip != m_producer_topics.end(); ++ip )
        {
            delete ip->second.second;
            delete ip->second.first;
        }
        m_producer_topics.clear();

        // Disconnect all message consumers
        for ( map<ITopicListener*,Translator*>::iterator il = m_listeners.begin(); il != m_listeners.end(); ++il )
        {
            il->second->disconnect_all();
        }

        // Disconnect control listener, if specified
        if ( m_input_listener )
            m_input_translator->disconnect_all();

        delete m_session;
        m_session = 0;
        delete m_connection;
        m_connection = 0;

        // Wake up status notify thread
        m_status_cond.notify_one();
    }
}


bool
Connection::status( StatusCode a_status )
{
    StatusMessage msg(a_status);
    return broadcast( msg );
}


bool
Connection::log( const std::string &a_msg, Level a_level, const char *a_file, uint32_t a_line, uint32_t a_tid )
{
    bool res = false;

    if ( m_connected )
    {
        LogMessage msg( a_msg, a_level, a_file, a_line, a_tid );
        res = broadcast( msg );
    }

    // If combus fails, fallback to log file output
    if ( !res )
    {
        ofstream outf( m_log_file.c_str(), ios_base::out | ios_base::app );
        if ( outf.is_open() )
        {
            outf << time(0) << "," << a_level << ",\"" << a_msg << "\"," << (a_file?a_file:"") << "," << a_line << "," << a_tid << endl;
            outf.close();
        }
    }

    return res;
}



bool
Connection::broadcast( MessageBase &a_msg )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        try
        {
            a_msg.setRoutingInfo( m_proc_id, "", time(0) );

            cmsmsg = m_session->createTextMessage();
            a_msg.serialize( *cmsmsg );

            string topic = a_msg.getTopic();
            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator itop = m_producer_topics.find( topic );
            if ( itop == m_producer_topics.end())
            {
                // First message sent on this topic, create producer and put in "cache"
                pair<cms::Topic*,cms::MessageProducer*> p;
                p.first = m_session->createTopic( m_domain + topic + "." + m_proc_name );
                p.second = m_session->createProducer( p.first );
                m_producer_topics[topic] = p;
                p.second->send( cmsmsg );
            }
            else
            {
                itop->second.second->send( cmsmsg );
            }

            delete cmsmsg;
            res = true;
        }
        catch(...)
        {
            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }

    return res;
}


/**
 * @param a_msg - ComBus ControlMessage to send
 * @param a_dest_proc - Name of recipient process
 * @param a_correlation_id - Correlation ID to use (or will be set if empty)
 *
 * This method send a ControlMessage to the CONTROL topic associated with the specified recipient. If the message
 * sent is the first of a "conversation", then the correlation ID should be left empty as it will be filled-in
 * when the message is successfully sent. If the message is a continuation of an ongoing conversation, then the
 * correlation ID of that conversation should be passed-in (if not, the recipient will assum this is a new
 * conversation). The life cycle of conversations is application-specific and both parties must implement the same
 * life cycle (i.e. how long correlation IDs and associated state information is maintained).
 */
bool
Connection::send( MessageBase &a_msg, const std::string &a_dest_proc_id, const std::string *a_correlation_id )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        size_t pos = a_dest_proc_id.find_first_of('.');
        if ( pos != string::npos )
        {
            try
            {
                a_msg.setRoutingInfo( m_proc_id, a_dest_proc_id, time(0), a_correlation_id );

                cmsmsg = m_session->createTextMessage();
                a_msg.serialize( *cmsmsg );

                string dest_proc_name( a_dest_proc_id.substr( 0, pos ));

                map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator itop = m_producer_topics.find( dest_proc_name );
                if ( itop == m_producer_topics.end())
                {
                    // First message sent on this topic, create producer and put in "cache"
                    pair<cms::Topic*,cms::MessageProducer*> pr;
                    pr.first = m_session->createTopic( m_domain + "INPUT." + dest_proc_name );
                    pr.second = m_session->createProducer( pr.first );
                    m_producer_topics[dest_proc_name] = pr;

                    pr.second->send( cmsmsg );
                }
                else
                {
                    itop->second.second->send( cmsmsg );
                }

                // If the correl ID is requested (not set or empty), use the current message ID (receiver will use same)
                if ( !a_correlation_id || a_correlation_id->empty() )
                {
                    a_msg.setCorrelationID( cmsmsg->getCMSMessageID() );
                }

                delete cmsmsg;
                res = true;
            }
            catch(...)
            {
                // An exception indicates a loss of connection
                delete cmsmsg;
                disconnect();
            }
        }
    }

    return res;
}


bool
Connection::postWorkflow( MessageBase &a_msg )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        try
        {
            auto_ptr<cms::Queue>                q( m_session->createQueue( "POSTPROCESS.DATA_READY" ));
            auto_ptr<cms::MessageProducer>      producer( m_session->createProducer( q.get()) );

            cmsmsg = m_session->createTextMessage();
            a_msg.serialize( *cmsmsg );

            producer->send( cmsmsg );

            res = true;
        }
        catch(...)
        {
            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }

    return res;
}


MessageBase*
Connection::makeMessage( const cms::TextMessage &a_msg )
{
    // Due to constraints imposed by the RESTful interface, the message type
    // must be supplied in the message body. This means that the body must be
    // parsed here first to extract the message type (for object creation).

    boost::property_tree::ptree prop_tree;
    std::stringstream sstr( a_msg.getText() );
    read_json( sstr, prop_tree );

    uint32_t msg_type = prop_tree.get( "msg_type", 0UL );

    MessageBase *msg = Factory::Inst().make( (MessageType) msg_type );

    msg->unserialize( prop_tree );

    if ( msg->getCorrelationID().empty() )
        msg->setCorrelationID( a_msg.getCMSMessageID() );

    return msg;
}


void
Connection::attach( IConnectionListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    vector<IConnectionListener*>::iterator l = find( m_status_listeners.begin(), m_status_listeners.end(), &a_subscriber );
    if ( l == m_status_listeners.end() )
    {
        m_status_listeners.push_back( &a_subscriber );
        a_subscriber.comBusConnectionStatus( m_connected );
    }
}


void
Connection::detach( IConnectionListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    vector<IConnectionListener*>::iterator l = find( m_status_listeners.begin(), m_status_listeners.end(), &a_subscriber );
    if ( l != m_status_listeners.end() )
    {
        m_status_listeners.erase( l );
    }
}


void
Connection::attach( ITopicListener &a_listener, const std::string &a_topic )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<ITopicListener*,Translator*>::iterator l = m_listeners.find(&a_listener);
    if ( l == m_listeners.end())
    {
        Translator *trans = new Translator(a_listener);
        m_listeners[&a_listener] = trans;
        trans->attach( m_domain + a_topic );
    }
    else
    {
        l->second->attach( m_domain + a_topic );
    }
}


void
Connection::detach( ITopicListener &a_listener, const std::string &a_topic )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<ITopicListener*,Translator*>::iterator l = m_listeners.find( &a_listener );
    if ( l != m_listeners.end())
    {
        l->second->detach( m_domain + a_topic );
        if ( !l->second->haveTopics() )
        {
            delete l->second;
            m_listeners.erase( l );
        }
    }
}


void
Connection::detach( ITopicListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<ITopicListener*,Translator*>::iterator l = m_listeners.find( &a_listener );
    if ( l != m_listeners.end())
    {
        delete l->second;
        m_listeners.erase( l );
    }
}


void
Connection::createTopicConsumer( const string &a_topic_name, cms::Topic **a_topic, cms::MessageConsumer **a_consumer )
{
    if ( m_connected )
    {
        try
        {
            if ( m_session )
            {
                *a_topic = m_session->createTopic( a_topic_name );
                *a_consumer = m_session->createConsumer( *a_topic );
            }
        }
        catch(...)
        {
            *a_topic = 0;
            *a_consumer = 0;
        }
    }
}


}}
