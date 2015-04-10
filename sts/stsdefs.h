#ifndef STSDEFS_H
#define STSDEFS_H

#include <unistd.h>
#include <vector>
#include <string>
#include <syslog.h>
#include "ADARAUtils.h"

// Global syslog info
#define STS_VERSION "1.2.2"
extern pid_t g_pid;

namespace STS {


// ============================================================================
// Neutron Event Classes and Types
// ============================================================================

/// Contains neutron pulse data
struct PulseInfo
{
    PulseInfo() : start_time(0), last_time(0)
    {}

    uint64_t                start_time;         ///< Time in nanoseconds of first pulse
    uint64_t                last_time;          ///< Time in nanoseconds of last received pulse
    std::vector<double>     times;              ///< Pulse time buffer (seconds)
    std::vector<double>     freqs;              ///< Pulse frequency buffer (Hz)
    std::vector<double>     charges;            ///< Pulse charge buffer
    std::vector<uint32_t>   flags;              ///< Pulse flags (defined in BankedEventPkt class)
};


/// Detector Bank Set information (used in BankInfo)
struct DetectorBankSet
{
    std::string             name;
    std::vector<uint32_t>   banklist;
    uint32_t                flags;
    uint32_t                tofOffset;
    uint32_t                tofMax;
    uint32_t                tofBin;
    double                  throttle;
    std::string             suffix;
};


/// Base class for detector bank info
class BankInfo
{
public:
    /// BankInfo constructor
    BankInfo
    (
        uint16_t a_id,              ///< [in] ID of detector bank
        uint16_t a_pixel_count,     ///< [in] Pixel count of bank
        uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
        uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
    )
    :
        m_id(a_id),
        m_pixel_count(a_pixel_count),
        m_event_count(0),
        m_last_pulse_with_data(0)
    {
        m_index_buffer.reserve(a_idx_buf_reserve);
        m_tof_buffer.reserve(a_buf_reserve);
        m_pid_buffer.reserve(a_buf_reserve);
    }

    /// BankInfo destructor
    virtual ~BankInfo()
    {}

    uint32_t                m_id;                   ///< ID of detector bank
    uint16_t                m_pixel_count;          ///< Number of pixels in bank
    uint64_t                m_event_count;          ///< Running event count
    uint64_t                m_last_pulse_with_data; ///< Index of last pulse with data for this bank
    std::vector<uint64_t>   m_index_buffer;         ///< Event index buffer
    std::vector<float>      m_tof_buffer;           ///< Time of flight buffer (microseconds)
    std::vector<uint32_t>   m_pid_buffer;           ///< Pixel ID buffer

    std::vector<DetectorBankSet *>  m_bank_sets;    ///< Any Detector Bank Set info for this detector bank
};


/// Beam Monitor Configuration information (used in MonitorInfo)
struct BeamMonitorConfig
{
    uint32_t                id;
    uint32_t                tofOffset;
    uint32_t                tofMax;
    uint32_t                tofBin;
    double                  distance;
};


/// Base class for monitor info
class MonitorInfo
{
public:
    ///< MonitorInfo constructor
    MonitorInfo
    (
        uint16_t a_id,               ///< [in] ID of detector bank
        uint32_t a_buf_reserve,      ///< [in] Event buffer initial capacity
        uint32_t a_idx_buf_reserve,  ///< [in] Index buffer initial capacity
        BeamMonitorConfig *a_config  ///< [in] Beam Mon Histo Config (opt)
    )
    :
        m_id(a_id),
        m_event_count(0),
        m_event_uncounted(0),
        m_last_pulse_with_data(0),
        m_config(a_config)
    {
        // Histo-based Monitor
        if ( m_config != NULL )
        {
            // Number of Time Bin Values Needed...
            m_num_tof_bins = ( ( m_config->tofMax - m_config->tofOffset )
                / m_config->tofBin ) + 1;

            // If Max TOF doesn't divide evenly into TOF Bin size,
            // then need "One Extra" Bin Value...
            if ( ( m_config->tofMax - m_config->tofOffset )
                    % m_config->tofBin )
            {
                m_num_tof_bins++;
            }

            // Fail Safe: Make Sure We Get At Least One Actual TOF Bin!
            if ( m_num_tof_bins < 2 )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s %u Histogram Warning: num_tof_bins=%u < 2!",
                    g_pid, "STS Error:", "Beam Monitor", m_id,
                    m_num_tof_bins);
                m_num_tof_bins = 2;
            }

            // Actual Histogram Storage, Non-Inclusive Max TOF Bin...
            m_data_buffer.reserve(m_num_tof_bins - 1);

            // TOF Bin Values...
            m_tofbin_buffer.reserve(m_num_tof_bins);

            syslog( LOG_INFO,
            "[%i] Beam Monitor %u Histogram: %u Time Bin Values, %u to %u.",
                g_pid, m_id, m_num_tof_bins,
                m_config->tofOffset, m_config->tofMax );

            uint32_t tofbin = m_config->tofOffset;
            for (uint32_t i=0 ; i < m_num_tof_bins - 1 ; i++)
            {
                m_data_buffer.push_back(0);

                m_tofbin_buffer.push_back((float)tofbin);
                tofbin += m_config->tofBin;
            }

            // Max TOF Bin Value...
            m_tofbin_buffer.push_back((float)(m_config->tofMax));
        }

        // Event-based Monitor
        else
        {
            m_tof_buffer.reserve(a_buf_reserve);
            m_index_buffer.reserve(a_idx_buf_reserve);
        }
    }

    ///< MonitorInfo destructor
    virtual ~MonitorInfo()
    {}

    uint16_t                m_id;                   ///< ID of monitor
    uint64_t                m_event_count;          ///< Running event count
    uint64_t                m_event_uncounted;      ///< Events not counted in Histogram for this monitor
    uint64_t                m_last_pulse_with_data; ///< Index of last pulse with data for this monitor
    std::vector<uint64_t>   m_index_buffer;         ///< Event index buffer
    std::vector<float>      m_tof_buffer;           ///< Time of flight buffer

    uint32_t                m_num_tof_bins;         ///< Histo Number of TOF Bins
    std::vector<uint32_t>   m_data_buffer;          ///< Histo data buffer
    std::vector<float>      m_tofbin_buffer;        ///< Histo TOF Bin buffer

    BeamMonitorConfig      *m_config;               ///< Any (Histogram) config info for this monitor
};


/// User information (part of RunInfo)
struct UserInfo
{
    std::string             id;
    std::string             name;
    std::string             role;
};


/// RunInformation extracted from RunInfo packet xml payload
struct RunInfo
{
    RunInfo() : run_number(0)
    {}

    std::string             instr_id;
    std::string             instr_shortname;
    std::string             instr_longname;
    unsigned long           run_number;
    std::string             run_title;
    std::string             proposal_id;
    std::string             facility_name;
    std::string             sample_id;
    std::string             sample_name;
    std::string             sample_nature;
    std::string             sample_formula;
    std::string             sample_environment;
    std::vector<UserInfo>   users;
};


/// Run metrics collected by STS during translation
struct RunMetrics
{
    RunMetrics() : total_charge(0.0), events_counted(0), events_uncounted(0), non_events_counted(0)
    {}

    double                  total_charge;
    uint64_t                events_counted;
    uint64_t                events_uncounted;
    uint64_t                non_events_counted;
    struct timespec         start_time;
    struct timespec         end_time;
    Statistics              charge_stats;       ///< Pulse charge statistics
    Statistics              freq_stats;         ///< Pulse frequency statistics
};


// ============================================================================
// Process Variable Classes and Types
// ============================================================================

// TODO These should be defined in ADARA.h
enum TranslationStatusCode
{
    TS_SUCCESS         = 0x0000,
    TS_TRANSIENT_ERROR = 0x0001,
    TS_PERM_ERROR      = 0x8000
};

/// Identifier type used for devices and process variables
typedef uint32_t Identifier;

/// Process varibale key used for maps and/or sets
typedef std::pair<Identifier,Identifier>    PVKey;

/// Process variable types
enum PVType
{
    PVT_INT,
    PVT_UINT,
    PVT_FLOAT,
    PVT_DOUBLE,
    PVT_ENUM,
    PVT_STRING
};

/// Base class for all process variable (PV) types
class PVInfoBase
{
public:
    /// PVInfoBase constructor
    PVInfoBase
    (
        const std::string  &a_name,         ///< [in] Name of PV
        const std::string  &a_device_name,  ///< [in] Name of device that owns the PV
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type,         ///< [in] Type of PV
        const std::string  &a_units         ///< [in] Units of PV (empty if not needed)
    )
    :
        m_name(a_name),
        m_device_name(a_device_name),
        m_device_id(a_device_id),
        m_pv_id(a_pv_id),
        m_type(a_type),
        m_units(a_units),
        m_last_time(0)
    {}

    /// PVInfoBase destructor
    virtual ~PVInfoBase()
    {}

    bool sameDefiniton( const std::string &a_name, const std::string &a_device_name, PVType a_type, const std::string &a_units )
    {
        // TODO - Add enumeration check when supported
        if ( m_name == a_name && m_device_name == a_device_name && m_type == a_type && m_units == a_units )
            return true;
        else
            return false;
    }

    /// Determine if PVs have equivalent definitions
    bool sameDefiniton( const PVInfoBase &a_pv )
    {
        // TODO - Add enumeration check when supported
        if ( m_name == a_pv.m_name && m_device_name == a_pv.m_device_name && m_type == a_pv.m_type && m_units == a_pv.m_units )
            return true;
        else
            return false;
    }

    /// Virtual method to allow subclasses to write buffered PV values and time axis
    virtual void flushBuffers( struct RunMetrics *a_run_metrics = 0 ) = 0;

    std::string         m_name;         ///< Name of PV
    std::string         m_device_name;  ///< Name of device that owns the PV
    Identifier          m_device_id;    ///< ID of device that owns the PV
    Identifier          m_pv_id;        ///< ID of the PV
    PVType              m_type;         ///< Type of PV
    std::string         m_units;        ///< Units of PV
    Statistics          m_stats;        ///< Statistics of PV
    uint64_t            m_last_time;    ///< Nanosec time (EPICS epoch) of last received update
    std::vector<double> m_time_buffer;  ///< Buffer that holds time axis (seconds) of PV values
};

/// Intermmediary PV template class that provides typed value buffer
template<class T>
class PVInfo : public PVInfoBase
{
public:
    /// PVInfo constructor
    PVInfo
    (
        const std::string  &a_name,         ///< [in] Name of PV
        const std::string  &a_device_name,  ///< [in] Name of device that owns the PV
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type,         ///< [in] Type of PV
        const std::string  &a_units         ///< [in] Units of PV (empty if not needed)
    )
    : PVInfoBase( a_name, a_device_name, a_device_id, a_pv_id, a_type, a_units )
    {}

    /// PVInfo destructor
    virtual ~PVInfo()
    {}

    std::vector<T>      m_value_buffer; ///< Value buffer for PV
};


// ============================================================================
// ADARA Stream Adapter Class Interface
// ============================================================================

/// Interface that ADARA stream adapter subclasses must implement
class IStreamAdapter
{
public:
    virtual void            initialize() = 0;
    virtual void            finalize( const RunMetrics &a_run_metrics ) = 0;
    virtual PVInfoBase*     makePVInfo( const std::string & a_name,
                                const std::string & a_device_name,
                                Identifier a_device_id,
                                Identifier a_pv_id, PVType a_type,
                                const std::string & a_units ) = 0;
    virtual BankInfo*       makeBankInfo( uint16_t a_id,
                                uint16_t a_pixel_count,
                                uint32_t a_buf_reserve,
                                uint32_t a_idx_buf_reserve ) = 0;
    virtual MonitorInfo*    makeMonitorInfo( uint16_t a_id,
                                uint32_t a_buf_reserve,
                                uint32_t a_idx_buf_reserve,
                                STS::BeamMonitorConfig *a_config,
                                bool a_known_monitor ) = 0;
    virtual void            processRunInfo(
                                const RunInfo & a_run_info ) = 0;
    virtual void            processGeometry(
                                const std::string & a_xml ) = 0;
    virtual void            pulseBuffersReady(
                                STS::PulseInfo &a_pulse_info ) = 0;
    virtual void            bankBuffersReady( STS::BankInfo &a_bank ) = 0;
    virtual void            bankPulseGap( STS::BankInfo &a_bank,
                                uint64_t a_count ) = 0;
    virtual void            bankFinalize( STS::BankInfo &a_bank ) = 0;
    virtual void            monitorBuffersReady(
                                STS::MonitorInfo &a_monitor_info ) = 0;
    virtual void            monitorPulseGap( STS::MonitorInfo &a_monitor,
                                uint64_t a_count ) = 0;
    virtual void            monitorFinalize(
                                STS::MonitorInfo &a_monitor ) = 0;
    virtual void            runComment( const std::string &a_comment ) = 0;
    virtual void            markerPause( double a_time,
                                const std::string &a_comment ) = 0;
    virtual void            markerResume( double a_time,
                                const std::string &a_comment ) = 0;
    virtual void            markerScanStart( double a_time,
                                unsigned long a_scan_index,
                                const std::string &a_scan_comment ) = 0;
    virtual void            markerScanStop( double a_time,
                                unsigned long a_scan_index,
                                const std::string &a_comment ) = 0;
    virtual void            markerComment( double a_time,
                                const std::string &a_comment ) = 0;
};


// ============================================================================
// Error Codes for TraceExceptions
// ============================================================================

enum ErrorCodes
{
    ERR_GENERAL_ERROR = 1,
    ERR_PV_NOT_DEFINED,
    ERR_CAST_FAILED,
    ERR_INVALID_OPERATION,
    ERR_UNEXPECTED_INPUT,
    ERR_OUTPUT_FAILURE,
    ERR_LAST
};

} // End STS Namespace

#endif // STSDEFS_H

// vim: expandtab

