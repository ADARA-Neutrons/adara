#include <fstream>
#include <activemq/commands/Command.h>
#include <decaf/io/EOFException.h>
#include <boost/thread/locks.hpp>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <time.h>
#include "ComBus.h"
#include "ComBusMessages.h"
#include "STSMessages.h"

using namespace std;

namespace ADARA {
namespace ComBus {

//////////////////////////////////////////////////////////////////////////////
// ComBus Translator Class

Connection::Translator::Translator( ITopicListener &a_listener )
    : m_listener(&a_listener), m_handler(0)
{
}


Connection::Translator::Translator( IControllable &a_handler )
    : m_listener(0), m_handler(&a_handler)
{
}


Connection::Translator::~Translator() throw()
{
    disconnect_all();
}


void
Connection::Translator::onMessage( const cms::Message *a_msg ) throw()
{
    try
    {
        if ( m_listener )
        {
            Message *msg = Connection::makeMessage( *a_msg );

            m_listener->comBusMessage( *msg );

            delete msg;
        }
        else
        {
#if 0
            // TODO Need message factory here
            Command cmd;
            if ( m_handler->onCommand( cmd ))
            {
                // ACK
            }
            else
            {
                // NACK
            }
#endif
        }
    }
    catch(...)
    {
    }
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




//////////////////////////////////////////////////////////////////////////////
// ComBus Connection Class

Connection * Connection::g_inst = 0;


Connection::Connection(  const std::string &a_proc_name, unsigned long a_inst_num,
                         const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass,
                         const std::string &a_log_dir,  IControllable *a_cmd_handler )
  : m_running(true), m_connected(false), m_proc_name(a_proc_name), m_inst_num(a_inst_num),
    m_cmd_translator(0), m_cmd_handler(a_cmd_handler), m_cmd_destination(0), m_cmd_consumer(0),
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
        boost::lexical_cast<unsigned long>( m_broker_uri.substr( pos + 1 ));
    }
    catch ( boost::bad_lexical_cast &e )
    {
        m_broker_uri += string( ":61616" );
    }

    if ( a_inst_num )
        m_proc_name += string(".") + boost::lexical_cast<string>(a_inst_num);

    m_log_file += a_log_dir + "/ComBus.Log." + m_proc_name + ".txt";

    activemq::library::ActiveMQCPP::initializeLibrary();

#if 0
    // If a command handler is specified, this process can receive AMQP commands
    if ( m_cmd_handler )
    {
        // Setup topic for incomming commands
        m_cmd_destination = m_session->createTopic( string("ADARA.CMD.") + m_proc_name );
        m_cmd_consumer = m_session->createConsumer( m_cmd_destination );

        // Connect incomming CMS messages to translator and process cmd handler
        m_cmd_translator = new Translator( *a_cmd_handler );
        m_cmd_consumer->setMessageListener( m_cmd_translator );
    }
#endif

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

    //delete m_cmd_consumer;
    //delete m_cmd_destination;
    //delete m_cmd_translator;

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


void
Connection::reconnectThread()
{
    activemq::core::ActiveMQConnectionFactory factory(m_broker_uri); // = new activemq::core::ActiveMQConnectionFactory( m_broker_uri );
    unsigned short retry_period = 2;
    boost::unique_lock<boost::mutex> lock( m_status_mutex, boost::defer_lock );

    while( 1 )
    {
        lock.lock();

        // Exit if terminating
        if ( !m_running )
            break;

        try
        {
            cout << "retry connect" << endl;
            m_connection = dynamic_cast<activemq::core::ActiveMQConnection*>( factory.createConnection() );
            if ( !m_connection )
                throw std::runtime_error( "Failed to create ActiveMQConnection" );

            m_connection->start();
            m_session = m_connection->createSession( cms::Session::AUTO_ACKNOWLEDGE );

            m_connected = true;
            lock.unlock();

            // Notify connection status thread
            m_status_cond.notify_one();

            // Reconnect all message consumers
            for ( map<ITopicListener*,Translator*>::iterator il = m_listeners.begin(); il != m_listeners.end(); ++il )
            {
                il->second->connect_all();
            }

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
        cout << "chk status" << endl;
        boost::unique_lock<boost::mutex> lock( m_status_mutex );

        // If Connection object is being destroyed, exit
        if ( !m_running )
            break;

        // Notify listeners of any connection status change
        if ( last_connection_state != m_connected )
        {
            boost::lock_guard<boost::mutex> lock(m_mutex);
            for ( vector<IStatusListener*>::iterator l = m_status_listeners.begin(); l != m_status_listeners.end(); ++l )
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

        delete m_session;
        m_session = 0;
        delete m_connection;
        m_connection = 0;

        // Wake up status notify thread
        m_status_cond.notify_one();
    }
}


bool
Connection::sendStatus( StatusCode a_status )
{
    StatusMessage msg(a_status);
    return sendMessage( msg );
}


bool
Connection::sendLog( const std::string &a_msg, LogLevel a_level, const char *a_file, unsigned long a_line, unsigned long a_tid )
{
    bool res = false;

    if ( m_connected )
    {
        LogMessage msg( a_msg, a_level, a_file, a_line, a_tid );
        res = sendMessage( msg );
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


#if 0
void
Connection::sendCommand( Command &a_cmd, bool a_wait_ack = true, unsigned long a_timeout )
{
}


void
Connection::sendCommandAsync( Command &a_cmd, unsigned long a_timeout )
{
}
#endif


bool
Connection::sendMessage( Message &a_msg )
{
    bool res = false;
    cms::Message *cmsmsg = 0;

    if ( m_connected )
    {
        try
        {
            cmsmsg = a_msg.createCMSMessage( *m_session );

            // Set source and timestamp when msg sent
            cmsmsg->setStringProperty( "source", m_proc_name );
            cmsmsg->setIntProperty( "timestamp", time(0) );

            string topic = a_msg.getTopic();
            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator itop = m_producer_topics.find( topic );
            if ( itop == m_producer_topics.end())
            {
                // First message sent on this topic, create producer and put in "cache"
                pair<cms::Topic*,cms::MessageProducer*> p;
                p.first = m_session->createTopic( string("ADARA.") + topic + "." + m_proc_name );
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



Message*
Connection::makeMessage( const cms::Message &a_msg )
{
    Message *msg = 0;
    short msg_type = a_msg.getShortProperty( "type" );

    switch( msg_type )
    {
    case MSG_STATUS:
        msg = new StatusMessage( a_msg );
        break;

    case MSG_LOG:
        msg = new LogMessage( a_msg );
        break;

    case MSG_STATUS_STS_TRANS_COMPLETE:
        msg = new STS::TranslationCompleteMessage( a_msg );
        break;

    default:
        throw std::runtime_error("Unknown message type");
    }

    return msg;
}


void
Connection::attach( IStatusListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    vector<IStatusListener*>::iterator l = find( m_status_listeners.begin(), m_status_listeners.end(), &a_subscriber );
    if ( l == m_status_listeners.end() )
    {
        m_status_listeners.push_back( &a_subscriber );
    }
}


void
Connection::detach( IStatusListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    vector<IStatusListener*>::iterator l = find( m_status_listeners.begin(), m_status_listeners.end(), &a_subscriber );
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
        trans->attach( a_topic );
    }
    else
    {
        l->second->attach( a_topic );
    }
}


void
Connection::detach( ITopicListener &a_listener, const std::string &a_topic )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    map<ITopicListener*,Translator*>::iterator l = m_listeners.find( &a_listener );
    if ( l != m_listeners.end())
    {
        l->second->detach( a_topic );
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
