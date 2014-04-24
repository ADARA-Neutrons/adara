#ifndef COMBUSMESSAGES_H
#define COMBUSMESSAGES_H

#include "ComBusDefs.h"

namespace ADARA {
namespace ComBus {

/// Requests a process to (re)emit its status (StatusCode)
DEF_SIMPLE_MSG(EmitStatusCommand,MSG_EMIT_STATUS)
/// Requests a process to (re)emit it's state (application defined)
DEF_SIMPLE_MSG(EmitStateCommand,MSG_EMIT_STATE)
/// Requests a process to reinitialize (configuration has changed)
DEF_SIMPLE_MSG(ReinitCommand,MSG_REINIT)
/// Requests a process to shutdown
DEF_SIMPLE_MSG(ShutdownCommand,MSG_SHUTDOWN)
/// Generic acknowledge message
DEF_SIMPLE_MSG(AckReply,MSG_ACK)
/// Generic negative acknowledge message
DEF_SIMPLE_MSG(NackReply,MSG_NACK)


/// Logging not currently used
class ConfigureLoggingCommand: public TemplMessageBase<MSG_CONFIG_LOGGING,ConfigureLoggingCommand>
{
public:
    ConfigureLoggingCommand()
        : m_enabled(false), m_level(ADARA::TRACE)
    {}

    ConfigureLoggingCommand( bool a_enabled, Level a_level )
        : m_enabled(a_enabled), m_level(a_level)
    {}

    bool    m_enabled;
    Level   m_level;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_enabled = a_prop_tree.get( "log_enabled", false );
        m_level = (ADARA::Level) a_prop_tree.get( "log_level", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "log_enabled", m_enabled );
        a_prop_tree.put( "log_level", (unsigned short)m_level );
    }
};

/// Logging is not currently used
class LogMessage : public TemplMessageBase<MSG_LOG,LogMessage>
{
public:
    LogMessage()
        : m_level(TRACE), m_line(0), m_tid(0)
    {}

    LogMessage( const std::string &a_msg, Level a_level, const char *a_file, uint32_t a_line, uint32_t a_tid = 0 )
        : m_msg(a_msg), m_level(a_level), m_file(a_file), m_line(a_line), m_tid(a_tid)
    {}

    std::string getFormattedMessage( bool a_debug_info ) const
    {
        std::string formatted_msg = getSourceID() + ":" + ComBusHelper::toText(m_level)
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
    uint32_t            m_line;
    uint32_t            m_tid;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_msg = a_prop_tree.get( "message", "" );
        m_level = (Level)a_prop_tree.get( "level", 0 );
        m_file = a_prop_tree.get( "file", "" );
        m_line = a_prop_tree.get( "line", 0UL );
        m_tid = a_prop_tree.get( "tid", 0UL );
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
};


/** \brief The StatusMessage class is used to broadcast process status on the STATUS topic.
  *
  */
class StatusMessage : public TemplMessageBase<MSG_STATUS,StatusMessage>
{
public:
    StatusMessage()
        : m_status(STATUS_FAULT)
    {}

    StatusMessage( StatusCode a_status )
        : m_status(a_status)
    {}

    StatusCode   m_status;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_status = (StatusCode) a_prop_tree.get( "status", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "status", m_status );
    }
};


/** \brief The SignalRetractMessage message is used to broadcast a signal retracted notice on the SIGNAL topic.
  *
  */
class SignalRetractMessage : public TemplMessageBase<MSG_SIGNAL_RETRACTED,SignalRetractMessage>
{
public:
    SignalRetractMessage()
    {}

    SignalRetractMessage( const std::string &a_name )
        : m_sig_name(a_name)
    {}

    virtual ~SignalRetractMessage()
    {}

    std::string         m_sig_name;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_sig_name = a_prop_tree.get( "sig_name", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "sig_name", m_sig_name );
    }
};


/** \brief Used to broadcast a signal asserted alert on SIGNAL topic
  *
  */
class SignalAssertMessage : public TemplMessageBase<MSG_SIGNAL_ASSERTED,SignalAssertMessage>
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

    std::string         m_sig_name;
    std::string         m_sig_source;
    std::string         m_sig_message;
    Level               m_sig_level;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
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
};


}}

#endif // COMBUSMESSAGES_H
