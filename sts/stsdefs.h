#ifndef STSDEFS_H
#define STSDEFS_H

#include <unistd.h>
#include <vector>
#include <string>
#include <syslog.h>
#include "ADARAUtils.h"
#include "ADARAPackets.h"

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
        uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
        uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
    )
    :
        m_id(a_id),
        m_buf_reserve(a_buf_reserve),
        m_idx_buf_reserve(a_idx_buf_reserve),
        m_initialized(false),
        m_event_count(0),
        m_histo_event_count(0),
        m_histo_event_uncounted(0),
        m_last_pulse_with_data(0),
        m_has_event(false),
        m_has_histo(false)
    {
        // Save Initialization for initializeBank() method...
    }

    /// BankInfo destructor
    virtual ~BankInfo()
    {}

    void initializeBank(void)
    {
        // Already Initialized...
        if ( m_initialized )
            return;

        // Iterate Thru All Detector Bank Sets & Set Up Appropriate Data
        for ( std::vector<STS::DetectorBankSet *>::iterator dbs =
                m_bank_sets.begin(); dbs != m_bank_sets.end() ; ++dbs )
        {
            if ( !*dbs )
                continue;

            // Histo-based Detector Bank
            if ( (*dbs)->flags & ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
            {
                // Only Room for *One* Histogram per Detector Bank (for now)
                if ( m_has_histo )
                {
                    syslog( LOG_ERR,
                    "[%i] %s %s %u %s! Ignoring %s %s (%u to %u by %u).",
                        g_pid, "STS Error:", "Detector Bank", m_id,
                        "Duplicate Histogram Request",
                        (*dbs)->name.c_str(), "Bank Set",
                        (*dbs)->tofOffset, (*dbs)->tofMax, (*dbs)->tofBin );
                }

                else
                {
                    // Determine Total Number of Logical PixelIds in Bank
                    uint32_t num_pids = m_logical_pixelids.size();

                    // If there aren't any PixelIds, we won't get any data!
                    if ( num_pids == 0 )
                    {
                        // Better Alert Someone... (this shouldn't happen)
                        syslog( LOG_ERR,
                       "[%i] %s %s %u %s! Ignoring %s %s (%u to %u by %u).",
                            g_pid, "STS Error:", "Detector Bank", m_id,
                            "No PixelIds for Histogram",
                            (*dbs)->name.c_str(), "Bank Set",
                            (*dbs)->tofOffset, (*dbs)->tofMax,
                            (*dbs)->tofBin );

                        // Don't set "m_has_histo", just fall thru...
                        // (all subsequent Histo attempts will also fail)
                        continue;
                    }

                    // Number of Time Bin Values Needed...
                    m_num_tof_bins =
                        ( ( (*dbs)->tofMax - (*dbs)->tofOffset )
                            / (*dbs)->tofBin ) + 1;

                    // If Max TOF doesn't divide evenly into TOF Bin size,
                    // then need "One Extra" Bin Value...
                    if ( ( (*dbs)->tofMax - (*dbs)->tofOffset )
                            % (*dbs)->tofBin )
                    {
                        m_num_tof_bins++;
                    }

                    // Fail Safe: Make Sure We Get At Least One
                    // Actual TOF Bin!
                    if ( m_num_tof_bins < 2 )
                    {
                        syslog( LOG_ERR,
                    "[%i] %s %s %u Histogram Warning: num_tof_bins=%u < 2!",
                            g_pid, "STS Error:", "Detector Bank", m_id,
                            m_num_tof_bins);
                        m_num_tof_bins = 2;
                    }

                    syslog( LOG_ERR,
                   "[%i] %s %u Histogram: num_tof_bins=%u num_pids=%u (%u)",
                        g_pid, "Detector Bank", m_id, m_num_tof_bins,
                        num_pids, num_pids * ( m_num_tof_bins - 1 ) );

                    // Actual Histogram Storage, Non-Inclusive Max TOF Bin
                    m_data_buffer.reserve( num_pids
                        * ( m_num_tof_bins - 1 ) );

                    // TOF Bin Values...
                    m_tofbin_buffer.reserve(m_num_tof_bins);

                    syslog( LOG_INFO,
                    "[%i] %s %u Histogram: %u Time Bin Values, %u to %u.",
                        g_pid, "Detector Bank", m_id, m_num_tof_bins,
                        (*dbs)->tofOffset, (*dbs)->tofMax );

                    uint32_t tofbin = (*dbs)->tofOffset;
                    for (uint32_t i=0 ; i < m_num_tof_bins - 1 ; i++)
                    {
                        // Inverted Indexing, but it's the right count...!
                        for (uint32_t p=0 ; p < num_pids ; p++)
                            m_data_buffer.push_back(0);

                        m_tofbin_buffer.push_back((float)tofbin);
                        tofbin += (*dbs)->tofBin;
                    }

                    // Max TOF Bin Value...
                    m_tofbin_buffer.push_back((float)((*dbs)->tofMax));

                    // Calculate Per-PixelId Offset Index
                    //    into Histogram Data Buffer...
                    // (saves time, and we sorta _Have_ to do this
                    //    to account for non-contiguous PixelId spaces!)

                    // Determine Min & Max PixelIds for This Bank...
                    uint32_t minPid, maxPid;
                    minPid = maxPid = m_logical_pixelids[0];
                    for (uint32_t p=1 ; p < num_pids ; p++)
                    {
                        if ( m_logical_pixelids[p] < minPid )
                            minPid = m_logical_pixelids[p];
                        if ( m_logical_pixelids[p] > maxPid )
                            maxPid = m_logical_pixelids[p];
                    }

                    // Save Minimum PixelId as Offset into Offset Index!
                    m_base_pid = minPid;

                    // Determine Required Offset Index Size...
                    size_t offset_size = maxPid - minPid + 1;

                    syslog( LOG_ERR,
                "[%i] %s %u Histogram: minPid=%u maxPid=%u offset_size=%lu",
                        g_pid, "Detector Bank", m_id, minPid, maxPid,
                        offset_size );

                    // Reserve Required Index Size & Initialize Vector...
                    // (I hope there aren't huge gaps in the PixelIds...!)
                    m_histo_pid_offset.reserve( offset_size );
                    for (size_t i=0 ; i < offset_size ; i++)
                        m_histo_pid_offset.push_back( -1 );

                    syslog( LOG_ERR,
                        "[%i] %s %u Histogram: Filling in Offsets...",
                        g_pid, "Detector Bank", m_id );

                    // Fill In Offsets per PixelId...
                    size_t offset = 0;
                    size_t index;
                    for (uint32_t p=0 ; p < num_pids ; p++)
                    {
                        // Index is Logical PixelId...
                        index = m_logical_pixelids[p] - m_base_pid;

                        // New PixelId Offset...
                        if ( m_histo_pid_offset[ index ] < 0 )
                        {
                            m_histo_pid_offset[ index ] = 
                                offset++ * ( m_num_tof_bins - 1 );

                            // syslog( LOG_ERR,
                            // "[%i] %s %u Histogram: Offset[%lu] = %d",
                                // g_pid, "Detector Bank", m_id,
                                // index, m_histo_pid_offset[ index ] );
                        }

                        // Duplicate PixelId!  (shouldn't happen...)
                        else
                        {
                            syslog( LOG_INFO,
                                "[%i] %s: %s %u has %s %lu - Ignoring!",
                                g_pid, "STS Error", "Detector Bank", m_id,
                                "Duplicate PixelId in Histo Offset Map",
                                index );

                            // Still need to increment offset past PixelId!
                            offset++;
                        }
                    }

                    syslog( LOG_ERR,
                        "[%i] %s %u Done with Histogram Init.",
                        g_pid, "Detector Bank", m_id );

                    // Got One, That's All We'll Ever Need... ;-D
                    m_has_histo = true;
                }
            }

            // Event-based Detector Bank
            if ( (*dbs)->flags & ADARA::DetectorBankSetsPkt::EVENT_FORMAT )
            {
                // Only Allocate Event Storage *Once*...
                if ( !m_has_event )
                {
                    m_index_buffer.reserve(m_idx_buf_reserve);
                    m_tof_buffer.reserve(m_buf_reserve);
                    m_pid_buffer.reserve(m_buf_reserve);

                    // Got One, That's All We'll Ever Need... ;-D
                    m_has_event = true;
                }
            }
        }

        // Handle *Default* Case, No Detector Bank Sets at All...! ;-D
        // (We _Always_ Save Events, Unless Specifically Directed Not To!)
        if ( m_bank_sets.size() == 0 )
        {
            m_index_buffer.reserve(m_idx_buf_reserve);
            m_tof_buffer.reserve(m_buf_reserve);
            m_pid_buffer.reserve(m_buf_reserve);

            m_has_event = true;
        }

        // Done Initializing
        m_initialized = true;
    }

    uint32_t                m_id;                   ///< ID of detector bank
    std::vector<uint32_t>   m_logical_pixelids;     ///< Logical PixelIds in detector bank
    uint32_t                m_buf_reserve;          ///< Event buffer initial capacity
    uint32_t                m_idx_buf_reserve;      ///< Index buffer initial capacity
    bool                    m_initialized;          ///< Has detector bank been initialized yet?

    uint64_t                m_event_count;          ///< Running event count
    uint64_t                m_histo_event_count;    ///< Running Histogram event count
    uint64_t                m_histo_event_uncounted;///< Running Histogram uncounted events
    uint64_t                m_last_pulse_with_data; ///< Index of last pulse with data for this bank

    bool                    m_has_event;            ///< Has an Event output already been defined?
    std::vector<uint64_t>   m_index_buffer;         ///< Event index buffer
    std::vector<float>      m_tof_buffer;           ///< Time of flight buffer (microseconds)
    std::vector<uint32_t>   m_pid_buffer;           ///< Pixel ID buffer

    bool                    m_has_histo;            ///< Has a Histogram output already been defined?
    uint32_t                m_num_tof_bins;         ///< Histo Number of TOF Bins
    uint32_t                m_base_pid;             ///< Base PixelId Offset into Histo Offset Index
    std::vector<int32_t>    m_histo_pid_offset;     ///< Histo PixelId Offsets into data buffer
    std::vector<uint32_t>   m_data_buffer;          ///< Histo data buffer
    std::vector<float>      m_tofbin_buffer;        ///< Histo TOF Bin buffer

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

