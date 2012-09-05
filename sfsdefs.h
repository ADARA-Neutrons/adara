#ifndef SFSDEFS_H
#define SFSDEFS_H

#include <vector>
#include "Utils.h"

namespace SFS {


// TODO These should be defined in ADARA.h


// ============================================================================
// Process Variable Classes and Types
// ============================================================================

enum TranslationStatusCode
{
    TS_SUCCESS         = 0x0000,
    TS_TRANSIENT_ERROR = 0x0001,
    TS_PERM_ERROR      = 0x8000
};

typedef uint32_t Identifier;

typedef std::pair<Identifier,Identifier>    PVKey;

enum PVType
{
    PVT_INT,
    PVT_UINT,
    PVT_FLOAT,
    PVT_DOUBLE,
    PVT_ENUM
};

class PVInfoBase
{
public:
    PVInfoBase( const std::string & a_name, Identifier a_device_id, Identifier a_pv_id, PVType a_type, const std::string & a_units )
        : m_name(a_name), m_device_id(a_device_id), m_pv_id(a_pv_id), m_type(a_type), m_units(a_units)
    {}

    virtual ~PVInfoBase()
    {}

    virtual void flushBuffers( bool lastWrite = false ) = 0;

    std::string         m_name;
    Identifier          m_device_id;
    Identifier          m_pv_id;
    PVType              m_type;
    std::string         m_units;
    Statistics          m_stats;
    std::vector<float>  m_time_buffer;
};


template<class T>
class PVInfo : public PVInfoBase
{
public:
    PVInfo( const std::string & a_name, Identifier a_device_id, Identifier a_pv_id, PVType a_type, const std::string & a_units )
        : PVInfoBase( a_name, a_device_id, a_pv_id, a_type, a_units )
    {}

    virtual ~PVInfo()
    {}

    std::vector<T>      m_value_buffer;
};


// ============================================================================
// Neutron Event Classes and Types
// ============================================================================

struct PulseInfo
{
    PulseInfo()
        : start_time_nsec(0), last_time(0)
    {}

    struct timespec         start_time_ts;
    uint64_t                start_time_nsec;
    uint64_t                last_time;
    std::vector<double>     times;
    std::vector<double>     freqs;
    std::vector<double>     charges;
    Statistics              charge_stats;
    Statistics              freq_stats;
};


class BankInfo
{
public:
    BankInfo( uint16_t a_id, uint16_t a_pixel_count, uint32_t a_buf_reserve, uint32_t a_idx_buf_reserve )
        : m_id(a_id), m_pixel_count(a_pixel_count), m_event_count(0), m_last_pulse_with_data(0)
    {
        m_index_buffer.reserve(a_idx_buf_reserve);
        m_tof_buffer.reserve(a_buf_reserve);
        m_pid_buffer.reserve(a_buf_reserve);
    }

    virtual ~BankInfo()
    {}

    uint32_t                m_id;
    uint16_t                m_pixel_count;
    uint64_t                m_event_count;
    uint64_t                m_last_pulse_with_data;
    std::vector<uint64_t>   m_index_buffer;
    std::vector<float>      m_tof_buffer;
    std::vector<uint32_t>   m_pid_buffer;
};

class MonitorInfo
{
public:
    MonitorInfo( uint16_t a_id, uint32_t a_buf_reserve )
      : m_id(a_id), m_event_count(0), m_last_pulse_with_data(0)
    {
        m_tof_buffer.reserve(a_buf_reserve);
    }

    virtual ~MonitorInfo()
    {}

    uint16_t                m_id;
    uint64_t                m_event_count;
    uint64_t                m_last_pulse_with_data;
    std::vector<uint64_t>   m_index_buffer;
    std::vector<float>      m_tof_buffer;
};

// ============================================================================
// ADARA Stream Adapter Class Interface
// ============================================================================

class IStreamAdapter
{
public:
    virtual void            initialize() = 0;
    virtual void            finalize() = 0;
    virtual PVInfoBase*     makePVInfo( const std::string & a_name, Identifier a_device_id, Identifier a_pv_id, PVType a_type, const std::string & a_units ) = 0;
    virtual BankInfo*       makeBankInfo( uint16_t a_id, uint16_t a_pixel_count, uint32_t a_buf_reserve, uint32_t a_idx_buf_reserve ) = 0;
    virtual MonitorInfo*    makeMonitorInfo( uint16_t a_id, uint32_t a_buf_reserve ) = 0;
    virtual void            processBeamLineInfo( const std::string &a_id, const std::string &a_shortname, const std::string &a_longname ) = 0;
    virtual void            processRunInfo( const std::string & a_xml ) = 0;
    virtual void            processGeometry( const std::string & a_xml ) = 0;
    virtual void            pulseBuffersReady( SFS::PulseInfo &a_pulse_info ) = 0;
    virtual void            pulseFinalize( SFS::PulseInfo &a_pulse_info ) = 0;
    virtual void            bankBuffersReady( SFS::BankInfo &a_bank ) = 0;
    virtual void            bankPulseGap( SFS::BankInfo &a_bank, uint64_t a_count ) = 0;
    virtual void            bankFinalize( SFS::BankInfo &a_bank ) = 0;
    virtual void            monitorBuffersReady( SFS::MonitorInfo &a_monitor_info ) = 0;
    virtual void            monitorPulseGap( SFS::MonitorInfo &a_monitor, uint64_t a_count ) = 0;
    virtual void            monitorFinalize( SFS::MonitorInfo &a_monitor ) = 0;
};

} // End SFS Namespace

#endif // SFSDEFS_H
