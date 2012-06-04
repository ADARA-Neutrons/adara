/**
 * @file PVStreamerSupport.h
 * @brief Common interfaces, enums, typedefs, and helper classes for PVStreamer classes
 */

#ifndef PVSTREAMERSUPPORT
#define PVSTREAMERSUPPORT

#include <string>
#include <map>

namespace SNS { namespace PVS {

typedef unsigned long Protocol;
typedef unsigned long Identifier;

#define PROT_UNDEFINED 0

class PVConfigProvider;
class PVReader;
class PVWriter;
class PVStreamer;

struct PrototolInfo
{
    Protocol            protocol_id;
    PVConfigProvider*   (*config_factory)(PVStreamer&,Protocol);
    PVReader*           (*reader_factory)(PVStreamer&,Protocol);
    PVWriter*           (*writer_factory)(PVStreamer&,Protocol);
};

class Range
{
public:
    Range() : m_active(false), m_min(0), m_max(0) {}

    bool    m_active;   //! Is range active?
    double  m_min;      //! Minimum value ( pv must be >= )
    double  m_max;      //! Maximum value ( pv must be <= )
};

enum Alarms
{
    PV_NO_ALARM     = 0x00,
    PV_HW_ALARM_HI  = 0x01,
    PV_HW_ALARM_LO  = 0x02,
    PV_HW_LIMIT_HI  = 0x04,
    PV_HW_LIMIT_LO  = 0x08,
    PV_SW_ALARM_HI  = 0x10,
    PV_SW_ALARM_LO  = 0x20,
    PV_SW_LIMIT_HI  = 0x40,
    PV_SW_LIMIT_LO  = 0x80
};

std::string
alarmText( unsigned short a_alarms );

enum DataType
{
    PV_INVALID = 0,
    PV_INT,
    PV_UINT,
    PV_DOUBLE,
    PV_ENUM
};

enum Access
{
    PV_READ         = 0x01,
    PV_WRITE        = 0x02,
    PV_READWRITE    = 0x03
};

enum Criticality
{
    PV_NONESSENTIAL = 0,    // Stream if available, log if missing
    PV_ESSENTIAL            // Must be present, log & warn if missing/disconnected via control system
};

/**
 * The PVEnum class is used to define and manage enums defined from config
 * sources. This class allows pvs to share common enums.
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
 * The PVInfo class aggregates all configuration data and live readings for a
 * defined process variable.
 */
class PVInfo
{
public:
    PVInfo() :
        m_id(0), m_device_id(0), m_protocol(PROT_UNDEFINED), m_type(PV_INT), m_enum(0), m_access(PV_READ),
        m_criticality(PV_NONESSENTIAL), m_offset(0), m_active(false), m_alarms(PV_NO_ALARM), m_dval(0)
    {}

    // ---------- Static PV config members ------------------------------------

    Identifier          m_id;           //! Local ID of pv (must combine with device ID)
    Identifier          m_device_id;    //! Device ID (control application)
    Protocol            m_protocol;     //! DAS protocol of pv source
    std::string         m_source;       //! DAS protocol-specific configuration source (i.e. hostname, file, etc.)
    std::string         m_connection;   //! DAS protocol-specific connection string
    std::string         m_name;         //! Human-readable name of pv
    DataType            m_type;         //! Data type (int,double,enum,etc)
    const Enum*         m_enum;         //! PVEnum instance if type is PV_ENUM
    Access              m_access;       //! Read / Write access
    std::string         m_units_class;  // TODO Units need to be centrally managed
    std::string         m_units;
    Criticality         m_criticality;  // TODO This concept is not yet defined in config files
    double              m_offset;       //! Value offset (optional)
    Range               m_hw_limits;    //! Hardware limits (optional)
    Range               m_hw_alarms;    //! Hardware alarms (optional)
    Range               m_sw_limits;    //! Software limits (optional)
    Range               m_sw_alarms;    //! Software alarms (optional)

    // ---------- Dynamic PV values -------------------------------------------

    bool                m_active;   //! Is pv device connected and actively sending data?
    unsigned short      m_alarms;
    union
    {
        long            m_ival;
        unsigned long   m_uval;
        double          m_dval;
    };

};

/**
 * Used to define packet types of the (internal) PVStreamPacket.
 */
enum PVStreamPktType
{
    InvalidPacket = 0,
    DeviceActive,
    DeviceInactive,
    VarActive,
    VarInactive,
    VarStatusUpdate,
    VarValueUpdate
};

struct Timestamp
{
    Timestamp() : sec(0), nsec(0) {}

    unsigned long       sec;
    unsigned long       nsec;
};

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

}}

#endif
