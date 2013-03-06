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
#include "DASMonMessages.h"

using namespace std;

namespace ADARA {
namespace ComBus {

//////////////////////////////////////////////////////////////////////////////
// ComBus Translator Class

Connection::Translator::Translator( ITopicListener &a_listener )
    : m_listener(&a_listener), m_handler(0)
{
    m_proc_name = Connection::getInst().m_proc_name;
    m_inst_num = Connection::getInst().m_inst_num;
}


Connection::Translator::Translator( IControlListener &a_handler )
    : m_listener(0), m_handler(&a_handler)
{
    m_proc_name = Connection::getInst().m_proc_name;
    m_inst_num = Connection::getInst().m_inst_num;
}


Connection::Translator::~Translator() throw()
{
    disconnect_all();
}


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

        // Filter-out messages this process sent on topics that it is also listening to
        //if ( !m_handler && msg->m_src_name != m_proc_name ) //&& msg->m_src_inst != m_inst_num )
        {
            if ( m_listener )
            {
                m_listener->comBusMessage( *msg );
            }
            else
            {
                if ( msg->getMessageCategory() == CAT_CONTROL)
                {
                    ControlMessage *control = dynamic_cast<ControlMessage*>(msg);

                    // Filter out Control messages sent to other instances
                    //if ( control && control->m_dest_inst == m_inst_num )
                        m_handler->comBusControlMessage( *control );
                }
            }
        }

        delete msg;
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
                         const std::string &a_log_dir )
  : m_running(true), m_connected(false), m_proc_name(a_proc_name), m_inst_num(a_inst_num),
    m_ctrl_listener(0), m_ctrl_translator(0),
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

    //if ( a_inst_num )
    m_proc_name += string(".") + boost::lexical_cast<string>(a_inst_num);

    m_log_file += a_log_dir + "/ComBus.Log." + m_proc_name + "." + boost::lexical_cast<string>(a_inst_num) + ".txt";

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

    if ( m_ctrl_translator )
        delete m_ctrl_translator;

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
Connection::setBroker( const std::string &a_broker_uri, const std::string &a_user, const std::string &a_pass )
{
    boost::unique_lock<boost::mutex> lock( m_status_mutex );

    m_broker_uri = a_broker_uri;
    m_broker_user = a_user;
    m_broker_pass = a_pass;

    lock.unlock();

    disconnect();
}


void
Connection::setControlListener( IControlListener &a_ctrl_listener )
{
    if ( m_ctrl_translator )
    {
        delete m_ctrl_translator;
        m_ctrl_translator = 0;
        m_ctrl_listener = 0;
    }

    m_ctrl_listener = &a_ctrl_listener;
    m_ctrl_translator = new Translator( *m_ctrl_listener );
    m_ctrl_translator->attach( string("ADARA.CONTROL.") + m_proc_name );
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
            //cout << "retry connect" << endl;
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

            // Notify connection status thread
            m_status_cond.notify_one();

            // Reconnect control listener, if specified
            if ( m_ctrl_listener )
                m_ctrl_translator->connect_all();

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
        //cout << "chk status" << endl;
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

        // Disconnect control listener, if specified
        if ( m_ctrl_listener )
            m_ctrl_translator->disconnect_all();

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
Connection::sendLog( const std::string &a_msg, Level a_level, const char *a_file, unsigned long a_line, unsigned long a_tid )
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



bool
Connection::sendMessage( MessageBase &a_msg )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        try
        {
            // Set source and timestamp when msg sent
            a_msg.setSourceInfo( m_proc_name ); //, m_inst_num );
            a_msg.setTimestamp( time(0) );

            //cmsmsg = a_msg.createCMSMessage( *m_session );
            cmsmsg = m_session->createTextMessage();
            a_msg.serialize( *cmsmsg );

            string topic = a_msg.getTopic();
            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator itop = m_producer_topics.find( topic );
            if ( itop == m_producer_topics.end())
            {
                // First message sent on this topic, create producer and put in "cache"
                pair<cms::Topic*,cms::MessageProducer*> p;
                p.first = m_session->createTopic( string("ADARA.") + topic + "." + m_proc_name );
                p.second = m_session->createProducer( p.first );
                m_producer_topics[topic] = p;
                //cout << "send: ADARA." << topic << "." << m_proc_name  << ", ty: " << a_msg.getMessageType() << endl;
                p.second->send( cmsmsg );
            }
            else
            {
                //cout << "--> send: ADARA." << topic << "." << m_proc_name << ", ty: " << a_msg.getMessageType() << endl;
                itop->second.second->send( cmsmsg );
            }

            delete cmsmsg;
            res = true;
        }
        catch(...)
        {
            cout << "send failed - exception." << endl;
            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }
    else
    {
        cout << "send failed - not connected." << endl;
    }

    return res;
}


/**
 * @param a_msg - ComBus ControlMessage to send
 * @param a_dest_proc - Name of recipient process
 * @param a_dest_inst - Instance ID of recipient process
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
Connection::sendControl( ControlMessage &a_msg, const std::string &a_dest_proc /*, unsigned long a_dest_inst*/, std::string &a_correlation_id  )
{
    bool res = false;

    if ( m_connected )
    {
        cms::TextMessage *cmsmsg = 0;

        try
        {
            a_msg.setSourceInfo( m_proc_name ); //, m_inst_num );
            a_msg.setDestInfo( /*a_dest_inst,*/ a_correlation_id );
            a_msg.setTimestamp( time(0) );

            //string full_dest = a_dest_proc + "." + boost::lexical_cast<string>(a_dest_inst);

            //cout << "Send Topic = " << "ADARA.CONTROL." << full_dest << endl;

            //cmsmsg = a_msg.createCMSMessage( *m_session );
            cmsmsg = m_session->createTextMessage();
            a_msg.serialize( *cmsmsg );

            map<string,pair<cms::Topic*,cms::MessageProducer*> >::iterator itop = m_producer_topics.find( a_dest_proc );
            if ( itop == m_producer_topics.end())
            {
                // First message sent on this topic, create producer and put in "cache"
                pair<cms::Topic*,cms::MessageProducer*> p;
                p.first = m_session->createTopic( string("ADARA.CONTROL.") + a_dest_proc );
                p.second = m_session->createProducer( p.first );
                m_producer_topics[a_dest_proc] = p;

                p.second->send( cmsmsg );
            }
            else
            {
                itop->second.second->send( cmsmsg );
            }

            // If the correl ID is not set, use the current message ID (receiver will use same)
            if ( a_correlation_id.empty() )
            {
                a_correlation_id = cmsmsg->getCMSMessageID();
                cout << "New msg assigned CID = " << a_correlation_id << endl;
            }

            delete cmsmsg;
            res = true;
        }
        catch(...)
        {
            cout << "send failed - exception." << endl;
            // An exception indicates a loss of connection
            delete cmsmsg;
            disconnect();
        }
    }
    else
    {
        cout << "send failed - not connected." << endl;
    }

    return res;
}


MessageBase*
Connection::makeMessage( const cms::TextMessage &a_msg )
{
    unsigned long msg_type = a_msg.getIntProperty( "type" );
    MessageBase *msg = 0;

    switch( msg_type )
    {
    case MSG_LOG:                   msg = new LogMessage(); break;
    case MSG_STATUS:                msg = new StatusMessage(); break;
    case MSG_SIGNAL_ASSERT:         msg = new SignalAssertMessage(); break;
    case MSG_SIGNAL_RETRACT:        msg = new SignalRetractMessage(); break;
    case MSG_CMD_EMIT_STATUS:       msg = new EmitStatusCommand(); break;
    case MSG_CMD_EMIT_STATE:        msg = new EmitStateCommand(); break;
    case MSG_REPLY_ACK:             msg = new AckReply(); break;
    case MSG_REPLY_NACK:            msg = new NackReply(); break;
    case MSG_STS_TRANS_COMPLETE:    msg = new STS::TranslationCompleteMessage(); break;
    case MSG_DASMON_SMS_CONN_STATUS:msg = new DASMON::ConnectionStatusMessage(); break;
    case MSG_DASMON_RUN_STATUS:     msg = new DASMON::RunStatusMessage(); break;
    case MSG_DASMON_PAUSE_STATUS:   msg = new DASMON::PauseStatusMessage(); break;
    case MSG_DASMON_SCAN_STATUS:    msg = new DASMON::ScanStatusMessage(); break;
    case MSG_DASMON_BEAM_INFO:      msg = new DASMON::BeamInfoMessage(); break;
    case MSG_DASMON_RUN_INFO:       msg = new DASMON::RunInfoMessage(); break;
    case MSG_DASMON_BEAM_METRICS:   msg = new DASMON::BeamMetricsMessage(); break;
    case MSG_DASMON_RUN_METRICS:    msg = new DASMON::RunMetricsMessage(); break;

    case MSG_DASMON_RULE_DEFINITIONS:msg = new DASMON::RuleDefinitions(); break;
    case MSG_DASMON_GET_RULES:      msg = new DASMON::GetRuleDefinitions(); break;
    case MSG_DASMON_SET_RULES:      msg = new DASMON::SetRuleDefinitions(); break;
    case MSG_DASMON_GET_INPUT_FACTS:msg = new DASMON::GetInputFacts(); break;
    case MSG_DASMON_INPUT_FACTS:    msg = new DASMON::InputFacts(); break;
    default:
        throw std::runtime_error("Unknown message type");
    }

    msg->unserialize( a_msg );

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
        a_subscriber.comBusConnectionStatus( m_connected );
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
