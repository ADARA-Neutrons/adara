
#include <fstream>
#include <sstream>
#include <activemq/commands/Command.h>
#include <decaf/io/EOFException.h>
#include <boost/thread/locks.hpp>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <cctype>
#include <string>
#include <time.h>
#include "ComBus.h"
#include "ComBusMessages.h"
#include "ADARAUtils.h"

#if defined(SYSLOG_LOGGING)

#include <syslog.h>

#elif defined(LOGCXX_LOGGING)

#include "../sms/Logging.h"

static LoggerPtr logger(Logger::getLogger("ADARA.ComBus"));

#endif

using namespace std;

namespace ADARA {
namespace ComBus {


///////////////////////////////////////////////////////////////////////////
//================= Translator Class ======================================


/** \brief Translator constructor for Topic mode.
  * \param a_listener - ITopicListener instance
  *
  * This constructor builds a Translator for topic-listener mode (i.e. for
  * instances that wish to subscribe to broadcast topics)
  */
Connection::Translator::Translator( ITopicListener &a_listener )
    : m_listener(&a_listener), m_handler(0)
{
    m_proc_id = Connection::getInst().m_proc_id;
}


/** \brief Translator constructor for Command mode.
  * \param a_handler - IInputListener instance
  *
  * This constructor builds a Translator for command mode (i.e. for
  * instances that wish to subscribe to the command-input topic for
  * the process). There can be only one command handler for a process.
  */
Connection::Translator::Translator( IInputListener &a_handler )
    : m_listener(0), m_handler(&a_handler)
{
    m_proc_id = Connection::getInst().m_proc_id;
}


/** \brief Translator destructor.
  *
  * This destructor disconnects from all topics before destroying the
  * Translator instance.
  */
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
    const cms::TextMessage *txtmsg =
        dynamic_cast<const cms::TextMessage*>(a_msg);

    // ALL ComBus messages are TextMessages - ignore if not
    if ( !txtmsg )
        return;

    try
    {
        MessageBase *msg = Connection::makeMessage( *txtmsg );

        // Generic ComBus messages are always broadcast, so are never
        // directed at a particular process. If filtering is desired,
        // it must be performed by the receiving process (in the
        // comBusMessage callback).
        // Input messages are always directed at a particular process,
        // so no filtering is required.
        if ( m_listener )
            m_listener->comBusMessage( *msg );
        else if ( msg->getMessageCategory()
                == CAT_INPUT && msg->getDestID() == m_proc_id )
        {
            m_handler->comBusInputMessage( *msg );
        }

        delete msg;
    }
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
    }
}


/** \brief Attach translator to a topic
  * \param a_topic - Topic to attach to
  *
  * This method attaches the Translator to the specified topic which will
  * result in messages received from this topic being routed to the
  * listener/handler.
  * For command-mode, only the input topic ([domain].INPUT.[proc_name]
  * should be attached.
  */
void
Connection::Translator::attach( const std::string &a_topic )
{
    try
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
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
    }
}


/** \brief Detach translator from a topic
  * \param a_topic - Topic to detach from
  *
  * This method detaches the Translator from the specified topic.
  */
void
Connection::Translator::detach( const std::string &a_topic )
{
    try
    {
        map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator
            itop = m_topics.find( a_topic );
        if ( itop != m_topics.end())
        {
            delete itop->second.second;
            delete itop->second.first;
            m_topics.erase( itop );
        }
    }
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
    }
}


/** \brief Connects all topics
  *
  * This method connects (or re-connects) all topics associated with the
  * Translator. Topics are connected when first attached (if AMQP broker
  * connection is active), but connection may be lost if broker goes away.
  */
void
Connection::Translator::connect_all()
{
    try
    {
        Connection &conn = ComBus::Connection::getInst();
        map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator
            itop = m_topics.begin();
        for ( ; itop != m_topics.end(); ++itop )
        {
            // If called when still connected, clean-up current connections
            // before proceeding
            if ( itop->second.first )
            {
                delete itop->second.first;
                itop->second.first = 0;
            }
            if ( itop->second.second )
            {
                delete itop->second.second;
                itop->second.second = 0;
            }

            conn.createTopicConsumer( itop->first,
                &itop->second.first, &itop->second.second );
            if ( itop->second.second )
                itop->second.second->setMessageListener( this );
        }
    }
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
    }
}


/** \brief Disconnects all topics
  *
  * This method disconnects all topics associated with the Translator.
  * This method is used when exceptions are encountered while sending or
  * receiving messages and allows recovery by the connection maintenance
  * thread.
  */
void
Connection::Translator::disconnect_all()
{
    try
    {
        map<string,pair<cms::Topic*,cms::MessageConsumer*> >::iterator
            itop = m_topics.begin();
        for ( ; itop != m_topics.end(); ++itop )
        {
            delete itop->second.second;
            itop->second.second = 0;
            delete itop->second.first;
            itop->second.first = 0;
        }
    }
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
    }
}


///////////////////////////////////////////////////////////////////////////
//================= Connection Class ======================================


/// Initializes global Connection instance
Connection * Connection::g_inst = 0;


/** \brief Connection constructor.
  * \param a_domain - AMQP topic prefix
  * \param a_proc_name - Owning process name
  * \param a_proc_inst - Owning process instance number (pid)
  * \param a_broker_uri - AMQP broker URI
  * \param a_user - AMQP broker username
  * \param a_pass - AMQP broker password
  * \param a_log_dir - Directory for back-up log output
  *
  * This constructor builds a(the) Connection instance from the provided
  * parameters. If a Connection instance already exists, the constructor
  * will throw an exception. The domain, broker uri, user name, and
  * password may be changed later using the setConnection() method.
  */
Connection::Connection( std::string &a_domain,
        const std::string &a_proc_name, uint32_t a_proc_inst,
        std::string &a_broker_uri, const std::string &a_user,
        const std::string &a_pass,
        const std::string &a_log_info_prefix,
        const std::string &a_log_err_prefix,
        const std::string &a_log_dir )
    : m_running(true), m_connected(false), m_domain(a_domain),
    m_proc_name(a_proc_name), m_proc_inst(a_proc_inst),
    m_input_listener(0), m_input_translator(0),
    m_broker_uri(a_broker_uri), m_broker_user(a_user),
    m_broker_pass(a_pass),
    m_log_info_prefix(a_log_info_prefix),
    m_log_err_prefix(a_log_err_prefix),
    m_connection(0), m_session(0),
    m_reconnect_thread(0), m_reconn_retry(2), m_status_thread(0)
{
    exceptionLog("ComBus Connection() Entry", INFO_LOG);

    if ( g_inst )
    {
        throw std::runtime_error(
            "Only one instance of Connection class allowed." );
    }

    // Apply default protocol & port if not set
    m_broker_uri = checkBrokerURI( a_broker_uri );

    // If base path is specified, ensure it ends with a '.' character
    m_domain = checkDomain( a_domain );

    m_proc_id += m_proc_name + "_"
        + boost::lexical_cast<string>(m_proc_inst);

    m_log_file += a_log_dir + "/ComBus.Log." + m_proc_id + ".txt";

    try
    {
        exceptionLog(
            "ComBus Connection(): Initializing ActiveMQ Library...",
            INFO_LOG);

        activemq::library::ActiveMQCPP::initializeLibrary();

        exceptionLog( "ComBus Connection(): ActiveMQ Library Initialized.",
            INFO_LOG);
    }
    catch(...)
    {
        exceptionLog("Error Initializing ActiveMQ CPP Library!", ERR_LOG);
    }

    m_status_thread = new boost::thread( boost::bind(
        &Connection::connectionStatusNotifyThread, this ));

    exceptionLog("ComBus Subsystem Activated", INFO_LOG);

    g_inst = this;
}


/** \brief Destructor for Connection class.
  *
  * Since Connection is a singleton, calling this destructor will re-init
  * the global Connection instance to NULL, and will allow another
  * Connection instance to be subsequently created if desired.
  */
Connection::~Connection() throw()
{
    boost::unique_lock<boost::mutex> lock(m_status_mutex);
    m_running = false;
    lock.unlock();

    // Wait for status thread to exit
    m_status_cond.notify_one();
    m_status_thread->join();

    // Wait for reconnect thread to exit
    int i = 0;
    while ( m_reconnect_thread && i++ < 12 )
        sleep(1);

    if ( m_reconnect_thread )
    {
        exceptionLog(
            "Destructor Timed Out Waiting for Reconnect Thread!", ERR_LOG);
    }

    disconnect();

    try
    {
        activemq::library::ActiveMQCPP::shutdownLibrary();
    }
    catch(...)
    {
        exceptionLog("Error Shutting Down ActiveMQ CPP Library!", ERR_LOG);
    }

    exceptionLog("ComBus Subsystem Deactivated", INFO_LOG);

    g_inst = 0;
}


/** \brief Gets the global Connection instance.
  * \return Connection instance pointer
  *
  * This method returns the global Connection instance pointer.
  * If no Connection instance has been created yet, an exception is thrown.
  */
Connection&
Connection::getInst()
{
    if ( !g_inst )
    {
        throw std::runtime_error(
            "No ComBus::Connection instance present.");
    }

    return *g_inst;
}


/** \brief Checks/fixes Broker URI (protocol and port) for Connection.
  * \return Possibly-enhanced/corrected Broker URI string.
  *
  * This (static) method check the Broker URI string for the Connection,
  * ensures it has the proper protocol ("tcp://"), and if omitted,
  * supplies the default ComBus port (":61616").
  */
std::string&
Connection::checkBrokerURI( std::string &a_broker_uri )
{
    // Better Sanitize the Input String, So ComBus/ActiveMQ Doesn't Crash!
    Utils::sanitizeString( a_broker_uri, true /* a_preserve_uri */ );

    // Apply default protocol if not set
    if ( a_broker_uri.find("://") == string::npos )
        a_broker_uri = string("tcp://") + a_broker_uri;

    // Will always return a valid pos b/c of code above
    size_t pos = a_broker_uri.find_last_of(":") + 1;

    // Section Off/Check for Any Potential Port Number Suffix...
    size_t len = 0;
    for ( size_t i=pos ; i < a_broker_uri.length() ; i++ )
    {
        if ( isdigit( a_broker_uri[i] ) )
            len++;
        else
            break;
    }

    // Found Some Digits, Try Parsing Port Number...
    if ( len > 0 )
    {
        try
        {
            boost::lexical_cast<uint32_t>(
                a_broker_uri.substr( pos, len ) );
        }
        catch ( boost::bad_lexical_cast &e )
        {
            // Apply default port if not set
            a_broker_uri += string( ":61616" );
        }
    }
    
    // No Digits, No Port...
    else
    {
        // Apply default port if not set
        a_broker_uri += string( ":61616" );
    }

    return a_broker_uri;
}


/** \brief Checks/fixes Domain for Connection.
  * \return Possibly-enhanced/corrected Domain string.
  *
  * This (static) method check the Domain string for the Connection,
  * and ensures it has the proper trailing '.' character.
  */
std::string&
Connection::checkDomain( std::string &a_domain )
{
    // If base path is specified, ensure it ends with a '.' character
    if ( !a_domain.empty() && *a_domain.rbegin() != '.' )
        a_domain += ".";

    return a_domain;
}


/** \brief Sets (or re-sets) the AMQP connection parameters
  * \param a_domain - New AMQP domain prefix
  * \param a_broker_uri - URI of amqp broker (may specify failover)
  * \param a_user - Optional broker user name
  * \param a_pass - Optional broker password
  *
  * This method allows the AMQP connection parameters to be changed. If a
  * connection is active, it is disconnected and all topics are detached;
  * however, status and control listeners are not detached. A new
  * connection is started asynchronously before this method returns.
  * All status listeners will be notified about the disconnect and
  * subsequent connection events.
  */
void
Connection::setConnection( std::string &a_domain,
        std::string &a_broker_uri, const std::string &a_user,
        const std::string &a_pass )
{
    std::stringstream ss;
    ss << "setConnection(): Setting ComBus/ActiveMQ Connection"
        << " domain=[" << a_domain << "]"
        << " broker_uri=[" << a_broker_uri << "]"
        << " broker_user=[" << a_user << "]"
        << " broker_pass=[" << a_pass << "]";
    exceptionLog(ss.str(), ERR_LOG);

    boost::unique_lock<boost::mutex> lock( m_status_mutex );

    // Drop all general topic subscriptions
    try
    {
        map<ITopicListener*,Translator*>::iterator ilist =
            m_listeners.begin();
        for( ; ilist != m_listeners.end(); ++ilist )
            delete ilist->second;
        m_listeners.clear();
    }
    catch(...)
    {
        exceptionLog(
            "setConnection(): Error Dropping Topic Subscriptions!",
            ERR_LOG);
    }

    m_domain = a_domain;
    m_broker_uri = a_broker_uri;
    m_broker_user = a_user;
    m_broker_pass = a_pass;

    // Apply default protocol & port if not set
    m_broker_uri = checkBrokerURI( m_broker_uri );

    // If base path is specified, ensure it ends with a '.' character
    m_domain = checkDomain( m_domain );

    lock.unlock();

    disconnect();

    // Re-establish control listener
    if ( m_input_listener )
        setInputListener( *m_input_listener );
}


/** \brief Sets the input handler for the process
  * \param a_input_listener - New IInputListener instance for the process
  *
  * This method sets the input (command) listener for the owning process.
  * There can be only one input listener for a process, but it may be
  * changed using this method if needed.
  */
void
Connection::setInputListener( IInputListener &a_input_listener )
{
    try
    {
        if ( m_input_translator )
        {
            delete m_input_translator;
            m_input_translator = 0;
            m_input_listener = 0;
        }

        m_input_listener = &a_input_listener;
        m_input_translator = new Translator( *m_input_listener );
        m_input_translator->attach( m_domain + "INPUT." + m_proc_name );
    }
    catch(...)
    {
        exceptionLog(
            "setInputListener(): Error Attaching Translator!", ERR_LOG);
    }
}


/** \brief Waits for connection to be established.
  * \param a_timeout - Time to wait in seconds
  * \return True if connection was established; false otherwise
  *
  * This method waits and blocks the caller up to the specified timeout
  * (in seconds) for the AMQP connection to be established.
  */
bool
Connection::waitForConnect( unsigned short a_timeout ) const
{
    unsigned short t = a_timeout;
    while ( m_running )
    {
        if ( m_connected )
            return true;

        if ( a_timeout && t-- == 0 )
            return false;

        sleep( 1 );
    }
    return false;
}


/** \brief AMQP connection thread.
  *
  * This method is a (re)connection thread that attempts to establish a new
  * connection to the AMQP broker at a varying period. Initial retry period
  * is 2 seconds (m_reconn_retry), but after each failed attempt,
  * the retry period is increased to a max of 10 seconds.
  * (The retry period can also be set explicitly/externally via the
  * getReconnRetry() and setReconnRetry() methods, e.g. for extended
  * back-off and retry.)
  * Once a connection is established, this thread exits.
  *
  * This thread is started on-demand by the connectionStatusNotifyThread()
  * thread. A condition var (m_status_cond) and m_status_mutex is used
  * by this thread to notify the monitoring thread when the connection
  * is established.
  */
void
Connection::reconnectThread()
{
    exceptionLog("Entry reconnectThread().", INFO_LOG);

    boost::unique_lock<boost::mutex> lock( m_status_mutex,
        boost::defer_lock );
    bool unlocked = true; // because boost can't take a joke... ;-b

    while ( 1 )
    {
        exceptionLog("reconnectThread() - Loop...", INFO_LOG);

        lock.lock();
        unlocked = false;

        // Exit if terminating
        if ( !m_running )
            break;

        try
        {
            activemq::core::ActiveMQConnectionFactory factory(
                m_broker_uri + "?soConnectTimeout=500" );

            m_connection =
                dynamic_cast<activemq::core::ActiveMQConnection*>(
                    factory.createConnection(
                        m_broker_user, m_broker_pass ) );

            if ( !m_connection )
            {
                throw std::runtime_error(
                    "Failed to create ActiveMQConnection" );
            }

            // Unlock _Now_ as Connection Start & Session Create can Hang!
            // Besides, We're Done with the Broker Connection Parameters.
            lock.unlock();
            unlocked = true;

            m_connection->start();
            m_connection->setExceptionListener(this);

            m_session = m_connection->createSession(
                cms::Session::AUTO_ACKNOWLEDGE );

            m_connected = true;

            // Reconnect all message consumers
            for ( map<ITopicListener*,Translator*>::iterator il =
                    m_listeners.begin(); il != m_listeners.end(); ++il )
            {
                il->second->connect_all();
            }

            // Reconnect control listener, if specified
            if ( m_input_listener )
                m_input_translator->connect_all();

            // Notify connection status thread
            m_status_cond.notify_one();

            exceptionLog(
                "reconnectThread(): ActiveMQ Connection Successful.",
                INFO_LOG);

            // Connected! (Lock Already Unlocked...)
            break;
        }
        catch(...)
        {
            std::stringstream ss;
            ss << "reconnectThread(): Error Creating ActiveMQ Connection!"
                << " domain=[" << m_domain << "]"
                << " broker_uri=[" << m_broker_uri << "]"
                << " broker_user=[" << m_broker_user << "]";
            exceptionLog(ss.str(), ERR_LOG);

            try
            {
                // ActiveMQ CPP Session Destructor Broken... ;-Q
                if ( m_session )
                {
                    m_session->close();
                    delete m_session;
                    m_session = 0;
                }

                // ActiveMQ CPP Connection Destructor Broken... ;-Q
                if ( m_connection )
                {
                    m_connection->close();
                    delete m_connection;
                    m_connection = 0;
                }
            }
            catch(...)
            {
                std::stringstream ss;
                ss << "reconnectThread():"
                    << " Exception Freeing ActiveMQ Connection!"
                    << " domain=[" << m_domain << "]"
                    << " broker_uri=[" << m_broker_uri << "]"
                    << " broker_user=[" << m_broker_user << "]";
                exceptionLog(ss.str(), ERR_LOG);

                // We Couldn't Close Connection/Session Gracefully,
                // So Just Clear Them Out... ;-b
                // (Better to Leave Garbage Behind than Die Cleaning Up!)
                m_session = 0;
                m_connection = 0;
            }

            exceptionLog(
                "reconnectThread(): ActiveMQ Error Cleanup Complete.",
                INFO_LOG);

            // Failed to connect
            if ( m_reconn_retry < 10 )
                m_reconn_retry += 2;
        }

        exceptionLog(
            "reconnectThread(): After ActiveMQ Reconnect Attempt.",
            INFO_LOG);

        // _ONLY_ Unlock If Not Already Unlocked...! ;-Q
        if ( !unlocked )
        {
            exceptionLog("reconnectThread(): Unlocking Lock...",
                INFO_LOG);

            lock.unlock();
            unlocked = true;

            exceptionLog("reconnectThread(): After Unlock...",
                INFO_LOG);
        }

        std::stringstream ss;
        ss << "reconnectThread(): Sleeping for"
            << " reconn_retry=" << m_reconn_retry;
        exceptionLog( ss.str(), INFO_LOG );

        // TODO Replace with a timed cond var wait so destructor
        // can interrupt this thread
        // For Now, Wait in 1 Second Sleeps
        // - to allow finer-grained control of Reconn Retry Wait Time
        // - another way to potentially sneak in and interrupt thread,
        // like if we've stopped running and are shutting down... :-D
        uint32_t cnt = 0;
        while ( m_running && cnt++ < m_reconn_retry )
        {
            sleep( 1 );
        }

        std::stringstream ss2;
        ss2 << "reconnectThread(): After Retry Sleep..."
            << " (reconn_retry=" << m_reconn_retry
            << ", cnt=" << cnt << ")";
        exceptionLog( ss2.str(), INFO_LOG );
    }

    exceptionLog( "reconnectThread(): After Reconnect Loop...", INFO_LOG);

    // Notify connection status thread we're giving up...
    m_status_cond.notify_one();

    exceptionLog("Exiting reconnectThread().", INFO_LOG);

    // Self-destruct!
    delete m_reconnect_thread;
    m_reconnect_thread = 0;

    // lock will unlock (as needed) when method exits
}


/** \brief AMQP connection montioring thread.
  *
  * This thread monitors the AMQP connection, notifies listeners on
  * connection state changes, and initiates the reconnectThread when the
  * connection is lost. A condition var (m_status_cond) and m_status_mutex
  * are used by this thread to sleep until the connection is established.
  */
void
Connection::connectionStatusNotifyThread()
{
    exceptionLog("Entry connectionStatusNotifyThread().", INFO_LOG);

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
            string msg =
                "connectionStatusNotifyThread(): Connection Change";

            exceptionLog(msg + " Identified", INFO_LOG);

            boost::lock_guard<boost::mutex> lock(m_mutex);

            try
            {
                for ( vector<IConnectionListener*>::iterator l =
                            m_status_listeners.begin();
                        l != m_status_listeners.end(); ++l )
                {
                    (*l)->comBusConnectionStatus( m_connected );
                }
            }
            catch(...)
            {
                exceptionLog(msg + " - Error Notifying Listeners!",
                    ERR_LOG);
            }

            last_connection_state = m_connected;

            exceptionLog(msg + " - Done Notifying Listeners", INFO_LOG);
        }

        // If not connected, and reconnect thread is not running,
        // start reconnect thread
        if ( !m_connected && !m_reconnect_thread )
        {
            exceptionLog(
            "connectionStatusNotifyThread(): Starting Reconnect Thread...",
                INFO_LOG);

            m_reconnect_thread = new boost::thread( boost::bind(
                &Connection::reconnectThread, this ));
        }

        // Wait for status condition var to be signalled
        m_status_cond.wait( lock );
    }

    exceptionLog("Exiting connectionStatusNotifyThread().", INFO_LOG);
}

/** \brief Disconnects from AMQP broker.
  *
  * This method disconnects from the AMQP broker, cleans-up all topic
  * producers, and requests all Translators to disconnect. A condition var
  * is used to wake the connection monitor thread.
  */
void
Connection::disconnect()
{
    boost::lock_guard<boost::mutex> lock( m_status_mutex );

    if ( m_connected )
    {
        exceptionLog(
            "disconnect(): Disconnecting ComBus/ActiveMQ Connection!",
            INFO_LOG);

        m_connected = false;

        try
        {
            // Disconnect all message producers
            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator
                ip = m_producer_topics.begin();
            for ( ; ip != m_producer_topics.end(); ++ip )
            {
                // ActiveMQ CPP MessageProducer Destructor Broken... ;-Q
                ip->second.second->close();

                delete ip->second.second;
                delete ip->second.first;
            }
            m_producer_topics.clear();

            // Disconnect all message consumers
            for ( map<ITopicListener*,Translator*>::iterator il =
                    m_listeners.begin(); il != m_listeners.end(); ++il )
            {
                il->second->disconnect_all();
            }

            // Disconnect control listener, if specified
            if ( m_input_listener )
                m_input_translator->disconnect_all();

            // ActiveMQ CPP Session Destructor Broken... ;-Q
            if ( m_session )
            {
                m_session->close();
                delete m_session;
                m_session = 0;
            }

            // ActiveMQ CPP Connection Destructor Broken... ;-Q
            if ( m_connection )
            {
                m_connection->close();
                delete m_connection;
                m_connection = 0;
            }
        }
        catch(...)
        {
            exceptionLog(
                "Error Disconnecting Message Producers/Consumers!",
                ERR_LOG);
        }

        exceptionLog(
            "disconnect(): Done Disconnecting ComBus/ActiveMQ Connection",
            INFO_LOG);

        // Wake up status notify thread
        m_status_cond.notify_one();
    }

    else
    {
        exceptionLog(
            "disconnect(): Already Disconnected from ComBus/ActiveMQ...!",
            ERR_LOG);
    }
}


/** \brief Broadcasts status of process
  * \param a_status - Status to broadcast
  *
  * This method may be used by clients to broadcast status from a
  * watchdog thread.
  */
bool
Connection::status( StatusCode a_status )
{
    StatusMessage msg(a_status);
    return broadcast( msg );
}


/// The log API is not currently used...
bool
Connection::log( const std::string &a_msg, Level a_level,
        const char *a_file, uint32_t a_line, uint32_t a_tid )
{
    bool res = false;

    try
    {
        if ( m_connected )
        {
            LogMessage msg( a_msg, a_level, a_file, a_line, a_tid );
            res = broadcast( msg );
        }

        // If combus fails, fallback to log file output
        if ( !res )
        {
            ofstream outf( m_log_file.c_str(),
                ios_base::out | ios_base::app );
            if ( outf.is_open() )
            {
                outf << time(0) << "," << a_level << ",\""
                    << a_msg << "\","
                    << (a_file?a_file:"") << "," << a_line << "," << a_tid
                    << endl;
                outf.close();
            }
        }
    }
    catch(...)
    {
        // TODO - Should probably report exceptions somewhere
        // (Log the Log! ;-D)
    }

    return res;
}


/** \brief Broadcasts a message on inferred topic.
  * \param a_msg - Message to broadcast
  * \return True if message is sent; false otherwise
  *
  * This method broadcasts the provided message on the appropriate topic
  * given the message category.
  */
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
            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator
                itop = m_producer_topics.find( topic );
            if ( itop == m_producer_topics.end())
            {
                // First message sent on this topic,
                // create producer and put in "cache"
                pair<cms::Topic*,cms::MessageProducer*> p;
                p.first = m_session->createTopic(
                    m_domain + topic + "." + m_proc_name );
                p.second = m_session->createProducer( p.first );
                p.second->setDeliveryMode(
                    cms::DeliveryMode::NON_PERSISTENT );
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
            std::stringstream ss;
            ss << "broadcast(): Error Broadcasting Message!"
                << " domain=[" << m_domain << "]"
                << " broker_uri=[" << m_broker_uri << "]"
                << " broker_user=[" << m_broker_user << "]"
                << " topic=[" << a_msg.getTopic() << "]";
            exceptionLog(ss.str(), ERR_LOG);

            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }

    else
    {
        std::stringstream ss;
        ss << "broadcast(): Disconnected! Can't Broadcast Message...!"
            << " domain=[" << m_domain << "]"
            << " broker_uri=[" << m_broker_uri << "]"
            << " broker_user=[" << m_broker_user << "]"
            << " topic=[" << a_msg.getTopic() << "]";
        exceptionLog(ss.str(), ERR_LOG);
    }

    return res;
}


/** \brief Sends a message to a specified destination process
  * \param a_msg - Message to send
  * \param a_dest_proc_id - ID of recipient process
  * \param a_correlation_id - Correlation ID to use (optional)
  * \return True if message is sent; false otherwise
  *
  * This method sends a mesage to the INPUT topic associated with the
  * specified recipient. If the message sent is the first of a
  * "conversation", then the correlation ID should be left empty (null)
  * as it will be filled-in on the message itself when the message is
  * successfully sent. If the message is a continuation of an ongoing
  * conversation, then the correlation ID of that conversation should
  * be passed-in (if not, the recipient will assume this is a new
  * conversation). The life cycle of conversations is application-
  * specific and both parties must implement the same life cycle
  * (i.e. how long correlation IDs and associated state information
  * is maintained).
  */
bool
Connection::send( MessageBase &a_msg, const std::string &a_dest_proc_id,
        const std::string *a_correlation_id )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        size_t pos = a_dest_proc_id.find_last_of('_');
        if ( pos != string::npos )
        {
            try
            {
                a_msg.setRoutingInfo( m_proc_id, a_dest_proc_id, time(0),
                    a_correlation_id );

                cmsmsg = m_session->createTextMessage();
                a_msg.serialize( *cmsmsg );

                string dest_proc_name( a_dest_proc_id.substr( 0, pos ));

                map<string, pair<cms::Topic*, cms::MessageProducer*> >
                    ::iterator itop =
                        m_producer_topics.find( dest_proc_name );
                if ( itop == m_producer_topics.end())
                {
                    // First message sent on this topic,
                    // create producer and put in "cache"
                    pair<cms::Topic*,cms::MessageProducer*> pr;
                    pr.first = m_session->createTopic(
                        m_domain + "INPUT." + dest_proc_name );
                    pr.second = m_session->createProducer( pr.first );
                    pr.second->setDeliveryMode(
                        cms::DeliveryMode::NON_PERSISTENT );
                    m_producer_topics[dest_proc_name] = pr;

                    pr.second->send( cmsmsg );
                }
                else
                {
                    itop->second.second->send( cmsmsg );
                }

                // If the correl ID is requested (not set or empty),
                // use the current message ID (receiver will use same)
                if ( !a_correlation_id || a_correlation_id->empty() )
                    a_msg.setCorrelationID( cmsmsg->getCMSMessageID() );

                delete cmsmsg;
                res = true;
            }
            catch(...)
            {
                std::stringstream ss;
                ss << "send(): Error Sending Message!"
                    << " domain=[" << m_domain << "]"
                    << " broker_uri=[" << m_broker_uri << "]"
                    << " broker_user=[" << m_broker_user << "]";
                exceptionLog(ss.str(), ERR_LOG);

                // An exception indicates a loss of connection
                delete cmsmsg;
                disconnect();
            }
        }
    }

    else
    {
        std::stringstream ss;
        ss << "send(): Disconnected! Can't Send Message...!"
            << " domain=[" << m_domain << "]"
            << " broker_uri=[" << m_broker_uri << "]"
            << " broker_user=[" << m_broker_user << "]";
        exceptionLog(ss.str(), ERR_LOG);
    }

    return res;
}


/** \brief Posts a message to the workflow manager input queue
  * \param a_msg - Message to post
  * \return True if message is posted; false otherwise
  *
  * This method posts the specified message to the workflow manager
  * "POSTPROCESS.DATA_READY" queue.
  */
bool
Connection::postWorkflow( MessageBase &a_msg )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        try
        {
            auto_ptr<cms::Queue> q( m_session->createQueue(
                "POSTPROCESS.DATA_READY" ));

            auto_ptr<cms::MessageProducer> producer(
                m_session->createProducer( q.get()) );

            cmsmsg = m_session->createTextMessage();
            a_msg.serialize( *cmsmsg );

            producer->send( cmsmsg );

            // ActiveMQ CPP MessageProducer Destructor Broken... ;-Q
            producer->close();

            res = true;
        }
        catch(...)
        {
            exceptionLog(
                "postWorkflow(): Error Posting Data Ready Message!",
                ERR_LOG);

            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }

    else
    {
        exceptionLog(
         "postWorkflow(): Disconnected! Can't Post Data Ready Message...!",
            ERR_LOG);
    }

    return res;
}

/** \brief Factory method for ComBus messages based on AMQP text messages.
  * \param a_msg - A received AMQP text message.
  * \return A new ComBus message as a MessageBase pointer
  *
  * This method examines the provided AMQP TextMessage and based on the
  * expected JSON payload, constructs a corresponding ComBus message and
  * unserializes the received payload into the new message. An exception
  * will be thrown if the message payload is not structured correctly.
  */
MessageBase*
Connection::makeMessage( const cms::TextMessage &a_msg )
{
    // Due to constraints imposed by the RESTful interface, the message
    // type must be supplied in the message body. This means that the
    // body must be parsed here first to extract the message type
    // (for object creation).

    MessageBase *msg = 0;

    try
    {
        boost::property_tree::ptree prop_tree;
        std::stringstream sstr( a_msg.getText() );
        read_json( sstr, prop_tree );

        uint32_t msg_type = prop_tree.get( "msg_type", 0UL );

        msg = Factory::Inst().make( (MessageType) msg_type );

        msg->unserialize( prop_tree );

        if ( msg->getCorrelationID().empty() )
            msg->setCorrelationID( a_msg.getCMSMessageID() );
    }
    catch(...)
    {
        ComBus::Connection::getInst().exceptionLog(
            "makeMessage(): Error Making Message!", ERR_LOG);
        std::stringstream sstr( a_msg.getText() );
        ComBus::Connection::getInst().exceptionLog(
            "makeMessage(): a_msg.getText()=[" + sstr.str() + "]",
            ERR_LOG);
    }

    return msg;
}


/** \brief Attaches a connection listener
  * \param a_subscriber - New connection listener to attach
  */
void
Connection::attach( IConnectionListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        vector<IConnectionListener*>::iterator l =
            find( m_status_listeners.begin(), m_status_listeners.end(),
                &a_subscriber );
        if ( l == m_status_listeners.end() )
        {
            m_status_listeners.push_back( &a_subscriber );
            a_subscriber.comBusConnectionStatus( m_connected );
        }
    }
    catch(...)
    {
        exceptionLog(
            "attach(): Error Attaching Connection Listener!", ERR_LOG);
    }
}


/** \brief Detaches a connection listener
  * \param a_subscriber - Connection listener to detach
  */
void
Connection::detach( IConnectionListener  &a_subscriber )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        vector<IConnectionListener*>::iterator l =
            find( m_status_listeners.begin(), m_status_listeners.end(),
                &a_subscriber );
        if ( l != m_status_listeners.end() )
        {
            m_status_listeners.erase( l );
        }
    }
    catch(...)
    {
        exceptionLog(
            "detach(): Error Detaching Connection Listener!", ERR_LOG);
    }
}


/** \brief Attaches a topic to a listener
  * \param a_listener - Listener to attach topic to
  * \param a_topic - New topic to listen to
  *
  * This method attaches the specified topic to the specified listener.
  * Once connected, all messages received on this topic will be routed
  * to the listener via the comBusMessage() callback.
  */
void
Connection::attach( ITopicListener &a_listener,
        const std::string &a_topic )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        map<ITopicListener*,Translator*>::iterator l =
            m_listeners.find(&a_listener);
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
    catch(...)
    {
        exceptionLog(
            "attach(): Error Attaching Topic Listener!", ERR_LOG);
    }
}


/** \brief Detaches a topic from a listener
  * \param a_listener - Listener to detach topic from
  * \param a_topic - Topic to be detached
  *
  * This method detaches the specified topic from the specified listener.
  */
void
Connection::detach( ITopicListener &a_listener,
        const std::string &a_topic )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        map<ITopicListener*,Translator*>::iterator l =
            m_listeners.find( &a_listener );
        if ( l != m_listeners.end())
        {
            // If the listener is attached to topic, ask associated
            // translator to detach from topic.

            l->second->detach( m_domain + a_topic );
            if ( !l->second->haveTopics() )
            {
                // If associated Translator is now empty, delete it.
                delete l->second;
                m_listeners.erase( l );
            }
        }
    }
    catch(...)
    {
        exceptionLog(
            "detach(): Error Detaching Topic Listener!", ERR_LOG);
    }
}


/** \brief Detaches all topics for the specified listener
  * \param a_listener - Listener to detach topics from
  *
  * This method detaches all currently attached topic for the specified
  * listener.
  */
void
Connection::detach( ITopicListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    try
    {
        map<ITopicListener*,Translator*>::iterator l =
            m_listeners.find( &a_listener );
        if ( l != m_listeners.end())
        {
            delete l->second;
            m_listeners.erase( l );
        }
    }
    catch(...)
    {
        exceptionLog(
            "detach(): Error Detaching All Topic Listeners!", ERR_LOG);
    }
}


/** \brief Creates a topic and consumer instance for the given topic.
  * \param a_topic_name - Topic to use
  * \param a_topic - Outputs new cms::Topic pointer
  * \param a_consumer - Outputs new cms::MessageConsumer pointer
  *
  * This is a helper method that tries to create the AMQP topic and
  * consumer objects needed for connecting to a topic. If not connected,
  * no session is established, or an error occurs, the outputs will be
  * null pointers.
  */
void
Connection::createTopicConsumer( const string &a_topic_name,
       cms::Topic **a_topic, cms::MessageConsumer **a_consumer )
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
            exceptionLog(
                "createTopicConsumer(): Error Creating Topic Consumer!",
                ERR_LOG);

            *a_topic = 0;
            *a_consumer = 0;
        }
    }
}


/** \brief Log ComBus Exceptions using Logger of Parent's Choosing.
  * \param a_msg - Log Message
  * \param a_status - Status of Log Message (Info or Error)
  *
  * Using the logger type selected in the Connection constructor
  * (currently either Log4cxx or Syslog), log the given exception
  * message.
  */
void
Connection::exceptionLog( string a_msg, ADARA::ComBus::LogStatus a_status )
{

#if defined(SYSLOG_LOGGING)

    // Syslog Logging...
    if ( a_status == INFO_LOG )
    {
        syslog( LOG_INFO, "%s: %s",
            m_log_info_prefix.c_str(), a_msg.c_str() );
    }

    else if ( a_status == ERR_LOG )
    {
        syslog( LOG_ERR, "%s: %s",
            m_log_err_prefix.c_str(), a_msg.c_str() );
    }

#elif defined(LOGCXX_LOGGING)

    // Log4cxx Logging...
    if ( a_status == INFO_LOG )
    {
        DEBUG(m_log_info_prefix << ": " << a_msg);
    }
    else if ( a_status == ERR_LOG )
    {
        ERROR(m_log_err_prefix << ": " << a_msg);
    }

#else

    // Else NO_LOGGING, Just Ignore...

    // for compiler "unused parameter" warnings... ;-b
    a_msg = a_msg;
    a_status = a_status;

#endif

}


/** \brief ActiveMQ Exception Listener Method.
  * \param ex - CMS Exception to Handle.
  *
  * Don't let the ActiveMQ Library gracefully shut us down on a
  * severe network exception (or any exception for that matter).
  * Just Log & Continue - Rage against the dying of the light...! ;-D
  */
void
Connection::onException( const cms::CMSException &ex )
{
    std::stringstream ss;
    ss << "*** ActiveMQ Exception Listener Called - "
        << ex.getMessage()
        << " [" << ex.getStackTraceString() << "]";
    exceptionLog(ss.str(), ERR_LOG);
}


} // End ComBus namespace
} // End ADARA namespace

// vim: expandtab

