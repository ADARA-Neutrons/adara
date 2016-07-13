#include <stdexcept>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <libxml/tree.h>
#include "NxGen.h"
#include "TraceException.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"

// Do Stu's Dummy PixelId-Filled Histogram Test...
// #define HISTO_TEST

using namespace std;

/*! \brief Constructor for NxGen class.
 *
 * This constructor builds an NxGen instance using the options specified. If the nexus filename is empty, then no
 * nexus output file is produced. The chunk size is passed to the HDF5 library and the ADARA stream buffers are
 * sized based on integer multiples of the chunk size. The cache size relates tto HDF5 library processing and may affect
 * compression performance (i.e. when compression level > 0)
 */
NxGen::NxGen
(
    int             a_fd_in,                    ///< [in] File descriptor of input ADARA byte stream
    string         &a_adara_out_file,           ///< [in] Filename of output ADARA stream file (disabled if empty)
    string         &a_nexus_out_file,           ///< [in] Filename of output Nexus file (disabled if empty)
    bool            a_strict,                   ///< [in] Controls strict processing of input stream
    bool            a_gather_stats,             ///< [in] Controls stream statistics gathering
    unsigned long   a_chunk_size,               ///< [in] HDF5 chunk size
    unsigned short  a_event_buf_chunk_count,    ///< [in] ADARA event buffer size in chunks
    unsigned short  a_anc_buf_chunk_count,      ///< [in] ADARA ancillary buffer size in chunks
    unsigned long   a_cache_size,               ///< [in] HDF5 cache size
    unsigned short  a_compression_level         ///< [in] HDF5 compression level (0 = off to 9 = max)
)
:
    StreamParser( a_fd_in, a_adara_out_file, a_strict, a_gather_stats, a_chunk_size*a_event_buf_chunk_count,
                  a_chunk_size*a_anc_buf_chunk_count ),
    m_gen_nexus(false),
    m_nexus_filename(a_nexus_out_file),
    m_entry_path(string("/entry")),
    m_instrument_path(m_entry_path + string("/instrument")),
    m_daslogs_path(m_entry_path + string("/DASlogs")),
    m_daslogs_freq_path(m_daslogs_path + string("/frequency")),
    m_daslogs_pchg_path(m_daslogs_path + string("/proton_charge")),
    m_pid_name(string("event_id")),
    m_tof_name(string("event_time_offset")),
    m_index_name(string("event_index")),
    m_pulse_time_name(string("event_time_zero")),
    m_data_name(string("data")),
    m_histo_pid_name(string("pixel_id")),
    m_tofbin_name(string("time_of_flight")),
    m_chunk_size(a_chunk_size),
    m_h5nx(a_compression_level),
    m_pulse_info_cur_size(0),
    m_pulse_vetoes_cur_size(0),
    m_pulse_flags_cur_size(0),
    m_haveRunComment(false)
{
    // Capture STS "Start of Processing Time"...
    clock_gettime( CLOCK_REALTIME, &m_sts_start_time );

    if ( !a_nexus_out_file.empty() )
    {
        m_gen_nexus = true;
        m_h5nx.H5NXset_cache_size( a_cache_size );
    }

    // Reserve internal buffer for veto pulse times
    m_pulse_vetoes.reserve( a_chunk_size );
}


/// NxGen destructor
NxGen::~NxGen()
{
    // Nothing to do here, for now
}


/*! \brief Factory method for PVInfoBase instances
 *  \return A new PVInfoBase (derived) instance
 *
 * This method constructs Nexus-specific PVInfoBase objects for use by
 * the generalized process variable handlers in the StreamParser class.
 * Due to ADARA protocol limitations, only uint32, double and string
 * types are supported (others are mapped to these).
 */
STS::PVInfoBase*
NxGen::makePVInfo
(
    const string           &a_device_name,  ///< [in] Name of device that owns the PV
    const string           &a_name,         ///< [in] Name of PV
    const string           &a_connection,   ///< [in] PV Connection String
    STS::Identifier         a_device_id,    ///< [in] ID of device that owns the PV
    STS::Identifier         a_pv_id,        ///< [in] ID of the PV
    STS::PVType             a_type,         ///< [in] Type of PV
    std::vector<STS::PVEnumeratedType>
                           *a_enum_vector,  ///< [in] Enumerated Type Vector for PV
    uint32_t                a_enum_index,   ///< [in] Enumerated Type Index for PV
    const string           &a_units,        ///< [in] Units of PV (empty if not needed)
    bool                    a_ignore        ///< [in] PV Ignore Flag
)
{
    string internal_name = a_name;
    uint32_t name_ver = 0;

    string internal_connection = a_connection;
    uint32_t connection_ver = 0;

    set<string>::iterator i;

    // Check for PV Name Collisions: This code looks for the Name (Alias)
    // across all PV names and connection strings encountered thus far,
    // and if found increments/appends a version number.
    // Then it checks again to make sure _This_ auto-generated internal
    // name doesn't collide with an existing (top-level) name.
    // This continues until a version is found that doesn't collide.

    while ( 1 )
    {
        i = m_pv_name_history.find( internal_name );
        if ( i != m_pv_name_history.end() )
        {
            internal_name = a_name + "("
                + boost::lexical_cast<string>( ++name_ver ) + ")";
        }
        else
        {
            if ( name_ver > 0 )
            {
                syslog( LOG_ERR,
                    "[%i] %s Device %s: %s Clash %s -> %s",
                    g_pid, "STS Error:", a_device_name.c_str(),
                    "PV Name", a_name.c_str(), internal_name.c_str() );
                usleep(30000); // give syslog a chance...
            }
            m_pv_name_history.insert( internal_name );
            break;
        }
    }

    // Now Handle Connection String Issues/Collisions.

    // If the Name and Connection String were the same before,
    // then just make them the same again now... ;-D
    if ( a_name == a_connection )
    {
        internal_connection = internal_name;
    }

    // Otherwise Check for Connection String Collisions: This code looks
    // for this connection string across all PVs and if found increments
    // a version number.  Then it checks again to make sure _This_
    // auto-generated internal connection string doesn't collide with
    // an existing (top-level) PV name or connection string.
    // This continues until a version is found that doesn't collide.

    // Let's *Not* Assume that any Connection String collisions
    // correspond to 2 Different Aliases of the Same Variable, just
    // in case that happens Not to be true... Better to duplicate a
    // PV than throw away the values for a distinct PV with a Name Clash.)

    else
    {
        while ( 1 )
        {
            i = m_pv_name_history.find( internal_connection );
            if ( i != m_pv_name_history.end())
            {
                internal_connection = a_connection + "("
                    + boost::lexical_cast<string>( ++connection_ver )
                    + ")";
            }
            else
            {
                if ( connection_ver > 0 )
                {
                    syslog( LOG_ERR,
                        "[%i] %s Device %s: %s Clash %s -> %s",
                        g_pid, "STS Error:", a_device_name.c_str(),
                        "PV Connection String", a_connection.c_str(),
                        internal_connection.c_str() );
                    usleep(30000); // give syslog a chance...
                }
                m_pv_name_history.insert( internal_connection );
                break;
            }
        }
    }

    switch ( a_type )
    {
    case STS::PVT_INT:  // ADARA only supports uint32_t currently
    case STS::PVT_ENUM:
    case STS::PVT_UINT:
        return new NxPVInfo<uint32_t>( a_device_name,
            a_name, internal_name, a_connection, internal_connection,
            a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
            a_units, a_ignore, *this );
    case STS::PVT_FLOAT: // ADARA only supports double currently
    case STS::PVT_DOUBLE:
        return new NxPVInfo<double>( a_device_name,
            a_name, internal_name, a_connection, internal_connection,
            a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
            a_units, a_ignore, *this );
    case STS::PVT_STRING:
        return new NxPVInfo<string>( a_device_name,
            a_name, internal_name, a_connection, internal_connection,
            a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
            a_units, a_ignore, *this );
    case STS::PVT_UINT_ARRAY:
        return new NxPVInfo< vector<uint32_t> >( a_device_name,
            a_name, internal_name, a_connection, internal_connection,
            a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
            a_units, a_ignore, *this );
    case STS::PVT_DOUBLE_ARRAY:
        return new NxPVInfo< vector<double> >( a_device_name,
            a_name, internal_name, a_connection, internal_connection,
            a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
            a_units, a_ignore, *this );
    }

    THROW_TRACE( STS::ERR_UNEXPECTED_INPUT,
        "makePVInfo() failed - invalid PV type: " << a_type );
}


/*! \brief Factory method for BankInfo instances
 *  \return A new BankInfo derived instance
 *
 * This method constructs Nexus-specific BankInfo objects. The Nexus-specific NxBankInfo extends the BankInfo class to
 * include a number of attributes needed for writing banked event data efficiently to a Nexus file.
 */
STS::BankInfo*
NxGen::makeBankInfo
(
    uint16_t a_id,              ///< [in] ID of detector bank
    uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
)
{
    try
    {
        NxBankInfo* bi = new NxBankInfo( a_id,
            a_buf_reserve, a_idx_buf_reserve, *this );

        // "Late" Initialization Now via NxGen::initializeNxBank()...
        // (after All BankInfos & Any Detector Bank Sets have been defined)

        return bi;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "makeBankInfo( bank: " << a_id << " ) failed." )
    }
}


/*! \brief Initialization method for BankInfo NeXus file groups & datasets
 *
 * Late Initialization of NeXus Groups and Datasets, specific to whether
 * Events or Histograms (or Both) are to be Output.  Called after both the
 * BankInfo instances have all been created (PixelMapPkt) and any
 * Detector Bank Sets have been defined (DetectorBankSetsPkt).
 */
void
NxGen::initializeNxBank
(
    NxBankInfo* a_bi,           ///< [in] Ptr to NeXus detector bank info
    bool a_end_of_run           ///< [in] Is there more data yet to come?
)
{
    // Make Sure BankInfo has been (Late) Initialized...
    // (to know whether Events, Histo or Both...?)
    if ( !(a_bi->m_initialized) )
        a_bi->initializeBank( a_end_of_run );

    try
    {
        if ( m_gen_nexus)
        {
            // Instrument bank group (contains *Both* Event and Histo data)
            makeGroup( a_bi->m_instr_path, "NXdetector" );

            // NeXus Event-based Structures
            if ( a_bi->m_has_event )
            {
                // Event data
                makeDataset( a_bi->m_instr_path, m_tof_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS );
                makeDataset( a_bi->m_instr_path, m_pid_name,
                    NeXus::UINT32 );
                makeDataset( a_bi->m_instr_path, m_index_name,
                    NeXus::UINT64 );

                // Top-level Event data group
                makeGroup( a_bi->m_event_path, "NXevent_data" );
                makeLink( a_bi->m_tof_path,
                    a_bi->m_event_path + "/" + m_tof_name );
                makeLink( a_bi->m_pid_path,
                    a_bi->m_event_path + "/" + m_pid_name );
                makeLink( a_bi->m_index_path,
                    a_bi->m_event_path + "/" + m_index_name );

                // Link pulse time to bank event times
                makeLink( a_bi->m_time_path,
                    a_bi->m_instr_path + "/" + m_pulse_time_name );
                makeLink( a_bi->m_time_path,
                    a_bi->m_event_path + "/" + m_pulse_time_name );
            }

            // NeXus Histogram-based Structures
            if ( a_bi->m_has_histo )
            {
                // Histo data

                // (defer creation/writing of actual histogram data
                //     to bankFinalize(), create & write in one shot...)

                makeDataset( a_bi->m_instr_path, m_histo_pid_name,
                    NeXus::UINT32 );
                makeDataset( a_bi->m_instr_path, m_tofbin_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS );

                // Top-level Histo data group

                makeGroup( a_bi->m_histo_path, "NXdata" );

                // (defer linking of histogram data, which won't exist
                //     until later in bankFinalize()... :-)

                makeLink( a_bi->m_histo_pid_path,
                    a_bi->m_histo_path + "/" + m_histo_pid_name );
                makeLink( a_bi->m_tofbin_path,
                    a_bi->m_histo_path + "/" + m_tofbin_name );
            }

            // NeXus Structures are Now Initialized
            a_bi->m_nexus_init = true;
        }
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "initializeNxBank( bank: " << a_bi->m_id
            << ", end_of_run=" << a_end_of_run
            << " ) initialization failed." )
    }
}


/*! \brief Factory method for MonitorInfo instances
 *  \return A new MonitorInfo derived instance
 *
 * This method constructs Nexus-specific MonitorInfo objects.
 * The Nexus-specific NxMonitorInfo class extends the
 * MonitorInfo class to include a number of attributes needed
 * for writing monitor event data efficiently to a Nexus file.
 */
STS::MonitorInfo*
NxGen::makeMonitorInfo
(
    uint16_t a_id,                    ///< [in] ID of detector bank
    uint32_t a_buf_reserve,           ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve,       ///< [in] Index buffer initial capacity
    STS::BeamMonitorConfig *a_config, ///< [in] Beam Monitor Histo Config (opt)
    bool a_known_monitor              ///< [in] Is this a "Known" Monitor?
)
{
    try
    {
        NxMonitorInfo* mi = new NxMonitorInfo(
            a_id, a_buf_reserve, a_idx_buf_reserve,
            a_config, a_known_monitor, *this );

        if ( m_gen_nexus)
        {
            makeGroup( mi->m_path, mi->m_group_type );

            // Histo-based Monitor
            if ( mi->m_config != NULL )
            {
                makeDataset( mi->m_path, m_data_name,
                    NeXus::UINT32, "" );
                makeDataset( mi->m_path, m_tofbin_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS );

                writeScalar( mi->m_path, "distance",
                    mi->m_config->distance, "" );
                writeString( mi->m_path, "mode", "monitor" );
            }

            // Event-based Monitor
            else
            {
                makeDataset( mi->m_path, m_tof_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS );
                makeDataset( mi->m_path, m_index_name, NeXus::UINT64 );

                makeLink( m_daslogs_freq_path + "/time",
                    mi->m_path + "/" + m_pulse_time_name );
            }
        }

        return mi;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "makeMonitorInfo (mon: " << a_id << ") failed." )
    }
}


/*! \brief Initializes Nexus output file
 *
 * This method performs Nexus-specific initialization (creates file and
 * several HDF5 entries).
 */
void
NxGen::initialize()
{
    if (!m_gen_nexus)
        return;

    try
    {
        syslog( LOG_INFO, "[%i] Creating Nexus file: %s",
            g_pid, m_nexus_filename.c_str() );

        m_h5nx.H5NXcreate_file( m_nexus_filename );

        // Create general Nexus entries
        makeGroup( m_entry_path, "NXentry" );
        makeGroup( m_instrument_path, "NXinstrument" );
        makeGroup( m_daslogs_path, "NXcollection" );

        // Create pulse frequency log
        makeGroup( m_daslogs_freq_path, "NXlog" );
        makeDataset( m_daslogs_freq_path, "time",
            NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( m_daslogs_freq_path, "value",
            NeXus::FLOAT64, FREQ_UNITS );

        // Create proton charge log (time same as pulse frequency)
        makeGroup( m_daslogs_pchg_path, "NXlog" );
        makeDataset( m_daslogs_pchg_path, "value",
            NeXus::FLOAT64, CHARGE_UNITS );
        makeLink( m_daslogs_freq_path + "/time",
            m_daslogs_pchg_path + "/time" );

        // Create pulse veto log
        makeGroup( m_daslogs_path + "/Veto_pulse", "NXcollection" );
        makeDataset( m_daslogs_path + "/Veto_pulse", "veto_pulse_time",
            NeXus::FLOAT64, TIME_SEC_UNITS );

        // Create pulse flag log
        makeGroup( m_daslogs_path + "/pulse_flags", "NXcollection" );
        makeDataset( m_daslogs_path + "/pulse_flags", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( m_daslogs_path + "/pulse_flags", "value",
            NeXus::UINT32 );

        // Create pause event log
        makeGroup( m_daslogs_path + "/pause", "NXlog" );
        makeDataset( m_daslogs_path + "/pause", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( m_daslogs_path + "/pause", "value", NeXus::UINT16 );

        // Create scan event log
        makeGroup( m_daslogs_path + "/scan_index", "NXlog" );
        makeDataset( m_daslogs_path + "/scan_index", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( m_daslogs_path + "/scan_index", "value",
            NeXus::UINT32 );

        // Create comment event log
        makeGroup( m_daslogs_path + "/comments", "NXcollection" );
        makeDataset( m_daslogs_path + "/comments", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS );

        // Insert initial "not in scan" value
        m_scan_time.push_back( 0.0 );
        m_scan_value.push_back( 0 );

        // Insert initial "not paused" value
        m_pause_time.push_back( 0.0 );
        m_pause_value.push_back( 0 );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "initialization of nexus file ("
            << m_nexus_filename << ") failed." )
    }
}


/*! \brief Finalizes Nexus output file
 *
 * This method performs Nexus-specific finalization (writes various metrics and closes file).
 */
void
NxGen::finalize
(
    const STS::RunMetrics &a_run_metrics    ///< [in] Run metrics object
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        writeString( m_entry_path, "definition", "NXsnsevent" );

        writeScalar( m_daslogs_freq_path, "minimum_value",
            a_run_metrics.freq_stats.min(), FREQ_UNITS );
        writeScalar( m_daslogs_freq_path, "maximum_value",
            a_run_metrics.freq_stats.max(), FREQ_UNITS );
        writeScalar( m_daslogs_freq_path, "average_value",
            a_run_metrics.freq_stats.mean(), FREQ_UNITS );
        writeScalar( m_daslogs_freq_path, "average_value_error",
            a_run_metrics.freq_stats.stdDev(), FREQ_UNITS );

        writeScalar( m_daslogs_pchg_path, "minimum_value",
            a_run_metrics.charge_stats.min(), CHARGE_UNITS );
        writeScalar( m_daslogs_pchg_path, "maximum_value",
            a_run_metrics.charge_stats.max(), CHARGE_UNITS );
        writeScalar( m_daslogs_pchg_path, "average_value",
            a_run_metrics.charge_stats.mean(), CHARGE_UNITS );
        writeScalar( m_daslogs_pchg_path, "average_value_error",
            a_run_metrics.charge_stats.stdDev(), CHARGE_UNITS );

        // Flush any remaining pulse vetos
        if ( m_pulse_vetoes.size() )
        {
            writeSlab( m_daslogs_path + "/Veto_pulse/veto_pulse_time",
                m_pulse_vetoes, m_pulse_vetoes_cur_size );
            m_pulse_vetoes_cur_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
        }

        // Flush any remaining pulse flags
        if ( m_pulse_flags_time.size() )
        {
            writeSlab( m_daslogs_path + "/pulse_flags/time",
                m_pulse_flags_time, m_pulse_flags_cur_size );
            writeSlab( m_daslogs_path + "/pulse_flags/value",
                m_pulse_flags_value, m_pulse_flags_cur_size );
            m_pulse_flags_cur_size +=  m_pulse_flags_time.size();
            m_pulse_flags_time.clear();
            m_pulse_flags_value.clear();
        }

        // Flush stream marker data
        flushPauseData();
        flushScanData();
        flushCommentData();

        // Capture Run Total Duration (for processing bandwidth statistics)
        m_duration = calcDiffSeconds(
            a_run_metrics.end_time, a_run_metrics.start_time );

        writeScalar( m_entry_path, "duration",
            m_duration, TIME_SEC_UNITS );
        writeScalar( m_entry_path, "total_pulses",
            a_run_metrics.charge_stats.count(), "" );

        // Link raw_frames to total_pulses for backward compatibility
        makeLink( m_entry_path + "/total_pulses",
            m_entry_path + "/raw_frames" );

        // Capture Run Total Counts (for processing bandwidth statistics)
        m_total_counts = a_run_metrics.events_counted;
        m_total_uncounts = a_run_metrics.events_uncounted;
        m_total_non_counts = a_run_metrics.non_events_counted;

        writeScalar( m_entry_path, "total_counts",
            a_run_metrics.events_counted, "" );

        writeScalar( m_entry_path, "total_uncounted_counts",
            a_run_metrics.events_uncounted, "" );
        writeScalarAttribute( m_entry_path + "/total_uncounted_counts",
            "ERROR_bit_or_unknown_other", a_run_metrics.events_error );
        writeScalarAttribute( m_entry_path + "/total_uncounted_counts",
            "events_have_no_bank", a_run_metrics.events_unmapped );

        writeScalar( m_entry_path, "total_other_counts",
            a_run_metrics.non_events_counted, "" );
        writeScalar( m_entry_path, "proton_charge",
            a_run_metrics.total_charge, CHARGE_UNITS );

        // Start time
        string time = timeToISO8601( a_run_metrics.start_time );
        writeString( m_entry_path, "start_time", time );

        // Add start time (offset) properties to all time axis in DAS logs
        writeStringAttribute( m_daslogs_freq_path + "/time",
            "offset", time );
        writeScalarAttribute( m_daslogs_freq_path + "/time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_freq_path + "/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/pause/time",
            "start", time );
        writeScalarAttribute( m_daslogs_path + "/pause/time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/pause/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/scan_index/time",
            "start", time );
        writeScalarAttribute( m_daslogs_path + "/scan_index/time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/scan_index/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/comments/time",
            "start", time );
        writeScalarAttribute( m_daslogs_path + "/comments/time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/comments/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "start", time );
        writeScalarAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/pulse_flags/time",
            "start", time );
        writeScalarAttribute( m_daslogs_path + "/pulse_flags/time",
            "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/pulse_flags/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.start_time.tv_nsec );

        // End time
        time = timeToISO8601( a_run_metrics.end_time );
        writeString( m_entry_path, "end_time", time );

        m_h5nx.H5NXclose_file();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "finalization of nexus file failed." )
    }
}


/*! \brief Dump Overall STS Processing Statistics
 *
 * This method dump the overall processing time/event bandwidth
 * for Nexus-specific output generation, including statistics
 * for the _Run Itself_ and how long the STS took to process it.
 */
void
NxGen::dumpProcessingStatistics(void)
{
    if (!m_gen_nexus)
        return;

    // Overall Run Statistics

    uint64_t total_counts =
        m_total_counts + m_total_uncounts + m_total_non_counts;

    syslog( LOG_INFO, "[%i] %s = %ld in %f seconds",
        g_pid, "Run Total Counts", total_counts, m_duration );
    usleep(30000); // give syslog a chance...

    double run_bandwidth = (double) total_counts / (double) m_duration;

    syslog( LOG_INFO, "[%i] %s = %lf events/sec",
        g_pid, "Overall Run Bandwidth", run_bandwidth );
    usleep(30000); // give syslog a chance...

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Counted(Det)", m_total_counts,
        (double) m_total_counts / (double) m_duration );
    usleep(30000); // give syslog a chance...

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Uncounted(Err)", m_total_uncounts,
        (double) m_total_uncounts / (double) m_duration );
    usleep(30000); // give syslog a chance...

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Non-Counts(Mon)", m_total_non_counts,
        (double) m_total_non_counts / (double) m_duration );
    usleep(30000); // give syslog a chance...

    // STS Processing Statistics

    struct timespec sts_end_time;

    clock_gettime( CLOCK_REALTIME, &sts_end_time );

    float sts_duration = calcDiffSeconds( sts_end_time, m_sts_start_time );

    syslog( LOG_INFO, "[%i] %s = %f seconds",
        g_pid, "Total STS Processing Time", sts_duration );
    usleep(30000); // give syslog a chance...

    double sts_bandwidth = (double) total_counts
        / (double) sts_duration;

    syslog( LOG_INFO, "[%i] %s = %lf events/sec",
        g_pid, "Overall STS Bandwidth", sts_bandwidth );
    usleep(30000); // give syslog a chance...

    double overhead_ratio = run_bandwidth / sts_bandwidth;

    syslog( LOG_INFO, "[%i] %s = %lf",
        g_pid, "STS Overhead Ratio", overhead_ratio );
    usleep(30000); // give syslog a chance...
}


/*! \brief Processes run information
 *
 * This method translates run information to the output Nexus file.
 */
void
NxGen::processRunInfo
(
    const STS::RunInfo & a_run_info     ///< [in] Run information object
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        writeScalar( m_instrument_path, "target_station_number",
            a_run_info.target_station_number, "" );

        writeString( m_instrument_path, "beamline", a_run_info.instr_id );

        if ( a_run_info.instr_longname.size())
        {
            writeString( m_instrument_path, "name",
                a_run_info.instr_longname );

            if ( a_run_info.instr_shortname.size())
            {
                writeStringAttribute( m_instrument_path + "/name",
                    "short_name", a_run_info.instr_shortname );
            }
        }

        string tmp = boost::lexical_cast<string>(a_run_info.run_number);
        writeString( m_entry_path, "run_number", tmp );
        writeString( m_entry_path, "entry_identifier", tmp );

        writeString( m_entry_path, "experiment_identifier",
            a_run_info.proposal_id );
        writeString( m_entry_path, "title", a_run_info.run_title );

        string sample_path = m_entry_path + "/sample";
        makeGroup( sample_path, "NXsample" );
        writeString( sample_path, "identifier", a_run_info.sample_id );
        writeString( sample_path, "name", a_run_info.sample_name );
        writeString( sample_path, "nature", a_run_info.sample_nature );
        writeString( sample_path, "chemical_formula",
            a_run_info.sample_formula );
        writeString( sample_path, "environment",
            a_run_info.sample_environment );

        writeScalar( sample_path, "mass", a_run_info.sample_mass, "g" );
        writeScalar( sample_path, "density",
            a_run_info.sample_density, "g cm-3" );

        writeString( sample_path, "component", // no container in NXsample
            a_run_info.sample_container );
        writeString( sample_path, "description",
            a_run_info.sample_description );
        writeString( sample_path, "comments", a_run_info.sample_comments );

        size_t user_count = 0;
        string path;
        for ( vector<STS::UserInfo>::const_iterator u =
                a_run_info.users.begin();
                u != a_run_info.users.end(); ++u )
        {
            path = m_entry_path + "/user"
                + boost::lexical_cast<string>(++user_count);
            makeGroup( path, "NXuser" );

            writeString( path, "facility_user_id", u->id );
            writeString( path, "name", u->name );
            writeString( path, "role", u->role );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "processRunInfo() failed." )
    }
}


/*! \brief Processes geometry information
 *
 * This method translates instrument geometry information to the output Nexus file.
 */
void
NxGen::processGeometry
(
    const std::string & a_xml   ///< [in] Geometry data in xml format
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        std::string geom_path = m_instrument_path + "/instrument_xml";
        makeGroup( geom_path, "NXnote" );
        writeString( geom_path, "description",
            "XML contents of the instrument IDF" );
        writeString( geom_path, "type", "text/xml" );
        writeString( geom_path, "data", a_xml );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "processGeometry() failed." )
    }
}


/*! \brief Writes pulse buffers to Nexus file
 *
 * This method writes time, frequency, and charge data in pulse buffers
 * to the Nexus file.
 */
void
NxGen::pulseBuffersReady
(
    STS::PulseInfo &a_pulse_info    ///< [in] Pulse data object
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        writeSlab( m_daslogs_freq_path + "/time",
            a_pulse_info.times, m_pulse_info_cur_size );
        writeSlab( m_daslogs_freq_path + "/value",
            a_pulse_info.freqs, m_pulse_info_cur_size );
        writeSlab( m_daslogs_pchg_path + "/value",
            a_pulse_info.charges, m_pulse_info_cur_size );

        m_pulse_info_cur_size += a_pulse_info.times.size();

        // Must process pulse flags linearly
        vector<double>::iterator t = a_pulse_info.times.begin();
        for ( vector<uint32_t>::iterator f = a_pulse_info.flags.begin();
                f != a_pulse_info.flags.end(); ++f, ++t )
        {
            // If any pulse flags are set (except veto),
            // write them to pulse_flags DASLog
            // (For Forward-/Backwards-Compatibility, Strip Off Veto Flags
            //  in top 12 bits of flags...)
            if ( (*f & 0xfffff) & ~ADARA::BankedEventPkt::PULSE_VETO )
            {
                m_pulse_flags_time.push_back( *t );
                m_pulse_flags_value.push_back(
                    (*f & 0xfffff) & ~ADARA::BankedEventPkt::PULSE_VETO );
            }

            // Write pulse vetoes to dedicated DASlog area
            if ( *f & ADARA::BankedEventPkt::PULSE_VETO )
                m_pulse_vetoes.push_back( *t );
        }

        if ( m_pulse_vetoes.size() > m_chunk_size )
        {
            writeSlab( m_daslogs_path + "/Veto_pulse/veto_pulse_time",
                m_pulse_vetoes, m_pulse_vetoes_cur_size );
            m_pulse_vetoes_cur_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
        }

        if ( m_pulse_flags_value.size() > m_chunk_size )
        {
            writeSlab( m_daslogs_path + "/pulse_flags/time",
                m_pulse_flags_time, m_pulse_flags_cur_size );
            writeSlab( m_daslogs_path + "/pulse_flags/value",
                m_pulse_flags_value, m_pulse_flags_cur_size );
            m_pulse_flags_cur_size +=  m_pulse_flags_time.size();
            m_pulse_flags_time.clear();
            m_pulse_flags_value.clear();
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "pulseBuffersReady() failed." )
    }
}


/*! \brief Writes bank event buffers to Nexus file
 *
 * This method writes time of flight, pixel ID, and index data in bank event buffers to the Nexus file.
 */
void
NxGen::bankBuffersReady
(
    STS::BankInfo &a_bank   ///< [in] Detector bank to write
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid bank object passed to bankBuffersReady()" )
        }

        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( false );

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_init) )
            initializeNxBank( bi, false );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            writeSlab( bi->m_tof_path,
                a_bank.m_tof_buffer, a_bank.m_tof_buffer_size,
                bi->m_event_cur_size );
            writeSlab( bi->m_pid_path,
                a_bank.m_pid_buffer, a_bank.m_tof_buffer_size,
                bi->m_event_cur_size );

            bi->m_event_cur_size += a_bank.m_tof_buffer_size;

            writeSlab( bi->m_index_path,
                a_bank.m_index_buffer, bi->m_index_cur_size );

            bi->m_index_cur_size += a_bank.m_index_buffer.size();
        }

        // No NeXus Histogram-based Handling Needed Here...

    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankBuffersReady() failed for bank id: "
            << a_bank.m_id )
    }
}


/*! \brief Fills pulse gaps in bank index dataset
 *
 * This method fills pulse gaps in the index dataset for a given bank
 * in the Nexus file.
 */
void
NxGen::bankPulseGap
(
    STS::BankInfo  &a_bank,     ///< [in] Detector bank with pulse gap
    uint64_t        a_count     ///< [in] Number of missing pulses
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid bank object passed to bankPulseGap()" )
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_init) )
            initializeNxBank( bi, false );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            fillSlab( bi->m_index_path,
                bi->m_event_count, a_count, bi->m_index_cur_size );
            bi->m_index_cur_size += a_count;
        }

        // No NeXus Histogram-based Handling Needed Here...

    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankPulseGap() failed for bank id: "
            << a_bank.m_id << ", gap count: " << a_count )
    }
}


/*! \brief Finalizes bank data in Nexus file
 *
 * This method writes event counts for the specified bank to the Nexus file.
 */
void
NxGen::bankFinalize
(
    STS::BankInfo &a_bank   ///< [in] Detector bank to finalize
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid bank object passed to bankFinalize()" )
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_init) )
            initializeNxBank( bi, true );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            string total_path = m_instrument_path + "/" + bi->m_name;
            writeScalar( total_path, "total_counts",
                bi->m_event_count, "" );
            makeLink( total_path + "/total_counts",
                m_entry_path + "/" + bi->m_eventname + "/total_counts" );
        }

        // NeXus Histogram-based Data...
        if ( bi->m_has_histo )
        {
            syslog( LOG_INFO,
                "[%i] Detector Bank %d - Writing Histogram Data",
                g_pid, a_bank.m_id );
            usleep(30000); // give syslog a chance...

            // Create & Write Histogram Multi-dimensional Data...
            std::vector<hsize_t> dims;
            dims.push_back( bi->m_logical_pixelids.size() );
            dims.push_back( bi->m_num_tof_bins - 1 );
#ifdef HISTO_TEST
            uint32_t num_pids = bi->m_logical_pixelids.size();
            syslog( LOG_INFO, "[%i] %s for %s [%u x %u]",
                g_pid, "Creating Dummy Histogram",
                bi->m_instr_path.c_str(),
                num_pids, bi->m_num_tof_bins - 1 );
            usleep(30000); // give syslog a chance...
            std::vector<uint32_t> dummy_histo;
            dummy_histo.reserve( num_pids
                * ( bi->m_num_tof_bins - 1 ) );
            for (uint32_t p=0 ; p < num_pids ; p++)
            {
                for (uint32_t i=0 ; i < bi->m_num_tof_bins - 1 ; i++)
                    dummy_histo.push_back( bi->m_logical_pixelids[p] );
            }
            writeMultidimDataset( bi->m_instr_path, m_data_name,
                dummy_histo, dims );
#else
            writeMultidimDataset( bi->m_instr_path, m_data_name,
                bi->m_data_buffer, dims );
#endif

            // Add "Axes" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute( bi->m_instr_path + "/" + m_data_name,
                "axes", m_histo_pid_name + "," + m_tofbin_name );

            // Add "Signal" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute( bi->m_instr_path + "/" + m_data_name,
                "signal", "1" );

            // Link Multi-dimensional Data into NXdata Histo group...
            makeLink( bi->m_data_path,
                bi->m_histo_path + "/" + m_data_name );

            // Write out Bank PixelIds...
            writeSlab( bi->m_histo_pid_path,
                bi->m_logical_pixelids, 0 );

            // Add "Axis" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute(
                bi->m_instr_path + "/" + m_histo_pid_name, "axis", "1" );

            // Write out TOF Bins...
            writeSlab( bi->m_tofbin_path, bi->m_tofbin_buffer, 0 );

            // Add "Axis" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute(
                bi->m_instr_path + "/" + m_tofbin_name, "axis", "2" );

            // Write Out Total Counts for Histograms, too... ;-D
            writeScalar( bi->m_histo_path, "total_counts",
                bi->m_histo_event_count, "" );
            writeScalar( bi->m_histo_path, "total_uncounted_counts",
                bi->m_histo_event_uncounted, "" );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankFinalize() failed for bank id: "
            << a_bank.m_id )
    }
}


/*! \brief Writes monitor event buffers to Nexus file
 *
 * This method writes time of flight and index data in monitor buffers
 * to the Nexus file.
 */
void
NxGen::monitorBuffersReady
(
    STS::MonitorInfo &a_monitor     ///< [in] Monitor with events to write
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid monitor object passed to monitorBuffersReady()" )
        }

        // Event-based Monitors Only...
        if ( mi->m_config == NULL )
        {
            writeSlab( mi->m_tof_path,
                a_monitor.m_tof_buffer, a_monitor.m_tof_buffer_size,
                mi->m_event_cur_size );
            mi->m_event_cur_size += a_monitor.m_tof_buffer_size;

            writeSlab( mi->m_index_path,
                a_monitor.m_index_buffer, mi->m_index_cur_size );
            mi->m_index_cur_size += a_monitor.m_index_buffer.size();
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorBuffersReady() failed for monitor id: "
            << a_monitor.m_id )
    }
}


/*! \brief Fills pulse gaps in monitor index dataset
 *
 * This method fills pulse gaps in the index dataset for a given monitor
 * in the Nexus file.
 */
void
NxGen::monitorPulseGap
(
    STS::MonitorInfo   &a_monitor,  ///< [in] Monitor with a pulse gap
    uint64_t            a_count     ///< [in] Number of missing pulses
)
{
    try
    {
        if (!m_gen_nexus)
            return;

        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid monitor object passed to monitorPulseGap()" )
        }

        // Event-based Monitors Only...
        if ( mi->m_config == NULL )
        {
            fillSlab( mi->m_index_path, mi->m_event_count,
                a_count, mi->m_index_cur_size );
            mi->m_index_cur_size += a_count;
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorPulseGap() failed for monitor id: "
            << a_monitor.m_id << ", gap count: " << a_count )
    }
}


/*! \brief Finalizes monitor data in Nexus file
 *
 * This method writes event counts for the specified monitor
 * to the Nexus file.
 */
void
NxGen::monitorFinalize
(
    STS::MonitorInfo &a_monitor     ///< [in] Monitor to finalize
)
{
    if (!m_gen_nexus)
        return;

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STS::ERR_CAST_FAILED,
                "Invalid monitor object passed to monitorFinalize()" )
        }

        // Histo-based Monitor
        if ( mi->m_config != NULL )
        {
            writeSlab( mi->m_data_path, mi->m_data_buffer, 0 );

            writeSlab( mi->m_tofbin_path, mi->m_tofbin_buffer, 0 );

            // Write Out Total Counts for Histogram Mode, too... ;-D
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_counts", mi->m_event_count, "" );
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_uncounted_counts", mi->m_event_uncounted, "" );
        }

        // Event-based Monitor
        else
        {
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_counts", mi->m_event_count, "" );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorFinalize() failed for monitor id: "
            << a_monitor.m_id )
    }
}


/*! \brief Sets the overall run comments in Nexus file
 *
 * This method writes the overall run comments to the Nexus file.
 */
void
NxGen::runComment
(
    const std::string &a_comment    ///< [in] Overall run comments
)
{
    if ( m_haveRunComment ) {
        syslog( LOG_WARNING,
        "[%i] %s Unexpected input: duplicate run comment specified: %s",
            g_pid, "STS Error:", a_comment.c_str() );
        usleep(30000); // give syslog a chance...
        return;
    }

    try
    {
        writeString( m_entry_path, "notes", a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "runComment() failed." )
    }

    m_haveRunComment = true;
}


/*! \brief Inserts a pause marker into Nexus file
 *
 * This method inserts a pause marker into the marker logs of the Nexus file.
 */
void
NxGen::markerPause
(
    double a_time,              ///< [in] Time associated with marker
    const string &a_comment     ///< [in] Comment associated with marker
)
{
    try
    {
        m_pause_time.push_back( a_time );
        m_pause_value.push_back( 1 ); // Current Nexus scan log calls for 1 to be used for pause

        if ( a_comment.size())
            markerComment( a_time, a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerPause() failed." )
    }
}


/*! \brief Inserts a resume marker into Nexus file
 *
 * This method inserts a resume marker into the marker logs of the Nexus file.
 */
void
NxGen::markerResume
(
    double a_time,              ///< [in] Time associated with marker
    const string &a_comment     ///< [in] Comment associated with marker
)
{
    try
    {
        m_pause_time.push_back( a_time );
        m_pause_value.push_back( 0 ); // Current Nexus scan log calls for 0 to be used for resume

        if ( a_comment.size())
            markerComment( a_time, a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerResume() failed." )
    }
}


/*! \brief Inserts a scan start marker into Nexus file
 *
 * This method inserts a scan start marker into the marker logs of the Nexus file.
 */
void
NxGen::markerScanStart
(
    double a_time,                      ///< [in] Time associated with marker
    unsigned long a_scan_index,         ///< [in] Scan index associated with scan
    const string &a_comment             ///< [in] Comment associated with scan
)
{
    try
    {
        m_scan_time.push_back( a_time );
        m_scan_value.push_back( a_scan_index );

        if ( a_comment.size())
            markerComment( a_time, a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerScanStart() failed." )
    }
}


/*! \brief Inserts a scan stop marker into Nexus file
 *
 * This method inserts a scan stop marker into the marker logs of the Nexus file.
 */
void
NxGen::markerScanStop
(
    double a_time,                      ///< [in] Time associated with marker
    unsigned long UNUSED(a_scan_index), ///< [in] Scan index associated with scan
    const string &a_comment             ///< [in] Comment associated with scan
)
{
    try
    {
        m_scan_time.push_back( a_time );
        m_scan_value.push_back( 0 ); // Current Nexus scan log calls for 0 to be used for all scan stops

        if ( a_comment.size())
            markerComment( a_time, a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerScanStop() failed." )
    }
}


/*! \brief Inserts a comment marker into Nexus file
 *
 * This method inserts a comment marker into the marker logs of the Nexus file.
 */
void
NxGen::markerComment
(
    double a_time,                      ///< [in] Time associated with marker
    const std::string &a_comment        ///< [in] Comment to insert
)
{
    try
    {
        if ( a_comment.size() )
        {
            m_comment_time.push_back( a_time );

            m_comment_vec.push_back( a_comment );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerComment() failed." )
    }
}


/*! \brief Writes buffered pause data into Nexus file
 *
 * This method flushes buffered pause data to the logs of the Nexus file.
 */
void
NxGen::flushPauseData()
{
    try
    {
        writeSlab( m_daslogs_path + "/pause/time", m_pause_time, 0 );
        writeSlab( m_daslogs_path + "/pause/value", m_pause_value, 0 );

        m_pause_time.clear();
        m_pause_value.clear();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "flushPauseData() failed." )
    }
}


/*! \brief Writes buffered scan data into Nexus file
 *
 * This method flushes buffered scan data to the logs of the Nexus file.
 */
void
NxGen::flushScanData()
{
    try
    {
        writeSlab( m_daslogs_path + "/scan_index/time", m_scan_time, 0 );
        writeSlab( m_daslogs_path + "/scan_index/value", m_scan_value, 0 );

        m_scan_time.clear();
        m_scan_value.clear();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "flushScanData() failed." )
    }
}


/*! \brief Writes buffered comment data into Nexus file
 *
 * This method flushes buffered comment data to the logs of the Nexus file.
 */
void
NxGen::flushCommentData()
{
    try
    {
        writeSlab( m_daslogs_path + "/comments/time", m_comment_time, 0 );

        // Comment Strings as 2D String Dataset

        // Determine Max Comment String Length...
        uint32_t max_len = (uint32_t) -1;
        for ( uint32_t i=0 ; i < m_comment_vec.size() ; i++ )
        {
            if ( max_len == (uint32_t) -1
                    || m_comment_vec[i].size() > max_len )
            {
                max_len = m_comment_vec[i].size();
            }
        }
        // Make Sure We Don't Freak Out HDF5 No Matter What...
        if ( max_len == (uint32_t) -1 || max_len == 0 )
            max_len = 1;

        syslog( LOG_INFO, "[%i] DASlogs Comments size=%lu max_len=%u",
            g_pid, m_comment_vec.size(), max_len );
        usleep(30000); // give syslog a chance...

        if ( m_comment_vec.size() )
        {
            vector<hsize_t> dims;
            dims.push_back( m_comment_vec.size() );
            dims.push_back( max_len );

            // Pad the Strings with Spaces to Be of Uniform Length...
            vector<string> value_vec;
            for ( uint32_t i=0 ; i < m_comment_vec.size() ; i++ )
            {
                string str = m_comment_vec[i];
                if ( str.size() < max_len )
                    str.insert( str.end(), max_len - str.size(), ' ' );
                value_vec.push_back( str );
            }
            writeMultidimDataset( m_daslogs_path + "/comments",
                "value", value_vec, dims );
        }
        else
        {
            syslog( LOG_INFO, "[%i] %s", g_pid,
                "No Comment Strings, Creating Empty Comments Value" );
            usleep(30000); // give syslog a chance...
            makeDataset( m_daslogs_path + "/comments",
                "value", NeXus::CHAR );
        }

        m_comment_time.clear();
        m_comment_vec.clear();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "flushCommentData() failed." )
    }
}


/*! \brief Writes PV Enumerated Type Definitions to NeXus logs
 *
 * This method creates Enumerated Type groups for the given DeviceId
 * in the NeXus DAS Logs.
 */
void
NxGen::writeDeviceEnums
(
    STS::Identifier a_devId,                 ///< [in] DeviceId
    vector<STS::PVEnumeratedType> &a_enumVec ///< [in/out] Vector of Enumerated Type Structs
)
{
    for ( vector<STS::PVEnumeratedType>::iterator ienum =
                a_enumVec.begin();
            ienum != a_enumVec.end(); ++ienum )
    {
        // First Check for Name Clashes - Increment Counter Til Clean...

        string enum_name = ienum->name;
        uint32_t count = 1;
        bool done;

        do
        {
            done = true;

            // Search Preceding Enums...
            for ( vector<STS::PVEnumeratedType>::iterator idup =
                        a_enumVec.begin();
                    idup != ienum; ++idup )
            {
                // Name Clash...!
                if ( !enum_name.compare( idup->name ) )
                {
                    count++;

                    stringstream ss;
                    ss << "Enum Log Group Name Clash - Device " << a_devId
                        << " Duplicate Enum Name " << enum_name
                        << ", Bumping Count to " << count
                        << " - Next Enum Name to Try: "
                        << ienum->name << "_" << count;
                    syslog( LOG_ERR, "[%i] %s %s",
                        g_pid, "STS Error:", ss.str().c_str() );
                    usleep(30000); // give syslog a chance...

                    stringstream ss_new;
                    ss_new << ienum->name << "_" << count;
                    enum_name = ss_new.str();

                    done = false;

                    break;
                }
            }
        }
        while ( !done );

        stringstream ss;
        ss << "Creating Enum Log Group for Device " << a_devId
            << " enum_name=" << enum_name;
        syslog( LOG_INFO, "[%i] %s", g_pid, ss.str().c_str() );
        usleep(30000); // give syslog a chance...

        // Save "New Name" for Enum, For Subsequent Comparisons...!
        ienum->name = enum_name;

        try
        {
            // Enum Group (NXcollection)

            stringstream ss;
            ss << m_daslogs_path << "/" << "Device" << a_devId
                << ":" << "Enum" << ":" << enum_name;

            makeGroup( ss.str(), "NXcollection" );

            makeDataset( ss.str(), "values", NeXus::UINT32 );

            // Does Everything "Match Up" for the "Easy" Enum Format...?
            bool easy = true;
            if ( ienum->element_values.size()
                    != ienum->element_names.size() )
            {
                // Dang, No Easy Solution... (I.e. Element Array Mismatch!)
                stringstream sss;
                sss << "STS Error:"
                    << " writeDeviceEnums() Element Array Mismatch"
                    << " for Device " << a_devId
                    << " Enum " << enum_name
                    << " - No Easy Strings!";
                syslog( LOG_ERR, "[%i] %s", g_pid, sss.str().c_str() );
                usleep(30000); // give syslog a chance...

                easy = false;
            }

            uint32_t max_len = (uint32_t) -1;

            for ( uint32_t i=0 ; i < ienum->element_names.size() ; i++ )
            {
                // Stuff in "Easy-to-Read" Per-Element Scalar Strings!
                if ( easy )
                {
                    stringstream ss_easy;
                    ss_easy << "name_" << ienum->element_values[i];
                    writeString( ss.str(), ss_easy.str(),
                        ienum->element_names[i] );
                }

                // Determine Max Element Name String Length...
                if ( max_len == (uint32_t) -1
                        || ienum->element_names[i].size() > max_len )
                {
                    max_len = ienum->element_names[i].size();
                }
            }

            // Make Sure We Don't Freak Out HDF5 No Matter What...
            if ( max_len == (uint32_t) -1 || max_len == 0 )
                max_len = 1;

            syslog( LOG_ERR, "[%i] Enum %s size=%lu max_len=%u", g_pid,
                ss.str().c_str(), ienum->element_names.size(), max_len );
            usleep(30000); // give syslog a chance...

            // Element Names as 2D String Dataset
            if ( ienum->element_names.size() )
            {
                vector<hsize_t> dims;
                dims.push_back( ienum->element_names.size() );
                dims.push_back( max_len );

                // Pad the Strings with Spaces to Be of Uniform Length...
                vector<string> names_vec;
                for ( uint32_t i=0 ;
                        i < ienum->element_names.size() ; i++ )
                {
                    string str = ienum->element_names[i];
                    if ( str.size() < max_len )
                        str.insert( str.end(), max_len - str.size(), ' ' );
                    names_vec.push_back( str );
                }

                writeMultidimDataset( ss.str(), "names", names_vec, dims );
            }
            else
            {
                syslog( LOG_ERR, "[%i] %s! %s for %s", g_pid,
                    "STS Error: Empty Enum Names",
                    "Creating Dummy Names", ss.str().c_str() );
                usleep(30000); // give syslog a chance...
                makeDataset( ss.str(), "names", NeXus::CHAR );
            }

            // Enum Element Values

            writeSlab( ss.str() + "/values", ienum->element_values, 0 );

            // Manually Create "Target" String for Linking
            // (as per makeGroupLink usage...)
            writeString( ss.str(), "target", ss.str() );
        }
        catch( TraceException &e )
        {
            // Don't Propagate TraceException, Just Log Failure...
            // (Enumerated Types are _Not_ Mission Critical (Hopefully!).
            stringstream sse;
            sse << "STS Error:"
                << " writeDeviceEnums() failed"
                << " for Device " << a_devId
                << " Enum " << enum_name
                << " " << e.toString( true, true );
            syslog( LOG_ERR, "[%i] %s", g_pid, sse.str().c_str() );
            usleep(30000); // give syslog a chance...
        }
    }
}


/*! \brief Converts a PVType to a Nexus NXnumtype
 *  \return The most appropriate Nxnumtype for the provided PVType
 *
 * This method converts the provided PVType to a Nexus NXnumtype.
 * Throws an exception for unknown / unsupported inputs.
 */
NeXus::NXnumtype
NxGen::toNxType
(
    STS::PVType a_type  ///< [in] PVType to be converted
) const
{
    switch( a_type )
    {
    case STS::PVT_INT:
        return NeXus::INT32;
    case STS::PVT_UINT:
    case STS::PVT_ENUM:
        return NeXus::UINT32;
    case STS::PVT_FLOAT:
        return NeXus::FLOAT32;
    case STS::PVT_DOUBLE:
        return NeXus::FLOAT64;
    case STS::PVT_STRING:
        return NeXus::CHAR;
    case STS::PVT_UINT_ARRAY:
        return NeXus::UINT32;
    case STS::PVT_DOUBLE_ARRAY:
        return NeXus::FLOAT64;
        break;
    }

    THROW_TRACE( STS::ERR_UNEXPECTED_INPUT, "toNxType() failed - invalid PV type: " << a_type )
}


/*! \brief Creates a Nexus group
 *
 * This method creates a Nexus group of the specified type in the output Nexus file.
 */
void
NxGen::makeGroup
(
    const string &a_path,   ///< [in] Nexus path of new group
    const string &a_type    ///< [in] Nexus type/class of new group
)
{
    if ( m_h5nx.H5NXmake_group( a_path, a_type ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_group() failed for path: " << a_path )
    }
}


/*! \brief Creates a Nexus Dataset
 *
 * This method creates a Nexus Dataset with the specified type and
 * (optional) units in the output Nexus file.
 */
void
NxGen::makeDataset
(
    const std::string  &a_path,     ///< [in] Nexus path of new dataset
    const std::string  &a_name,     ///< [in] Name of new dataset
    NeXus::NXnumtype    a_type,     ///< [in] Nexus type of new dataset
    const string        a_units     ///< [in] Optional units of new dataset
)
{
    if ( m_h5nx.H5NXcreate_dataset_extend( a_path, a_name, a_type,
            m_chunk_size ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
            "H5NXcreate_dataset_extend() failed for path: " << a_path
                << ", name: " << a_name )
    }

    if ( a_units.size() )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name,
                "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
                "H5NXmake_attribute_string() failed for path: " << a_path
                     << ", name: " << a_name )
        }
    }
}


/*! \brief Creates and Writes a Nexus Multi-dimensional Dataset
 *
 * This method Creates and Writes a Nexus Multi-dimensional Dataset
 * with the specified type and (optional) units in the output Nexus file.
 */
template <typename TypeT>
void
NxGen::writeMultidimDataset
(
    const std::string       &a_path,    ///< [in] Nexus path of new dataset
    const std::string       &a_name,    ///< [in] Name of new dataset
    std::vector<TypeT>      &a_data,    ///< [in] Multi-dim Data Array
    std::vector<hsize_t>    &a_dims,    ///< [in] Dimensions of Data
    const string            a_units     ///< [in] Optional units of dataset
)
{
    int cc;
    if ( (cc = m_h5nx.H5NXmake_dataset_vector( a_path, a_name, a_data,
            a_dims.size(), a_dims )) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
            "H5NXmake_dataset_vector() failed for path: " << a_path
                << ", name: " << a_name )
    }

    if ( a_units.size() )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name,
                "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
                "H5NXmake_attribute_string() failed for path: " << a_path
                     << ", name: " << a_name )
        }
    }
}


/*! \brief Creates a Nexus link
 *
 * This method creates a link from the source path
 * to the destination path in the output Nexus file.
 */
void
NxGen::makeLink
(
    const string &a_source_path,  ///< [in] Source path in Nexus file (must already exist)
    const string &a_dest_name     ///< [in] Destination path in Nexus file (must NOT exist)
)
{
    if ( m_h5nx.H5NXmake_link( a_source_path, a_dest_name ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
            "H5NXmake_link() failed for source: "
                << a_source_path << ", dest: " << a_dest_name )
    }
}


/*! \brief Creates a Nexus link to a GROUP (duh)
 *
 * This method creates a link from the source path of a GROUP
 * to the destination path in the output Nexus file.
 *
 * (and doesn't try to create any "target" attributes
 * for a dataset that doesn't actually exist... ;-b)
 */
void
NxGen::makeGroupLink
(
    const string &a_source_path,  ///< [in] Source path in Nexus file (must already exist)
    const string &a_dest_name     ///< [in] Destination path in Nexus file (must NOT exist)
)
{
    if ( m_h5nx.H5NXmake_group_link( a_source_path, a_dest_name )
            != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
            "H5NXmake_group_link() failed for source: "
                << a_source_path << ", dest: " << a_dest_name )
    }
}


/*! \brief Writes a string value to a Nexus location
 *
 * This method writes a string value to the specified location (path/dataset) in the output Nexus file.
 */
void
NxGen::writeString
(
    const string &a_path,       ///< [in] Path in Nexus file to write string
    const string &a_dataset,    ///< [in] Name of dataset at specified path to receive string value
    const string &a_value       ///< [in] String value to write
)
{
    if ( !a_value.empty() )
    {
        if ( m_h5nx.H5NXmake_dataset_string( a_path, a_dataset, a_value ) != SUCCEED )
        {
            THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_dataset_string() failed for path: " << a_path << ", value: "
                         << a_value )
        }
    }
}


/*! \brief Writes a string attribute to the specified Nexus path
 *
 * This method writes a string attribute value to the specified path in the output Nexus file.
 */
void
NxGen::writeStringAttribute
(
    const string &a_path,       ///< [in] Path in Nexus file to write attribute
    const string &a_attrib,     ///< [in] Name of the attribute
    const string &a_value       ///< [in] Value of the attribute
)
{
    if ( m_h5nx.H5NXmake_attribute_string( a_path, a_attrib, a_value ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_string() failed for path: " << a_path << ", attrib: "
                     << a_attrib << ", value: " << a_value )
    }
}


/*! \brief Writes a scalar value to the specified Nexus path
 *
 * This method writes a scalar value to the specified path in the output Nexus file. Units are optional (leave empty if
 * not wanted).
 */
template<typename T>
void
NxGen::writeScalar
(
    const std::string & a_path,     ///< [in] Path in Nexus file to write scalar
    const std::string & a_name,     ///< [in] Name of scalar
    T                   a_value,    ///< [in] New value of scalar
    const std::string & a_units     ///< [in] Units of scalar (optional)
)
{
    if ( m_h5nx.H5NXmake_dataset_scalar( a_path, a_name, a_value ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_dataset_scalar() failed for path: " << a_path << ", name: "
                     << a_name << ", value: " << a_value )
    }

    if ( a_units.size())
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name, "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_string() failed for path: " << a_path
                         << ", name: " << a_name )
        }
    }
}


/*! \brief Writes a scalar attribute to the specified Nexus path
 *
 * This method writes a scalar attribute to the specified path in the output Nexus file.
 */
template<typename T>
void
NxGen::writeScalarAttribute
(
    const std::string & a_path,     ///< [in] Path in Nexus file to write attribute
    const std::string & a_attrib,   ///< [in] Name of attribute
    T                   a_value     ///< [in] New value of attribute
)
{
    if ( m_h5nx.H5NXmake_attribute_scalar( a_path, a_attrib, a_value ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_scalar() failed for path: " << a_path << ", attrib: "
                     << a_attrib << ", value: " << a_value )
    }
}

// vim: expandtab

