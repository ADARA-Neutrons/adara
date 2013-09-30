/**
 * \file PVStreamerSupport.h
 * \brief Common interfaces, enums, typedefs, and helper classes for PVStreamer classes.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVSTREAMERSUPPORT
#define PVSTREAMERSUPPORT

#include <string>
#include <sstream>
#include <vector>
#include <map>

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif

namespace SNS { namespace PVS {

// Forward declares
class PVConfigProvider;
class PVReader;
class PVWriter;
class PVStreamer;
class TraceException;

/// Streaming protocol identifier typedef
typedef unsigned long Protocol;

/// Generic stream identifier typedef (device/variable)
typedef unsigned long Identifier;

/// Protocol 0 is reserved
#define PROT_UNDEFINED 0

/// Used to define a hi-low range for variable alarms and limits
class Range
{
public:
    Range() : m_active(false), m_min(0), m_max(0) {}

    bool    m_active;   //! Is range active?
    double  m_min;      //! Minimum value ( pv must be >= )
    double  m_max;      //! Maximum value ( pv must be <= )
};

/// Variable alarm types
enum Alarms
{
    PV_NO_ALARM     = 0x0000,
    PV_HW_ALARM_HI  = 0x0001,
    PV_HW_ALARM_LO  = 0x0002,
    PV_HW_LIMIT_HI  = 0x0004,
    PV_HW_LIMIT_LO  = 0x0008,
    PV_SW_ALARM_HI  = 0x0010, // TODO Not currently using SW alarms or limits, what is their purpose?
    PV_SW_ALARM_LO  = 0x0020,
    PV_SW_LIMIT_HI  = 0x0040,
    PV_SW_LIMIT_LO  = 0x0080,
    PV_COMM_ALARM   = 0x0100
};

std::string
alarmText( unsigned short a_alarms );

/// Process variable data types
enum DataType
{
    PV_INVALID = 0,
    PV_INT,
    PV_UINT,
    PV_DOUBLE,
    PV_ENUM
};

/// Process variable access types
enum Access
{
    PV_READ         = 0x01,
    PV_WRITE        = 0x02,
    PV_READWRITE    = 0x03
};


/**
 * \class Enum
 * \brief Defines process variable enums loaded from configuration sources.
 */
class Enum
{
public:
    Enum( Identifier a_id, const std::map<int,std::string> &a_values );

    Identifier              getID() const { return m_id; }
    bool                    isValid( int a_value ) const;
    const std::string &     getName( int a_value ) const;
    int                     getValue( const std::string & a_name ) const;
    const std::map<int,const std::string> &getMap() const;
    bool                    operator==(const std::map<int,std::string> &a_values) const;

private:
    Identifier                  m_id;
    std::map<int,std::string>   m_values;
};


/**
 * \class PVInfo
 * \brief Aggregates process variable configuration data, source, and live readings.
 */
class PVInfo
{
public:
    PVInfo() :
        m_id(0), m_device_id(0), m_protocol(PROT_UNDEFINED), m_type(PV_INT), m_enum(0), m_access(PV_READ),
        m_active(false), m_alarms(PV_NO_ALARM), m_dval(0)
    {}

    // ---------- Static PV config members ------------------------------------

    Identifier          m_id;           ///< Local ID of pv (must combine with device ID)
    Identifier          m_device_id;    ///< Device ID (control application)
    Protocol            m_protocol;     ///< DAS protocol of pv source
    std::string         m_source;       ///< DAS protocol-specific configuration source (i.e. hostname, file, etc.)
    std::string         m_connection;   ///< DAS protocol-specific connection string
    std::string         m_name;         ///< Human-readable name of pv
    DataType            m_type;         ///< Data type (int,double,enum,etc)
    const Enum*         m_enum;         ///< PVEnum instance if type is PV_ENUM
    Access              m_access;       ///< Read / Write access
    std::string         m_units;        ///< Units
    Range               m_hw_limits;    ///< Hardware limits (optional)
    Range               m_hw_alarms;    ///< Hardware alarms (optional)
    Range               m_sw_limits;    ///< Software limits (optional)
    Range               m_sw_alarms;    ///< Software alarms (optional)
    std::string         m_hints;        ///< Protocol specific hints (xml)

    // ---------- Dynamic PV values -------------------------------------------

    bool                m_active;       ///< Indicates if pv device is connected and actively sending
    unsigned short      m_alarms;       ///< Current alarm state
    union
    {
        long            m_ival;         ///< Integer value
        unsigned long   m_uval;         ///< Unsigned integer value
        double          m_dval;         ///< Double float value
    };

};


/// Used to define packet types of the (internal) PVStreamPacket.
enum PVStreamPktType
{
    InvalidPacket = 0,
    DeviceActive,
    DeviceInactive,
    VarActive,
    VarInactive,
    VarUpdate
};

/// Timestamp associated with device activity and variable values
struct Timestamp
{
    Timestamp() : sec(0), nsec(0) {}

    unsigned long       sec;
    unsigned long       nsec;
};

/// Packet structure for the internal protocol-independent event stream
struct PVStreamPacket
{
    PVStreamPktType     pkt_type;
    Timestamp           time;
    Identifier          device_id;
    const PVInfo       *pv_info;
    unsigned short      alarms;
    union
    {
        long            ival;
        unsigned long   uval;
        double          dval;
    };
};

// ---------- Interfaces used by PVStreamer and clients -----------------------

/**
 * \class IPVStreamerStatusListener
 *
 * The IPVStreamerStatusListener interface is used by clients that wish to be notified
 * of PVStreamer status.
 */
class IPVStreamerStatusListener
{
public:
    virtual void                unhandledException( const TraceException &e ) = 0;
};

/**
 * \class IPVStreamListener
 *
 * The IPVStreamListener interface is used by non-critical stream listeners
 * to access the PV event stream (prior to protocol serialization). Under heavy
 * loading it is possible for packets to be dropped.
 */
class IPVStreamListener
{
public:
    virtual void                deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name ) = 0;
    virtual void                deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name ) = 0;
    virtual void                pvActive( Timestamp &a_time, const PVInfo &a_pv_info ) = 0;
    virtual void                pvInactive( Timestamp &a_time, const PVInfo &a_pv_info ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value ) = 0;
};


/**
 * \class IPVConfigListener
 *
 * The IPVConfigListener interface is used by clients that wish to be notified
 * of process-variable and device configuration events.
 */
class IPVConfigListener
{
public:
    virtual void                configurationLoaded( Protocol a_protocol, const std::string &a_source ) = 0;
    virtual void                configurationInvalid( Protocol a_protocol, const std::string &a_source ) = 0;

    // TODO Need API for reconnecting or configuration changes? (CfgMgr can just stop and restart all running devices)
};

class IPVCommonServices
{
public:
    virtual void                unhandledException( TraceException &e ) = 0;
};

/**
 * \class IPVConfigServices
 *
 * The IPVConfigServices interface is used by PVConfig instances to add and
 * remove pvs and enums to/from the central pv repository. Only PVConfig
 * instances have the ability to define and undefine pvs.
 */
class IPVConfigServices : public IPVCommonServices
{
public:

    virtual void                defineApp( Protocol a_protocol, Identifier a_app_id, const std::string &a_source ) = 0;
    virtual void                undefineApp( Identifier a_app_id ) = 0;
    virtual void                defineDevice( Protocol a_protocol, Identifier a_dev_id, const std::string &a_name, const std::string &a_source,  Identifier a_app_id = 0 ) = 0;
    virtual void                undefineDevice( Identifier a_dev_id ) = 0;
    virtual void                undefineDeviceIfNoPVs( Identifier a_dev_id ) = 0;
    virtual void                definePV( PVInfo & info ) = 0;
    virtual void                undefinePV( Identifier a_dev_id, Identifier a_pv_id ) = 0;
    virtual const Enum*         getEnum( Identifier a_id ) const = 0;
    virtual const Enum*         defineEnum( const std::map<int,std::string> &a_values ) = 0;
    virtual void                configurationLoaded( Protocol a_protocol, const std::string &a_source ) = 0;
    virtual void                configurationInvalid( Protocol a_protocol, const std::string &a_source ) = 0;
    virtual PVInfo*             getWriteablePV( const std::string & a_name ) const = 0;
    virtual Identifier          getDeviceIdentifier( const std::string &a_device_name ) = 0;
};


/**
 * \class IPVReaderServices
 *
 * The IPVReaderServices interface is used to grant access to reader-specific
 * services (buffers and writeable PVInfo objects).
 */
class IPVReaderServices : public IPVCommonServices
{
public:
    virtual PVStreamPacket*     getFreePacket() = 0;
    virtual PVStreamPacket*     getFreePacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual void                putFilledPacket( PVStreamPacket *a_pkt ) = 0;
    virtual PVInfo*             getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const = 0;
    virtual std::vector<PVInfo*> &  getWriteableDevicePVs( Identifier a_dev_id ) const = 0;
};


/**
 * \class IPVWriterServices
 *
 * The IPVWriterServices interface is used to grant access to writer-specific
 * services (buffers). Only one writer may be attached to the streamer at a
 * given time. (a future revision may provide multiple writers, but will require
 * substantial changes to the writer interface.)
 */
class IPVWriterServices : public IPVCommonServices
{
public:
    virtual PVStreamPacket*     getFilledPacket() = 0;
    virtual PVStreamPacket*     getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual void                putFreePacket( PVStreamPacket *a_pkt ) = 0;
};


/**
 * \class ILogger
 *
 * The ILogger interface and following macros are used to provide simple but
 * flexible stream-based global logging. The ILogger class should never be
 * used directly; rather, the LOG_XXX() macros should always be used instead.
 */
class ILogger
{
public:
    enum RecordType
    {
        RT_INFO,
        RT_WARNING,
        RT_ERROR
    };

    virtual std::ostream &   write(ILogger::RecordType a_type) = 0;
    virtual void        done() = 0;

    static ILogger *    g_inst;
};

// Macros for accessing global logging feature

#define LOG(t,x)                        \
if ( ILogger::g_inst )                  \
{                                       \
    ILogger::g_inst->write(t) << x;     \
    ILogger::g_inst->done();            \
};

#define LOG_INFO(x) LOG(ILogger::RT_INFO, x )
#define LOG_WARNING(x) LOG(ILogger::RT_WARNING, x )
#define LOG_ERROR(x) LOG(ILogger::RT_ERROR, "{" << __FUNCTION__ << ":" << __LINE__ << "} " << x)

// Exception classes & macros

class TraceException
{
public:
    TraceException( const char *a_file, unsigned long a_line, unsigned long a_error_code, const std::string & a_context )
        : m_file(a_file), m_line(a_line), m_error_code(a_error_code), m_context(a_context)
    {}

    virtual ~TraceException() {}

    void addContext( const std::string & a_context )
    {
        if ( a_context.size() )
        {
            m_context = a_context + "\n" + m_context;
        }
    }

    std::string toString( bool debug = false ) const
    {
        if ( debug )
        {
            std::stringstream sstr;
            sstr << m_context << std::endl;
            sstr << "(source: " << m_file << ":" << m_line << " code:" << m_error_code << ")" << std::endl;

            return sstr.str();
        }
        else
            return m_context;
    }

    /*
    friend std::ostream & operator<<( std::ostream &os, const TraceException & e )
    {
        os << e.m_context << std::endl << "(source: " << e.m_file << ":" << e.m_line << " code:" << e.m_error_code << ")" << std::endl;
    }
    */

private:
    const char         *m_file;
    unsigned long       m_line;
    unsigned long       m_error_code;
    //std::stringstream   m_context;
    std::string         m_context;
};


#define EXC(err_code,msg) throw TraceException( __FUNCTION__, __LINE__, err_code, msg )

#define EXCP(err_code,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    throw TraceException( __FUNCTION__, __LINE__, err_code, s.str()); \
}

#define EXC_ADD(e,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    e.addContext( s.str()); \
}

enum
{
    EC_INVALID_OPERATION = 1,
    EC_INVALID_PARAM,
    EC_INVALID_CONFIG_DATA,
    EC_UNKOWN_ERROR,
    EC_WINDOWS_ERROR = 0x1000
};

}}

#endif
