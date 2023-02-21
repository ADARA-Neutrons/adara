#ifndef STCDEFS_H
#define STCDEFS_H

#include <unistd.h>
#include <stdint.h>
#include <vector>
#include <sstream>
#include <string>
#include <syslog.h>
#include "ADARAUtils.h"
#include "ADARAPackets.h"

// Global syslog info
#define STC_VERSION "1.13.3"
extern pid_t g_pid;

#define STC_DOUBLE_EPSILON (0.00000000000001)

// Verbose Logging Self-Limiting/Metering... a la the Dreaded USleep()...!
// (for when Syslog won't let us log profusely without being suppressed!)

// Do the USleep() for 30000 Microseconds, i.e. 30 Milliseconds...
//#define give_syslog_a_chance  usleep(30000)

// Or

// Do Nothing...
#define give_syslog_a_chance

namespace STC {


// ============================================================================
// Neutron Event Classes and Types
// ============================================================================

/// Contains neutron pulse data
struct PulseInfo
{
    PulseInfo() : start_time(0), last_time(0), max_time(0)
    {}

    uint64_t                start_time;         ///< Time in nanoseconds of first pulse
    uint64_t                last_time;          ///< Time in nanoseconds of last received pulse
    uint64_t                max_time;           ///< Time in nanoseconds of maximum received pulse
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
        uint32_t a_state,           ///< [in] State of detector bank
        uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
        uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
    )
    :
        m_id(a_id),
        m_state(a_state),
        m_buf_reserve(a_buf_reserve),
        m_idx_buf_reserve(a_idx_buf_reserve),
        m_initialized(false),
        m_event_count(0),
        m_histo_event_count(0),
        m_histo_event_uncounted(0),
        m_last_pulse_with_data(0),
        m_has_event(false),
        m_tof_buffer_size(0),
        m_has_histo(false)
    {
        // Save Initialization for initializeBank() method...
    }

    /// BankInfo destructor
    virtual ~BankInfo()
    {}

    void initializeBank( bool a_end_of_run, uint32_t a_verbose_level )
    {
        // Already Initialized...
        if ( m_initialized )
            return;

        // Iterate Thru All Detector Bank Sets & Set Up Appropriate Data
        for ( std::vector<STC::DetectorBankSet *>::iterator dbs =
                m_bank_sets.begin(); dbs != m_bank_sets.end() ; ++dbs )
        {
            if ( !*dbs )
                continue;

            // Histo-based Detector Bank
            if ( (*dbs)->flags & ADARA::HISTO_FORMAT )
            {
                // Only Room for *One* Histogram per Detector Bank
                // (for now)
                if ( m_has_histo )
                {
                    syslog( LOG_ERR,
                    "[%i] %s %s %u %s %u %s! %s %s %s (%u to %u by %u).",
                        g_pid, "STC Error:", "Detector Bank", m_id,
                        "State", m_state,
                        "Duplicate Histogram Request", "Ignoring",
                        (*dbs)->name.c_str(), "Bank Set",
                        (*dbs)->tofOffset,
                        (*dbs)->tofMax, (*dbs)->tofBin );
                    give_syslog_a_chance;
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
                      "[%i] %s %s %u %s %u %s! %s %s %s (%u to %u by %u).",
                            g_pid, "STC Error:", "Detector Bank", m_id,
                            "State", m_state,
                            "No PixelIds for Histogram", "Ignoring",
                            (*dbs)->name.c_str(), "Bank Set",
                            (*dbs)->tofOffset, (*dbs)->tofMax,
                            (*dbs)->tofBin );
                        give_syslog_a_chance;

                        // Don't set "m_has_histo", just fall thru...
                        // (all subsequent Histo attempts will also fail)
                        continue;
                    }

                    // Did we reach End of Run with No Data for this Bank?
                    if ( a_end_of_run )
                    {
                        // Fake a Tiny Histogram to Save Space,
                        // Yet Remain Compatible... ;-D

                        m_num_tof_bins = 3;

                        // Divide the TOF Range "Evenly"...
                        m_tof_bin_size = (*dbs)->tofMax / 2;

                        syslog( LOG_INFO,
                            "[%i] %s %u %s %u %s: %s %s to %u, %s to %u",
                            g_pid, "Detector Bank", m_id,
                            "State", m_state,
                            "Empty Histogram", "Setting",
                            "num_tof_bins", m_num_tof_bins,
                            "tof_bin_size", m_tof_bin_size);
                        give_syslog_a_chance;
                    }

                    else
                    {
                        // Number of Time Bin Values Needed...
                        m_num_tof_bins =
                            ( ( (*dbs)->tofMax - (*dbs)->tofOffset )
                                / (*dbs)->tofBin ) + 1;

                        // If Max TOF doesn't divide evenly into TOF Bin
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
                                "[%i] %s %s %u %s %u %s: %s=%u < 2!",
                                g_pid, "STC Error:", "Detector Bank", m_id,
                                "State", m_state,
                                "Histogram Warning",
                                "num_tof_bins", m_num_tof_bins);
                            give_syslog_a_chance;
                            m_num_tof_bins = 2;
                        }

                        // Use the TOF Bin Size from the Detector Bank Set
                        m_tof_bin_size = (*dbs)->tofBin;
                    }

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

                    std::stringstream ss;
                    ss << "Detector Bank " << m_id
                        << " State " << m_state
                        << " Histogram: "
                        << m_num_tof_bins << " Time Bin Values, "
                        << (*dbs)->tofOffset << " to " << (*dbs)->tofMax
                        << " by " << m_tof_bin_size << ","
                        << " minPid=" << minPid << " maxPid=" << maxPid
                        << " offset_size=" << offset_size << " ("
                        << ( num_pids * ( m_num_tof_bins - 1 ) ) << ")";

                    syslog( LOG_INFO, "[%i] %s", g_pid, ss.str().c_str() );
                    give_syslog_a_chance;

                    // Actual Histogram Storage, Non-Inclusive Max TOF Bin
                    m_data_buffer.reserve( num_pids
                        * ( m_num_tof_bins - 1 ) );

                    // TOF Bin Values...
                    m_tofbin_buffer.reserve(m_num_tof_bins);

                    uint32_t tofbin = (*dbs)->tofOffset;
                    for (uint32_t i=0 ; i < m_num_tof_bins - 1 ; i++)
                    {
                        // Inverted Indexing, but it's the right count...!
                        for (uint32_t p=0 ; p < num_pids ; p++)
                            m_data_buffer.push_back(0);

                        m_tofbin_buffer.push_back((float)tofbin);
                        tofbin += m_tof_bin_size;
                    }

                    // Max TOF Bin Value...
                    m_tofbin_buffer.push_back((float)((*dbs)->tofMax));

                    // Verify Histogram Data Size
                    if ( m_data_buffer.size() !=
                            num_pids * ( m_num_tof_bins - 1 ) )
                    {
                        syslog( LOG_ERR,
                      "[%i] %s %s %u %s %u %s: %s %s.size()=%lu vs. %s %u",
                            g_pid, "STC Error:", "Detector Bank", m_id,
                            "State", m_state,
                            "Histogram", "Verifying",
                            "m_data_buffer", m_data_buffer.size(),
                            "expected",
                            num_pids * ( m_num_tof_bins - 1 ) );
                        give_syslog_a_chance;
                    }

                    // Reserve Required Index Size & Initialize Vector...
                    // (I hope there aren't huge gaps in the PixelIds...!)
                    m_histo_pid_offset.reserve( offset_size );
                    for (size_t i=0 ; i < offset_size ; i++)
                        m_histo_pid_offset.push_back( -1 );

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

                            // syslog( LOG_INFO,
                            // "[%i] %s %u %s %u %s: p=%u offset[%lu]=%d",
                                // g_pid, "Detector Bank", m_id,
                                // "State", m_state, "Histogram",
                                // p, index, m_histo_pid_offset[ index ] );
                            // give sleep a chance...
                            // give_syslog_a_chance;
                        }

                        // Duplicate PixelId!  (shouldn't happen...)
                        else
                        {
                            syslog( LOG_ERR,
                             "[%i] %s: %s %u %s %u has %s %lu - Ignoring!",
                                g_pid, "STC Error", "Detector Bank", m_id,
                                "State", m_state,
                                "Duplicate PixelId in Histo Offset Map",
                                index );
                            give_syslog_a_chance;

                            // Still need to increment offset past PixelId!
                            offset++;
                        }
                    }

                    if ( a_verbose_level > 2 )
                    {
                        syslog( LOG_INFO,
                            "[%i] %s %u %s %u Done with Histogram Init.",
                            g_pid, "Detector Bank", m_id,
                            "State", m_state );
                        give_syslog_a_chance;
                    }

                    // Got One, That's All We'll Ever Need... ;-D
                    m_has_histo = true;
                }
            }

            // Event-based Detector Bank
            if ( (*dbs)->flags & ADARA::EVENT_FORMAT )
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
    uint32_t                m_state;                ///< State of detector bank
    std::vector<uint32_t>   m_logical_pixelids;     ///< Logical PixelIds in detector bank
    std::vector<uint32_t>   m_physical_pixelids;    ///< Physical PixelIds in detector bank
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
    uint64_t                m_tof_buffer_size;      ///< "In Use" Size of Time of flight buffer (microseconds)
    std::vector<uint32_t>   m_pid_buffer;           ///< Pixel ID buffer

    bool                    m_has_histo;            ///< Has a Histogram output already been defined?
    uint32_t                m_num_tof_bins;         ///< Histo Number of TOF Bins
    uint32_t                m_tof_bin_size;         ///< Histo Actual TOF Bin Size (differs for "empty")
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
    uint32_t                format;
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
        BeamMonitorConfig &a_config  ///< [in] Beam Mon Histo Config
    )
    :
        m_id(a_id),
        m_event_count(0),
        m_event_uncounted(0),
        m_last_pulse_with_data(0),
        m_tof_buffer_size(0),
        m_config(a_config)
    {
        // Histo-based Monitor
        if ( m_config.format == ADARA::HISTO_FORMAT )
        {
            // Number of Time Bin Values Needed...
            m_num_tof_bins = ( ( m_config.tofMax - m_config.tofOffset )
                / m_config.tofBin ) + 1;

            // If Max TOF doesn't divide evenly into TOF Bin size,
            // then need "One Extra" Bin Value...
            if ( ( m_config.tofMax - m_config.tofOffset )
                    % m_config.tofBin )
            {
                m_num_tof_bins++;
            }

            // Fail Safe: Make Sure We Get At Least One Actual TOF Bin!
            if ( m_num_tof_bins < 2 )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s %u Histogram Warning: %s=%u < 2!",
                    g_pid, "STC Error:", "Beam Monitor", m_id,
                    "num_tof_bins", m_num_tof_bins);
                give_syslog_a_chance;
                m_num_tof_bins = 2;
            }

            // Actual Histogram Storage, Non-Inclusive Max TOF Bin...
            m_data_buffer.reserve(m_num_tof_bins - 1);

            // TOF Bin Values...
            m_tofbin_buffer.reserve(m_num_tof_bins);

            syslog( LOG_INFO,
                "[%i] Beam Monitor %u Histogram: %u %s, %u to %u by %u",
                g_pid, m_id, m_num_tof_bins, "Time Bin Values",
                m_config.tofOffset, m_config.tofMax, m_config.tofBin );
            give_syslog_a_chance;

            uint32_t tofbin = m_config.tofOffset;
            for (uint32_t i=0 ; i < m_num_tof_bins - 1 ; i++)
            {
                m_data_buffer.push_back(0);

                m_tofbin_buffer.push_back((float)tofbin);
                tofbin += m_config.tofBin;
            }

            // Max TOF Bin Value...
            m_tofbin_buffer.push_back((float)(m_config.tofMax));
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
    uint64_t                m_tof_buffer_size;      ///< "In Use" Size of Time of flight buffer

    uint32_t                m_num_tof_bins;         ///< Histo Number of TOF Bins
    std::vector<uint32_t>   m_data_buffer;          ///< Histo data buffer
    std::vector<float>      m_tofbin_buffer;        ///< Histo TOF Bin buffer

    BeamMonitorConfig       m_config;               ///< Beam Monitor Config
};


/// BeamlineInformation extracted from BeamlineInfo packet payload
struct BeamlineInfo
{
    BeamlineInfo()
        : target_station_number(1)
    {}

    uint32_t                target_station_number;
    std::string             instr_id;
    std::string             instr_shortname;
    std::string             instr_longname;
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
    RunInfo()
        : run_number(0), run_title("NONE"),
            no_sample_info(false), save_pixel_map(false),
            run_notes_updates_enabled(true),
            // Initialize Double Values to Prevent Undue Randomization
            // from Unset Values Later...! ;-Q
            sample_mass(0.0), sample_mass_density(0.0),
            sample_height_in_container(0.0),
            sample_interior_diameter(0.0),
            sample_interior_height(0.0),
            sample_interior_width(0.0),
            sample_interior_depth(0.0),
            sample_outer_diameter(0.0),
            sample_outer_height(0.0),
            sample_outer_width(0.0),
            sample_outer_depth(0.0),
            sample_volume_cubic(0.0)
    {}

    uint32_t                run_number;
    std::string             run_title;
    std::string             proposal_id;
    std::string             proposal_title;
    std::string             das_version;
    std::string             facility_name;
    bool                    no_sample_info;
    bool                    save_pixel_map;
    bool                    run_notes_updates_enabled;
    std::string             sample_id;
    std::string             sample_name;
    std::string             sample_nature;
    std::string             sample_formula;
    double                  sample_mass;
    std::string             sample_mass_units;
    double                  sample_mass_density;
    std::string             sample_mass_density_units;
    std::string             sample_container_id;
    std::string             sample_container_name;
    std::string             sample_can_indicator;
    std::string             sample_can_barcode;
    std::string             sample_can_name;
    std::string             sample_can_materials;
    std::string             sample_description;
    std::string             sample_comments;
    double                  sample_height_in_container;
    std::string             sample_height_in_container_units;
    double                  sample_interior_diameter;
    std::string             sample_interior_diameter_units;
    double                  sample_interior_height;
    std::string             sample_interior_height_units;
    double                  sample_interior_width;
    std::string             sample_interior_width_units;
    double                  sample_interior_depth;
    std::string             sample_interior_depth_units;
    double                  sample_outer_diameter;
    std::string             sample_outer_diameter_units;
    double                  sample_outer_height;
    std::string             sample_outer_height_units;
    double                  sample_outer_width;
    std::string             sample_outer_width_units;
    double                  sample_outer_depth;
    std::string             sample_outer_depth_units;
    double                  sample_volume_cubic;
    std::string             sample_volume_cubic_units;
    std::vector<UserInfo>   users;
};


/// Run metrics collected by STC during translation
struct RunMetrics
{
    RunMetrics() : total_charge(0.0), events_counted(0),
        events_uncounted(0), events_unmapped(0), events_error(0),
        non_events_counted(0)
    {}

    double                  total_charge;
    uint64_t                events_counted;
    uint64_t                events_uncounted;
    uint64_t                events_unmapped;
    uint64_t                events_error;
    uint64_t                non_events_counted;
    struct timespec         run_start_time;
    struct timespec         run_end_time;
    Statistics              charge_stats;       ///< Pulse charge statistics
    Statistics              freq_stats;         ///< Pulse frequency statistics
    Statistics              scan_stats;         ///< Scan (index) statistics
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
    PVT_STRING,
    PVT_UINT_ARRAY,
    PVT_DOUBLE_ARRAY
};

/// Enumerated Type structure (for PVs of type PVT_ENUM)
struct PVEnumeratedType
{
    std::string                name;
    std::vector<std::string>   element_names;
    std::vector<uint32_t>      element_values;

    bool sameEnum( const PVEnumeratedType *a_enum,
            bool exactMatch /* "Exact" Match? (Device Enum Name) */ ) const
    {
        if ( a_enum == NULL )
            return( false );

        // For "Duplicate" PV Connections, We Don't Care
        // Whether the Device/Enum Name Matches,
        // Just the *Contents* (Element Names/Values)...! ;-D
        if ( exactMatch && name.compare( a_enum->name ) != 0 )
            return( false );

        if ( element_names.size() != a_enum->element_names.size() )
            return( false );

        for ( uint32_t i=0 ; i < element_names.size() ; i++ )
        {
            if ( element_names[i].compare(
                a_enum->element_names[i] ) != 0 )
            {
                return( false );
            }
        }

        if ( element_values.size() != a_enum->element_values.size() )
            return( false );

        for ( uint32_t i=0 ; i < element_values.size() ; i++ )
        {
            if ( element_values[i] != a_enum->element_values[i] )
                return( false );
        }

        return( true );
    }
};

/// Base class for all process variable (PV) types
class PVInfoBase
{
public:
    /// PVInfoBase constructor
    PVInfoBase
    (
        const std::string  &a_device_name,  ///< [in] Name of device that owns the PV
        const std::string  &a_name,         ///< [in] Name of PV
        const std::string  &a_connection,   ///< [in] PV Connection String
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type,         ///< [in] Type of PV
        std::vector<PVEnumeratedType>
                           *a_enum_vector,  ///< [in] Enumerated Type Vector of PV
        uint32_t            a_enum_index,   ///< [in] Enumerated Type Index of PV
        const std::string  &a_units,        ///< [in] Units of PV (empty if not needed)
        bool                a_ignore,       ///< [in] PV Ignore Flag
        bool                a_duplicate     ///< [in] Flag to Indicate PV Connection is a Duplicate...!
    )
    :
        m_device_name(a_device_name),
        m_name(a_name),
        m_connection(a_connection),
        m_device_id(a_device_id),
        m_pv_id(a_pv_id),
        m_type(a_type),
        m_enum_vector(a_enum_vector),
        m_enum_index(a_enum_index),
        m_units(a_units),
        m_ignore(a_ignore),
        m_last_time(0),
        m_has_non_normalized(false),
        m_duplicate(a_duplicate)
    {}

    /// PVInfoBase destructor
    virtual ~PVInfoBase()
    {}

    bool diffEnum(
            const PVEnumeratedType *a_enum1,
            const PVEnumeratedType *a_enum2,
            bool exactMatch /* "Exact" Match? (Device Enum Name) */ ) const
    {
        // Both NULL Enum Pointers... (Most Common Case...)
        if ( a_enum1 == NULL && a_enum2 == NULL )
            return( false );

        // Mismatched NULL Enum Pointers...
        if ( ( a_enum1 == NULL && a_enum2 != NULL )
                || ( a_enum1 != NULL && a_enum2 == NULL ) )
            return( true );

        // Compare 2 Non-Null Enums
        return( ! a_enum1->sameEnum( a_enum2, exactMatch ) );
    }

    bool sameDefinitionPVConn(
            const std::string &a_connection,
            const PVType a_type,
            const std::vector<PVEnumeratedType> *a_enum_vector,
            const uint32_t a_enum_index,
            const std::string &a_units,
            const bool a_ignore,
            bool exactMatch /* "Exact" Match? (Device Enum Name) */ ) const
    {
        const PVEnumeratedType *enum1 = NULL, *enum2 = NULL;

        if ( m_enum_vector != NULL && m_enum_index != (uint32_t) -1 )
            enum1 = &((*m_enum_vector)[ m_enum_index ]);

        if ( a_enum_vector != NULL && a_enum_index != (uint32_t) -1 )
            enum2 = &((*a_enum_vector)[ a_enum_index ]);

        if ( m_connection == a_connection
                && m_type == a_type
                && !diffEnum( enum1, enum2, exactMatch )
                && m_units == a_units && m_ignore == a_ignore ) {
            return true;
        }
        else {
            return false;
        }
    }

    /// Determine if PVs have equivalent definitions
    bool sameDefinitionPVConn( const PVInfoBase* a_pv,
            bool exactMatch /* "Exact" Match? (Device Enum Name) */ ) const
    {
        return sameDefinitionPVConn( a_pv->m_connection,
            a_pv->m_type, a_pv->m_enum_vector, a_pv->m_enum_index,
            a_pv->m_units, a_pv->m_ignore, exactMatch );
    }

    bool sameDefinition(
            const std::string &a_device_name,
            const std::string &a_name,
            const std::string &a_connection,
            const PVType a_type,
            const std::vector<PVEnumeratedType> *a_enum_vector,
            const uint32_t a_enum_index,
            const std::string &a_units,
            const bool a_ignore ) const
    {
        if ( m_device_name == a_device_name && m_name == a_name
                && sameDefinitionPVConn( a_connection,
                    a_type, a_enum_vector, a_enum_index,
                    a_units, a_ignore, true ) ) {
            return true;
        }
        else {
            return false;
        }
    }

    /// Determine if PVs have equivalent definitions
    bool sameDefinition( const PVInfoBase* a_pv ) const
    {
        return sameDefinition( a_pv->m_device_name,
            a_pv->m_name, a_pv->m_connection, a_pv->m_type,
            a_pv->m_enum_vector, a_pv->m_enum_index,
            a_pv->m_units, a_pv->m_ignore );
    }

    /// Virtual method to allow subclasses to write buffered PV values and time axis
    virtual int32_t flushBuffers( uint64_t start_time,
        struct RunMetrics *a_run_metrics = 0 ) = 0;

    virtual void createSTCConfigConditionalGroups(void) = 0;

    std::string         m_device_name;  ///< Name of device that owns the PV
    std::string         m_name;         ///< Name of PV
    std::string         m_connection;   ///< PV Connection String
    Identifier          m_device_id;    ///< ID of device that owns the PV
    Identifier          m_pv_id;        ///< ID of the PV
    std::string         m_device_str;   ///< Handy Device String for Logging
    std::string         m_pv_str;       ///< Handy PV String for Logging
    std::string         m_device_pv_str;///< Handy Device/PV String for Logging
    PVType              m_type;         ///< Type of PV
    std::vector<PVEnumeratedType>
                       *m_enum_vector;  ///< Enumerated Type Vector of PV
    uint32_t            m_enum_index;   ///< Enumerated Type Index of PV
    std::string         m_units;        ///< Units of PV
    bool                m_ignore;       ///< PV Ignore Flag
    Statistics          m_stats;        ///< Statistics of PV
    uint64_t            m_last_time;    ///< Nanosec time (EPICS epoch) of last received update
    std::vector<uint64_t> m_abs_time_buffer; ///< Buffer that holds absolute (non-normalized) timestamp (nanoseconds) of PV value updates
    std::vector<double> m_time_buffer;  ///< Buffer that holds time axis (seconds) of PV values
    bool                m_has_non_normalized; ///< This PV received value updates before 1st pulse...
    bool                m_duplicate;    ///< Flag to Indicate PV Connection is a Duplicate...!
};

/// Intermmediary PV template class that provides typed value buffer
template<class T>
class PVInfo : public PVInfoBase
{
public:
    /// PVInfo constructor
    PVInfo
    (
        const std::string  &a_device_name,  ///< [in] Name of device that owns the PV
        const std::string  &a_name,         ///< [in] Name of PV
        const std::string  &a_connection,   ///< [in] PV Connection String
        Identifier          a_device_id,    ///< [in] ID of device that owns the PV
        Identifier          a_pv_id,        ///< [in] ID of the PV
        PVType              a_type,         ///< [in] Type of PV
        std::vector<PVEnumeratedType>
                           *a_enum_vector,  ///< [in] Enumerated Type Vector of PV
        uint32_t            a_enum_index,   ///< [in] Enumerated Type Index of PV
        const std::string  &a_units,        ///< [in] Units of PV (empty if not needed)
        bool                a_ignore,       ///< [in] PV Ignore Flag
        bool                a_duplicate     ///< [in] Flag to Indicate PV Connection is a Duplicate...!
    )
    : PVInfoBase( a_device_name, a_name, a_connection,
        a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
        a_units, a_ignore, a_duplicate ),
    m_last_value_set(false), m_last_value_more(false),
    m_value_changed(false)
    {}

    /// PVInfo destructor
    virtual ~PVInfo()
    {}

    virtual void addToStats( T a_value ) = 0;

    /// Compare Two Uint32 PV Values
    bool valuesEqual
    (
        uint32_t value1,               ///< Uint32 Value
        uint32_t value2                ///< Uint32 Value
    )
    {
        return( value1 == value2 );
    }

    /// Compare Two Double PV Values
    bool valuesEqual
    (
        double value1,                 ///< Double Value
        double value2                  ///< Double Value
    )
    {
        return( approximatelyEqual( value1, value2, STC_DOUBLE_EPSILON ) );
    }

    /// Compare Two String PV Values
    bool valuesEqual
    (
        std::string value1,            ///< String Value
        std::string value2             ///< String Value
    )
    {
        return( !(value1.compare( value2 )) );
    }

    /// Compare Two Uint32 PV Arrays
    bool valuesEqual
    (
        std::vector<uint32_t> value1,  ///< Uint32 Array
        std::vector<uint32_t> value2   ///< Uint32 Array
    )
    {
        if ( value1.size() != value2.size() )
            return( false );

        for ( uint32_t i=0 ; i < value1.size() ; i++ )
        {
            if ( value1[i] != value2[i] )
                return( false );
        }

        return( true );
    }

    /// Compare Two Double PV Arrays
    bool valuesEqual
    (
        std::vector<double> value1,    ///< Double Array
        std::vector<double> value2     ///< Double Array
    )
    {
        if ( value1.size() != value2.size() )
            return( false );

        for ( uint32_t i=0 ; i < value1.size() ; i++ )
        {
            if ( !approximatelyEqual( value1[i], value2[i],
                    STC_DOUBLE_EPSILON ) ) {
                return( false );
            }
        }

        return( true );
    }

    /// Convert Uint32 PV Value to String
    std::string valueToString
    (
        uint32_t value                 ///< Uint32 Value
    )
    {
        std::stringstream ss;
        ss << value;
        return( ss.str() );
    }

    /// Convert Double PV Value to String
    std::string valueToString
    (
        double value                   ///< Double Value
    )
    {
        std::stringstream ss;
        ss << std::setprecision(17) << value;
        return( ss.str() );
    }

    /// Convert String PV Value to String (Lol... ;-D)
    std::string valueToString
    (
        std::string value              ///< String Value
    )
    {
        return( value );
    }

    /// Convert Uint32 PV Array to String
    std::string valueToString
    (
        std::vector<uint32_t> value    ///< Uint32 Array
    )
    {
        std::stringstream ss;
        ss << "[";
        for ( uint32_t j=0 ; j < value.size() ; j++ )
        {
            if ( j ) ss << ", ";
            ss << value[j];
        }
        ss << "]";
        return( ss.str() );
    }

    /// Convert Double PV Array to String
    std::string valueToString
    (
        std::vector<double> value      ///< Double Array
    )
    {
        std::stringstream ss;
        ss << "[" << std::setprecision(17);
        for ( uint32_t j=0 ; j < value.size() ; j++ )
        {
            if ( j ) ss << ", ";
            ss << value[j];
        }
        ss << "]";
        return( ss.str() );
    }

    /// Normalize PV values/timestamps relative to 1st Pulse Start Time
    void normalizeTimestamps
    (
        uint64_t start_time,        ///< 1st Pulse Time (nanosecs)
        uint32_t a_verbose_level    ///< Verbose Logging Level
    )
    {
        // Normalize All Non-Normalized Timestamps...

        int32_t last_pre_pulse_index = -1;

        bool done = false;

        for ( uint32_t i=0 ; !done && i < this->m_time_buffer.size(); ++i )
        {
            // Not-Yet-Normalized Time...
            if ( this->m_time_buffer[i] < 0.0 )
            {
                // Positive Time Update,
                // Normalize PV Time to 1st Pulse...
                if ( this->m_abs_time_buffer[i] > start_time )
                {
                    double t = ( this->m_abs_time_buffer[i] - start_time )
                        / NANO_PER_SECOND_D;
                    this->m_time_buffer[i] = t;

                    // Verbose Logging Level 2 or Above...
                    if ( a_verbose_level > 1 )
                    {
                        syslog( LOG_INFO,
                            "[%i] %s %s %s: %s = %s @ %lf",
                            g_pid, "PVInfo::normalizeTimestamps()",
                            this->m_device_str.c_str(),
                            "Positive Time Update",
                            this->m_pv_str.c_str(),
                            this->valueToString(
                                this->m_value_buffer[i] ).c_str(),
                            this->m_time_buffer[i] );
                        give_syslog_a_chance;
                    }

                    // Time is Normalized Now,
                    // We Can Add This Value to Stats.
                    addToStats( this->m_value_buffer[i] );
                }

                // Truncate Any Negative Time Offsets to 0.
                // Log Negative Time Truncation as Error
                // After 1st PV Value.
                // (otherwise we get spammed for nearly
                // every PV in the run! ;-D)
                else
                {
                    // Verbose Logging Level 2 or Above...
                    if ( a_verbose_level > 1 )
                    {
                        std::string log_hdr = "";
                        int log_type = LOG_INFO;
                        // Don't Log _Any_ of These as "Errors",
                        // Just Too Much Spam...! ;-b
                        // if ( this->m_last_value_set ) {
                            // log_type = LOG_ERR;
                            // log_hdr = "STC Error: ";
                        // }
                        std::stringstream ss;
                        ss << log_hdr;
                        ss << "PVInfo::normalizeTimestamps() ";
                        ss << this->m_device_pv_str;
                        ss << " = ";
                        ss << this->valueToString(
                            this->m_value_buffer[i] ).c_str();
                        ss << ":";
                        ss << " Truncate Negative Variable";
                        ss << " Value Update Time to Zero";
                        syslog( log_type,
                            "[%i] %s %lu.%09lu (%lu) < %lu.%09lu (%lu)",
                            g_pid, ss.str().c_str(),
                            (unsigned long)( this->m_abs_time_buffer[i]
                                    / NANO_PER_SECOND_LL )
                                - ADARA::EPICS_EPOCH_OFFSET,
                            (unsigned long)( this->m_abs_time_buffer[i]
                                    % NANO_PER_SECOND_LL ),
                            this->m_abs_time_buffer[i],
                            (unsigned long)( start_time
                                    / NANO_PER_SECOND_LL )
                                - ADARA::EPICS_EPOCH_OFFSET,
                            (unsigned long)( start_time
                                    % NANO_PER_SECOND_LL ),
                            start_time );
                        give_syslog_a_chance;
                    }

                    this->m_time_buffer[i] = 0.0;

                    last_pre_pulse_index = i;
                }
            }

            // Done...!
            else
                done = true;
        }

        // Now Trim Off Any But the Last Pre-First-Pulse
        // Variable Value Update (& Add This One to Stats!)
        if ( last_pre_pulse_index > 0 )
        {
            // Log PV Values We're About to Throw Away...
            std::stringstream ss;
            ss << "PVInfo::normalizeTimestamps() ";
            ss << this->m_device_pv_str;
            ss << ":";
            ss << " Purging Pre-First-Pulse Values [";
            for ( int32_t i=0 ; i < last_pre_pulse_index ; i++ )
            {
                if ( i ) ss << ", ";
                ss << this->valueToString( this->m_value_buffer[i] );
                ss << " @ ";
                ss << ( (unsigned long)( (this->m_abs_time_buffer[i])
                            / NANO_PER_SECOND_LL )
                        - ADARA::EPICS_EPOCH_OFFSET );
                ss << "." << std::setfill('0') << std::setw(9)
                    << ( (unsigned long)( (this->m_abs_time_buffer[i])
                            % NANO_PER_SECOND_LL ) );
                ss << " (" << this->m_abs_time_buffer[i] << ")";
            }
            ss << "]";
            ss << " (up to last_pre_pulse_index=";
            ss << last_pre_pulse_index;
            ss << ")";
            syslog( LOG_ERR,
               "[%i] %s %s < %lu.%09lu (%lu)",
                g_pid, "STC Error:", ss.str().c_str(),
                (unsigned long)( start_time
                        / NANO_PER_SECOND_LL )
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)( start_time
                        % NANO_PER_SECOND_LL ),
                start_time );
            give_syslog_a_chance;

            // Erase PV Value Updates Up to the
            // "Last" Pre-First-Pulse Update...

            this->m_value_buffer.erase(
                this->m_value_buffer.begin(),
                this->m_value_buffer.begin()
                    + last_pre_pulse_index );

            // Don't Bother Erasing Entries from Absolute Time Buffer,
            // We're Done With It Now, And About to Free the Whole Thing...

            this->m_time_buffer.erase(
                this->m_time_buffer.begin(),
                this->m_time_buffer.begin()
                    + last_pre_pulse_index );
        }

        // Now Add the "Last" Pre-First-Pulse Update
        // to the Stats for this PV...
        if ( last_pre_pulse_index >= 0 )
        {
            addToStats( this->m_value_buffer[0] );
        }

        // Done Normalizing This PV's Value Updates.
        this->m_abs_time_buffer.clear();
        this->m_has_non_normalized = false;
    }

    /// Subsume Values from a Duplicate PV Connection Log...
    void subsumeValues
    (
        std::vector<T> a_value_buffer,      ///< Duplicate PV Values
        std::vector<double> a_time_buffer   ///< Duplicate PV Timestamps
    )
    {
        // Go Thru PV Values/Timestamps and Subsume Any Omitted...

        typename std::vector<T>::iterator ival =
            this->m_value_buffer.begin();
        std::vector<double>::iterator itim =
            this->m_time_buffer.begin();

        uint32_t index = 0;

        typename std::vector<T>::iterator ivalDup =
            a_value_buffer.begin();
        std::vector<double>::iterator itimDup =
            a_time_buffer.begin();

        while ( ivalDup != a_value_buffer.end() )
        {
            // If We're at the End of Our Log Values, Then Just
            // Snag the Rest of the Duplicate's Values/Timestamps...
            if ( ival == this->m_value_buffer.end() )
            {
                syslog( LOG_ERR, "[%i] %s %s: %s - %s: %s @ %.9lf",
                    g_pid, "STC Error:", "PVInfo::subsumeValues()",
                    "END of Our Log",
                    "ADD Omitted Value/Timestamp from Duplicate",
                    valueToString( *ivalDup ).c_str(),
                    (*itimDup) );
                give_syslog_a_chance;
    
                this->m_value_buffer.insert( ival, *ivalDup );
                this->m_time_buffer.insert( itim, *itimDup );
    
                // Stay at the End of the Main PV, Reset Iterators...!
                ival = this->m_value_buffer.end();
                itim = this->m_time_buffer.end();

                ++ivalDup; ++itimDup;
            }

            // Skip Our Values/Timestamps that are Omitted in Duplicate...
            else if ( (*itim) < (*itimDup) - STC_DOUBLE_EPSILON )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s: %s: %s @ %.9lf < %.9lf [%lg]",
                    g_pid, "STC Error:", "PVInfo::subsumeValues()",
                    "Skip Our Value/Timestamp Omitted in Duplicate",
                    valueToString( *ival ).c_str(),
                    (*itim), (*itimDup), STC_DOUBLE_EPSILON );
                give_syslog_a_chance;
    
                ++ival; ++itim; ++index;
            }

            // Add in Any Omitted Values/Timestamps from Duplicate...
            else if ( (*itimDup) < (*itim) - STC_DOUBLE_EPSILON )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s: %s: %s @ %.9lf < %.9lf [%lg]",
                    g_pid, "STC Error:", "PVInfo::subsumeValues()",
                    "ADD Omitted Value/Timestamp from Duplicate",
                    valueToString( *ivalDup ).c_str(),
                    (*itimDup), (*itim), STC_DOUBLE_EPSILON );
                give_syslog_a_chance;
    
                this->m_value_buffer.insert( ival, *ivalDup );
                this->m_time_buffer.insert( itim, *itimDup );
    
                // Reset Iterators...!
                ++index;
                ival = this->m_value_buffer.begin() + index;
                itim = this->m_time_buffer.begin() + index;

                ++ivalDup; ++itimDup;
            }

            // Next Timestamp is Identical with Duplicate... 
            else if ( approximatelyEqual( *itim, *itimDup,
                    STC_DOUBLE_EPSILON ) )
            {
                // Skip Past Any Identical Values... (Present in Both Logs)
                if ( this->valuesEqual( *ival, *ivalDup ) )
                {
                    syslog( LOG_ERR,
                        "[%i] %s %s: %s: %s @ %.9lf == %s @ %.9lf [%lg]",
                        g_pid, "STC Error:", "PVInfo::subsumeValues()",
                        "Skip Past Identical Values",
                        valueToString( *ival ).c_str(), (*itim),
                        valueToString( *ivalDup ).c_str(), (*itimDup),
                        STC_DOUBLE_EPSILON );
                    give_syslog_a_chance;

                    ++ival; ++itim; ++index;
                    ++ivalDup; ++itimDup;
                }

                // Hmmm... Two Different Values at "Same" Timestamp...?!
                // Man, I hope these two PVs are _Really_ "Duplicates"...
                // Go Ahead and Subsume Duplicate's Value Anyway,
                // With Fingers Crossed... ;-Q
                else
                {
                    syslog( LOG_ERR,
                    "[%i] %s %s: %s %s: %s @ %.9lf vs %s @ %.9lf [%lg]",
                        g_pid, "STC Error:", "PVInfo::subsumeValues()",
                        "WHOA...! Two Different Values at Same Timestamp!",
                        "ADD Other Value/Timestamp from Duplicate Anyway",
                        valueToString( *ivalDup ).c_str(), (*itimDup),
                        valueToString( *ival ).c_str(), (*itim),
                        STC_DOUBLE_EPSILON );
                    give_syslog_a_chance;
     
                    // Insert These Weirdos _After_ Current Value...
                    ++ival; ++itim; ++index;
                    this->m_value_buffer.insert( ival, *ivalDup );
                    this->m_time_buffer.insert( itim, *itimDup );
    
                    // Reset Iterators...!
                    ++index;
                    ival = this->m_value_buffer.begin() + index;
                    itim = this->m_time_buffer.begin() + index;

                    ++ivalDup; ++itimDup;
                }
            }
        }
    }

    std::vector<T>      m_value_buffer; ///< Value buffer for PV

    std::string m_last_enum_string; ///< Enum for Last Recorded PV Value
    T   m_last_value;   ///< Last Recorded Value for PV (Conditional Groups)
    bool    m_last_value_set;   ///< Has there been a "Last Value"...? ;-D
    bool    m_last_value_more;  ///< More than just a "Last Value"...? ;-O
    bool    m_value_changed;    ///< Did PV Value "Change" During Run? :-D
};


// ============================================================================
// ADARA Stream Adapter Class Interface
// ============================================================================

/// Interface that ADARA stream adapter subclasses must implement
class IStreamAdapter
{
public:
    virtual bool            initialize( bool a_force_init = false,
                                std::string caller = "" ) = 0;
    virtual void            finalize( const RunMetrics &a_run_metrics,
                                const RunInfo &a_run_info ) = 0;
    virtual void            dumpProcessingStatistics(void) = 0;
    virtual PVInfoBase*     makePVInfo( const std::string &a_device_name,
                                const std::string &a_name,
                                const std::string &a_connection,
                                Identifier a_device_id,
                                Identifier a_pv_id,
                                PVType a_type,
                                std::vector<PVEnumeratedType>
                                    *a_enum_vector,
                                uint32_t a_enum_index,
                                const std::string &a_units,
                                bool a_ignore ) = 0;
    virtual BankInfo*       makeBankInfo( uint16_t a_id, uint32_t a_state,
                                uint32_t a_buf_reserve,
                                uint32_t a_idx_buf_reserve ) = 0;
    virtual MonitorInfo*    makeMonitorInfo( uint16_t a_id,
                                uint32_t a_buf_reserve,
                                uint32_t a_idx_buf_reserve,
                                STC::BeamMonitorConfig &a_config ) = 0;
    virtual void            updateRunInfo(
                                const RunInfo &a_run_info ) = 0;
    virtual void            processBeamlineInfo(
                                const BeamlineInfo &a_beamline_info,
                                bool a_force_init = false ) = 0;
    virtual void            processRunInfo(
                                const RunInfo &a_run_info ,
                                const bool a_strict ) = 0;
    virtual void            processGeometry(
                                const std::string &a_xml,
                                bool a_force_init = false ) = 0;
    virtual void            pulseBuffersReady(
                                STC::PulseInfo &a_pulse_info ) = 0;
    virtual void            bankPidTOFBuffersReady(
                                STC::BankInfo &a_bank ) = 0;
    virtual void            bankIndexBuffersReady(
                                STC::BankInfo &a_bank,
                                bool use_default_chunk_size ) = 0;
    virtual void            bankPulseGap( STC::BankInfo &a_bank,
                                uint64_t a_count ) = 0;
    virtual void            bankFinalize( STC::BankInfo &a_bank ) = 0;
    virtual void            monitorTOFBuffersReady(
                                STC::MonitorInfo &a_monitor_info ) = 0;
    virtual void            monitorIndexBuffersReady(
                                STC::MonitorInfo &a_monitor_info,
                                bool use_default_chunk_size ) = 0;
    virtual void            monitorPulseGap( STC::MonitorInfo &a_monitor,
                                uint64_t a_count ) = 0;
    virtual void            monitorFinalize(
                                STC::MonitorInfo &a_monitor ) = 0;
    virtual void            runComment( double a_time, uint64_t a_ts_nano,
                                const std::string &a_comment,
                                bool a_force_init = false ) = 0;
    virtual void            markerPause( double a_time,
                                uint64_t a_ts_nano,
                                const std::string &a_comment ) = 0;
    virtual void            markerResume( double a_time,
                                uint64_t a_ts_nano,
                                const std::string &a_comment ) = 0;
    virtual void            markerScanStart( double a_time,
                                uint64_t a_ts_nano,
                                uint32_t a_scan_index,
                                const std::string &a_scan_comment ) = 0;
    virtual void            markerScanStop( double a_time,
                                uint64_t a_ts_nano,
                                uint32_t a_scan_index,
                                const std::string &a_comment ) = 0;
    virtual void            markerComment( double a_time,
                                uint64_t a_ts_nano,
                                const std::string &a_comment ) = 0;
    virtual void            writeDeviceEnums( Identifier a_devId,
                                std::vector<STC::PVEnumeratedType>
                                    &a_enumVec ) = 0;
    virtual void            checkSTCConfigElementUnitsPaths(void) = 0;
    virtual uint32_t        executePrePostCommands(void) = 0;
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

} // End STC Namespace

#endif // STCDEFS_H

// vim: expandtab

