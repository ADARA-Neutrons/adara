#ifndef COMBUSDEFS_H
#define COMBUSDEFS_H

#include <string>
#include <map>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>
#include "ADARADefs.h"

namespace ADARA {
namespace ComBus {

/** Message categories map directly to JMS/AMQP topics and are used internally
  * by the ComBus library. Category is encoded in the upper 3 bits (bits 29 to
  * 31) of the 32-bit message type.
  */
enum MessageCategory
{
    CAT_LOG     = 0x20000000,
    CAT_STATUS  = 0x40000000,
    CAT_SIGNAL  = 0x80000000,
    CAT_APP     = 0xA0000000,
    CAT_INPUT   = 0xC0000000
};

#define MSG_CAT_MASK 0xE0000000

// TODO Is there a good way to make the following emums external definitions?

/** AppCategory defines the application ID for application-specific messages and
  * is encoded in 9 bits from bits 20 to 28 in the 32-bit message type.
  */
enum AppCategory
{
    APP_NONE        = 0x00000000,
    APP_SAS         = 0x00100000,
    APP_SMS         = 0x00200000,
    APP_STC         = 0x00300000,
    APP_SRS         = 0x00400000,
    APP_WFM         = 0x00500000,
    APP_CATALOG     = 0x00600000,
    APP_AUTORED     = 0x00700000,
    APP_LOGGER      = 0x00800000,
    APP_SCANSERV    = 0x00900000,
    APP_CONTROL     = 0x00A00000,
    APP_IOC         = 0x00B00000,
    APP_DASMON      = 0x00C00000
};

#define APP_CAT_MASK 0x1FF00000

// TODO Should version bits be reserved here?

/**
 * Message types are used to uniquely identify a message class and indirectly
 * identify the category (topic) for a message. These values are used by the
 * message factory to register (un)serialization methods for each message class.
 * The lower 20 bits are reserved for assignment within an application domain.
 */
enum MessageType
{
    MSG_LOG                     = CAT_LOG,
    MSG_STATUS                  = CAT_STATUS,
    MSG_SIGNAL_ASSERTED         = CAT_SIGNAL,
    MSG_SIGNAL_RETRACTED,
    MSG_EMIT_STATUS             = CAT_INPUT,
    MSG_EMIT_STATE,
    MSG_REINIT,
    MSG_SHUTDOWN,
    MSG_CONFIG_LOGGING,
    MSG_ACK,
    MSG_NACK,
    MSG_STC_TRANS_STARTED       = CAT_APP | APP_STC,
    MSG_STC_TRANS_FINISHED,
    MSG_STC_TRANS_FAILED,
    MSG_SMS_RUN_STATUSUPDATE    = CAT_APP | APP_SMS,
    MSG_DASMON_GET_RULES        = CAT_INPUT | APP_DASMON,
    MSG_DASMON_SET_RULES,
    MSG_DASMON_RULE_DEFINITIONS,
    MSG_DASMON_RULE_ERRORS,
    MSG_DASMON_RESTORE_DEFAULT_RULES,
    MSG_DASMON_GET_INPUT_FACTS,
    MSG_DASMON_INPUT_FACTS,
    MSG_DASMON_GET_PVS,
    MSG_DASMON_PVS,
    MSG_DASMON_SMS_CONN_STATUS  = CAT_APP | APP_DASMON,
    MSG_DASMON_RUN_STATUS,
    MSG_DASMON_PAUSE_STATUS,
    MSG_DASMON_SCAN_STATUS,
    MSG_DASMON_BEAM_INFO,
    MSG_DASMON_RUN_INFO,
    MSG_DASMON_BEAM_METRICS,
    MSG_DASMON_RUN_METRICS,
    MSG_DASMON_STREAM_METRICS
};


/** Status codes are emitted by all required ADARA processes to enable health
  * monitoring via the ComBus. Status must be emitted periodically even if status
  * is unchanged such that hung/non-responsive/aborted processes can be detected.
  * The fault state is used to indaicte an error that prevents correct operation
  * of the process (a process may autorestart when a fault is detected).
  */
enum StatusCode
{
    STATUS_OK = 0,
    STATUS_FAULT,
    STATUS_UNRESPONSIVE,
    STATUS_INACTIVE
};


/** \brief ComBusHelper provides message utility methods.
  *
  * The ComBusHelper class is used internally by the ComBus library; however,
  * user code may access these methods as well.
  */
class ComBusHelper
{
public:

    /// Convert a SatusCode value to text
    static const char* toText( StatusCode a_code )
    {
        switch( a_code )
        {
        case STATUS_INACTIVE: return "Inactive";
        case STATUS_UNRESPONSIVE: return "Unresponsive";
        case STATUS_OK: return "OK";
        case STATUS_FAULT: return "Fault";
        default: return "Unknown";
        }
    }

    /// Convert a Level value to text
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

    /// Convert text to a SatusCode value, may be a number (0 to 5) or a label (i.e. TRACE)
    static Level toLevel( std::string a_level )
    {
        try
        {
            unsigned short tmp = boost::lexical_cast<unsigned short>( a_level );
            if ( tmp > 6 )
                throw "Invalid level";

            return (ADARA::Level)tmp;
        }
        catch(...)
        {
            // Will throw an exception is symbol is used

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
    }
};

/** \brief Base class for all ComBus message classes
  *
  * The MessageBase class provides common interface and accessors required by
  * all ComBus message classes. New message classes should not inherit directly
  * from MessageBase, but use the TemplMessageBase class instead for automatic
  * factory support.
  */
class MessageBase
{
public:
    MessageBase() :
        m_src_id(""), m_dest_id(""), m_correlation_id(""), m_timestamp(0)
    { }

    virtual ~MessageBase()
    {}

    /** \brief Serializes a MessageBase-derived instance to an AMQ TextMessage payload
      * \param a_msg - AMQ TextMessage object to receive serialized MessageBase data
      */
    void serialize( cms::TextMessage &a_msg )
    {
        boost::property_tree::ptree prop_tree;

        write( prop_tree );

        std::stringstream sstr;
        write_json( sstr, prop_tree );

        a_msg.setText( sstr.str() );
    }

    /** \brief Unserializes a MessageBase-derived instance from a boost property tree
      * \param a_prop_tree - Boost property tree containing serialized MessageBase data
      */
    void unserialize( boost::property_tree::ptree &a_prop_tree )
    {
        read( a_prop_tree );
    }

    /// Retrieves the message type (defined in MessageType enum)
    virtual unsigned long getMessageType() const = 0;

    /// Retrieves message category (encoded in message type)
    inline MessageCategory getMessageCategory() const
    { return (MessageCategory)(getMessageType() & MSG_CAT_MASK ); }

    /// Retrieves application category (encoded in message type)
    inline AppCategory getAppCategory() const
    { return (AppCategory)(getMessageType() & APP_CAT_MASK ); }

    /// Retrieves topic associated with message category
    std::string getTopic() const
    {
        switch( getMessageCategory())
        {
        case CAT_LOG: return "LOG";
        case CAT_SIGNAL: return "SIGNAL";
        case CAT_STATUS: return "STATUS";
        case CAT_APP: return "APP";
        case CAT_INPUT: return "INPUT";
        }
        throw std::runtime_error("bad message category");
    }

    /// Gets source process ID
    inline const std::string &getSourceID() const
    { return m_src_id; }

    /// Gets destination process ID
    inline const std::string &getDestID() const
    { return m_dest_id; }

    /// Gets correlation ID of message
    inline const std::string& getCorrelationID() const
    { return m_correlation_id; }

    /// Gets timestamp of message
    inline unsigned long getTimestamp() const
    { return m_timestamp; }

protected:
    /** \brief Unserializes object data from boost property tree
      * \param a_prop_tree - Boost property containing serialized data
      *
      * This virtual method may be overridden by subclasses to unserialize
      * object data from the supplied boost property tree. If the class is
      * overridden, the base class version must be called from the read()
      * method within the derived class.
      */
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        // Note that "msg_type" is not read here as it must be parsed out by the
        // message receive code such that the appropriate message class can be
        // constructed prior to calling read().

        m_src_id = a_prop_tree.get( "src_id", "" );
        m_dest_id = a_prop_tree.get( "dest_id", "" );
        m_correlation_id = a_prop_tree.get( "correl_id", "" );
        m_timestamp = a_prop_tree.get( "timestamp", 0UL );
    }

    /** \brief Serializes object data to a boost property tree
      * \param a_prop_tree - Boost property to receive serialized data
      *
      * This virtual method may be overridden by subclasses to serialize
      * object data into the supplied boost property tree. If the class is
      * overridden, the base class version must be called from the write()
      * method within the derived class.
      */
    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        a_prop_tree.put( "msg_type", getMessageType() );
        a_prop_tree.put( "src_id", m_src_id );
        a_prop_tree.put( "dest_id", m_dest_id );
        if ( !m_correlation_id.empty())
            a_prop_tree.put( "correl_id", m_correlation_id );
        a_prop_tree.put( "timestamp", m_timestamp );
    }

private:
    /** \brief Used by Connection class to set message routing data
      * \param a_src_name - Name of source process (sender)
      * \param a_timestamp - Time message was/will be sent
      * \param a_correlation_id - Optional correlation ID (unique string)
      */
    void setRoutingInfo( const std::string &a_src_id, const std::string &a_dest_id, unsigned long a_timestamp, const std::string *a_correlation_id = 0 )
    {
        m_src_id = a_src_id;
        m_dest_id = a_dest_id;
        m_timestamp = a_timestamp;
        if ( a_correlation_id )
            m_correlation_id = *a_correlation_id;
        else
            m_correlation_id.clear();
    }

    /** \brief Used by Connection class to set correlation ID
      * \param a_correlation_id - Correlation ID (unique string)
      */
    void setCorrelationID( const std::string &a_correlation_id )
    {
        m_correlation_id = a_correlation_id;
    }

    std::string         m_src_id;           ///< Name (with proc ID) of sender
    std::string         m_dest_id;          ///< Name (with proc ID) of destination
    std::string         m_correlation_id;   ///< Identifier used to (re)associate messages
    unsigned long       m_timestamp;        ///< Unix time when message was sent

    // Connection class sets source, instance, and timestamp when message is sent
    friend class Connection;
};


/** \brief Implements factory methods for message classes.
  *
  * The TemplMessageBase class provides automatic factory registration
  * and instance construction methods for all derived message classes.
  * This class makes use of the "curiously recursive template parameter"
  * pattern. Template parameter T must be a class that inherits directly
  * from TemplMessageBase. The derived class may use multiple inheritance
  * to implement other interfaces, but message ploymorphism (families)
  * can not be supported due to the need to have each subclass derive
  * directly from TemplMessageBase.
  */
template<unsigned long MT, class T>
class TemplMessageBase : public MessageBase
{
public:
    TemplMessageBase() {}
    virtual ~TemplMessageBase() {}

    /// Gets the message type of the class
    unsigned long     getMessageType() const { return MSG_TYPE; }

private:
    static const unsigned long MSG_TYPE;
    enum { _MSG_TYPE = MT };

    /// Makes a new instance of the derived class T
    static MessageBase *factory()
    {
        return new T();
    }
};

/// Macro that defines a simple MessageBase subclass with no additional features/data
#define DEF_SIMPLE_MSG(name,type) \
    class name : public TemplMessageBase<type,name> { \
    public: \
        name() {} \
    };


/** \brief Provides message factory services.
  *
  * The Factory class is a singleton that provides class registration and instance
  * creation methods for message classes. Messages classes should inherit from
  * the TmpleMessageBase class which will result in automatic registration of the
  * class during static initialization.
  */
class Factory
{
public:
    /// Gets (or creates) global instance of Factory class
    static Factory& Inst()
    {
        static Factory factory;
        return factory;
    }

    /** \brief Registers a class with message factory
      * \param a_msg_type - Message type (from MessageType enum)
      * \param a_ctor - Pointer to Message constructor
      * \returns Passed-in message type value
      */
    unsigned long registerClass( unsigned long a_msg_type, MessageBase *(*a_ctor)() )
    {
        m_registry[a_msg_type] = a_ctor;
        return a_msg_type;
    }

    /** \brief Constructs an instance of a message class
      * \param a_msg_type - Type of message to create
      * \returns New instance of a MessageBase derived class
      * \throws runtime_error if class type is not registered
      */
    MessageBase *make( unsigned long a_msg_type )
    {
         std::map<unsigned long, MessageBase *(*)()>::iterator f = m_registry.find( a_msg_type );
         if ( f == m_registry.end() )
             throw std::runtime_error("Unknown class type");

         return f->second();
    }

private:
    /// Message type to message constructor map
    std::map<unsigned long, MessageBase *(*)()> m_registry;
};


/// Provides static initialization and automatic registration of a TemplMessagBase derived class
template <unsigned long MT, class T>
const unsigned long TemplMessageBase<MT,T>::MSG_TYPE = Factory::Inst().registerClass(
            TemplMessageBase<MT,T>::_MSG_TYPE, &TemplMessageBase<MT,T>::factory );


}}

#endif // COMBUSDEFS_H

// vim: expandtab

