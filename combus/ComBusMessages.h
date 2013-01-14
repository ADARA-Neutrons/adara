#ifndef COMBUSMESSAGES_H
#define COMBUSMESSAGES_H

#include <string>
#include <boost/lexical_cast.hpp>

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
    CAT_LOG     = 0x1000,
    CAT_SIGNAL  = 0x2000,
    CAT_STATUS  = 0x3000,
    CAT_CONTROL = 0x4000
};

/**
 * Message types are used to uniquely identify a message class and indirectly
 * identify the category (topic) for a message. These values are used by the
 * message factory to register (un)serialization methods for each message class.
 */
enum MessageType
{
    MSG_LOG     = CAT_LOG,
    MSG_STATUS  = CAT_STATUS,
    MSG_STATUS_STS_TRANS_COMPLETE,
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


/**
 * Log levels are identical to log4cxx log levels.
 */
enum LogLevel
{
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};


class Message
{
public:
    Message()
        : m_timestamp(0)
    {
    }

    Message( const cms::Message &a_msg )
        : m_timestamp(0)
    {
        translateFrom( a_msg );
    }

    cms::Message           *createCMSMessage( cms::Session &a_session )
                            {
                                cms::Message *msg = a_session.createMessage();
                                translateTo( *msg );
                                return msg;
                            }

    virtual MessageType     getMessageType() const = 0;

    MessageCategory         getMessageCategory() const
                            { return (MessageCategory)(getMessageType() & 0xF000 ); }

    std::string             getTopic() const
                            {
                                switch( getMessageCategory())
                                {
                                case CAT_LOG: return "LOG";
                                case CAT_SIGNAL: return "SIGNAL";
                                case CAT_STATUS: return "STATUS";
                                case CAT_CONTROL: return "CONTROL";
                                }
                                throw std::runtime_error("bad message category");
                            }

    const std::string      &getSource() const
                            { return m_source; }

    unsigned long           getTimestamp() const
                            { return m_timestamp; }

protected:
    virtual void            translateTo( cms::Message &a_msg )
                            {
                                a_msg.setShortProperty( "type", (short) getMessageType() );
                                a_msg.setIntProperty( "timestamp", m_timestamp );
                            }

    void                    translateFrom( const cms::Message &a_msg )
                            {
                                m_source = a_msg.getStringProperty( "source" );
                                m_timestamp = a_msg.getIntProperty( "timestamp" );
                            }

    std::string         m_source;
    unsigned long       m_timestamp;

    // Connection class sets source & timestamp when message is sent
    friend class Connection;
};


class LogMessage : public Message
{
public:
    LogMessage( const cms::Message &a_msg )
        : Message( a_msg ), m_line(0), m_tid(0)
    {
        translateFrom( a_msg );
    }

    LogMessage( const std::string &a_msg, LogLevel a_level, const char *a_file, unsigned long a_line, unsigned long a_tid = 0 )
        : m_msg(a_msg), m_level(a_level), m_file(a_file), m_line(a_line), m_tid(a_tid)
    {}

    MessageType         getMessageType() const
                        { return MSG_LOG; }

    std::string         getFormattedMessage( bool a_debug_info ) const
                        {
                            std::string formatted_msg = m_source + ":" + getLevelText(m_level)
                                        + " [" + boost::lexical_cast<std::string>(m_timestamp) + "] "
                                        + m_msg;

                            if ( a_debug_info )
                            {
                                formatted_msg += " <" + m_file + ":" + boost::lexical_cast<std::string>(m_line)
                                        + " (" + boost::lexical_cast<std::string>(m_tid) + ")>";
                            }

                            return formatted_msg;
                        }

    const std::string  &getMessage() const
                        { return m_msg; }

    LogLevel            getLevel() const
                        { return m_level; }

    unsigned long       getThreadID() const
                        { return m_tid; }

    const std::string  &getSourceFile() const
                        { return m_file; }

    unsigned long       getSourceLine() const
                        { return m_line; }

    std::string         getLevelText() const
                        {
                            return getLevelText( m_level );
                        }

    static std::string  getLevelText( LogLevel a_level )
                        {
                            switch( a_level )
                            {
                            case LOG_TRACE: return "TRACE";
                            case LOG_DEBUG: return "DEBUG";
                            case LOG_INFO:  return "INFO";
                            case LOG_WARN:  return "WARNING";
                            case LOG_ERROR: return "ERROR";
                            case LOG_FATAL: return "FATAL";
                            }
                        }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            Message::translateTo( a_msg );

                            a_msg.setStringProperty( "message", m_msg );
                            a_msg.setShortProperty( "level", m_level );
                            if ( !m_file.empty() )
                                a_msg.setStringProperty( "file", m_file );
                            if ( m_line > 0 )
                                a_msg.setIntProperty( "line", m_line );
                            if ( m_tid > 0 )
                                a_msg.setIntProperty( "tid", m_tid );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_msg = a_msg.getStringProperty( "message" );
                            m_level = (LogLevel)a_msg.getShortProperty( "level" );
                            if ( a_msg.propertyExists( "file" ))
                                m_file = a_msg.getStringProperty( "file" );
                            if ( a_msg.propertyExists( "line" ))
                                m_line = a_msg.getIntProperty( "line" );
                            if ( a_msg.propertyExists( "tid" ))
                                m_tid = a_msg.getIntProperty( "tid" );
                        }

    std::string         m_msg;
    LogLevel            m_level;
    std::string         m_file;
    unsigned long       m_line;
    unsigned long       m_tid;
};


class StatusMessage : public Message
{
public:
    StatusMessage( const cms::Message &a_msg )
        : Message( a_msg )
    {
        translateFrom( a_msg );
    }

    StatusMessage( StatusCode a_status )
        : m_status(a_status)
    {}

    MessageType         getMessageType() const
                        { return MSG_STATUS; }

    inline StatusCode   getStatus() const
                        { return m_status; }

    static std::string  getStatusText( StatusCode a_status )
                        {
                            switch( a_status )
                            {
                            case STATUS_UNRESPONSIVE: return "Unresponsive";
                            case STATUS_STARTING: return "Starting";
                            case STATUS_RUNNING: return "Running";
                            case STATUS_STOPPING:  return "Stopping";
                            case STATUS_FAULT:  return "Fault";
                            default: return "Unknown";
                            }
                        }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            Message::translateTo( a_msg );
                            a_msg.setShortProperty( "status", (short)m_status );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_status = (StatusCode) a_msg.getShortProperty( "status" );
                        }

    StatusCode          m_status;
};

}}

#endif // COMBUSMESSAGES_H
