#ifndef COMBUSMESSAGES_H
#define COMBUSMESSAGES_H

#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include "ADARADefs.h"

//#include <activemq/core/ActiveMQConnectionFactory.h>
//#include <activemq/core/ActiveMQConnection.h>
//#include <activemq/library/ActiveMQCPP.h>
//#include <cms/Connection.h>
//#include <cms/Session.h>

/*
Message Hierarchy maps to topics on ComBus.

All
  header: source, time, msg type
Log [out]
  log: level, msg, location
Status [out]
  update: state
Signals [out]
  assert: name, id, message
  retract: id
Control [in]
  command: destination, id
  response: id, ack/nack
*/

namespace ADARA {
namespace ComBus {

/**
 * Message categories map directly to JMS/AMQP topics and are used internally
 * by the ComBus library.
 */
enum MessageCategory
{
    CAT_LOG     = 0x01000000,
    CAT_STATUS  = 0x02000000,
    CAT_SIGNAL  = 0x03000000,
    CAT_CONTROL = 0x04000000,
    CAT_APP     = 0x05000000
};


enum AppCategory
{
    APP_NONE        = 0x000000,
    APP_SAS         = 0x010000,
    APP_SMS         = 0x020000,
    APP_STS         = 0x030000,
    APP_SRS         = 0x040000,
    APP_WFM         = 0x050000,
    APP_CATALOG     = 0x060000,
    APP_AUTORED     = 0x070000,
    APP_LOGGER      = 0x080000,
    APP_SCANSERV    = 0x090000,
    APP_CONTROL     = 0x0A0000,
    APP_IOC         = 0x0B0000,
    APP_DASMON      = 0x0C0000
};

//#define CTRL_REPLY_FLAG 0x800000

/**
 * Message types are used to uniquely identify a message class and indirectly
 * identify the category (topic) for a message. These values are used by the
 * message factory to register (un)serialization methods for each message class.
 */
enum MessageType
{
    MSG_LOG                     = CAT_LOG,
    MSG_STATUS                  = CAT_STATUS,
    MSG_SIGNAL_ASSERT           = CAT_SIGNAL,
    MSG_SIGNAL_RETRACT,
    MSG_SIGNAL_EVENT,
    MSG_CMD_EMIT_STATUS         = CAT_CONTROL,
    MSG_CMD_EMIT_STATE,
    MSG_CMD_REINIT,
    MSG_CMD_SHUTDOWN,
    MSG_CMD_CONFIG_LOGGING,
    MSG_REPLY_ACK,               //= CAT_CONTROL | CTRL_REPLY_FLAG,
    MSG_REPLY_NACK,
    MSG_DASMON_GET_RULES        = CAT_CONTROL | APP_DASMON,
    MSG_DASMON_SET_RULES,
    MSG_DASMON_RULE_DEFINITIONS,
    MSG_DASMON_GET_INPUT_FACTS,
    MSG_DASMON_INPUT_FACTS,
    MSG_STS_TRANS_COMPLETE      = CAT_APP | APP_STS,
    MSG_DASMON_SMS_CONN_STATUS  = CAT_APP | APP_DASMON,
    MSG_DASMON_RUN_STATUS,
    MSG_DASMON_PAUSE_STATUS,
    MSG_DASMON_SCAN_STATUS,
    MSG_DASMON_BEAM_INFO,
    MSG_DASMON_RUN_INFO,
    MSG_DASMON_BEAM_METRICS,
    MSG_DASMON_RUN_METRICS
};

/**
 * Status codes are emitted by all required ADARA processes to enable health
 * monitoring via the ComBus. Status must be emitted periodically even if status
 * is unchanged such that hung/non-responsive/aborted processes can be detected.
 * The fault state is used to indaicte an error that prevents correct operation
 * of the process (a process may autorestart when a fault is detected).
 */
enum StatusCode
{
    STATUS_UNRESPONSIVE = 0,
    STATUS_STARTING,
    STATUS_RUNNING,
    STATUS_STOPPING,
    STATUS_FAULT
};


class ComBusHelper
{
public:

    static const char* toText( StatusCode a_code )
    {
        switch( a_code )
        {
        case STATUS_UNRESPONSIVE: return "Unresponsive";
        case STATUS_STARTING: return "Starting";
        case STATUS_RUNNING: return "Running";
        case STATUS_STOPPING: return "Stopping";
        case STATUS_FAULT: return "Fault";
        default: return "Unknown";
        }
    }

    static const char* toText( Level a_level )
    {
        switch( a_level )
        {
        case TRACE: return "TRACE";
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARNING";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default: return "UNKNOWN";
        }
    }

    static Level toLevel( std::string a_level )
    {
        if ( boost::iequals( a_level, "TRACE" ))
            return TRACE;
        else if ( boost::iequals( a_level, "DEBUG" ))
            return DEBUG;
        else if ( boost::iequals( a_level, "INFORMATION" ))
            return INFO;
        else if ( boost::iequals( a_level, "INFO" ))
            return INFO;
        else if ( boost::iequals( a_level, "WARNING" ))
            return WARN;
        else if ( boost::iequals( a_level, "WARN" ))
            return WARN;
        else if ( boost::iequals( a_level, "ERROR" ))
            return ERROR;
        else if ( boost::iequals( a_level, "FATAL" ))
            return FATAL;

        throw std::runtime_error( "Invalid level" );
    }
};


class MessageBase
{
public:
    MessageBase()
        : m_timestamp(0)
//        : m_src_inst(0), m_timestamp(0)
    { }

    virtual ~MessageBase()
    {}

    virtual void serialize( cms::TextMessage &a_msg )
    {
        // Type must not be in payload - used by factory method
        a_msg.setIntProperty( "type", (long) getMessageType() );

        boost::property_tree::ptree prop_tree;

        write( prop_tree );

        std::stringstream sstr;
        write_json( sstr, prop_tree );

        a_msg.setText( sstr.str() );
    }

    virtual void unserialize( const cms::TextMessage &a_msg )
    {
        boost::property_tree::ptree prop_tree;

        std::stringstream sstr( a_msg.getText() );
        read_json( sstr, prop_tree );

        read( prop_tree );
    }

    /*
    cms::Message *createCMSMessage( cms::Session &a_session )
    {
        boost::property_tree::ptree prop_tree;

        cms::Message *msg = a_session.createMessage();
        // Type must not be in payload - used by factory method
        msg.setIntProperty( "type", (long) getMessageType() );
        // Translate message content
        write( prop_tree );
        //translateTo( *msg );
        return msg;
    }
*/
    virtual MessageType getMessageType() const = 0;

    inline MessageCategory getMessageCategory() const
    { return (MessageCategory)(getMessageType() & 0xFF000000 ); }

    inline AppCategory getAppCategory() const
    { return (AppCategory)(getMessageType() & 0xFF0000 ); }

    std::string getTopic() const
    {
        switch( getMessageCategory())
        {
        case CAT_LOG: return "LOG";
        case CAT_SIGNAL: return "SIGNAL";
        case CAT_STATUS: return "STATUS";
        case CAT_CONTROL: return "CONTROL";
        case CAT_APP: return "APP";
        }
        throw std::runtime_error("bad message category");
    }

    inline const std::string &getSourceName() const
    { return m_src_name; }

//    inline unsigned long getSourceInstance() const
//    { return m_src_inst; }

    inline unsigned long getTimestamp() const
    { return m_timestamp; }

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        m_src_name = a_prop_tree.get( "src_name", "" );
//        m_src_inst = a_prop_tree.get( "src_inst", 0 );
        m_timestamp = a_prop_tree.get( "timestamp", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        a_prop_tree.put( "src_name", m_src_name );
//        a_prop_tree.put( "src_inst", m_src_inst );
        a_prop_tree.put( "timestamp", m_timestamp );
    }
/*
    virtual void translateTo( cms::Message &a_msg )
    {
        a_msg.setIntProperty( "type", (long) getMessageType() );
        a_msg.setStringProperty( "src_name", m_src_name );
        a_msg.setIntProperty( "src_inst", (long) m_src_inst );
        a_msg.setIntProperty( "timestamp", m_timestamp );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_src_name = a_msg.getStringProperty( "src_name" );
        m_src_inst = (unsigned long)a_msg.getIntProperty( "src_inst" );
        m_timestamp = (unsigned long)a_msg.getIntProperty( "timestamp" );
    }
*/

private:
    void setSourceInfo( const std::string &a_src_name ) //, unsigned long a_src_inst )
    {
        m_src_name = a_src_name;
//        m_src_inst = a_src_inst;
    }

    void setTimestamp( unsigned long a_timestamp )
    {
        m_timestamp = a_timestamp;
    }

    std::string         m_src_name;
//    unsigned long       m_src_inst;
    unsigned long       m_timestamp;

    // Connection class sets source, instance, and timestamp when message is sent
    friend class Connection;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ControlMessage Class

/**
 * @class ControlMessage
 *
 * The ControlMessage class is a type of message intended for perr-to-peer communication over the CONTROL topic
 * of the ComBus. The primary specialization of this class is the inclusion of destination information (to
 * identify a specific ComBus process) and a unique session ID that can be used to (re)associate messages within
 * a "conversation". There is no automatic reply mechanism as "conversations" could invlobe an arbitrary number of
 * related exchanges (i.e. handshaking). The requirements for a conversation are application specific. The ComBus
 * class does provide an API for sending and receiving basic commands and replies.
 */
class ControlMessage : public MessageBase
{
public:
    ControlMessage()
//        : m_dest_inst(0)
    {}

    std::string         m_correlation_id;   ///< Key identifier used to (re)associate messages
//    unsigned long       m_dest_inst;        ///< Destination instance (dest name is implied by topic)

    virtual void unserialize( const cms::TextMessage &a_msg )
    {
        MessageBase::unserialize( a_msg );

        if ( m_correlation_id.empty() )
        {
            m_correlation_id = a_msg.getCMSMessageID();
            std::cout << "Rcv new msg, assigned CID from MID = " << m_correlation_id << std::endl;
        }
    }

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_correlation_id = a_prop_tree.get( "correl_id", "" );
//        m_dest_inst = a_prop_tree.get( "dest_inst", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "correl_id", m_correlation_id );
//        a_prop_tree.put( "dest_inst", m_dest_inst );
    }
/*
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );

        a_msg.setStringProperty( "correl_id", m_correlation_id );
        a_msg.setIntProperty( "dest_inst", (long)m_dest_inst );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_correlation_id = a_msg.getIntProperty( "correl_id" );
        if ( m_correlation_id.empty() )
            m_correlation_id = a_msg.getCMSMessageID();

        m_dest_inst = (unsigned long)a_msg.getIntProperty( "dest_inst" );
    }
*/

private:
//    void setDestInfo( unsigned long a_dest_inst, const std::string &a_correlation_id = "" )
    void setDestInfo( const std::string &a_correlation_id )
    {
        //m_dest_inst = a_dest_inst;
        m_correlation_id = a_correlation_id;
    }


    // Connection class sets cmd_id when message is sent
    friend class Connection;
};


// ---------------------------- Common Commands / Replies ---------------------------

#define DEF_SIMPLE_CMD(name,type) \
    class name : public ControlMessage { \
    public: \
        name() {} \
        inline MessageType getMessageType() const \
        { return type; } };

DEF_SIMPLE_CMD(EmitStatusCommand,MSG_CMD_EMIT_STATUS)
DEF_SIMPLE_CMD(EmitStateCommand,MSG_CMD_EMIT_STATE)
DEF_SIMPLE_CMD(ReinitCommand,MSG_CMD_REINIT)
DEF_SIMPLE_CMD(ShutdownCommand,MSG_CMD_SHUTDOWN)
DEF_SIMPLE_CMD(AckReply,MSG_REPLY_ACK)
DEF_SIMPLE_CMD(NackReply,MSG_REPLY_NACK)


class ConfigureLoggingCommand: public ControlMessage
{
public:
    ConfigureLoggingCommand( bool a_enabled, Level a_level )
        : m_enabled(a_enabled), m_level(a_level)
    {}

    inline MessageType getMessageType() const
    { return MSG_CMD_CONFIG_LOGGING; }

    bool    m_enabled;
    Level   m_level;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::read( a_prop_tree );

        m_enabled = a_prop_tree.get( "log_enabled", false );
        m_level = (ADARA::Level) a_prop_tree.get( "log_level", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::write( a_prop_tree );

        a_prop_tree.put( "log_enabled", m_enabled );
        a_prop_tree.put( "log_level", (unsigned short)m_level );
    }

    /*
    virtual void translateTo( cms::Message &a_msg )
    {
        ControlMessage::translateTo( a_msg );

        a_msg.setBooleanProperty( "log_enabled", m_enabled );
        a_msg.setShortProperty( "log_level", (short)m_level );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_enabled = a_msg.getBooleanProperty( "log_enabled" );
        m_level = (Level)a_msg.getShortProperty( "log_level" );
    }
    */
};



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LogMessage Class

class LogMessage : public MessageBase
{
public:
    LogMessage()
        : m_level(TRACE), m_line(0), m_tid(0)
    {}

    LogMessage( const std::string &a_msg, Level a_level, const char *a_file, unsigned long a_line, unsigned long a_tid = 0 )
        : m_msg(a_msg), m_level(a_level), m_file(a_file), m_line(a_line), m_tid(a_tid)
    {}

    inline MessageType getMessageType() const
    { return MSG_LOG; }

    std::string getFormattedMessage( bool a_debug_info ) const
    {
        std::string formatted_msg = getSourceName() + ":" + ComBusHelper::toText(m_level)
                    + " [" + boost::lexical_cast<std::string>(getTimestamp()) + "] "
                    + m_msg;
//        std::string formatted_msg = getSourceName() + "." + boost::lexical_cast<std::string>(getSourceInstance()) + ":" + ComBusHelper::toText(m_level)
//                    + " [" + boost::lexical_cast<std::string>(getTimestamp()) + "] "
//                    + m_msg;

        if ( a_debug_info )
        {
            formatted_msg += " <" + m_file + ":" + boost::lexical_cast<std::string>(m_line)
                    + " (" + boost::lexical_cast<std::string>(m_tid) + ")>";
        }

        return formatted_msg;
    }

    inline std::string  getLevelText() const
    { return ComBusHelper::toText( m_level ); }

    std::string         m_msg;
    Level               m_level;
    std::string         m_file;
    unsigned long       m_line;
    unsigned long       m_tid;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_msg = a_prop_tree.get( "message", "" );
        m_level = (Level)a_prop_tree.get( "level", 0 );
        m_file = a_prop_tree.get( "file", "" );
        m_line = a_prop_tree.get( "line", 0 );
        m_tid = a_prop_tree.get( "tid", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "message", m_msg );
        a_prop_tree.put( "level", (unsigned short)m_level );
        a_prop_tree.put( "file", m_file );
        a_prop_tree.put( "line", m_line );
        a_prop_tree.put( "tid", m_tid );
    }

    /*
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );

        a_msg.setStringProperty( "message", m_msg );
        a_msg.setShortProperty( "level", m_level );
        if ( !m_file.empty() )
            a_msg.setStringProperty( "file", m_file );
        if ( m_line > 0 )
            a_msg.setIntProperty( "line", m_line );
        if ( m_tid > 0 )
            a_msg.setIntProperty( "tid", m_tid );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_msg = a_msg.getStringProperty( "message" );
        m_level = (Level)a_msg.getShortProperty( "level" );
        if ( a_msg.propertyExists( "file" ))
            m_file = a_msg.getStringProperty( "file" );
        if ( a_msg.propertyExists( "line" ))
            m_line = a_msg.getIntProperty( "line" );
        if ( a_msg.propertyExists( "tid" ))
            m_tid = a_msg.getIntProperty( "tid" );
    }
    */

};


class StatusMessage : public MessageBase
{
public:
    StatusMessage()
        : m_status(STATUS_FAULT)
    {}

    StatusMessage( StatusCode a_status )
        : m_status(a_status)
    {}

    inline MessageType getMessageType() const
    { return MSG_STATUS; }

    StatusCode   m_status;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_status = (StatusCode) a_prop_tree.get( "status", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "status", m_status );
    }
    /*
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );
        a_msg.setShortProperty( "status", (short)m_status );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_status = (StatusCode) a_msg.getShortProperty( "status" );
    }
    */
};


#if 0
class SignalEventMessage : virtual public MessageBase
{
public:
    SignalEventMessage( const cms::Message &a_msg )
        : MessageBase( a_msg )
    { translateFrom( a_msg ); }

    SignalEventMessage( const std::string &a_name, const std::string &a_source, const std::string &a_msg, Level a_level )
        : m_sig_name(a_name), m_sig_source(a_source), m_sig_message(a_msg), m_sig_level(a_level)
    {}

    virtual ~SignalEventMessage()
    {}

    inline MessageType getMessageType() const
    { return MSG_SIGNAL_EVENT; }

    inline const std::string &getSignalName() const
    { return m_sig_name; }

    inline const std::string &getSignalSource() const
    { return m_sig_source; }

    inline Level getSignalLevel() const
    { return m_sig_level; }

    inline const std::string  &getSignalMessage() const
    { return m_sig_message; }

protected:
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );
        a_msg.setStringProperty( "name", m_sig_name );
        a_msg.setStringProperty( "source", m_sig_source );
        a_msg.setStringProperty( "message", m_sig_message );
        a_msg.setShortProperty( "level", (short)m_sig_level );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_sig_name = a_msg.getStringProperty( "name" );
        m_sig_source = a_msg.getStringProperty( "source" );
        m_sig_message = a_msg.getStringProperty( "message" );
        m_sig_level = (Level) a_msg.getShortProperty( "level" );
    }

    std::string         m_sig_name;
    std::string         m_sig_source;
    std::string         m_sig_message;
    Level               m_sig_level;
};
#endif


class SignalRetractMessage : public MessageBase
{
public:
    SignalRetractMessage()
    {}

    SignalRetractMessage( const std::string &a_name )
        : m_sig_name(a_name)
    {}

    virtual ~SignalRetractMessage()
    {}

    inline MessageType getMessageType() const
    { return MSG_SIGNAL_RETRACT; }

    std::string         m_sig_name;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_sig_name = a_prop_tree.get( "sig_name", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "sig_name", m_sig_name );
    }
/*
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );
        a_msg.setStringProperty( "sig_name", m_sig_name );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_sig_name = a_msg.getStringProperty( "sig_name" );
    }
*/
};


#if 0
class SignalAssertMessage : virtual public SignalEventMessage, virtual public SignalRetractMessage
{
public:
    SignalAssertMessage( const cms::Message &a_msg )
        : SignalEventMessage( a_msg ), SignalRetractMessage( a_msg )
    {}

    SignalAssertMessage( const std::string &a_id, const std::string &a_name, const std::string &a_source,
                         const std::string &a_msg, Level a_level )
        : SignalEventMessage( a_name, a_source, a_msg, a_level ), SignalRetractMessage( a_id )
    {}

    inline MessageType getMessageType() const
    { return MSG_SIGNAL_ASSERT; }

protected:
    virtual void translateTo( cms::Message &a_msg )
    {
        SignalEventMessage::translateTo( a_msg );
        SignalRetractMessage::translateTo( a_msg );
    }
};
#endif

class SignalAssertMessage : public MessageBase
{
public:
    SignalAssertMessage()
        : m_sig_level(TRACE)
    {}

    SignalAssertMessage( const std::string &a_name, const std::string &a_source, const std::string &a_msg, Level a_level )
        : m_sig_name(a_name), m_sig_source(a_source), m_sig_message(a_msg), m_sig_level(a_level)
    {}

    virtual ~SignalAssertMessage()
    {}

    inline MessageType getMessageType() const
    { return MSG_SIGNAL_ASSERT; }

    std::string         m_sig_name;
    std::string         m_sig_source;
    std::string         m_sig_message;
    Level               m_sig_level;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_sig_name = a_prop_tree.get( "sig_name", "" );
        m_sig_source = a_prop_tree.get( "sig_source", "" );
        m_sig_message = a_prop_tree.get( "sig_message", "" );
        m_sig_level = (Level)a_prop_tree.get( "sig_level", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

         a_prop_tree.put( "sig_name", m_sig_name );
         a_prop_tree.put( "sig_source", m_sig_source );
         a_prop_tree.put( "sig_message", m_sig_message );
         a_prop_tree.put( "sig_level", (unsigned short)m_sig_level );
    }
/*
    virtual void translateTo( cms::Message &a_msg )
    {
        MessageBase::translateTo( a_msg );

        a_msg.setStringProperty( "sig_name", m_sig_name );
        a_msg.setStringProperty( "sig_source", m_sig_source );
        a_msg.setStringProperty( "sig_message", m_sig_message );
        a_msg.setShortProperty( "sig_level", (short)m_sig_level );
    }

    void translateFrom( const cms::Message &a_msg )
    {
        m_sig_name = a_msg.getStringProperty( "sig_name" );
        m_sig_source = a_msg.getStringProperty( "sig_source" );
        m_sig_message = a_msg.getStringProperty( "sig_message" );
        m_sig_level = (Level) a_msg.getShortProperty( "sig_level" );
    }
*/
};


}}

#endif // COMBUSMESSAGES_H
