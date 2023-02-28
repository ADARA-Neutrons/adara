#include <stdexcept>
#include <string.h>
#include <sstream>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <libxml/tree.h>
#include "NxGen.h"
#include "TraceException.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "combus/ComBus.h"
#include "UserIdLdap.h"

// Do Stu's Dummy PixelId-Filled Histogram Test...
// #define HISTO_TEST

using namespace std;

std::string NxGen::GroupNameIndex = "[XXX_INDEX_XXX]";

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
    string         &a_work_root,                ///< [in] Work Directory Root
    string         &a_work_base,                ///< [in] Work Directory Base
    string         &a_adara_out_file,           ///< [in] Filename of output ADARA stream file (disabled if empty)
    string         &a_nexus_out_file,           ///< [in] Filename of output Nexus file (disabled if empty)
    string         &a_config_file,              ///< [in] Filename of STC Config file (disabled if empty)
    bool            a_strict,                   ///< [in] Controls strict processing of input stream
    bool            a_gather_stats,             ///< [in] Controls stream statistics gathering
    unsigned long   a_chunk_size,               ///< [in] HDF5 chunk size (in Dataset Elements!)
    unsigned short  a_event_buf_chunk_count,    ///< [in] ADARA event buffer size in chunks
    unsigned short  a_anc_buf_chunk_count,      ///< [in] ADARA ancillary buffer size in chunks
    unsigned long   a_cache_size,               ///< [in] HDF5 cache size
    unsigned short  a_compression_level,        ///< [in] HDF5 compression level (0 = off to 9 = max)
    uint32_t        a_verbose_level             ///< [in] STC Verbosity Level
)
:
    StreamParser( a_fd_in,
        a_work_root, a_work_base, a_adara_out_file, a_config_file,
        a_strict, a_gather_stats,
        a_chunk_size * a_event_buf_chunk_count, // number of elements
        a_chunk_size * a_anc_buf_chunk_count, // number of elements
        a_verbose_level ),
    m_gen_nexus(false),
    m_nexus_init(false),
    m_nexus_beamline_init(false),
    m_nexus_filename(a_nexus_out_file),
    m_config_file(a_config_file),
    m_entry_path(string("/entry")),
    m_instrument_path(m_entry_path + string("/instrument")),
    m_daslogs_path(m_entry_path + string("/DASlogs")),
    m_daslogs_freq_path(m_daslogs_path + string("/frequency")),
    m_daslogs_pchg_path(m_daslogs_path + string("/proton_charge")),
    m_software_path(m_entry_path + string("/Software")),
    m_pid_name(string("event_id")),
    m_tof_name(string("event_time_offset")),
    m_index_name(string("event_index")),
    m_pulse_time_name(string("event_time_zero")),
    m_data_name(string("data")),
    m_histo_pid_name(string("pixel_id")),
    m_histo_pid_name_raw(string("pixel_id_raw")),
    m_tofbin_name(string("time_of_flight")),
    m_chunk_size(a_chunk_size),
    m_h5nx(a_compression_level),
    m_pulse_info_cur_size(0),
    m_pulse_vetoes_cur_size(0),
    m_pulse_flags_cur_size(0),
    m_nexus_run_comment_init(false),
    m_nexus_geometry_init(false)
{
    // Capture STC "Start of Processing Time"...
    clock_gettime( CLOCK_REALTIME, &m_stc_run_start_time );

    if ( !a_nexus_out_file.empty() )
    {
        m_gen_nexus = true;
        m_h5nx.H5NXset_cache_size( a_cache_size );
    }

    // Parse STC Config File
    if ( !m_config_file.empty() )
        parseSTCConfigFile( m_config_file );

    // Reserve internal buffer for veto pulse times (in Dataset Elements!)
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
 * scalar types are supported (others are mapped to these), in addition
 * to the new uint32 and double array PVs.
 */
STC::PVInfoBase*
NxGen::makePVInfo
(
    const string           &a_device_name,  ///< [in] Name of device that owns the PV
    const string           &a_name,         ///< [in] Name of PV
    const string           &a_connection,   ///< [in] PV Connection String
    STC::Identifier         a_device_id,    ///< [in] ID of device that owns the PV
    STC::Identifier         a_pv_id,        ///< [in] ID of the PV
    STC::PVType             a_type,         ///< [in] Type of PV
    std::vector<STC::PVEnumeratedType>
                           *a_enum_vector,  ///< [in] Enumerated Type Vector for PV
    uint32_t                a_enum_index,   ///< [in] Enumerated Type Index for PV
    const string           &a_units,        ///< [in] Units of PV (empty if not needed)
    bool                    a_ignore        ///< [in] PV Ignore Flag
)
{
    string internal_connection = a_connection;
    string internal_name = a_name;

    // *Only* When PV Name (Alias) is Different than PV Connection String,
    // Check for Name Clashes & Do Versioning of PV Name (Alias) Here...
    // (If the PV Name == Connection String, then the "Name" is Actually
    // just a copy of the Connection String, so Defer to Duplicates Below!)
    // [Note: This is True for All Recent PVSD Incarnations, tho this is
    // _Not_ True for Ancient Legacy Test Cases in the ADARA Test Harness;
    // But These Test Cases have been Checked and Have No Name Clashes.]

    if ( a_name.compare( a_connection ) )
    {
        // Check for PV Name Collisions:
        // This code looks for the Name (Alias) across all PV Names and
        // Connection Strings encountered thus far, and if found
        // Increments/Appends a Version Number to the Name.
        // Then, it checks again to make sure _This_ New Auto-Generated
        // Internal Name doesn't collide with an existing (top-level) Name.
        // This continues until a Version is found that doesn't collide.

        set<string>::iterator i;

        uint32_t name_ver = 0;

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
                        "[%i] %s %s: %s: Device %s: %s -> %s",
                        g_pid, "STC Error:", "makePVInfo()",
                        "PV Name Clash",
                        a_device_name.c_str(),
                        a_name.c_str(),
                        internal_name.c_str() );
                    give_syslog_a_chance;
                }
                m_pv_name_history.insert( internal_name );
                break;
            }
        }
    }

    // Now, Check for PV Connection String Collisions/Duplicates:
    // This code looks for this PV Connection String across All Known PVs,
    // and, if found, Marks This PV (and its Dopplegangers) as "Duplicate".

    // NOTE: We *Now* Indeed Assume that PV Connection Strings are
    // *Unique* Across a Given Beamline, so that any PV Connection String
    // Collisions _Must_ Correspond to 2 Different Aliases of the
    // Same Variable.

    // In the case of such Duplicates, We Collect All the PV Value Updates
    // for *All* Copies of the PV, in the hopes that we can Authoritatively
    // Verify a Complete Match, and/or Stitch Together _All_ the Values
    // from All Copies, as needed.

    vector<STC::PVInfoBase*>::iterator ipv = m_pvs_list.begin();

    bool isDuplicate = false;

    while ( ipv != m_pvs_list.end() )
    {
        // Check for Duplicate PV Connections...
        // (Ignore Device Name and PV Name (Alias)... ;-D)
        if ( (*ipv)->sameDefinitionPVConn( internal_connection,
                a_type, a_enum_vector, a_enum_index, a_units, a_ignore,
                false /* Not "Exact" Match (Device Enum Name)... */ ) )
        {
            // Mark This PV as a Duplicate...!
            // (And It's Doppleganger, Too...! ;-D)

            syslog( LOG_ERR,
                "[%i] %s %s: %s: Device %s %s == Device %s %s",
                g_pid, "STC Error:", "makePVInfo()",
                "Duplicate PV Connection",
                a_device_name.c_str(),
                internal_connection.c_str(),
                (*ipv)->m_device_name.c_str(),
                (*ipv)->m_connection.c_str() );
            give_syslog_a_chance;

            // Create New PVInfo as a "Duplicate", with flag set...
            isDuplicate = true;

            // Mark This Matching PV
            (*ipv)->m_duplicate = true;

            // Once We've Found One Duplicate,
            // We've Added Ourselves to the Chain... ;-D
            break;
        }

        // _ALSO_ Check for Legit PV Connection Collisions:
        // This code looks for the PV Connection String across
        // all PV Names and Connection Strings encountered thus far,
        // and if found Increments/Appends a Version Number to the Name.
        // Then, it checks again to make sure _This_ New Auto-Generated
        // Internal Name doesn't collide with an existing (top-level) Name.
        // This continues until a Version is found that doesn't collide.
        // Note: This can happen if the Enumerated Type for a PV
        // _Changes_ in the Middle of a Run...! ;-Q  (It happens... ;-)
        else if ( (*ipv)->m_connection == internal_connection )
        {
            syslog( LOG_ERR,
                "[%i] %s %s: %s: Device %s %s == Device %s %s",
                g_pid, "STC Error:", "makePVInfo()",
                "Duplicate PV Connection for Different PV Definition",
                a_device_name.c_str(),
                internal_connection.c_str(),
                (*ipv)->m_device_name.c_str(),
                (*ipv)->m_connection.c_str() );
            give_syslog_a_chance;

            set<string>::iterator i;

            uint32_t conn_ver = 0;

            while ( 1 )
            {
                i = m_pv_name_history.find( internal_connection );
                if ( i != m_pv_name_history.end() )
                {
                    internal_connection = a_connection + "("
                        + boost::lexical_cast<string>( ++conn_ver ) + ")";
                }
                else
                {
                    if ( conn_ver > 0 )
                    {
                        syslog( LOG_ERR,
                            "[%i] %s %s: %s: Device %s: %s -> %s",
                            g_pid, "STC Error:", "makePVInfo()",
                            "PV Connection Clash",
                            a_device_name.c_str(),
                            a_connection.c_str(),
                            internal_connection.c_str() );
                        give_syslog_a_chance;
                    }
                    m_pv_name_history.insert( internal_connection );

                    // Check for Matching Alias, Which Also Needs Fix...
                    if ( !a_name.compare( a_connection ) )
                    {
                        internal_name = internal_connection;
                        syslog( LOG_ERR,
                            "[%i] %s %s: %s: Device %s: %s -> %s",
                            g_pid, "STC Error:", "makePVInfo()",
                            "Associated PV Name (Alias) Clash",
                            a_device_name.c_str(),
                            a_name.c_str(),
                            internal_name.c_str() );
                        give_syslog_a_chance;
                    }

                    break;
                }
            }
        }

        ++ipv;
    }

    // If _Not_ a Duplicate PV Connection String,
    // Add This Connection String to the List of PV Names...

    if ( !isDuplicate )
    {
        m_pv_name_history.insert( internal_connection );
    }

    // Now Proceed to Create the New PVInfo Instance for This Device/PV...

    switch ( a_type )
    {
        case STC::PVT_INT:  // ADARA only supports uint32_t currently
        case STC::PVT_ENUM:
        case STC::PVT_UINT:
            return new NxPVInfo<uint32_t>( a_device_name,
                a_name, internal_name, a_connection, internal_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, isDuplicate, *this );
        case STC::PVT_FLOAT: // ADARA only supports double currently
        case STC::PVT_DOUBLE:
            return new NxPVInfo<double>( a_device_name,
                a_name, internal_name, a_connection, internal_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, isDuplicate, *this );
        case STC::PVT_STRING:
            return new NxPVInfo<string>( a_device_name,
                a_name, internal_name, a_connection, internal_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, isDuplicate, *this );
        case STC::PVT_UINT_ARRAY:
            return new NxPVInfo< vector<uint32_t> >( a_device_name,
                a_name, internal_name, a_connection, internal_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, isDuplicate, *this );
        case STC::PVT_DOUBLE_ARRAY:
            return new NxPVInfo< vector<double> >( a_device_name,
                a_name, internal_name, a_connection, internal_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, isDuplicate, *this );
    }

    THROW_TRACE( STC::ERR_UNEXPECTED_INPUT,
        "makePVInfo() failed - invalid PV type: " << a_type );
}


/*! \brief Factory method for BankInfo instances
 *  \return A new BankInfo derived instance
 *
 * This method constructs Nexus-specific BankInfo objects. The Nexus-specific NxBankInfo extends the BankInfo class to
 * include a number of attributes needed for writing banked event data efficiently to a Nexus file.
 */
STC::BankInfo*
NxGen::makeBankInfo
(
    uint16_t a_id,              ///< [in] ID of detector bank
    uint32_t a_state,           ///< [in] State of detector bank
    uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
)
{
    try
    {
        NxBankInfo* bi = new NxBankInfo( a_id, a_state,
            a_buf_reserve, a_idx_buf_reserve, *this );

        // "Late" Initialization Now via NxGen::initializeNxBank()...
        // (after All BankInfos & Any Detector Bank Sets have been defined)

        return bi;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "makeBankInfo( bank: " << a_id
            << " state: " << a_state << " ) failed." )
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
        a_bi->initializeBank( a_end_of_run, verbose() );

    if ( !m_gen_nexus )
        return;

    // Already Initialized...
    if ( a_bi->m_nexus_bank_init )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data...!!)
    if ( !initialize( true, "NxGen::initializeNxBank()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u",
            g_pid, "STC Error:", "NxGen::initializeNxBank()",
            "Failed to Force Initialize NeXus File",
            "Losing Bank Group/Data!!",
            "bank_id", a_bi->m_id );
        give_syslog_a_chance;
        return;
    }

    try
    {
        // Instrument bank group (contains *Both* Event and Histo data)
        makeGroup( a_bi->m_instr_path, "NXdetector" );

        // NeXus Event-based Structures
        if ( a_bi->m_has_event )
        {
            // (Defer creation/writing of actual Bank Pid/TOF data
            //    to bankPidTOFBuffersReady(), create & write
            //    in one shot...)

            // (Defer creation/writing of actual Bank Event Index data
            //    to bankIndexBuffersReady(), create & write
            //    in one shot...)

            // Top-level Event data group
            makeGroup( a_bi->m_event_path, "NXevent_data" );

            // (Defer linking of Top-level Event data, which
            //     won't exist until later in bank*BuffersReady()...)

            // (Defer linking of Pulse Time data, which won't exist
            //     until later in bankFinalize()... :-)
        }

        // NeXus Histogram-based Structures
        // ( a_bi->m_has_histo )

        // (Defer creation/writing of actual histogram data
        //     to bankFinalize(), create & write in one shot...)

        // (Defer linking of histogram data, which won't exist
        //     until later in bankFinalize()... :-)

        // NeXus Structures are Now Initialized
        a_bi->m_nexus_bank_init = true;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "initializeNxBank( bank: " << a_bi->m_id
            << " state: " << a_bi->m_state
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
STC::MonitorInfo*
NxGen::makeMonitorInfo
(
    uint16_t a_id,                    ///< [in] ID of detector bank
    uint32_t a_buf_reserve,           ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve,       ///< [in] Index buffer initial capacity
    STC::BeamMonitorConfig &a_config  ///< [in] Beam Monitor Config
)
{
    try
    {
        NxMonitorInfo* mi = new NxMonitorInfo(
            a_id, a_buf_reserve, a_idx_buf_reserve,
            a_config, *this );

        // Initialization now done separately via
        // NxGen::initializeNxMonitor(), to account for
        // Working Directory resolution and  NeXus data file creation,
        // which may be deferred...

        return mi;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "makeMonitorInfo (mon: " << a_id << ") failed." )
    }
}


/*! \brief Initialization method for MonitorInfo NeXus file groups
 *
 * Initialization of NeXus Monitor Groups, as extracted from original
 * makeMonitorInfo() method, to enable Monitor data capture and bookkeeping
 * prior to Working Directory resolution/NeXus data file creation.
 */
void
NxGen::initializeNxMonitor
(
    NxMonitorInfo* a_mi        ///< [in] Ptr to NeXus Monitor info
)
{
    if ( !m_gen_nexus )
        return;

    // Already Initialized...
    if ( a_mi->m_nexus_monitor_init )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data...!!)
    if ( !initialize( true, "NxGen::initializeNxMonitor()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u",
            g_pid, "STC Error:", "NxGen::initializeNxMonitor()",
            "Failed to Force Initialize NeXus File",
            "Losing Monitor Group/Meta-Data!!",
            "bank_id", a_mi->m_id );
        give_syslog_a_chance;
        return;
    }

    try
    {
        makeGroup( a_mi->m_path, a_mi->m_group_type );

        // Histo-based Monitor
        if ( a_mi->m_config.format == ADARA::HISTO_FORMAT )
        {
            // (Defer creation/writing of actual histogram data
            //     to monitorFinalize(), create & write in one shot...)

            writeScalar( a_mi->m_path, "distance",
                a_mi->m_config.distance, "" );
            writeString( a_mi->m_path, "mode", "monitor" );
        }

        // Event-based Monitor (Nothing to Do Here, All Deferred! :-D)

        // (Defer creation/writing of actual Monitor TOF data
        //    to monitorTOFBuffersReady(), create & write
        //    in one shot...)

        // (Defer creation/writing of actual Monitor Event Index data
        //    to monitorIndexBuffersReady(), create & write
        //    in one shot...)

        // (Defer creation of Pulse Time Link to monitorFinalize(),
        //    after the Dataset is sure to have been created...)

        // NeXus Structures are Now Initialized
        a_mi->m_nexus_monitor_init = true;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "initializeNxMonitor( id: " << a_mi->m_id
            << " ) initialization failed." )
    }
}


/*! \brief Initializes Nexus output file
 *
 * This method performs Nexus-specific initialization (creates file and
 * several HDF5 entries).
 *
 * Note: Returns "true" when NeXus file is Created and Initialized,
 * else "false".
 */
bool
NxGen::initialize( bool a_force_init, string caller )
{
    if ( !m_gen_nexus )
        return( false );

    if ( m_nexus_init )
    {
        //syslog( LOG_INFO, "[%i] %s: Nexus File Already Initialized: %s",
            //g_pid, "NxGen::initialize()",
            //m_nexus_filename.c_str() );
        //give_syslog_a_chance;
        return( true );
    }

    // Do We Need to Construct a Working Directory Path...?
    if ( !isWorkingDirectoryReady() )
    {
        if ( a_force_init )
        {
            if ( constructWorkingDirectory( a_force_init, caller ) )
                flushAdaraStreamBuffer();

            else
            {
                syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                    g_pid, "STC Error:", "NxGen::initialize()",
                    "Failed to Force Construction of Working Directory",
                    "Bailing... (Probably...)" );
                give_syslog_a_chance;
                return( false );
            }
        }
        else
        {
            syslog( LOG_WARNING,
                "[%i] %s: %s: %s=[%s] %s=[%s]",
                g_pid, "NxGen::initialize()",
                "Still Missing Info for Working Directory Construction",
                "FacilityName", getFacilityName().c_str(),
                "BeamShortName", getBeamShortName().c_str() );
            give_syslog_a_chance;
            return( false );
        }
    }

    try
    {
        syslog( LOG_INFO, "[%i] Creating Nexus File: %s%s",
            g_pid, getWorkingDirectory().c_str(),
            m_nexus_filename.c_str() );
        give_syslog_a_chance;

        m_h5nx.H5NXcreate_file( getWorkingDirectory() + m_nexus_filename );

        // Create general Nexus entries
        makeGroup( m_entry_path, "NXentry" );
        makeGroup( m_instrument_path, "NXinstrument" );
        makeGroup( m_daslogs_path, "NXcollection" );

        // Create pulse frequency log
        makeGroup( m_daslogs_freq_path, "NXlog" );

        // (Defer creation of actual frequency datasets
        //     to pulseBuffersReady(), create & write in one shot...)

        // Create proton charge log (time same as pulse frequency)
        makeGroup( m_daslogs_pchg_path, "NXlog" );

        // (Defer creation of actual proton charge datasets
        //     to pulseBuffersReady(), create & write in one shot...)

        // Create pulse veto log
        makeGroup( m_daslogs_path + "/Veto_pulse", "NXcollection" );

        // (Defer creation of actual pulse veto dataset
        //     to pulseBuffersReady() or finalize(),
        //     create & write in one shot...)

        // Create pulse flag log
        makeGroup( m_daslogs_path + "/pulse_flags", "NXcollection" );

        // (Defer creation of actual pulse flags datasets
        //     to pulseBuffersReady() or finalize(),
        //     create & write in one shot...)

        // Create Software Provenance Collection
        makeGroup( m_software_path, "NXcollection" );

        // Create ADARA STC Software Provenance Note
        makeGroup( m_software_path + "/Translation", "NXprocess" );
        writeString( m_software_path + "/Translation", "program",
            "ADARA STC" );
        writeString( m_software_path + "/Translation", "note",
            ADARA::ATTRIB );

        // ADARA STC Version String
        string stc_version = "ADARA STC ";
        stc_version += STC_VERSION;
        stc_version += ", Common ";
        stc_version += ADARA::VERSION;
        stc_version += ", ComBus ";
        stc_version += ADARA::ComBus::VERSION;
        stc_version += ", Tag ";
        stc_version += ADARA::TAG_NAME;
        writeString( m_software_path + "/Translation", "version",
            stc_version );

        // ADARA STC Processing Start Time
        struct timespec now;
        clock_gettime( CLOCK_REALTIME_COARSE, &now );
        string time = timeToISO8601( now );
        writeString( m_software_path + "/Translation", "date", time );

        // Set "NeXus Initialized" Flag, We're There! :-D
        m_nexus_init = true;
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "Initialization of NeXus File ("
            << m_nexus_filename << ") Failed." )
    }

    return( true );
}


/*! \brief Finalizes Nexus output file
 *
 * This method performs Nexus-specific finalization (writes various metrics and closes file).
 */
void
NxGen::finalize
(
    const STC::RunMetrics &a_run_metrics,   ///< [in] Run metrics object
    const STC::RunInfo &a_run_info          ///< [in] Run information object
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some meta-data...!)
    if ( !initialize( true, "NxGen::finalize()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::finalize()",
            "Failed to Force Initialize NeXus File",
            "Losing Final Run Meta-Data!" );
        give_syslog_a_chance;
        return;
    }

    try
    {
        writeString( m_entry_path, "definition", "NXsnsevent" );

        // Create ADARA Software Provenance Note
        makeGroup( m_software_path + "/DataAcquistion", "NXprocess" );
        writeString( m_software_path + "/DataAcquistion", "program",
            "ADARA SMS" );
        writeString( m_software_path + "/DataAcquistion", "version",
            a_run_info.das_version );
        writeString( m_software_path + "/DataAcquistion", "note",
            ADARA::ATTRIB );

        // ADARA SMS Processing Start Time
        string run_start_time_str =
            timeToISO8601( a_run_metrics.run_start_time );
        writeString( m_software_path + "/DataAcquistion", "date",
            run_start_time_str );

        // Make Sure We Have "Some" Overall Run Comment... ;-D
        if ( !m_nexus_run_comment_init ) {
            if ( m_runComment.size() > 0 ) {
                runComment( m_runComment_time, m_runComment_ts_nano,
                    m_runComment, true );
            }
            else {
                struct timespec now;
                clock_gettime( CLOCK_REALTIME_COARSE, &now );
                uint64_t dummy_ts_nano = timespec_to_nsec( now );
                double dummy_time =
                    ( dummy_ts_nano
                        - timespec_to_nsec( a_run_metrics.run_start_time )
                    ) / NANO_PER_SECOND_D;
                std::string dummy = "(unset)";
                syslog( LOG_INFO,
                    "[%i] %s: %s - %s [%s] %lu.%09lu (%s=%lu)",
                    g_pid, "NxGen::finalize()",
                    "No Run Comment Has Been Set For This Run",
                    "Setting Dummy Empty Run Comment", dummy.c_str(),
                    (unsigned long)(dummy_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(dummy_ts_nano % NANO_PER_SECOND_LL),
                    "dummy_ts_nano", dummy_ts_nano );
                give_syslog_a_chance;
                runComment( dummy_time, dummy_ts_nano, dummy, true );
            }
        }

        // Try to Make Sure We Have Geometry/IDF XML...
        if ( !m_nexus_geometry_init && m_geometryXml.size() > 0 )
            processGeometry( m_geometryXml, true );

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
            // Create Pulse Vetoes Dataset on First Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !m_pulse_vetoes_cur_size )
            {
                makeDataset(
                    m_daslogs_path + "/Veto_pulse", "veto_pulse_time",
                    NeXus::FLOAT64, TIME_SEC_UNITS,
                    m_pulse_vetoes.size() );
            }

            // Write Pulse Vetoes Time Buffer
            writeSlab( m_daslogs_path + "/Veto_pulse/veto_pulse_time",
                m_pulse_vetoes, m_pulse_vetoes_cur_size );
            m_pulse_vetoes_cur_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
        }

        // Make Sure We Create Empty Pulse Vetoes Log, Even if No Vetoes!
        else if ( !m_pulse_vetoes_cur_size )
        {
            makeDataset( m_daslogs_path + "/Veto_pulse", "veto_pulse_time",
                NeXus::FLOAT64, TIME_SEC_UNITS, 1 );
        }

        // Flush any remaining pulse flags
        if ( m_pulse_flags_time.size() )
        {
            // Create Pulse Flags Datasets on First Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !m_pulse_flags_cur_size )
            {
                makeDataset( m_daslogs_path + "/pulse_flags", "time",
                    NeXus::FLOAT64, TIME_SEC_UNITS,
                    m_pulse_flags_time.size() );
                makeDataset( m_daslogs_path + "/pulse_flags", "value",
                    NeXus::UINT32, "",
                    m_pulse_flags_value.size() );
            }

            // Write Pulse Flags Time and Value Buffers
            writeSlab( m_daslogs_path + "/pulse_flags/time",
                m_pulse_flags_time, m_pulse_flags_cur_size );
            writeSlab( m_daslogs_path + "/pulse_flags/value",
                m_pulse_flags_value, m_pulse_flags_cur_size );
            m_pulse_flags_cur_size +=  m_pulse_flags_time.size();
            m_pulse_flags_time.clear();
            m_pulse_flags_value.clear();
        }

        // Make Sure We Create Empty Pulse Flags Logs, Even if No Flags!
        else if ( !m_pulse_flags_cur_size )
        {
            makeDataset( m_daslogs_path + "/pulse_flags", "time",
                NeXus::FLOAT64, TIME_SEC_UNITS, 1 );
            makeDataset( m_daslogs_path + "/pulse_flags", "value",
                NeXus::UINT32, "", 1 );
        }

        // Flush stream marker data
        flushPauseData( timespec_to_nsec( a_run_metrics.run_start_time ) );
        flushScanData( timespec_to_nsec( a_run_metrics.run_start_time ),
            a_run_metrics );
        flushCommentData(
            timespec_to_nsec( a_run_metrics.run_start_time ) );

        // Capture Run Total Duration (for processing bandwidth statistics)
        m_duration = calcDiffSeconds(
            a_run_metrics.run_end_time, a_run_metrics.run_start_time );

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
        writeString( m_entry_path, "start_time", run_start_time_str );

        // Add start time (offset) properties to all time axis in DAS logs
        writeStringAttribute( m_daslogs_freq_path + "/time",
            "offset", run_start_time_str );
        writeScalarAttribute( m_daslogs_freq_path + "/time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_freq_path + "/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/pause/time",
            "start", run_start_time_str );
        writeScalarAttribute( m_daslogs_path + "/pause/time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/pause/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/scan_index/time",
            "start", run_start_time_str );
        writeScalarAttribute( m_daslogs_path + "/scan_index/time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/scan_index/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/comments/time",
            "start", run_start_time_str );
        writeScalarAttribute( m_daslogs_path + "/comments/time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/comments/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        writeStringAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "start", run_start_time_str );
        writeScalarAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute(
            m_daslogs_path + "/Veto_pulse/veto_pulse_time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        writeStringAttribute( m_daslogs_path + "/pulse_flags/time",
            "start", run_start_time_str );
        writeScalarAttribute( m_daslogs_path + "/pulse_flags/time",
            "offset_seconds", (uint32_t)a_run_metrics.run_start_time.tv_sec
                - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( m_daslogs_path + "/pulse_flags/time",
            "offset_nanoseconds",
            (uint32_t)a_run_metrics.run_start_time.tv_nsec );

        // End time
        string run_end_time_str =
            timeToISO8601( a_run_metrics.run_end_time );
        writeString( m_entry_path, "end_time", run_end_time_str );

        m_h5nx.H5NXclose_file();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "finalization of nexus file failed." )
    }
}


/*! \brief Globally Check for Any Captured PV Units Paths/Attributes
 *
 * This method Globally Checks for Any Captured PV Units Attributes,
 * using All Saved "Units Paths" & Units Values,
 * now that All PV Values have been processed...
 */
void
NxGen::checkSTCConfigElementUnitsPaths(void)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose the final meta-data...!!)
    // Although if we just Forced Working Directory construction
    // in StreamParser::finalizeStreamProcessing(), then
    // Now May Be Our Chance to Finally Create a NeXus Data File...! ;-D
    if ( !initialize( true, "NxGen::checkSTCConfigElementUnitsPaths()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:",
            "NxGen::checkSTCConfigElementUnitsPaths()",
            "Failed to Force Initialize NeXus File",
            "Losing STC Config Element Units Meta-Data!" );
        give_syslog_a_chance;
        return;
    }

    // Check Each Config Group in Turn
    // for Any Saved ElementInfo Units Paths...
    for ( uint32_t g=0 ; g < m_config_groups.size() ; g++ )
    {
        struct GroupInfo *G = &(m_config_groups[g]);

        writeSTCConfigUnitsAttributes( G, G->elements );

        // Check for Conditional Elements as Well...
        for ( uint32_t c=0 ; c < G->conditions.size() ; c++ )
        {
            struct ConditionInfo *C = &(G->conditions[c]);

            if ( C->is_set )
            {
                writeSTCConfigUnitsAttributes( G, C->elements );
            }
        }
    }
}


/*! \brief Write PV Units Attributes for Any Captured Units Paths
 *
 * This method Writes Any PV Units Attributes
 * using All Saved "Units Paths" & Units Values,
 * now that All PV Values have been processed...
 */
void
NxGen::writeSTCConfigUnitsAttributes(
        struct GroupInfo *G, std::vector<struct ElementInfo> &elements )
{
    std::map<std::string, std::string>::iterator it;

    // Check Each Element in Turn for Any Captured Units Attributes...
    for ( uint32_t e=0 ; e < elements.size() ; e++ )
    {
        struct ElementInfo *E = &(elements[e]);

        // IFF Units Attribute Not Already Set
        // (checked in NxGen::checkStringAttribute()),
        // then Set Units Attribute from ElementInfo
        // (If we captured a "Units Value" PV Value,
        // or else Explicit Units were Specified!)
        if ( ( E->unitsValue.size() && E->unitsValue.compare("(unset)") )
                || ( E->units.size() && E->units.compare("(unset)") ) )
        {
            // Add Given Units Attribute for Every Saved Element Path...
            for ( it = E->unitsPaths.begin() ;
                    it != E->unitsPaths.end() ; ++it )
            {
                std::string label;
                std::string units;

                // PV Units Value Supersedes Explicit Units
                if ( E->unitsValue.size()
                        && E->unitsValue.compare("(unset)") )
                {
                    label = "PV Value Units";
                    units = E->unitsValue;
                }
                else
                {
                    label = "Explicit Config Units";
                    units = E->units;
                }

                std::string existing_attr_value;
                bool attrWasSet = checkStringAttribute(
                        it->first, // elem_link_path
                        "units", units, existing_attr_value );

                if ( verbose() > 0 ) {
                    syslog( LOG_INFO,
                        "[%i] Group %s: %s %s to %s %s=[%s] %s=[%s] %s=%d",
                        g_pid, G->name.c_str(),
                        "Setting PV Units Attribute",
                        it->second.c_str(), // pv_value_path
                        label.c_str(), "units", units.c_str(),
                        "existing_attr_value", existing_attr_value.c_str(),
                        "attrWasSet", attrWasSet );
                    give_syslog_a_chance;
                }
            }
        }

        // Hmmm... Had Some Units PVs Patterns But Didn't Match Anything...
        // Better Log It!
        else if ( ( verbose() > 1 ) && ( E->unitsPatterns.size() ) )
        {
            std::stringstream ss;
            ss << "unitsPatterns=[";
            for ( uint32_t i=0 ; i < E->unitsPatterns.size(); i++ )
            {
                if ( i ) ss << ", ";
                ss << E->unitsPatterns[i];
            }
            ss << "]";
            ss << " unitsPaths=[";
            for ( it = E->unitsPaths.begin() ;
                    it != E->unitsPaths.end() ; ++it )
            {
                if ( it != E->unitsPaths.begin() ) ss << ", ";
                ss << "(" << it->first << " -> " << it->second << ")";
            }
            ss << "]";
            syslog( LOG_ERR, "[%i] %s Group %s: %s - %s %s",
                g_pid, "STC Error:",
                G->name.c_str(), "No Matching Units PV Found",
                "Missing PV Value or Config...?", ss.str().c_str() );
            give_syslog_a_chance;
        }
    }
}


/*! \brief Execute Any Pre-Post-Autoreduction Command Scripts
 *
 * This method checks any conditional STC Config Commands
 * and otherwise Executes Every Command that has been specified;
 * append a crazy "logger" and bash command sequence to
 * Re-direct the Command Script Output to a temp file and then
 * insert it en masse into the local RSyslog (because when I do it
 * line by line it blows over the RSyslog Rate Limiter... ;-Q),
 * along with the proper "stc" tag (and the current STC Instance
 * Process ID...! ;-D).
 * Execute the Command via system(), and capture and report the
 * Return Status using the "correct bits"... ;-D
 */
uint32_t
NxGen::executePrePostCommands(void)
{
    uint32_t status = 0;

    // Check Each Config Command in Turn,
    // Execute Any Open or Conditional Commands...
    for ( uint32_t cmd=0 ; cmd < m_config_commands.size() ; cmd++ )
    {
        struct CommandInfo *CMD = &(m_config_commands[cmd]);

        bool do_cmd = false;

        // Check for Conditions...
        if ( CMD->conditions.size() )
        {
            for ( uint32_t c=0 ; c < CMD->conditions.size() ; c++ )
            {
                struct ConditionInfo *C = &(CMD->conditions[c]);

                if ( C->is_set )
                    do_cmd = true;
            }
        }
        // Unconditional, Always Execute! :-D
        else
        {
            do_cmd = true;
        }

        // Execute Command...
        if ( do_cmd )
        {
            syslog( LOG_INFO,
           "[%i] %s: Preparing %s Command for Execution - %s=[%s] %s=[%s]",
                g_pid, "executePrePostCommands()", CMD->name.c_str(),
                "path", CMD->path.c_str(),
                "args", CMD->args.c_str() );
            give_syslog_a_chance;

            // Append Basic Run Meta-Data Args for Command...
            std::stringstream ssargs;
            ssargs << CMD->args;
            ssargs << " facility=" << "\"" << getFacilityName() << "\"";
            ssargs << " beamline=" << "\"" << getBeamShortName() << "\"";
            ssargs << " proposal=" << "\"" << getProposalID() << "\"";
            ssargs << " run_number=" << "\"" << getRunNumber() << "\"";

            // Construct Logger Pipe Command...
            // (Use "logger" with the Whole Output File,
            // lest we blow over the RSyslog Rate Limit and
            // start to drop precious STC Log Messages, like
            // the "End of the Translation" ComBus trigger log... ;-D)
            std::stringstream sslogger;
            sslogger << " > /tmp/stc_cmd_out." << g_pid << ".txt";
            sslogger << " ; STC_CMD_RC=$?";
            sslogger << " ; /usr/bin/logger";
            sslogger << " --tag \"stc: [" << g_pid << "]\"";
            sslogger << " --file /tmp/stc_cmd_out." << g_pid << ".txt";
            sslogger << " ; unlink /tmp/stc_cmd_out." << g_pid << ".txt";
            sslogger << " ; exit ${STC_CMD_RC}";

            // Construct Command String...
            std::stringstream sscmd;
            sscmd << CMD->path;
            sscmd << ssargs.str();
            sscmd << sslogger.str();

            syslog( LOG_INFO,
              "[%i] %s: %s %s %s: %s=[%s] %s=[%s] %s=[%s] %s=[%s]",
                g_pid, "executePrePostCommands()",
                "Executing", CMD->name.c_str(), "Command",
                "path", CMD->path.c_str(),
                "args", ssargs.str().c_str(),
                "logger", sslogger.str().c_str(),
                "cmd", sscmd.str().c_str() );
            give_syslog_a_chance;

            int rc = system( sscmd.str().c_str() );

            char bash_rc = rc >> 8; // Grab Correct Bits... ;-D

            if ( bash_rc != 0 )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s: %s Command FAILED rc=%d: %s=[%s]",
                    g_pid, "STC Error:", "executePrePostCommands()",
                    CMD->name.c_str(), bash_rc,
                    "cmd", sscmd.str().c_str() );
                give_syslog_a_chance;

                status = bash_rc;
            }
            else
            {
                syslog( LOG_INFO,
                    "[%i] %s: %s Command rc=%d: %s=[%s]",
                    g_pid, "executePrePostCommands()",
                    CMD->name.c_str(), bash_rc,
                    "cmd", sscmd.str().c_str() );
                give_syslog_a_chance;
            }
        }
        else
        {
            syslog( LOG_INFO,
                "[%i] %s: NOT Executing %s Command - %s %s=[%s] %s=[%s]",
                g_pid, "executePrePostCommands()", CMD->name.c_str(),
                "Unmet Conditional Requirement(s)",
                "path", CMD->path.c_str(),
                "args", CMD->args.c_str() );
            give_syslog_a_chance;
        }
    }

    // Return Any Command Status/Failures...
    return( status );
}


/*! \brief Dump Overall STC Processing Statistics
 *
 * This method dump the overall processing time/event bandwidth
 * for Nexus-specific output generation, including statistics
 * for the _Run Itself_ and how long the STC took to process it.
 */
void
NxGen::dumpProcessingStatistics(void)
{
    if ( !m_gen_nexus )
        return;

    // Overall Run Statistics

    uint64_t total_counts =
        m_total_counts + m_total_uncounts + m_total_non_counts;

    syslog( LOG_INFO, "[%i] %s = %ld in %f seconds",
        g_pid, "Run Total Counts", total_counts, m_duration );
    give_syslog_a_chance;

    double run_bandwidth = (double) total_counts / (double) m_duration;

    syslog( LOG_INFO, "[%i] %s = %lf events/sec",
        g_pid, "Overall Run Bandwidth", run_bandwidth );
    give_syslog_a_chance;

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Counted(Det)", m_total_counts,
        (double) m_total_counts / (double) m_duration );
    give_syslog_a_chance;

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Uncounted(Err)", m_total_uncounts,
        (double) m_total_uncounts / (double) m_duration );
    give_syslog_a_chance;

    syslog( LOG_INFO, "[%i] (%s = %ld, %lf events/sec)",
        g_pid, "Non-Counts(Mon)", m_total_non_counts,
        (double) m_total_non_counts / (double) m_duration );
    give_syslog_a_chance;

    // STC Processing Statistics

    struct timespec stc_run_end_time;

    clock_gettime( CLOCK_REALTIME, &stc_run_end_time );

    float stc_duration = calcDiffSeconds(
        stc_run_end_time, m_stc_run_start_time );

    syslog( LOG_INFO, "[%i] %s = %f seconds",
        g_pid, "Total STC Processing Time", stc_duration );
    give_syslog_a_chance;

    double stc_bandwidth = (double) total_counts
        / (double) stc_duration;

    syslog( LOG_INFO, "[%i] %s = %lf events/sec",
        g_pid, "Overall STC Bandwidth", stc_bandwidth );
    give_syslog_a_chance;

    double overhead_ratio = ( stc_bandwidth > 0.0 ) ?
        ( run_bandwidth / stc_bandwidth ) : 0.0;

    syslog( LOG_INFO, "[%i] %s = %lf",
        g_pid, "STC Overhead Ratio", overhead_ratio );
    give_syslog_a_chance;

    // StreamParser Testing/Diagnostic Statistics

    syslog( LOG_INFO, "[%i] %s = %ld",
        g_pid, "Total Input Stream Byte Count of ADARA packets",
        m_total_bytes_count );
    give_syslog_a_chance;

    double stream_bandwidth = (double) m_total_bytes_count
        / (double) stc_duration;

    syslog( LOG_INFO, "[%i] %s = %lf bytes/sec",
        g_pid, "Average Stream Bandwidth", stream_bandwidth );
    give_syslog_a_chance;
}


/*! \brief Processes beamline information
 *
 * This method translates beamline information to the output Nexus file.
 */
void
NxGen::processBeamlineInfo
(
    const STC::BeamlineInfo & a_beamline_info,  ///< [in] Beamline information object
    bool a_force_init                           ///< [in] Force Initialize?
)
{
    if ( !m_gen_nexus )
        return;

    // Already Initialized...
    if ( m_nexus_beamline_init )
        return;

    // We've Got the Beamline Info Now, Try to Initialize the NeXus File
    if ( !initialize( a_force_init ) )
    {
        if ( a_force_init )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::processBeamlineInfo()",
                "Failed to Force Initialize NeXus File",
                "Losing Beamline Meta-Data!" );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_ERR,
                "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::processBeamlineInfo()",
                "Unable to Initialize NeXus File", "Retry Later..." );
            give_syslog_a_chance;
        }
        return;
    }

    try
    {
        writeScalar( m_instrument_path, "target_station_number",
            a_beamline_info.target_station_number, "" );

        writeString( m_instrument_path, "beamline",
            a_beamline_info.instr_id );

        if ( a_beamline_info.instr_longname.size() )
        {
            writeString( m_instrument_path, "name",
                a_beamline_info.instr_longname );

            if ( a_beamline_info.instr_shortname.size() )
            {
                writeStringAttribute( m_instrument_path + "/name",
                    "short_name", a_beamline_info.instr_shortname );
            }
        }

        m_nexus_beamline_init = true;
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "processBeamlineInfo() failed." )
    }
}


/*! \brief Processes run information
 *
 * This method translates run information to the output Nexus file.
 */
void
NxGen::processRunInfo
(
    const STC::RunInfo & a_run_info,    ///< [in] Run information object
    const bool a_strict                 ///< [in] Strict Protocol Parsing
)
{
    // Verify we received all required fields in Final Run Info pkt
    if ( a_strict )
    {
        string msg;
        if ( !a_run_info.facility_name.size() )
            msg = "Required facility_name missing from RunInfo.";
        else if ( !a_run_info.proposal_id.size() )
            msg = "Required proposal_id missing from RunInfo.";
        else if ( a_run_info.run_number == 0 )
            msg = "Required run_number missing from RunInfo.";

        if ( msg.size() )
            THROW_TRACE( STC::ERR_UNEXPECTED_INPUT, msg )
    }

    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose the final meta-data...!!)
    // Although if we just Forced Working Directory construction
    // in StreamParser::finalizeStreamProcessing(), then
    // Now's Our First Chance to Finally Create a NeXus Data File...! ;-D
    if ( !initialize( true, "NxGen::processRunInfo()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::processRunInfo()",
            "Failed to Force Initialize NeXus File",
            "Losing Final Run Meta-Data!" );
        give_syslog_a_chance;
        return;
    }

    try
    {
        string tmp = boost::lexical_cast<string>(a_run_info.run_number);
        writeString( m_entry_path, "run_number", tmp );
        writeString( m_entry_path, "entry_identifier", tmp );

        writeString( m_entry_path, "experiment_identifier",
            a_run_info.proposal_id );

        writeString( m_entry_path, "experiment_title",
            a_run_info.proposal_title );

        writeString( m_entry_path, "title", a_run_info.run_title );

        if ( ! a_run_info.no_sample_info )
        {
            string sample_path = m_entry_path + "/sample";
            makeGroup( sample_path, "NXsample" );

            writeString( sample_path, "identifier", a_run_info.sample_id );
            writeString( sample_path, "name", a_run_info.sample_name );
            writeString( sample_path, "nature", a_run_info.sample_nature );
            writeString( sample_path, "chemical_formula",
                a_run_info.sample_formula );

            writeScalar( sample_path, "mass",
                a_run_info.sample_mass, a_run_info.sample_mass_units );

            writeScalar( sample_path, "mass_density",
                a_run_info.sample_mass_density,
                a_run_info.sample_mass_density_units );

            // no "container" in NXsample
            writeString( sample_path, "component",
                a_run_info.sample_container_id + ": "
                    + a_run_info.sample_container_name );

            writeString( sample_path, "container_id",
                a_run_info.sample_container_id );
            writeString( sample_path, "container_name",
                a_run_info.sample_container_name );

            writeString( sample_path, "can_indicator",
                a_run_info.sample_can_indicator );
            writeString( sample_path, "can_barcode",
                a_run_info.sample_can_barcode );
            writeString( sample_path, "can_name",
                a_run_info.sample_can_name );
            writeString( sample_path, "can_materials",
                a_run_info.sample_can_materials );

            writeString( sample_path, "description",
                a_run_info.sample_description );

            writeString( sample_path, "comments",
                a_run_info.sample_comments );

            writeScalar( sample_path, "height_in_container",
                a_run_info.sample_height_in_container,
                a_run_info.sample_height_in_container_units );

            writeScalar( sample_path, "interior_diameter",
                a_run_info.sample_interior_diameter,
                a_run_info.sample_interior_diameter_units );

            writeScalar( sample_path, "interior_height",
                a_run_info.sample_interior_height,
                a_run_info.sample_interior_height_units );

            writeScalar( sample_path, "interior_width",
                a_run_info.sample_interior_width,
                a_run_info.sample_interior_width_units );

            writeScalar( sample_path, "interior_depth",
                a_run_info.sample_interior_depth,
                a_run_info.sample_interior_depth_units );

            writeScalar( sample_path, "outer_diameter",
                a_run_info.sample_outer_diameter,
                a_run_info.sample_outer_diameter_units );

            writeScalar( sample_path, "outer_height",
                a_run_info.sample_outer_height,
                a_run_info.sample_outer_height_units );

            writeScalar( sample_path, "outer_width",
                a_run_info.sample_outer_width,
                a_run_info.sample_outer_width_units );

            writeScalar( sample_path, "outer_depth",
                a_run_info.sample_outer_depth,
                a_run_info.sample_outer_depth_units );

            writeScalar( sample_path, "volume_cubic",
                a_run_info.sample_volume_cubic,
                a_run_info.sample_volume_cubic_units );
        }

        bool ldap_lookup = false;
        size_t user_count = 0;
        string path;
        for ( vector<STC::UserInfo>::const_iterator u =
                a_run_info.users.begin();
                u != a_run_info.users.end(); ++u )
        {
            path = m_entry_path + "/user"
                + boost::lexical_cast<string>(++user_count);
            makeGroup( path, "NXuser" );

            writeString( path, "facility_user_id", u->id );

            // If User Name _Not_ Specified, and User ID _Is_ Present,
            // Then Resolve User Name Via LDAP Lookup...! ;-D

            std::string user_name = u->name;

            if ( ( !u->name.compare( "XXX_UNRESOLVED_NAME_XXX" )
                        || u->name.empty() )
                    && !u->id.empty()
                    && u->id.compare( "XXX_UNRESOLVED_UID_XXX" ) )
            {
                // Create LDAP Connection Only as Needed...
                if ( !ldap_lookup )
                {
                    if ( stcLdapConnect() == 0 )
                        ldap_lookup = true;
                }

                if ( ldap_lookup )
                    stcLdapLookupUserName( u->id, user_name );
            }

            writeString( path, "name", user_name );

            // No Longer Include User "Role" If Not Set, Irrelevant... ;-D
            if ( !u->role.empty()
                    && u->role.compare( "XXX_UNRESOLVED_ROLE_XXX" ) )
            {
                writeString( path, "role", u->role );
            }
        }

        // Close Any Open LDAP Connections...
        if ( ldap_lookup )
            stcLdapDisconnect();
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
    const std::string & a_xml,  ///< [in] Geometry data in xml format
    bool a_force_init           ///< [in] Force Initialize?
)
{
    // Check for Duplicate Geometry...
    if ( m_geometryXml.size() > 0 && m_geometryXml.compare( a_xml ) ) {
        syslog( LOG_ERR, "[%i] %s %s: New [%s] != Orig [%s] - %s",
            g_pid, "STC Error:", "Duplicate Geometry/IDF Specified",
            a_xml.c_str(), m_geometryXml.c_str(),
            "Ignoring..." );
        give_syslog_a_chance;
    }

    // Save for Future Reference (& Retries, if No Working Directory Yet!)
    else {
        m_geometryXml = a_xml;
    }

    if ( !m_gen_nexus )
        return;

    // Already Initialized...
    if ( m_nexus_geometry_init )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    if ( !initialize( a_force_init ) )
    {
        if ( a_force_init )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::processGeometry()",
                "Failed to Force Initialize NeXus File",
                "Losing Geometry/IDF XML Data!" );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_ERR,
                "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::processGeometry()",
                "Unable to Initialize NeXus File", "Retry Later..." );
            give_syslog_a_chance;
        }
        return;
    }

    try
    {
        std::string geom_path = m_instrument_path + "/instrument_xml";
        makeGroup( geom_path, "NXnote" );
        writeString( geom_path, "description",
            "XML contents of the instrument IDF" );
        writeString( geom_path, "type", "text/xml" );
        writeString( geom_path, "data", m_geometryXml );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "processGeometry() failed." )
    }

    m_nexus_geometry_init = true;
}


/*! \brief Writes pulse buffers to Nexus file
 *
 * This method writes time, frequency, and charge data in pulse buffers
 * to the Nexus file.
 */
void
NxGen::pulseBuffersReady
(
    STC::PulseInfo &a_pulse_info    ///< [in] Pulse data object
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data...!!)
    if ( !initialize( true, "NxGen::pulseBuffersReady()" ) )
    {
        vector<double>::iterator tstart = a_pulse_info.times.begin();
        vector<double>::iterator tend = a_pulse_info.times.end(); tend--;
        syslog( LOG_ERR, "[%i] %s %s: %s - %s For Pulses from %lf to %lf.",
            g_pid, "STC Error:", "NxGen::pulseBuffersReady()",
            "Failed to Force Initialize NeXus File",
            "Losing Pulse Info Data!!", *tstart, *tend );
        give_syslog_a_chance;
        return;
    }

    try
    {
        // Create Pulse Frequency/Time and Pulse Proton Charge Datasets
        //    on First (or Final) Buffer Flush...
        // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
        if ( !m_pulse_info_cur_size )
        {
            // Determine Appropriate Chunk Size for Datasets...
            unsigned long chunk_size;
            if ( a_pulse_info.times.size() )
            {
                // This could be the same as the Default Chunk Size
                //    if we're dumping buffers mid-run...
                // Otherwise, if this is the Final Dump, it could be Less!
                chunk_size = a_pulse_info.times.size();
            }
            else
            {
                // This is the Final Dump and There aren't any Pulses?!
                // Create Some Dummy Datasets for Linking, etc...
                syslog( LOG_ERR, "[%i] %s! %s for %s and %s", g_pid,
                    "STC Error: Empty Final Pulse Info Buffer(s)",
                    "Creating Dummy Datasets",
                    m_daslogs_freq_path.c_str(),
                    m_daslogs_pchg_path.c_str() );
                give_syslog_a_chance;
                chunk_size = 1;
            }

            // Create Pulse Frequency Time & Value Datasets
            makeDataset( m_daslogs_freq_path, "time",
                NeXus::FLOAT64, TIME_SEC_UNITS, chunk_size );
            makeDataset( m_daslogs_freq_path, "value",
                NeXus::FLOAT64, FREQ_UNITS, chunk_size );

            // Create Pulse Proton Charge Dataset
            makeDataset( m_daslogs_pchg_path, "value",
                NeXus::FLOAT64, CHARGE_UNITS, chunk_size );

            // Link Proton Charge Time from Pulse Frequency Time Dataset
            // (Now that it's created... :-)
            makeLink( m_daslogs_freq_path + "/time",
                m_daslogs_pchg_path + "/time" );
        }

        // Now Only Write Pulse Frequency/Time and Pulse Proton Charge
        //    if there's something to write...
        // (Buffers could already be empty for final call...)
        if ( a_pulse_info.times.size() )
        {
            writeSlab( m_daslogs_freq_path + "/time",
                a_pulse_info.times, m_pulse_info_cur_size );
            writeSlab( m_daslogs_freq_path + "/value",
                a_pulse_info.freqs, m_pulse_info_cur_size );
            writeSlab( m_daslogs_pchg_path + "/value",
                a_pulse_info.charges, m_pulse_info_cur_size );

            m_pulse_info_cur_size += a_pulse_info.times.size();
        }

        // Must process pulse flags linearly
        vector<double>::iterator t = a_pulse_info.times.begin();
        for ( vector<uint32_t>::iterator f = a_pulse_info.flags.begin();
                f != a_pulse_info.flags.end(); ++f, ++t )
        {
            // If any pulse flags are set (except veto),
            // write them to pulse_flags DASLog
            // (For Forward-/Backwards-Compatibility, Strip Off Veto Flags
            //  in top 12 bits of flags...)
            if ( (*f & 0xfffff) & ~ADARA::PULSE_VETO )
            {
                m_pulse_flags_time.push_back( *t );
                m_pulse_flags_value.push_back(
                    (*f & 0xfffff) & ~ADARA::PULSE_VETO );
            }

            // Write pulse vetoes to dedicated DASlog buffer
            if ( *f & ADARA::PULSE_VETO )
                m_pulse_vetoes.push_back( *t );
        }

        // NOTE: Chunk Size is measured in *Dataset Elements*...! :-O
        if ( m_pulse_vetoes.size() > m_chunk_size )
        {
            // Create Pulse Vetoes Dataset on First Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !m_pulse_vetoes_cur_size )
            {
                makeDataset(
                    m_daslogs_path + "/Veto_pulse", "veto_pulse_time",
                    NeXus::FLOAT64, TIME_SEC_UNITS,
                    m_pulse_vetoes.size() );
            }

            // Write Pulse Vetoes Time Buffer
            writeSlab( m_daslogs_path + "/Veto_pulse/veto_pulse_time",
                m_pulse_vetoes, m_pulse_vetoes_cur_size );
            m_pulse_vetoes_cur_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
        }

        // NOTE: Chunk Size is measured in *Dataset Elements*...! :-O
        if ( m_pulse_flags_value.size() > m_chunk_size )
        {
            // Create Pulse Flags Datasets on First Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !m_pulse_flags_cur_size )
            {
                makeDataset( m_daslogs_path + "/pulse_flags", "time",
                    NeXus::FLOAT64, TIME_SEC_UNITS,
                    m_pulse_flags_time.size() );
                makeDataset( m_daslogs_path + "/pulse_flags", "value",
                    NeXus::UINT32, "",
                    m_pulse_flags_value.size() );
            }

            // Write Pulse Flags Time and Value Buffers
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


/*! \brief Writes Bank PID and TOF Event Buffers to Nexus file
 *
 * This method writes pixel ID and time of flight in bank event buffers to the Nexus file.
 */
void
NxGen::bankPidTOFBuffersReady
(
    STC::BankInfo &a_bank   ///< [in] Detector bank to write
)
{
    if ( !m_gen_nexus )
        return;

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid bank object passed to bankPidTOFBuffersReady()" )
        }

        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( false, verbose() );

        // Do We Have a Valid Initialized NeXus Data File...?
        // (We shouldn't get called if not, so if we do, better force it,
        // lest we actually lose some data...!!)
        if ( !initialize( true, "NxGen::bankPidTOFBuffersReady()" ) )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u %s=%lu %s=%lu",
                g_pid, "STC Error:", "NxGen::bankPidTOFBuffersReady()",
                "Failed to Force Initialize NeXus File",
                "Losing Bank PID/TOF Data!!",
                "bank_id", a_bank.m_id,
                "event_cur_size", bi->m_event_cur_size,
                "buffer_size", a_bank.m_tof_buffer_size );
            give_syslog_a_chance;
            return;
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_bank_init) )
            initializeNxBank( bi, false );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            // Create Bank Pid/TOF Dataset on First (or Final) Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !(bi->m_event_cur_size) )
            {
                // Chunk Size Override: If There's _No_ Pid/TOF Values,
                //    Then Create Dummy Empty Datasets (Chunk Size = 1)
                unsigned long chunk_size = ( a_bank.m_tof_buffer_size )
                    ? ( a_bank.m_tof_buffer_size ) : 1;

                makeDataset( bi->m_instr_path, m_tof_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS, chunk_size );
                makeDataset( bi->m_instr_path, m_pid_name,
                    NeXus::UINT32, "", chunk_size );

                // Link Bank Pid/TOF Datasets to Top-level Event Data Group
                // (Now that they're created... :-)
                makeLink( bi->m_tof_path,
                    bi->m_event_path + "/" + m_tof_name );
                makeLink( bi->m_pid_path,
                    bi->m_event_path + "/" + m_pid_name );
            }

            writeSlab( bi->m_tof_path,
                a_bank.m_tof_buffer, a_bank.m_tof_buffer_size,
                bi->m_event_cur_size );
            writeSlab( bi->m_pid_path,
                a_bank.m_pid_buffer, a_bank.m_tof_buffer_size,
                bi->m_event_cur_size );

            bi->m_event_cur_size += a_bank.m_tof_buffer_size;

            // Note: Reset TOF Buffer Size in Caller... ;-D
        }

        // No NeXus Histogram-based Handling Needed Here...

    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankPidTOFBuffersReady() failed for bank id: "
            << a_bank.m_id << " state: " << a_bank.m_state )
    }
}


/*! \brief Writes Bank Event Index Buffers to Nexus file
 *
 * This method writes index data in bank event buffers to the Nexus file.
 */
void
NxGen::bankIndexBuffersReady
(
    STC::BankInfo &a_bank,          ///< [in] Detector bank to write
    bool use_default_chunk_size     ///< [in] Use Default Chunk Size...?
)
{
    if ( !m_gen_nexus )
        return;

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid bank object passed to bankIndexBuffersReady()" )
        }

        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( false, verbose() );

        // Do We Have a Valid Initialized NeXus Data File...?
        // (We shouldn't get called if not, so if we do, better force it,
        // lest we actually lose some data...!!)
        if ( !initialize( true, "NxGen::bankIndexBuffersReady()" ) )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u %s=%lu %s=%lu",
                g_pid, "STC Error:", "NxGen::bankIndexBuffersReady()",
                "Failed to Force Initialize NeXus File",
                "Losing Bank Index Data!!",
                "bank_id", a_bank.m_id,
                "index_cur_size", bi->m_index_cur_size,
                "buffer_size", a_bank.m_index_buffer.size() );
            give_syslog_a_chance;
            return;
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_bank_init) )
            initializeNxBank( bi, false );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            // Create Bank Event Index Dataset
            //    on First (or Final) Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override,
            //    unless Explicitly Default for Pulse Gap Handling...)
            if ( !(bi->m_index_cur_size) )
            {
                unsigned long chunk_size;
                // Use Default Chunking (for Pulse Gap Fill Handling...)
                if ( use_default_chunk_size )
                    chunk_size = 0;
                // Use Chunk Size Override...
                else
                {
                    // If There's _No_ Event Indices,
                    //    Then Create Dummy Empty Dataset (Chunk Size = 1)
                    chunk_size = ( a_bank.m_index_buffer.size() )
                        ? ( a_bank.m_index_buffer.size() ) : 1;
                }

                makeDataset( bi->m_instr_path, m_index_name,
                    NeXus::UINT64, "", chunk_size );

                // Link Bank Event Index Dataset
                //    to Top-level Event Data Group
                // (Now that it's created... :-)
                makeLink( bi->m_index_path,
                    bi->m_event_path + "/" + m_index_name );
            }

            writeSlab( bi->m_index_path,
                a_bank.m_index_buffer, bi->m_index_cur_size );

            bi->m_index_cur_size += a_bank.m_index_buffer.size();
        }

        // No NeXus Histogram-based Handling Needed Here...

    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankIndexBuffersReady() failed for bank id: "
            << a_bank.m_id << " state: " << a_bank.m_state )
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
    STC::BankInfo  &a_bank,     ///< [in] Detector bank with pulse gap
    uint64_t        a_count     ///< [in] Number of missing pulses
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data integrity...!)
    if ( !initialize( true, "NxGen::bankPulseGap()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s count=%lu",
            g_pid, "STC Error:", "NxGen::bankPulseGap()",
            "Failed to Force Initialize NeXus File",
            "Losing Bank Pulse Gap Data!", a_count );
        give_syslog_a_chance;
        return;
    }

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid bank object passed to bankPulseGap()" )
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_bank_init) )
            initializeNxBank( bi, false );

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            // Note: bankIndexBuffersReady() must have been called
            //    _Before_ Now, to Create Bank Event Index Dataset...!
            // (This is done in StreamParser::handleBankPulseGap().)

            fillSlab( bi->m_index_path,
                bi->m_event_count, a_count, bi->m_index_cur_size );
            bi->m_index_cur_size += a_count;
        }

        // No NeXus Histogram-based Handling Needed Here...

    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankPulseGap() failed for bank id: "
            << a_bank.m_id << " state: " << a_bank.m_state
            << ", gap count: " << a_count )
    }
}


/*! \brief Finalizes bank data in Nexus file
 *
 * This method writes event counts for the specified bank
 * to the Nexus file.
 */
void
NxGen::bankFinalize
(
    STC::BankInfo &a_bank   ///< [in] Detector bank to finalize
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data...!!)
    if ( !initialize( true, "NxGen::bankFinalize()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s bank_id=%u",
            g_pid, "STC Error:", "NxGen::bankFinalize()",
            "Failed to Force Initialize NeXus File",
            "Losing Bank Histogram/Meta-Data!!", a_bank.m_id );
        give_syslog_a_chance;
        return;
    }

    try
    {
        NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);
        if ( !bi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid bank object passed to bankFinalize()" )
        }

        // Make Sure NeXus Structures have been (Late) Initialized...
        if ( !(bi->m_nexus_bank_init) )
            initializeNxBank( bi, true );

        // Track Whether Pixel Mapping Table has been Written to NeXus...
        bool logical_pixel_map_written = false;

        // NeXus Event-based Data...
        if ( bi->m_has_event )
        {
            // Create and Link Total Counts for this Detector Bank...
            string total_path = m_instrument_path + "/" + bi->m_name;
            writeScalar( total_path, "total_counts",
                bi->m_event_count, "" );
            makeLink( total_path + "/total_counts",
                m_entry_path + "/" + bi->m_eventname + "/total_counts" );

            // NOW Link Pulse Time to this Detector Bank...
            //    - the Pulse Time Dataset Must have been Created by Now!
            makeLink( bi->m_time_path,
                bi->m_instr_path + "/" + m_pulse_time_name );
            makeLink( bi->m_time_path,
                bi->m_event_path + "/" + m_pulse_time_name );
        }

        // NeXus Histogram-based Data...
        if ( bi->m_has_histo )
        {
            syslog( LOG_INFO,
                "[%i] %s: Detector Bank %d State %u - %s",
                g_pid, "NxGen::finalize()", a_bank.m_id, a_bank.m_state,
                "Writing Histogram Data");
            give_syslog_a_chance;

            // Create & Write Histogram Multi-dimensional Data...
            std::vector<hsize_t> dims;
            dims.push_back( bi->m_logical_pixelids.size() );
            dims.push_back( bi->m_num_tof_bins - 1 );
#ifdef HISTO_TEST
            uint32_t num_pids = bi->m_logical_pixelids.size();
            syslog( LOG_INFO, "[%i] %s: %s for %s [%u x %u]",
                g_pid, "NxGen::finalize()", "Creating Dummy Histogram",
                bi->m_instr_path.c_str(),
                num_pids, bi->m_num_tof_bins - 1 );
            give_syslog_a_chance;
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

            // Create Histo Bank PixelIds and TOF Bins Now...
            // (including proper Chunk Size Overriding...! ;-D)
            makeDataset( bi->m_instr_path, m_histo_pid_name,
                NeXus::UINT32, "", bi->m_logical_pixelids.size() );
            makeDataset( bi->m_instr_path, m_tofbin_name,
                NeXus::FLOAT32, TIME_USEC_UNITS,
                bi->m_tofbin_buffer.size() );

            // Write out Bank Logical PixelIds...
            writeSlab( bi->m_histo_pid_path,
                bi->m_logical_pixelids, 0 );
            logical_pixel_map_written = true;

            // Add "Axis" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute(
                bi->m_instr_path + "/" + m_histo_pid_name, "axis", "1" );

            // Write out TOF Bins...
            writeSlab( bi->m_tofbin_path, bi->m_tofbin_buffer, 0 );

            // Add "Axis" Attribute for NeXus NXdata Standards Compat
            writeStringAttribute(
                bi->m_instr_path + "/" + m_tofbin_name, "axis", "2" );

            // Top-level Histo data group
            makeGroup( bi->m_histo_path, "NXdata" );

            // Link Multi-dimensional Data into NXdata Histo group...
            makeLink( bi->m_data_path,
                bi->m_histo_path + "/" + m_data_name );

            // Link Pixel Id and TOF Bin Data into NXdata Histo group...
            makeLink( bi->m_histo_pid_path,
                bi->m_histo_path + "/" + m_histo_pid_name );
            makeLink( bi->m_tofbin_path,
                bi->m_histo_path + "/" + m_tofbin_name );

            // Write Out Total Counts for Histograms, too... ;-D
            writeScalar( bi->m_histo_path, "total_counts",
                bi->m_histo_event_count, "" );
            writeScalar( bi->m_histo_path, "total_uncounted_counts",
                bi->m_histo_event_uncounted, "" );
        }

        // (Optionally) Write Pixel Mapping Table to NeXus...
        if ( getSavePixelMap() )
        {
            syslog( LOG_INFO,
                "[%i] %s: Detector Bank %d State %u - %s, %s=%lu%s %s=%lu",
                g_pid, "NxGen::finalize()", a_bank.m_id, a_bank.m_state,
                "Optionally Saving Pixel Mapping Table For This Run",
                "bi->m_logical_pixelids.size()",
                bi->m_logical_pixelids.size(),
                ( logical_pixel_map_written ? " (Already Written)" : "" ),
                "bi->m_physical_pixelids.size()",
                bi->m_physical_pixelids.size() );
            give_syslog_a_chance;

            // Logical Pixel Map...
            if ( !logical_pixel_map_written )
            {
                // Create Bank Logical PixelIds Now...
                // (including proper Chunk Size Overriding...! ;-D)
                makeDataset( bi->m_instr_path, m_histo_pid_name,
                    NeXus::UINT32, "", bi->m_logical_pixelids.size() );

                // Write out Bank Logical PixelIds...
                writeSlab( bi->m_histo_pid_path,
                    bi->m_logical_pixelids, 0 );
            }

            // Physical Pixel Map...

            // Create Bank Physical PixelIds Now...
            // (including proper Chunk Size Overriding...! ;-D)
            makeDataset( bi->m_instr_path, m_histo_pid_name_raw,
                NeXus::UINT32, "", bi->m_physical_pixelids.size() );

            // Write out Bank Physical PixelIds...
            writeSlab( bi->m_histo_pid_path_raw,
                bi->m_physical_pixelids, 0 );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankFinalize() failed for bank id: "
            << a_bank.m_id << " state: " << a_bank.m_state )
    }
}


/*! \brief Writes Monitor Event TOF buffers to Nexus file
 *
 * This method writes event time of flight data in monitor buffers
 * to the Nexus file.
 */
void
NxGen::monitorTOFBuffersReady
(
    STC::MonitorInfo &a_monitor     ///< [in] Monitor with events to write
)
{
    if ( !m_gen_nexus )
        return;

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
              "Invalid monitor object passed to monitorTOFBuffersReady()" )
        }

        // Do We Have a Valid Initialized NeXus Data File...?
        // (We shouldn't get called if not, so if we do, better force it,
        // lest we actually lose some data...!!)
        if ( !initialize( true, "NxGen::monitorTOFBuffersReady()" ) )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u %s=%lu %s=%lu",
                g_pid, "STC Error:", "NxGen::monitorTOFBuffersReady()",
                "Failed to Force Initialize NeXus File",
                "Losing Monitor TOF Data!!",
                "monitor_id", a_monitor.m_id,
                "event_cur_size", mi->m_event_cur_size,
                "buffer_size", a_monitor.m_tof_buffer_size );
            give_syslog_a_chance;
            return;
        }

        // Make Sure NeXus Structures have been Initialized...
        if ( !(mi->m_nexus_monitor_init) )
            initializeNxMonitor( mi );

        // Event-based Monitors Only...
        if ( mi->m_config.format == ADARA::EVENT_FORMAT )
        {
            // Create Monitor TOF Dataset on First (or Final) Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override...! :-D)
            if ( !(mi->m_event_cur_size) )
            {
                // Chunk Size Override: If There's _No_ TOF Values,
                //    Then Create Dummy Empty Dataset (Chunk Size = 1)

                unsigned long chunk_size = ( a_monitor.m_tof_buffer_size )
                    ? ( a_monitor.m_tof_buffer_size ) : 1;

                syslog( LOG_INFO,
                    "[%i] Creating %s%s Dataset for %s/%s %s=%lu",
                    g_pid,
                    ( ( a_monitor.m_tof_buffer_size ) ? "" : "Dummy " ),
                    "Monitor TOF",
                    mi->m_path.c_str(), m_tof_name.c_str(),
                    "chunk_size", chunk_size );
                give_syslog_a_chance;

                makeDataset( mi->m_path, m_tof_name,
                    NeXus::FLOAT32, TIME_USEC_UNITS, chunk_size );
            }

            writeSlab( mi->m_tof_path,
                a_monitor.m_tof_buffer, a_monitor.m_tof_buffer_size,
                mi->m_event_cur_size );
            mi->m_event_cur_size += a_monitor.m_tof_buffer_size;
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e,
            "monitorTOFBuffersReady() failed for monitor id: "
            << a_monitor.m_id )
    }
}


/*! \brief Writes Monitor Event Index buffers to Nexus file
 *
 * This method writes event index data in monitor buffers
 * to the Nexus file.
 */
void
NxGen::monitorIndexBuffersReady
(
    STC::MonitorInfo &a_monitor,    ///< [in] Monitor with events to write
    bool use_default_chunk_size     ///< [in] Use Default Chunk Size...?
)
{
    if ( !m_gen_nexus )
        return;

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
            "Invalid monitor object passed to monitorIndexBuffersReady()" )
        }

        // Do We Have a Valid Initialized NeXus Data File...?
        // (We shouldn't get called if not, so if we do, better force it,
        // lest we actually lose some data...!!)
        if ( !initialize( true, "NxGen::monitorIndexBuffersReady()" ) )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s %s=%u %s=%lu %s=%lu",
                g_pid, "STC Error:", "NxGen::monitorIndexBuffersReady()",
                "Failed to Force Initialize NeXus File",
                "Losing Monitor Index Data!!",
                "monitor_id", a_monitor.m_id,
                "index_cur_size", mi->m_index_cur_size,
                "buffer_size", a_monitor.m_index_buffer.size() );
            give_syslog_a_chance;
            return;
        }

        // Make Sure NeXus Structures have been Initialized...
        if ( !(mi->m_nexus_monitor_init) )
            initializeNxMonitor( mi );

        // Event-based Monitors Only...
        if ( mi->m_config.format == ADARA::EVENT_FORMAT )
        {
            // Create Monitor Event Index Dataset
            //    on First (or Final) Buffer Flush
            // ("Lazy" Dataset Create, with Chunk Size Override,
            //    unless Explicitly Default for Pulse Gap Handling...)
            if ( !(mi->m_index_cur_size) )
            {
                unsigned long chunk_size;
                // Use Default Chunking (for Pulse Gap Fill Handling...)
                if ( use_default_chunk_size )
                    chunk_size = 0;
                // Use Chunk Size Override...
                else
                {
                    // If There's _No_ Event Indices,
                    //    Then Create Dummy Empty Dataset (Chunk Size = 1)
                    chunk_size = ( a_monitor.m_index_buffer.size() )
                        ? ( a_monitor.m_index_buffer.size() ) : 1;

                    syslog( LOG_INFO,
                        "[%i] Creating %s%s Dataset for %s/%s %s=%lu",
                        g_pid,
                        ( ( a_monitor.m_index_buffer.size() )
                            ? "" : "Dummy " ),
                        "Monitor Event Index",
                        mi->m_path.c_str(), m_index_name.c_str(),
                        "chunk_size", chunk_size );
                    give_syslog_a_chance;
                }

                makeDataset( mi->m_path, m_index_name,
                    NeXus::UINT64, "", chunk_size );
            }

            writeSlab( mi->m_index_path,
                a_monitor.m_index_buffer, mi->m_index_cur_size );
            mi->m_index_cur_size += a_monitor.m_index_buffer.size();
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e,
            "monitorIndexBuffersReady() failed for monitor id: "
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
    STC::MonitorInfo   &a_monitor,  ///< [in] Monitor with a pulse gap
    uint64_t            a_count     ///< [in] Number of missing pulses
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data integrity...!)
    if ( !initialize( true, "NxGen::monitorPulseGap()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s count=%lu",
            g_pid, "STC Error:", "NxGen::monitorPulseGap()",
            "Failed to Force Initialize NeXus File",
            "Losing Monitor Pulse Gap Data!", a_count );
        give_syslog_a_chance;
        return;
    }

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid monitor object passed to monitorPulseGap()" )
        }

        // Make Sure NeXus Structures have been Initialized...
        if ( !(mi->m_nexus_monitor_init) )
            initializeNxMonitor( mi );

        // Event-based Monitors Only...
        if ( mi->m_config.format == ADARA::EVENT_FORMAT )
        {
            // Note: monitorIndexBuffersReady() must have been called
            //    _Before_ Now, to Create Monitor Event Index Dataset...!
            // (This is done in StreamParser::handleMonitorPulseGap().)

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
    STC::MonitorInfo &a_monitor     ///< [in] Monitor to finalize
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose some data...!!)
    if ( !initialize( true, "NxGen::monitorFinalize()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s monitor_id=%u",
            g_pid, "STC Error:", "NxGen::monitorFinalize()",
            "Failed to Force Initialize NeXus File",
            "Losing Monitor Histogram/Meta-Data!!", a_monitor.m_id );
        give_syslog_a_chance;
        return;
    }

    try
    {
        NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);
        if ( !mi )
        {
            THROW_TRACE( STC::ERR_CAST_FAILED,
                "Invalid monitor object passed to monitorFinalize()" )
        }

        // Make Sure NeXus Structures have been Initialized...
        if ( !(mi->m_nexus_monitor_init) )
            initializeNxMonitor( mi );

        // Histo-based Monitor
        if ( mi->m_config.format == ADARA::HISTO_FORMAT )
        {
            // Create Monitor Histo Data and TOF Bins Now...
            // (including proper Chunk Size Overriding...! ;-D)
            makeDataset( mi->m_path, m_data_name,
                NeXus::UINT32, "",
                mi->m_data_buffer.size() );
            makeDataset( mi->m_path, m_tofbin_name,
                NeXus::FLOAT32, TIME_USEC_UNITS,
                mi->m_tofbin_buffer.size() );

            // Write out Monitor Histo Data...
            writeSlab( mi->m_data_path, mi->m_data_buffer, 0 );

            // Write out TOF Bins...
            writeSlab( mi->m_tofbin_path, mi->m_tofbin_buffer, 0 );

            // Write Out Total Counts for Histogram Mode, too... ;-D
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_counts", mi->m_event_count, "" );
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_uncounted_counts", mi->m_event_uncounted, "" );

            syslog( LOG_INFO,
                "[%i] Wrote Histogram Monitor for %s/%s %s=%lu %s=%lu",
                g_pid, m_entry_path.c_str(), mi->m_name.c_str(),
                "total_counts", mi->m_event_count,
                "total_uncounted_counts", mi->m_event_uncounted );
            give_syslog_a_chance;
        }

        // Event-based Monitor
        else
        {
            // Write Final Monitor Total Counts
            writeScalar( m_entry_path + "/" + mi->m_name,
                "total_counts", mi->m_event_count, "" );

            syslog( LOG_INFO,
                "[%i] Wrote Event Monitor for %s/%s %s=%lu",
                g_pid, m_entry_path.c_str(), mi->m_name.c_str(),
                "total_counts", mi->m_event_count );
            give_syslog_a_chance;

            // NOW Link Pulse Time to this Monitor...
            //    - the Pulse Time Dataset Must have been Created by Now!
            makeLink( m_daslogs_freq_path + "/time",
                mi->m_path + "/" + m_pulse_time_name );
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
    double a_time,                  ///< [in] Time associated with run comments
    uint64_t a_ts_nano,             ///< [in] Actual Timestamp in Nanoseconds
    const std::string &a_comment,   ///< [in] Overall run comments
    bool a_force_init               ///< [in] Force Initialize?
)
{
    // Always Handle Duplicate Run Comment, Even if Not Writing to NeXus.
    if ( m_runComment.size() > 0 && m_runComment.compare( a_comment ) ) {
        // Are Updates to the Run Notes Enabled During the Run...?
        if ( getRunNotesUpdatesEnabled() ) {
            syslog( LOG_INFO,
             "[%i] %s: %s - %s [%s] -> %s [%s] %lu.%09lu (%s=%lu) (%s=%d)",
                g_pid, "NxGen::runComment()",
                "Updating Run Notes Comment Prior to Write",
                "Orig", m_runComment.c_str(), "New", a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                "a_force_init", a_force_init );
            give_syslog_a_chance;
            // Push Previous Run Notes Comment onto Generic Annotations...
            markerComment( m_runComment_time, m_runComment_ts_nano,
                "[OVERWRITTEN RUN NOTES] " + m_runComment );
            // Update Run Notes Comment for This Run...
            m_runComment_time = a_time;
            m_runComment_ts_nano = a_ts_nano;
            m_runComment = a_comment;
        }
        else {
            syslog( LOG_ERR,
     "[%i] %s %s: %s - %s [%s] != %s [%s] %lu.%09lu (%s=%lu) - %s (%s=%d)",
                g_pid, "STC Error:", "NxGen::runComment()",
                "Duplicate Run Notes Comment Specified",
                "New", a_comment.c_str(), "Orig", m_runComment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                "Discarding...",
                "a_force_init", a_force_init );
            give_syslog_a_chance;
            // Save Duplicate Run Notes Comment onto Generic Annotations...
            markerComment( a_time, a_ts_nano,
                "[DUPLICATE RUN NOTES] " + a_comment );
        }
    }

    // Save for Future Reference (& Retries, if No Working Directory Yet!)
    else {
        m_runComment_time = a_time;
        m_runComment_ts_nano = a_ts_nano;
        m_runComment = a_comment;

        syslog( LOG_INFO, "[%i] %s: %s [%s] %lu.%09lu (%s=%lu) (%s=%d)",
            g_pid, "NxGen::runComment()",
            "Run Notes Comment Set to", m_runComment.c_str(),
            (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                - ADARA::EPICS_EPOCH_OFFSET,
            (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
            "ts_nano", a_ts_nano,
            "a_force_init", a_force_init );
        give_syslog_a_chance;
    }

    if ( !m_gen_nexus )
        return;

    // Last Chance to Actually Write Run Notes to NeXus...?
    if ( !a_force_init )
    {
        syslog( LOG_INFO, "[%i] %s: %s - %s [%s]",
            g_pid, "NxGen::runComment()",
            "Not the Last Chance to Write Run Notes Comment",
            "Deferring Until Later...", m_runComment.c_str() );
        give_syslog_a_chance;
        return;
    }

    // Already Initialized...?!
    if ( m_nexus_run_comment_init )
    {
        syslog( LOG_ERR,
            "[%i] %s %s: %s - %s %s [%s] %lu.%09lu (%s=%lu) - %s (%s=%d)",
            g_pid, "STC Error:", "NxGen::runComment()",
            "Duplicate Run Notes Comment Write Attempt",
            "Run Notes Already Written to NeXus!",
            "Current/Pending Run Notes Value:",
            m_runComment.c_str(),
            (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                - ADARA::EPICS_EPOCH_OFFSET,
            (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
            "ts_nano", a_ts_nano,
            "Discarding...",
            "a_force_init", a_force_init );
        give_syslog_a_chance;
        return;
    }

    // Do We Have a Valid Initialized NeXus Data File...?
    if ( !initialize( a_force_init ) )
    {
        if ( a_force_init )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::runComment()",
                "Failed to Force Initialize NeXus File",
                "Losing Run Notes Comment Data!" );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_ERR,
                "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::runComment()",
                "Unable to Initialize NeXus File", "Retry Later..." );
            give_syslog_a_chance;
        }
        return;
    }

    try
    {
        writeString( m_entry_path, "notes", m_runComment );

        string run_notes_time_str =
            timeToISO8601( nsec_to_timespec( m_runComment_ts_nano ) );
        writeStringAttribute( m_entry_path + "/notes",
            "time", run_notes_time_str );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "runComment() failed." )
    }

    syslog( LOG_INFO, "[%i] %s: %s [%s] %lu.%09lu (%s=%lu) (%s=%d)",
        g_pid, "NxGen::runComment()",
        "Final Run Notes Comment Written", m_runComment.c_str(),
        (unsigned long)(m_runComment_ts_nano / NANO_PER_SECOND_LL)
            - ADARA::EPICS_EPOCH_OFFSET,
        (unsigned long)(m_runComment_ts_nano % NANO_PER_SECOND_LL),
        "ts_nano", m_runComment_ts_nano,
        "a_force_init", a_force_init );
    give_syslog_a_chance;

    m_nexus_run_comment_init = true;
}


/*! \brief Normalize Annotation Timestamps Relative to 1st Pulse Start Time
 *
 * This method checks for Any Non-Normalized Annotation Timestamps, which
 * arrived before the 1st Pulse Time was known, and properly normalizes
 * these Timestamps relative to the 1st Pulse Start Time.
 */
template <typename TypeT>
void
NxGen::normalizeAnnotationTimestamps
(
    uint64_t a_start_time,      ///< 1st Pulse Time (nanosecs)
    std::string a_label,        ///< Annotation/Markers Label
    std::multimap<uint64_t, std::pair<double, TypeT> > &a_annot_multimap,  ///< Annotation/Markers Multimap to Normalize
    bool &a_has_non_normalized  ///< Corresponding Has Non Normalized Flag
)
{
    syslog( LOG_ERR, "[%i] %s %s: Entry for %s %s=%lu %s=%lu %s=%u",
        g_pid, "STC Error:", "NxGen::normalizeAnnotationTimestamps()",
        a_label.c_str(), "a_start_time", a_start_time,
        "a_annot_multimap.size()", a_annot_multimap.size(),
        "a_has_non_normalized", a_has_non_normalized );
    give_syslog_a_chance;

    // Normalize All Non-Normalized Timestamps...

    typename std::multimap<uint64_t, std::pair<double, TypeT> >::iterator
        annot_multimap_it;

    for ( annot_multimap_it = a_annot_multimap.begin() ;
            annot_multimap_it != a_annot_multimap.end() ;
            ++annot_multimap_it )
    {
        // REMOVEME
        /*
        stringstream ss;
        ss << "nano_ts=" <<  annot_multimap_it->first;
        ss << " time=" <<  annot_multimap_it->second.first;
        ss << " value=" <<  annot_multimap_it->second.second;
        if ( annot_multimap_it->second.first < 0.0 )
            ss << " Not-Yet-Normalized Time...";
        syslog( LOG_ERR, "[%i] %s %s: %s",
            g_pid, "STC Error:", "NxGen::normalizeAnnotationTimestamps()",
            ss.str().c_str() );
        give_syslog_a_chance;
        */

        // Not-Yet-Normalized Time...
        if ( annot_multimap_it->second.first < 0.0 )
        {
            // Positive Time Update,
            // Normalize Annotation Time to 1st Pulse...
            if ( annot_multimap_it->first > a_start_time )
            {
                double t = ( annot_multimap_it->first - a_start_time )
                    / NANO_PER_SECOND_D;
                annot_multimap_it->second.first = t;

                std::stringstream ss;
                ss << annot_multimap_it->second.second;
                syslog( LOG_ERR,
                    "[%i] %s %s: %s for %s: %s @ %lf (%lu)",
                    g_pid, "STC Error:",
                    "NxGen::normalizeAnnotationTimestamps()",
                    "Positive Time Annotation",
                    a_label.c_str(), ss.str().c_str(),
                    annot_multimap_it->second.first,
                    annot_multimap_it->first );
                give_syslog_a_chance;
            }

            // Truncate Any Negative Time Offsets to 0.
            // Log Negative Time Truncation as Error.
            else
            {
                std::stringstream ss;
                ss << "STC Error: ";
                ss << "NxGen::normalizeAnnotationTimestamps(): ",
                ss << "Truncate Negative Annotation Time for ";
                ss << a_label;
                ss << " to Zero: ";
                ss << annot_multimap_it->second.second;
                ss << " @";
                syslog( LOG_ERR,
                    "[%i] %s %lu.%09lu (%lu) < %lu.%09lu (%lu)",
                    g_pid, ss.str().c_str(),
                    (unsigned long)( annot_multimap_it->first
                            / NANO_PER_SECOND_LL )
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)( annot_multimap_it->first
                            % NANO_PER_SECOND_LL ),
                    annot_multimap_it->first,
                    (unsigned long)( a_start_time
                            / NANO_PER_SECOND_LL )
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)( a_start_time
                            % NANO_PER_SECOND_LL ),
                    a_start_time );
                give_syslog_a_chance;

                annot_multimap_it->second.first = 0.0;
            }
        }
    }

    // Done Normalizing This Annotation/Markers Log Timestamps.
    a_has_non_normalized = false;
}


/*! \brief Writes buffered pause data into Nexus file
 *
 * This method flushes buffered pause data to the logs of the Nexus file.
 */
void
NxGen::flushPauseData
(
    uint64_t a_start_time   ///< 1st Pulse Time (nanosecs)
)
{
    multimap< uint64_t, pair<double, uint16_t> >::iterator pmm;

    if ( m_pause_has_non_normalized )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::flushPauseData()",
            "Pause/Resume Marker Timestamps Require Normalization",
            "Marker(s) Received Before 1st Pulse...!" );
        give_syslog_a_chance;

        normalizeAnnotationTimestamps( a_start_time,
            "Pause/Resume Markers", m_pause_multimap,
            m_pause_has_non_normalized );
    }
    else
    {
        syslog( LOG_INFO, "[%i] %s: %s",
            g_pid, "NxGen::flushPauseData()",
            "Pause/Resume Marker Timestamps Are Already Normalized." );
        give_syslog_a_chance;
    }

    try
    {
        // Create Pause Event Log (with Known Minimum Chunk Size...)
        makeGroup( m_daslogs_path + "/pause", "NXlog" );
        makeDataset( m_daslogs_path + "/pause", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS, m_pause_multimap.size() );
        makeDataset( m_daslogs_path + "/pause", "value",
            NeXus::UINT16, "", m_pause_multimap.size() );

        // Extract Scan Index and Time Vectors from Multi-Map/Pair Beast!
        vector<uint16_t> value_vec;
        vector<double> time_vec;
        for ( pmm = m_pause_multimap.begin();
                pmm != m_pause_multimap.end(); ++pmm )
        {
            // Create Scan Index Vector
            value_vec.push_back( pmm->second.second );

            // Create Time Vector
            time_vec.push_back( pmm->second.first );
        }

        // Write Pause Time and Value Slabs
        writeSlab( m_daslogs_path + "/pause/value", value_vec, 0 );
        writeSlab( m_daslogs_path + "/pause/time", time_vec, 0 );

        m_pause_multimap.clear();
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
NxGen::flushScanData
(
    uint64_t a_start_time,                  ///< 1st Pulse Time (nanosecs)
    const STC::RunMetrics &a_run_metrics    ///< [in] Run metrics object
)
{
    multimap< uint64_t, pair<double, uint32_t> >::iterator smm;

    if ( m_scan_has_non_normalized )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::flushScanData()",
            "Scan Start/Stop Marker Timestamps Require Normalization",
            "Marker(s) Received Before 1st Pulse...!" );
        give_syslog_a_chance;

        normalizeAnnotationTimestamps( a_start_time,
            "Scan Start/Stop Markers", m_scan_multimap,
            m_scan_has_non_normalized );
    }
    else
    {
        syslog( LOG_INFO, "[%i] %s: %s",
            g_pid, "NxGen::flushScanData()",
            "Scan Start/Stop Marker Timestamps Are Already Normalized." );
        give_syslog_a_chance;
    }

    try
    {
        // Create Scan Event Log (with Known Minimum Chunk Size...)
        makeGroup( m_daslogs_path + "/scan_index", "NXlog" );
        makeDataset( m_daslogs_path + "/scan_index", "value",
            NeXus::UINT32, "", m_scan_multimap.size() );
        makeDataset( m_daslogs_path + "/scan_index", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS, m_scan_multimap.size() );

        writeScalar( m_daslogs_path + "/scan_index", "minimum_value",
            a_run_metrics.scan_stats.min(), "" );
        writeScalar( m_daslogs_path + "/scan_index", "maximum_value",
            a_run_metrics.scan_stats.max(), "" );
        writeScalar( m_daslogs_path + "/scan_index", "average_value",
            a_run_metrics.scan_stats.mean(), "" );
        writeScalar( m_daslogs_path + "/scan_index", "average_value_error",
            a_run_metrics.scan_stats.stdDev(), "" );

        // Extract Scan Index and Time Vectors from Multi-Map/Pair Beast!
        vector<uint32_t> value_vec;
        vector<double> time_vec;
        for ( smm = m_scan_multimap.begin();
                smm != m_scan_multimap.end(); ++smm )
        {
            // Create Scan Index Vector
            value_vec.push_back( smm->second.second );

            // Create Time Vector
            time_vec.push_back( smm->second.first );
        }

        // Write Scan Index Time and Value Slabs
        writeSlab( m_daslogs_path + "/scan_index/value", value_vec, 0 );
        writeSlab( m_daslogs_path + "/scan_index/time", time_vec, 0 );

        m_scan_multimap.clear();
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
NxGen::flushCommentData
(
    uint64_t a_start_time   ///< 1st Pulse Time (nanosecs)
)
{
    multimap< uint64_t, pair<double, string> >::iterator cmm;

    if ( m_comment_has_non_normalized )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::flushCommentData()",
            "Comment Timestamps Require Normalization",
            "Comment(s) Received Before 1st Pulse...!" );
        give_syslog_a_chance;

        normalizeAnnotationTimestamps( a_start_time,
            "Comments", m_comment_multimap,
            m_comment_has_non_normalized );
    }
    else
    {
        syslog( LOG_INFO, "[%i] s %s: %s",
            g_pid, "NxGen::flushCommentData()",
            "Comment Timestamps Are Already Normalized." );
        give_syslog_a_chance;
    }

    try
    {
        // Create Comment Event Log (with Known Minimum Chunk Size...)
        makeGroup( m_daslogs_path + "/comments", "NXcollection" );
        makeDataset( m_daslogs_path + "/comments", "time",
            NeXus::FLOAT64, TIME_SEC_UNITS, m_comment_multimap.size() );

        // Comment Strings as 2D String Dataset

        // Determine Max Comment String Length...
        uint32_t max_len = (uint32_t) -1;
        for ( cmm = m_comment_multimap.begin();
                cmm != m_comment_multimap.end(); ++cmm )
        {
            if ( max_len == (uint32_t) -1
                    || cmm->second.second.size() > max_len )
            {
                max_len = cmm->second.second.size();
            }
        }
        // Make Sure We Don't Freak Out HDF5 No Matter What...
        if ( max_len == (uint32_t) -1 || max_len == 0 )
            max_len = 1;

        syslog( LOG_INFO, "[%i] DASlogs Comments size=%lu max_len=%u",
            g_pid, m_comment_multimap.size(), max_len );
        give_syslog_a_chance;

        vector<double> time_vec;

        if ( m_comment_multimap.size() )
        {
            vector<hsize_t> dims;
            dims.push_back( m_comment_multimap.size() );
            dims.push_back( max_len );

            // Pad the Strings with Spaces to Be of Uniform Length...
            vector<string> value_vec;
            for ( cmm = m_comment_multimap.begin();
                    cmm != m_comment_multimap.end(); ++cmm )
            {
                // Pad Comment String...
                string str = cmm->second.second;
                if ( str.size() < max_len )
                    str.insert( str.end(), max_len - str.size(), ' ' );
                value_vec.push_back( str );

                // Create Time Vector
                time_vec.push_back( cmm->second.first );
            }

            // Write 2D String Dataset
            writeMultidimDataset( m_daslogs_path + "/comments",
                "value", value_vec, dims );
        }
        else
        {
            // Write Dummy Empty Comments Dataset...
            syslog( LOG_INFO, "[%i] %s", g_pid,
                "No Comment Strings, Creating Empty Comments Value" );
            give_syslog_a_chance;
            makeDataset( m_daslogs_path + "/comments",
                "value", NeXus::CHAR, "", 1 );
        }

        // Write Comment Time Slab
        writeSlab( m_daslogs_path + "/comments/time", time_vec, 0 );

        m_comment_multimap.clear();
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
    STC::Identifier a_devId,                 ///< [in] DeviceId
    vector<STC::PVEnumeratedType> &a_enumVec ///< [in/out] Vector of Enumerated Type Structs
)
{
    if ( !m_gen_nexus )
        return;

    // Do We Have a Valid Initialized NeXus Data File...?
    // (We shouldn't get called if not, so if we do, better force it,
    // lest we actually lose the final meta-data...!!)
    // Although if we just Forced Working Directory construction
    // in StreamParser::finalizeStreamProcessing(), then
    // Now May Be Our Chance to Finally Create a NeXus Data File...! ;-D
    if ( !initialize( true, "NxGen::writeDeviceEnums()" ) )
    {
        syslog( LOG_ERR, "[%i] %s %s: %s - %s",
            g_pid, "STC Error:", "NxGen::writeDeviceEnums()",
            "Failed to Force Initialize NeXus File",
            "Losing Device Enumeration Meta-Data!" );
        give_syslog_a_chance;
        return;
    }

    for ( vector<STC::PVEnumeratedType>::iterator ienum =
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
            for ( vector<STC::PVEnumeratedType>::iterator idup =
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
                        g_pid, "STC Error:", ss.str().c_str() );
                    give_syslog_a_chance;

                    stringstream ss_new;
                    ss_new << ienum->name << "_" << count;
                    enum_name = ss_new.str();

                    done = false;

                    break;
                }
            }
        }
        while ( !done );

        if ( verbose() > 2 )
        {
            stringstream ss;
            ss << "Creating Enum Log Group for Device " << a_devId
                << " enum_name=" << enum_name;
            syslog( LOG_INFO, "[%i] %s", g_pid, ss.str().c_str() );
            give_syslog_a_chance;
        }

        // Save "New Name" for Enum, For Subsequent Comparisons...!
        ienum->name = enum_name;

        try
        {
            // Enum Group (NXcollection)

            stringstream ss;
            ss << m_daslogs_path << "/" << "Device" << a_devId
                << ":" << "Enum" << ":" << enum_name;

            makeGroup( ss.str(), "NXcollection" );

            makeDataset( ss.str(), "values", NeXus::UINT32, "",
                ienum->element_values.size() );

            // Does Everything "Match Up" for the "Easy" Enum Format...?
            bool easy = true;
            if ( ienum->element_values.size()
                    != ienum->element_names.size() )
            {
                // Dang, No Easy Solution... (I.e. Element Array Mismatch!)
                stringstream sss;
                sss << "STC Error:"
                    << " writeDeviceEnums() Element Array Mismatch"
                    << " for Device " << a_devId
                    << " Enum " << enum_name
                    << " - No Easy Strings!";
                syslog( LOG_ERR, "[%i] %s", g_pid, sss.str().c_str() );
                give_syslog_a_chance;

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

            syslog( LOG_INFO, "[%i] Enum %s size=%lu max_len=%u", g_pid,
                ss.str().c_str(), ienum->element_names.size(), max_len );
            give_syslog_a_chance;

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
                    "STC Error: Empty Enum Names",
                    "Creating Dummy Names", ss.str().c_str() );
                give_syslog_a_chance;
                makeDataset( ss.str(), "names", NeXus::CHAR, "", 1 );
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
            sse << "STC Error:"
                << " writeDeviceEnums() failed"
                << " for Device " << a_devId
                << " Enum " << enum_name
                << " " << e.toString( true, true );
            syslog( LOG_ERR, "[%i] %s", g_pid, sse.str().c_str() );
            give_syslog_a_chance;
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
    STC::PVType a_type  ///< [in] PVType to be converted
) const
{
    switch( a_type )
    {
    case STC::PVT_INT:
        return NeXus::INT32;
    case STC::PVT_UINT:
    case STC::PVT_ENUM:
        return NeXus::UINT32;
    case STC::PVT_FLOAT:
        return NeXus::FLOAT32;
    case STC::PVT_DOUBLE:
        return NeXus::FLOAT64;
    case STC::PVT_STRING:
        return NeXus::CHAR;
    case STC::PVT_UINT_ARRAY:
        return NeXus::UINT32;
    case STC::PVT_DOUBLE_ARRAY:
        return NeXus::FLOAT64;
        break;
    }

    THROW_TRACE( STC::ERR_UNEXPECTED_INPUT,
        "toNxType() failed - invalid PV type: " << a_type )
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_group() failed for path: " << a_path )
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
    const string        a_units,    ///< [in] Optional units of new dataset
    unsigned long       a_chunk_size ///< [in] Optional chunk size override
)
{
    // Accept Chunk Size Override for Less-Than-Full-Chunk-Size Data...
    // NOTE: Chunk Size is measured in *Dataset Elements*...! :-O
    unsigned long chunk_size =
        ( a_chunk_size && a_chunk_size < m_chunk_size ) ?
            a_chunk_size : m_chunk_size;

    if ( m_h5nx.H5NXcreate_dataset_extend( a_path, a_name, a_type,
            chunk_size ) != SUCCEED )
    {
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
            "H5NXcreate_dataset_extend() failed for path: " << a_path
                << ", name: " << a_name )
    }

    if ( a_units.size() && a_units.compare("(unset)") )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name,
                "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
            "H5NXmake_dataset_vector() failed for path: " << a_path
                << ", name: " << a_name )
    }

    if ( a_units.size() && a_units.compare("(unset)") )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name,
                "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
                "H5NXmake_attribute_string() failed for path: " << a_path
                     << ", name: " << a_name )
        }
    }
}


/*! \brief Parses External STC Config File
 *
 * This method parses an External STC Config File
 * to generate a Custom NeXus File Group/Mapping Set.
 */
void
NxGen::parseSTCConfigFile
(
    const string &a_config_file     ///< [in] STC Config File Path
)
{
    xmlDocPtr doc = xmlReadFile( a_config_file.c_str(), 0, 0 );

    if ( doc )
    {
        string tag;
        string value;

        uint32_t conditionIndex = 0;

        bool parsed = false;

        try
        {
            xmlNode *root = xmlDocGetRootElement( doc );

            tag = (char*)root->name;
            getXmlNodeValue( root, value );

            if ( xmlStrcmp( root->name,
                    (const xmlChar*)"stc_config" ) != 0 )
            {
                syslog( LOG_ERR,
                    "[%i] %s %s <%s>=[%s] in STC Config File - %s",
                    g_pid, "STC Error:", "Unrecognized Root Tag",
                    tag.c_str(), value.c_str(), "Bailing on Config..." );
                give_syslog_a_chance;
                return;
            }

            for ( xmlNode *lev1 = root->children;
                    lev1 != 0; lev1 = lev1->next )
            {
                tag = (char*)lev1->name;
                getXmlNodeValue( lev1, value );

                parsed = true;

                //syslog( LOG_INFO, "[%i] %s Level 1 <%s>=[%s]",
                    //g_pid, "Parsing STC Config File",
                    //tag.c_str(), value.c_str() );
                //give_syslog_a_chance;

                // Log STC Config File Version...
                if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"version" ) == 0 )
                {
                    syslog( LOG_INFO, "[%i] %s Version: %s",
                        g_pid, "Parsing STC Config File", value.c_str() );
                    give_syslog_a_chance;
                }

                else if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"group" ) == 0 )
                {
                    std::stringstream ss_elements;
                    ss_elements << "elements=[";

                    std::string elem_sep = "";

                    std::stringstream ss_conditions;
                    ss_conditions << "conditions=[";

                    std::string cond_sep = "";

                    struct GroupInfo group;

                    group.created = false;

                    group.hasIndex = false;

                    conditionIndex = 0;

                    if ( verbose() > 2 )
                    {
                        syslog( LOG_INFO, "[%i] %s Found Group [%s]",
                            g_pid, "STC Config", value.c_str() );
                        give_syslog_a_chance;
                    }

                    for ( xmlNode *lev2 = lev1->children;
                            lev2 != 0; lev2 = lev2->next )
                    {
                        tag = (char*)lev2->name;
                        getXmlNodeValue( lev2, value );

                        //syslog( LOG_INFO, "[%i] %s Level 2 <%s>=[%s]",
                            //g_pid, "Parsing STC Config File",
                            //tag.c_str(), value.c_str() );
                        //give_syslog_a_chance;

                        if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"name" ) == 0 )
                        {
                            // Already Got A Group Name...?
                            if ( group.name.size() )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s [%s] -> [%s] - %s",
                                    g_pid, "STC Error:",
                                    "STC Config DUPLICATE Group Name",
                                    group.name.c_str(), value.c_str(),
                                    "Using New Group Name..." );
                                give_syslog_a_chance;
                            }
                            else if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group Name [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            group.name = value;

                            if ( group.name.find( NxGen::GroupNameIndex )
                                    != std::string::npos )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group is Indexed! [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;

                                group.hasIndex = true;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"path" ) == 0 )
                        {
                            // Already Got A Group Path...?
                            if ( group.path.size() )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s [%s] -> [%s] - %s",
                                    g_pid, "STC Error:",
                                    "STC Config DUPLICATE Group Path",
                                    group.path.c_str(), value.c_str(),
                                    "Using New Group Path..." );
                                give_syslog_a_chance;
                            }
                            else if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group Path [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            group.path = value;
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"type" ) == 0 )
                        {
                            // Already Got A Group Type...?
                            if ( group.type.size() )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s [%s] -> [%s] - %s",
                                    g_pid, "STC Error:",
                                    "STC Config DUPLICATE Group Type",
                                    group.type.c_str(), value.c_str(),
                                    "Using New Group Type..." );
                                give_syslog_a_chance;
                            }
                            else if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group Type [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            group.type = value;
                        }

                        else if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element" ) == 0
                                || xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_value" ) == 0
                                || xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_last_value" ) == 0 )
                        {
                            struct ElementInfo element;

                            if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_last_value" ) == 0 )
                            {
                                element.linkLastValue = true;
                                element.linkValue = false;
                            }
                            else if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_value" ) == 0 )
                            {
                                element.linkLastValue = false;
                                element.linkValue = true;
                            }
                            else
                            {
                                element.linkLastValue = false;
                                element.linkValue = false;
                            }

                            element.lastIndex = 0;

                            if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group Element [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            for ( xmlNode *lev3 = lev2->children;
                                    lev3 != 0; lev3 = lev3->next )
                            {
                                tag = (char*)lev3->name;
                                getXmlNodeValue( lev3, value );

                                //syslog( LOG_INFO,
                                    //"[%i] %s Element Level 3 <%s>=[%s]",
                                    //g_pid, "Parsing STC Config File",
                                    //tag.c_str(), value.c_str() );
                                //give_syslog_a_chance;

                                if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"pattern" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                       "[%i] %s Element Pattern #%ld [%s]",
                                            g_pid, "STC Config",
                                            element.patterns.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.patterns.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"index" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                        "[%i] %s Element Index #%ld [%s]",
                                            g_pid, "STC Config",
                                            element.indices.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.indices.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"name" ) == 0 )
                                {
                                    // Already Got An Element Name...?
                                    if ( element.name.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Element Name",
                                            element.name.c_str(),
                                            value.c_str(),
                                            "Using New Element Name..." );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s Element Name [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.name = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)
                                            "units_value" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "Element Units Value Pattern",
                                            element.unitsPatterns.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.unitsPatterns.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"units" ) == 0 )
                                {
                                    // Already Got Explicit Element Units?
                                    if ( element.units.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Element Units",
                                            element.units.c_str(),
                                            value.c_str(),
                                            "Using New Element Units..." );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s Element Units [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.units = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"text" ) != 0
                                    && xmlStrcmp( lev3->name,
                                        (const xmlChar*)"comment" ) != 0 )
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s %s at %s <%s>=[%s]",
                                        g_pid, "STC Error:",
                                        "Unknown Tag in STC Config",
                                        "Element Level 3",
                                        tag.c_str(), value.c_str() );
                                    give_syslog_a_chance;
                                }
                            }

                            // For Element Pattern Logging...
                            std::stringstream ss;
                            ss << "name=[" << element.name << "]";

                            ss << " patterns=[";
                            for ( uint32_t i=0 ;
                                    i < element.patterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.patterns[i];
                            }
                            ss << "]";

                            // For Element Index Logging...
                            ss << " indices=[";
                            for ( uint32_t i=0 ;
                                    i < element.indices.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.indices[i];
                            }
                            ss << "]";

                            // For Element Units Pattern Logging...
                            ss << " unitsPatterns=[";
                            for ( uint32_t i=0 ;
                                    i < element.unitsPatterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.unitsPatterns[i];
                            }
                            ss << "]";

                            // For Element Explicit Units Logging...
                            ss << " units=[" << element.units << "]";

                            // Add Element to Group Container...
                            // (If Required Fields are Present, Else Error)
                            if ( element.patterns.size()
                                    && ( !(group.hasIndex)
                                        || element.indices.size() )
                                    && element.name.size() )
                            {
                                // Check for Existing Element by Name...
                                if ( findGroupElementByName( element.name,
                                        group.elements ) )
                                {
                                    std::string err =
                                        "Duplicate Element Name ";
                                    err += "\"" + element.name + "\"";
                                    err += " in STC Config Group \""
                                        + group.name + "\"";
                                    syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                        g_pid, "STC Error:", err.c_str(),
                                        "Ignoring", ss.str().c_str() );
                                    give_syslog_a_chance;
                                }
                                else
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s \"%s\" - %s",
                                            g_pid, "STC Config",
                                            "Adding Element to Group",
                                            group.name.c_str(),
                                            ss.str().c_str() );
                                        give_syslog_a_chance;
                                    }

                                    group.elements.push_back( element );

                                    ss_elements
                                        << elem_sep << element.name;
                                    elem_sep = ", ";
                                }
                            }
                            else
                            {
                                std::string err = "Incomplete Element";
                                err += " in STC Config Group \""
                                    + group.name + "\"";
                                syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                    g_pid, "STC Error:", err.c_str(),
                                    "Ignoring", ss.str().c_str() );
                                give_syslog_a_chance;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"condition" ) == 0 )
                        {
                            std::stringstream ss_cond_elems;
                            ss_cond_elems << "elements=[";

                            std::string cond_elem_sep = "";

                            struct ConditionInfo condition;

                            condition.is_set = false;

                            if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Group Condition [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            for ( xmlNode *lev3 = lev2->children;
                                    lev3 != 0; lev3 = lev3->next )
                            {
                                tag = (char*)lev3->name;
                                getXmlNodeValue( lev3, value );

                                //syslog( LOG_INFO,
                                    //"[%i] %s %s Level 3 <%s>=[%s]",
                                    //g_pid, "Parsing STC Config File",
                                    //"Condition",
                                    //tag.c_str(), value.c_str() );
                                //give_syslog_a_chance;

                                if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"name" ) == 0 )
                                {
                                    // Already Got A Condition Name...?
                                    if ( condition.name.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Condition Name",
                                            condition.name.c_str(),
                                            value.c_str(),
                                            "Using New Condition Name..."
                                        );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                           "[%i] %s Condition Name [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.name = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"pattern" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config", "Pattern",
                                            condition.patterns.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.patterns.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"value_string" )
                                            == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "Value String",
                                        condition.value_strings.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.value_strings.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"value" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config", "Value",
                                            condition.values.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.values.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"not_value_string"
                                            ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "NOT Value String",
                                        condition.not_value_strings.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.not_value_strings.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"not_value" )
                                            == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "NOT Value",
                                            condition.not_values.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.not_values.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element" ) == 0
                                        || xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element_value" ) == 0
                                        || xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                              "element_last_value" ) == 0 )
                                {
                                    struct ElementInfo element;

                                    if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                              "element_last_value" ) == 0 )
                                    {
                                        element.linkLastValue = true;
                                        element.linkValue = false;
                                    }
                                    else if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element_value" ) == 0 )
                                    {
                                        element.linkLastValue = false;
                                        element.linkValue = true;
                                    }
                                    else
                                    {
                                        element.linkLastValue = false;
                                        element.linkValue = false;
                                    }

                                    element.lastIndex = 0;

                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s [%s]",
                                            g_pid, "STC Config",
                                            "Group Condition Element",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    for ( xmlNode *lev4 = lev3->children;
                                            lev4 != 0; lev4 = lev4->next )
                                    {
                                        tag = (char*)lev4->name;
                                        getXmlNodeValue( lev4, value );

                                        //syslog( LOG_INFO,
                                            //"[%i] %s %s <%s>=[%s]",
                                            //g_pid,
                                            //"Parsing STC Config File",
                                            //"Condition Element Level 3",
                                            //tag.c_str(),
                                            //value.c_str() );
                                        //give_syslog_a_chance;

                                        if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"pattern" )
                                                    == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                "[%i] %s %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                                    "Condition Element",
                                                    "Pattern",
                                                    element.patterns.size()
                                                        + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.patterns.push_back(
                                                value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"index" )
                                                    == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                "[%i] %s %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                                    "Condition Element",
                                                    "Index",
                                                    element.indices.size()
                                                        + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.indices.push_back(
                                                value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"name" )
                                                    == 0 )
                                        {
                                            // Already Got An Element Name?
                                            if ( element.name.size() )
                                            {
                                                syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                                    g_pid, "STC Error:",
                                                    "STC Config DUPLICATE",
                                                "Condition Element Name",
                                                    element.name.c_str(),
                                                    value.c_str(),
                                                "Using New Element Name..."
                                                );
                                                give_syslog_a_chance;
                                            }
                                            else if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                    "[%i] %s %s [%s]",
                                                    g_pid, "STC Config",
                                                  "Condition Element Name",
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.name = value;
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)
                                                    "units_value" ) == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                    "[%i] %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                             "Element Units Value Pattern",
                                                    element.unitsPatterns
                                                        .size() + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.unitsPatterns
                                                .push_back( value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)
                                                    "units" ) == 0 )
                                        {
                                            // Already Got Explicit
                                            // Element Units?
                                            if ( element.units.size() )
                                            {
                                                syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                                    g_pid, "STC Error:",
                                                    "STC Config DUPLICATE",
                                                    "Element Units",
                                                    element.units.c_str(),
                                                    value.c_str(),
                                               "Using New Element Units..."
                                                );
                                                give_syslog_a_chance;
                                            }
                                            else if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                              "[%i] %s Element Units [%s]",
                                                    g_pid, "STC Config",
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.units = value;
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"text" )
                                                    != 0
                                            && xmlStrcmp( lev4->name,
                                                (const xmlChar*)"comment" )
                                                    != 0 )
                                        {
                                            syslog( LOG_ERR,
                                            "[%i] %s %s at %s <%s>=[%s]",
                                                g_pid, "STC Error:",
                                            "Unknown Tag in STC Config",
                                            "Condition Element Level 4",
                                                tag.c_str(), value.c_str()
                                            );
                                            give_syslog_a_chance;
                                        }
                                    }

                                    // For Element Pattern Logging...
                                    std::stringstream ss;
                                    ss << "name=[" << element.name << "]";

                                    ss << " patterns=[";
                                    for ( uint32_t i=0 ;
                                            i < element.patterns.size();
                                            i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.patterns[i];
                                    }
                                    ss << "]";

                                    // For Element Index Logging...
                                    ss << " indices=[";
                                    for ( uint32_t i=0 ;
                                            i < element.indices.size();
                                            i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.indices[i];
                                    }
                                    ss << "]";

                                    // For Element Units Pattern Logging...
                                    ss << " unitsPatterns=[";
                                    for ( uint32_t i=0 ;
                                            i < element.unitsPatterns
                                                .size(); i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.unitsPatterns[i];
                                    }
                                    ss << "]";

                                    // For Element Explicit Units Logging
                                    ss << " units=["
                                        << element.units << "]";

                                    // Add Element to Group Condition...
                                    // (If Required Fields are Present,
                                    // Else Error)
                                    if ( element.patterns.size()
                                            && ( !(group.hasIndex)
                                                || element.indices.size() )
                                            && element.name.size() )
                                    {
                                        // Check for Existing Element
                                        // in Group by Name?
                                        if ( findGroupElementByName(
                                                element.name,
                                                group.elements ) )
                                        {
                                            std::string err =
                                                "Duplicate Element Name ";
                                            err += "\"" + element.name
                                                + "\"";
                                            err +=
                                                " in STC Config Group \""
                                                + group.name + "\"";
                                            syslog( LOG_ERR,
                                                "[%i] %s %s - %s %s",
                                                g_pid, "STC Error:",
                                                err.c_str(), "Ignoring",
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                        // Check for Existing Element
                                        // in Condition by Name?
                                        else if ( findGroupElementByName(
                                                element.name,
                                                condition.elements ) )
                                        {
                                            std::string err =
                                                "Duplicate Element Name ";
                                            err += "\"" + element.name
                                                + "\"";
                                            err +=
                                                " in STC Config Group \""
                                                + group.name + "\"";
                                            err += " Condition \""
                                                + condition.name + "\"";
                                            syslog( LOG_ERR,
                                                "[%i] %s %s - %s %s",
                                                g_pid, "STC Error:",
                                                err.c_str(), "Ignoring",
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                        else
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                std::string info =
                                                    "STC Config Adding";
                                                info += " Element";
                                                info += " to Condition \""
                                                   + condition.name + "\"";
                                                info += " for Group \""
                                                    + group.name + "\"";
                                                syslog( LOG_INFO,
                                                    "[%i] %s - %s",
                                                    g_pid, info.c_str(),
                                                    ss.str().c_str() );
                                                give_syslog_a_chance;
                                            }

                                            condition.elements.push_back(
                                                element );

                                            ss_cond_elems << cond_elem_sep
                                                << ss.str();
                                            cond_elem_sep = "; ";
                                        }
                                    }
                                    else
                                    {
                                        std::string err = "Incomplete";
                                        err += " Condition \""
                                            + condition.name + "\"";
                                        err += " Element in STC Config";
                                        err += " Group \""
                                            + group.name + "\"";
                                        syslog( LOG_ERR,
                                            "[%i] %s %s - %s %s",
                                            g_pid, "STC Error:",
                                            err.c_str(), "Ignoring",
                                            ss.str().c_str() );
                                        give_syslog_a_chance;
                                    }
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"text" ) != 0
                                    && xmlStrcmp( lev3->name,
                                        (const xmlChar*)"comment" ) != 0 )
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s %s at %s <%s>=[%s]",
                                        g_pid, "STC Error:",
                                        "Unknown Tag in STC Config",
                                        "Condition Level 3",
                                        tag.c_str(), value.c_str() );
                                    give_syslog_a_chance;
                                }
                            }

                            // Note: the Condition Name is optional, it's
                            // really only for human readability... ;-D
                            // So if this Condition doesn't have a Name,
                            // just assign it the next Unnamed Index...
                            if ( condition.name.size() == 0 )
                            {
                                stringstream ss;
                                ss << "UnnamedCondition"
                                    << ++conditionIndex;
                                condition.name = ss.str();
                            }

                            // Construct Logging String...
                            std::stringstream ss;
                            ss << "patterns=[";
                            for ( uint32_t i=0 ;
                                    i < condition.patterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.patterns[i];
                            }
                            ss << "] ";
                            ss << "value_strings=[";
                            for ( uint32_t i=0 ;
                                    i < condition.value_strings.size();
                                    i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.value_strings[i];
                            }
                            ss << "] ";
                            ss << "values=[";
                            for ( uint32_t i=0 ;
                                    i < condition.values.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.values[i];
                            }
                            ss << "] ";
                            ss << "not_value_strings=[";
                            for ( uint32_t i=0 ;
                                    i < condition.not_value_strings.size();
                                    i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.not_value_strings[i];
                            }
                            ss << "] ";
                            ss << "values=[";
                            for ( uint32_t i=0 ;
                                    i < condition.not_values.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.not_values[i];
                            }
                            ss << "]";

                            // Add Condition to Group Container...
                            // (If Required Fields are Present, Else Error)
                            if ( condition.patterns.size()
                                && ( condition.value_strings.size()
                                    || condition.values.size()
                                    || condition.not_value_strings.size()
                                    || condition.not_values.size() ) )
                            {
                                if ( verbose() > 2 )
                                {
                                    syslog( LOG_INFO,
                                        "[%i] %s \"%s\" %s \"%s\" - %s",
                                        g_pid,
                                        "STC Config Adding Condition",
                                        condition.name.c_str(),
                                        "to Group", group.name.c_str(),
                                        ss.str().c_str() );
                                    give_syslog_a_chance;
                                }

                                group.conditions.push_back( condition );

                                ss_conditions << cond_sep
                                    << "\"" << condition.name << "\": "
                                    << ss.str()
                                    << " " << ss_cond_elems.str() << "]";
                                cond_sep = "; ";
                            }
                            else
                            {
                                std::string err = "Incomplete Condition";
                                err += " \"" + condition.name + "\"";
                                err += " in STC Config Group \""
                                    + group.name + "\"";
                                syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                    g_pid, "STC Error:", err.c_str(),
                                    "Ignoring", ss.str().c_str() );
                                give_syslog_a_chance;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"text" ) != 0
                            && xmlStrcmp( lev2->name,
                                (const xmlChar*)"comment" ) != 0 )
                        {
                            syslog( LOG_ERR,
                                "[%i] %s %s at Level 2 <%s>=[%s]",
                                g_pid, "STC Error:",
                                "Unknown Tag in STC Config",
                                tag.c_str(), value.c_str() );
                            give_syslog_a_chance;
                        }
                    }

                    // Add Group Container to STC Config...
                    // (If Required Fields are Present, Else Error)
                    // Note: It's Ok to have No Elements, Only Conditions!
                    if ( group.name.size()
                            && group.path.size()
                            && group.type.size()
                            && ( group.elements.size()
                                || group.conditions.size() ) )
                    {
                        if ( verbose() > 1 ) {
                            std::stringstream ss;
                            ss << "Adding Group Container"
                                << " \"" << group.name << "\""
                                << " to STC Config -"
                                << " path=[" << group.path << "]"
                                << " type=[" << group.type << "]"
                                << " ("
                                << group.elements.size() << " elements, "
                                << group.conditions.size()
                                    << " conditions)"
                                << ": " << ss_elements.str() << "]"
                                << " " << ss_conditions.str() << "]";
                            syslog( LOG_INFO, "[%i] %s",
                                g_pid, ss.str().c_str() );
                            give_syslog_a_chance;
                        }

                        m_config_groups.push_back( group );
                    }
                    else
                    {
                        std::string err = "Incomplete Group Container \""
                            + group.name
                            + "\" in STC Config";
                        syslog( LOG_ERR,
                        "[%i] %s %s - %s %s=[%s] %s=[%s] (%lu %s, %lu %s)",
                            g_pid, "STC Error:", err.c_str(),
                            "Ignoring",
                            "path", group.path.c_str(),
                            "type", group.type.c_str(),
                            group.elements.size(), "elements",
                            group.conditions.size(), "conditions" );
                        give_syslog_a_chance;
                    }
                }

                else if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"command" ) == 0 )
                {
                    std::stringstream ss_elements;
                    ss_elements << "elements=[";

                    std::string elem_sep = "";

                    std::stringstream ss_conditions;
                    ss_conditions << "conditions=[";

                    std::string cond_sep = "";

                    struct CommandInfo command;

                    command.hasIndex = false;

                    conditionIndex = 0;

                    if ( verbose() > 2 )
                    {
                        syslog( LOG_INFO, "[%i] %s Found Command [%s]",
                            g_pid, "STC Config", value.c_str() );
                        give_syslog_a_chance;
                    }

                    for ( xmlNode *lev2 = lev1->children;
                            lev2 != 0; lev2 = lev2->next )
                    {
                        tag = (char*)lev2->name;
                        getXmlNodeValue( lev2, value );

                        //syslog( LOG_INFO, "[%i] %s Level 2 <%s>=[%s]",
                            //g_pid, "Parsing STC Config File",
                            //tag.c_str(), value.c_str() );
                        //give_syslog_a_chance;

                        if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"name" ) == 0 )
                        {
                            // Already Got A Command Name...?
                            if ( command.name.size() )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s [%s] -> [%s] - %s",
                                    g_pid, "STC Error:",
                                    "STC Config DUPLICATE Command Name",
                                    command.name.c_str(), value.c_str(),
                                    "Using New Command Name..." );
                                give_syslog_a_chance;
                            }
                            else if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Command Name [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            command.name = value;

                            if ( command.name.find( NxGen::GroupNameIndex )
                                    != std::string::npos )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Command is Indexed! [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;

                                command.hasIndex = true;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"path" ) == 0 )
                        {
                            // Already Got A Command Path...?
                            if ( command.path.size() )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s [%s] -> [%s] - %s",
                                    g_pid, "STC Error:",
                                    "STC Config DUPLICATE Command Path",
                                    command.path.c_str(), value.c_str(),
                                    "Using New Command Path..." );
                                give_syslog_a_chance;
                            }
                            else if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Command Path [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            command.path = value;
                        }

                        else if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element" ) == 0
                                || xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_value" ) == 0
                                || xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_last_value" ) == 0 )
                        {
                            struct ElementInfo element;

                            if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_last_value" ) == 0 )
                            {
                                element.linkLastValue = true;
                                element.linkValue = false;
                            }
                            else if ( xmlStrcmp( lev2->name,
                                    (const xmlChar*)
                                        "element_value" ) == 0 )
                            {
                                element.linkLastValue = false;
                                element.linkValue = true;
                            }
                            else
                            {
                                element.linkLastValue = false;
                                element.linkValue = false;
                            }

                            element.lastIndex = 0;

                            if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Command Element [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            for ( xmlNode *lev3 = lev2->children;
                                    lev3 != 0; lev3 = lev3->next )
                            {
                                tag = (char*)lev3->name;
                                getXmlNodeValue( lev3, value );

                                //syslog( LOG_INFO,
                                    //"[%i] %s Element Level 3 <%s>=[%s]",
                                    //g_pid, "Parsing STC Config File",
                                    //tag.c_str(), value.c_str() );
                                //give_syslog_a_chance;

                                if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"pattern" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                       "[%i] %s Element Pattern #%ld [%s]",
                                            g_pid, "STC Config",
                                            element.patterns.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.patterns.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"index" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                        "[%i] %s Element Index #%ld [%s]",
                                            g_pid, "STC Config",
                                            element.indices.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.indices.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"name" ) == 0 )
                                {
                                    // Already Got An Element Name...?
                                    if ( element.name.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Element Name",
                                            element.name.c_str(),
                                            value.c_str(),
                                            "Using New Element Name..." );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s Element Name [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.name = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)
                                            "units_value" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "Element Units Value Pattern",
                                            element.unitsPatterns.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.unitsPatterns.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"units" ) == 0 )
                                {
                                    // Already Got Explicit Element Units?
                                    if ( element.units.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Element Units",
                                            element.units.c_str(),
                                            value.c_str(),
                                            "Using New Element Units..." );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s Element Units [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    element.units = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"text" ) != 0
                                    && xmlStrcmp( lev3->name,
                                        (const xmlChar*)"comment" ) != 0 )
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s %s at %s <%s>=[%s]",
                                        g_pid, "STC Error:",
                                        "Unknown Tag in STC Config",
                                        "Element Level 3",
                                        tag.c_str(), value.c_str() );
                                    give_syslog_a_chance;
                                }
                            }

                            // For Element Pattern Logging...
                            std::stringstream ss;
                            ss << "name=[" << element.name << "]";

                            ss << " patterns=[";
                            for ( uint32_t i=0 ;
                                    i < element.patterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.patterns[i];
                            }
                            ss << "]";

                            // For Element Index Logging...
                            ss << " indices=[";
                            for ( uint32_t i=0 ;
                                    i < element.indices.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.indices[i];
                            }
                            ss << "]";

                            // For Element Units Pattern Logging...
                            ss << " unitsPatterns=[";
                            for ( uint32_t i=0 ;
                                    i < element.unitsPatterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << element.unitsPatterns[i];
                            }
                            ss << "]";

                            // For Element Explicit Units Logging...
                            ss << " units=[" << element.units << "]";

                            // Add Element to Command Container...
                            // (If Required Fields are Present, Else Error)
                            if ( element.patterns.size()
                                    && ( !(command.hasIndex)
                                        || element.indices.size() )
                                    && element.name.size() )
                            {
                                // Check for Existing Element by Name...
                                if ( findGroupElementByName( element.name,
                                        command.elements ) )
                                {
                                    std::string err =
                                        "Duplicate Element Name ";
                                    err += "\"" + element.name + "\"";
                                    err += " in STC Config Command \""
                                        + command.name + "\"";
                                    syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                        g_pid, "STC Error:", err.c_str(),
                                        "Ignoring", ss.str().c_str() );
                                    give_syslog_a_chance;
                                }
                                else
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s \"%s\" - %s",
                                            g_pid, "STC Config",
                                            "Adding Element to Command",
                                            command.name.c_str(),
                                            ss.str().c_str() );
                                        give_syslog_a_chance;
                                    }

                                    command.elements.push_back( element );

                                    ss_elements
                                        << elem_sep << element.name;
                                    elem_sep = ", ";
                                }
                            }
                            else
                            {
                                std::string err = "Incomplete Element";
                                err += " in STC Config Command \""
                                    + command.name + "\"";
                                syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                    g_pid, "STC Error:", err.c_str(),
                                    "Ignoring", ss.str().c_str() );
                                give_syslog_a_chance;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"condition" ) == 0 )
                        {
                            std::stringstream ss_cond_elems;
                            ss_cond_elems << "elements=[";

                            std::string cond_elem_sep = "";

                            struct ConditionInfo condition;

                            condition.is_set = false;

                            if ( verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s Command Condition [%s]",
                                    g_pid, "STC Config", value.c_str() );
                                give_syslog_a_chance;
                            }

                            for ( xmlNode *lev3 = lev2->children;
                                    lev3 != 0; lev3 = lev3->next )
                            {
                                tag = (char*)lev3->name;
                                getXmlNodeValue( lev3, value );

                                //syslog( LOG_INFO,
                                    //"[%i] %s %s Level 3 <%s>=[%s]",
                                    //g_pid, "Parsing STC Config File",
                                    //"Condition",
                                    //tag.c_str(), value.c_str() );
                                //give_syslog_a_chance;

                                if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"name" ) == 0 )
                                {
                                    // Already Got A Condition Name...?
                                    if ( condition.name.size() )
                                    {
                                        syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                            g_pid, "STC Error:",
                                            "STC Config DUPLICATE",
                                            "Condition Name",
                                            condition.name.c_str(),
                                            value.c_str(),
                                            "Using New Condition Name..."
                                        );
                                        give_syslog_a_chance;
                                    }
                                    else if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                           "[%i] %s Condition Name [%s]",
                                            g_pid, "STC Config",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.name = value;
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"pattern" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config", "Pattern",
                                            condition.patterns.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.patterns.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"value_string" )
                                            == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "Value String",
                                        condition.value_strings.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.value_strings.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"value" ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config", "Value",
                                            condition.values.size() + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.values.push_back( value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"not_value_string"
                                            ) == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "NOT Value String",
                                        condition.not_value_strings.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.not_value_strings.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"not_value" )
                                            == 0 )
                                {
                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                          "[%i] %s Condition %s #%ld [%s]",
                                            g_pid, "STC Config",
                                            "NOT Value",
                                            condition.not_values.size()
                                                + 1,
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    condition.not_values.push_back(
                                        value );
                                }

                                else if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element" ) == 0
                                        || xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element_value" ) == 0
                                        || xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                              "element_last_value" ) == 0 )
                                {
                                    struct ElementInfo element;

                                    if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                              "element_last_value" ) == 0 )
                                    {
                                        element.linkLastValue = true;
                                        element.linkValue = false;
                                    }
                                    else if ( xmlStrcmp( lev3->name,
                                            (const xmlChar*)
                                                "element_value" ) == 0 )
                                    {
                                        element.linkLastValue = false;
                                        element.linkValue = true;
                                    }
                                    else
                                    {
                                        element.linkLastValue = false;
                                        element.linkValue = false;
                                    }

                                    element.lastIndex = 0;

                                    if ( verbose() > 2 )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s %s [%s]",
                                            g_pid, "STC Config",
                                            "Command Condition Element",
                                            value.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    for ( xmlNode *lev4 = lev3->children;
                                            lev4 != 0; lev4 = lev4->next )
                                    {
                                        tag = (char*)lev4->name;
                                        getXmlNodeValue( lev4, value );

                                        //syslog( LOG_INFO,
                                            //"[%i] %s %s <%s>=[%s]",
                                            //g_pid,
                                            //"Parsing STC Config File",
                                            //"Condition Element Level 3",
                                            //tag.c_str(),
                                            //value.c_str() );
                                        //give_syslog_a_chance;

                                        if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"pattern" )
                                                    == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                "[%i] %s %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                                    "Condition Element",
                                                    "Pattern",
                                                    element.patterns.size()
                                                        + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.patterns.push_back(
                                                value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"index" )
                                                    == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                "[%i] %s %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                                    "Condition Element",
                                                    "Index",
                                                    element.indices.size()
                                                        + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.indices.push_back(
                                                value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"name" )
                                                    == 0 )
                                        {
                                            // Already Got An Element Name?
                                            if ( element.name.size() )
                                            {
                                                syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                                    g_pid, "STC Error:",
                                                    "STC Config DUPLICATE",
                                                "Condition Element Name",
                                                    element.name.c_str(),
                                                    value.c_str(),
                                                "Using New Element Name..."
                                                );
                                                give_syslog_a_chance;
                                            }
                                            else if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                    "[%i] %s %s [%s]",
                                                    g_pid, "STC Config",
                                                  "Condition Element Name",
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.name = value;
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)
                                                    "units_value" ) == 0 )
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                                    "[%i] %s %s #%ld [%s]",
                                                    g_pid, "STC Config",
                                             "Element Units Value Pattern",
                                                    element.unitsPatterns
                                                        .size() + 1,
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.unitsPatterns
                                                .push_back( value );
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)
                                                    "units" ) == 0 )
                                        {
                                            // Already Got Explicit
                                            // Element Units?
                                            if ( element.units.size() )
                                            {
                                                syslog( LOG_ERR,
                                        "[%i] %s %s %s [%s] -> [%s] - %s",
                                                    g_pid, "STC Error:",
                                                    "STC Config DUPLICATE",
                                                    "Element Units",
                                                    element.units.c_str(),
                                                    value.c_str(),
                                               "Using New Element Units..."
                                                );
                                                give_syslog_a_chance;
                                            }
                                            else if ( verbose() > 2 )
                                            {
                                                syslog( LOG_INFO,
                                              "[%i] %s Element Units [%s]",
                                                    g_pid, "STC Config",
                                                    value.c_str() );
                                                give_syslog_a_chance;
                                            }

                                            element.units = value;
                                        }

                                        else if ( xmlStrcmp( lev4->name,
                                                (const xmlChar*)"text" )
                                                    != 0
                                            && xmlStrcmp( lev4->name,
                                                (const xmlChar*)"comment" )
                                                    != 0 )
                                        {
                                            syslog( LOG_ERR,
                                            "[%i] %s %s at %s <%s>=[%s]",
                                                g_pid, "STC Error:",
                                            "Unknown Tag in STC Config",
                                            "Condition Element Level 4",
                                                tag.c_str(), value.c_str()
                                            );
                                            give_syslog_a_chance;
                                        }
                                    }

                                    // For Element Pattern Logging...
                                    std::stringstream ss;
                                    ss << "name=[" << element.name << "]";

                                    ss << " patterns=[";
                                    for ( uint32_t i=0 ;
                                            i < element.patterns.size();
                                            i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.patterns[i];
                                    }
                                    ss << "]";

                                    // For Element Index Logging...
                                    ss << " indices=[";
                                    for ( uint32_t i=0 ;
                                            i < element.indices.size();
                                            i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.indices[i];
                                    }
                                    ss << "]";

                                    // For Element Units Pattern Logging...
                                    ss << " unitsPatterns=[";
                                    for ( uint32_t i=0 ;
                                            i < element.unitsPatterns
                                                .size(); i++ )
                                    {
                                        if ( i ) ss << ", ";
                                        ss << element.unitsPatterns[i];
                                    }
                                    ss << "]";

                                    // For Element Explicit Units Logging
                                    ss << " units=["
                                        << element.units << "]";

                                    // Add Element to Command Condition...
                                    // (If Required Fields are Present,
                                    // Else Error)
                                    if ( element.patterns.size()
                                            && ( !(command.hasIndex)
                                                || element.indices.size() )
                                            && element.name.size() )
                                    {
                                        // Check for Existing Element
                                        // in Command by Name?
                                        if ( findGroupElementByName(
                                                element.name,
                                                command.elements ) )
                                        {
                                            std::string err =
                                                "Duplicate Element Name ";
                                            err += "\"" + element.name
                                                + "\"";
                                            err +=
                                                " in STC Config Command \""
                                                + command.name + "\"";
                                            syslog( LOG_ERR,
                                                "[%i] %s %s - %s %s",
                                                g_pid, "STC Error:",
                                                err.c_str(), "Ignoring",
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                        // Check for Existing Element
                                        // in Condition by Name?
                                        else if ( findGroupElementByName(
                                                element.name,
                                                condition.elements ) )
                                        {
                                            std::string err =
                                                "Duplicate Element Name ";
                                            err += "\"" + element.name
                                                + "\"";
                                            err +=
                                                " in STC Config Command \""
                                                + command.name + "\"";
                                            err += " Condition \""
                                                + condition.name + "\"";
                                            syslog( LOG_ERR,
                                                "[%i] %s %s - %s %s",
                                                g_pid, "STC Error:",
                                                err.c_str(), "Ignoring",
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                        else
                                        {
                                            if ( verbose() > 2 )
                                            {
                                                std::string info =
                                                    "STC Config Adding";
                                                info += " Element";
                                                info += " to Condition \""
                                                   + condition.name + "\"";
                                                info += " for Command \""
                                                    + command.name + "\"";
                                                syslog( LOG_INFO,
                                                    "[%i] %s - %s",
                                                    g_pid, info.c_str(),
                                                    ss.str().c_str() );
                                                give_syslog_a_chance;
                                            }

                                            condition.elements.push_back(
                                                element );

                                            ss_cond_elems << cond_elem_sep
                                                << ss.str();
                                            cond_elem_sep = "; ";
                                        }
                                    }
                                    else
                                    {
                                        std::string err = "Incomplete";
                                        err += " Condition \""
                                            + condition.name + "\"";
                                        err += " Element in STC Config";
                                        err += " Command \""
                                            + command.name + "\"";
                                        syslog( LOG_ERR,
                                            "[%i] %s %s - %s %s",
                                            g_pid, "STC Error:",
                                            err.c_str(), "Ignoring",
                                            ss.str().c_str() );
                                        give_syslog_a_chance;
                                    }
                                }

                                else if ( xmlStrcmp( lev3->name,
                                        (const xmlChar*)"text" ) != 0
                                    && xmlStrcmp( lev3->name,
                                        (const xmlChar*)"comment" ) != 0 )
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s %s at %s <%s>=[%s]",
                                        g_pid, "STC Error:",
                                        "Unknown Tag in STC Config",
                                        "Condition Level 3",
                                        tag.c_str(), value.c_str() );
                                    give_syslog_a_chance;
                                }
                            }

                            // Note: the Condition Name is optional, it's
                            // really only for human readability... ;-D
                            // So if this Condition doesn't have a Name,
                            // just assign it the next Unnamed Index...
                            if ( condition.name.size() == 0 )
                            {
                                stringstream ss;
                                ss << "UnnamedCondition"
                                    << ++conditionIndex;
                                condition.name = ss.str();
                            }

                            // Construct Logging String...
                            std::stringstream ss;
                            ss << "patterns=[";
                            for ( uint32_t i=0 ;
                                    i < condition.patterns.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.patterns[i];
                            }
                            ss << "] ";
                            ss << "value_strings=[";
                            for ( uint32_t i=0 ;
                                    i < condition.value_strings.size();
                                    i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.value_strings[i];
                            }
                            ss << "] ";
                            ss << "values=[";
                            for ( uint32_t i=0 ;
                                    i < condition.values.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.values[i];
                            }
                            ss << "] ";
                            ss << "not_value_strings=[";
                            for ( uint32_t i=0 ;
                                    i < condition.not_value_strings.size();
                                    i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.not_value_strings[i];
                            }
                            ss << "] ";
                            ss << "values=[";
                            for ( uint32_t i=0 ;
                                    i < condition.not_values.size(); i++ )
                            {
                                if ( i ) ss << ", ";
                                ss << condition.not_values[i];
                            }
                            ss << "]";

                            // Add Condition to Command Container...
                            // (If Required Fields are Present, Else Error)
                            if ( condition.patterns.size()
                                && ( condition.value_strings.size()
                                    || condition.values.size()
                                    || condition.not_value_strings.size()
                                    || condition.not_values.size() ) )
                            {
                                if ( verbose() > 2 )
                                {
                                    syslog( LOG_INFO,
                                        "[%i] %s \"%s\" %s \"%s\" - %s",
                                        g_pid,
                                        "STC Config Adding Condition",
                                        condition.name.c_str(),
                                        "to Command", command.name.c_str(),
                                        ss.str().c_str() );
                                    give_syslog_a_chance;
                                }

                                command.conditions.push_back( condition );

                                ss_conditions << cond_sep
                                    << "\"" << condition.name << "\": "
                                    << ss.str()
                                    << " " << ss_cond_elems.str() << "]";
                                cond_sep = "; ";
                            }
                            else
                            {
                                std::string err = "Incomplete Condition";
                                err += " \"" + condition.name + "\"";
                                err += " in STC Config Command \""
                                    + command.name + "\"";
                                syslog( LOG_ERR, "[%i] %s %s - %s %s",
                                    g_pid, "STC Error:", err.c_str(),
                                    "Ignoring", ss.str().c_str() );
                                give_syslog_a_chance;
                            }
                        }

                        else if ( xmlStrcmp( lev2->name,
                                (const xmlChar*)"text" ) != 0
                            && xmlStrcmp( lev2->name,
                                (const xmlChar*)"comment" ) != 0 )
                        {
                            syslog( LOG_ERR,
                                "[%i] %s %s at Level 2 <%s>=[%s]",
                                g_pid, "STC Error:",
                                "Unknown Tag in STC Config",
                                tag.c_str(), value.c_str() );
                            give_syslog_a_chance;
                        }
                    }

                    // Add Command Container to STC Config...
                    // (If Required Fields are Present, Else Error)
                    // Note: For a *Command*, It's Ok to have No Elements,
                    // and No Conditions! ;-D
                    if ( command.name.size()
                            && command.path.size() )
                    {
                        std::stringstream ss;
                        ss << "Adding Command Container"
                            << " \"" << command.name << "\""
                            << " to STC Config -"
                            << " path=[" << command.path << "]"
                            << " ("
                            << command.elements.size() << " elements, "
                            << command.conditions.size() << " conditions)"
                            << ": " << ss_elements.str() << "]"
                            << " " << ss_conditions.str() << "]";
                        syslog( LOG_INFO, "[%i] %s",
                            g_pid, ss.str().c_str() );
                        give_syslog_a_chance;

                        m_config_commands.push_back( command );
                    }
                    else
                    {
                        std::string err = "Incomplete Command Container \""
                            + command.name
                            + "\" in STC Config";
                        syslog( LOG_ERR,
                            "[%i] %s %s - %s %s=[%s] (%lu %s, %lu %s)",
                            g_pid, "STC Error:", err.c_str(),
                            "Ignoring",
                            "path", command.path.c_str(),
                            command.elements.size(), "elements",
                            command.conditions.size(), "conditions" );
                        give_syslog_a_chance;
                    }
                }

                else if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"text" ) != 0
                    && xmlStrcmp( lev1->name,
                        (const xmlChar*)"comment" ) != 0 )
                {
                    syslog( LOG_ERR, "[%i] %s %s at Level 1 <%s>=[%s]",
                        g_pid, "STC Error:", "Unknown Tag in STC Config",
                        tag.c_str(), value.c_str() );
                    give_syslog_a_chance;
                }
            }
        }
        catch( std::exception &e )
        {
            syslog( LOG_ERR,
                "[%i] %s Exception Parsing STC Config File: %s - %s",
                g_pid, "STC Error:", a_config_file.c_str(), e.what() );
            give_syslog_a_chance;
        }
        catch( ... )
        {
            syslog( LOG_ERR,
                "[%i] %s Unknown Exception Parsing STC Config File: %s",
                g_pid, "STC Error:", a_config_file.c_str() );
            give_syslog_a_chance;
        }

        if ( !parsed )
        {
            syslog( LOG_ERR, "[%i] %s Parsing STC Config File: %s - %s",
                g_pid, "STC Error:", a_config_file.c_str(),
                "No Valid XML Tags Parsed!" );
            give_syslog_a_chance;
        }

        syslog( LOG_INFO,
            "[%i] Parsed STC Config File: %s - %lu %s, %lu %s",
            g_pid, a_config_file.c_str(),
            m_config_groups.size(), "Valid Groups Found",
            m_config_commands.size(), "Valid Commands Found" );
        give_syslog_a_chance;

        xmlFreeDoc( doc );
    }

    else
    {
        syslog( LOG_ERR, "[%i] %s Reading STC Config File: %s - %s",
            g_pid, "STC Error:", a_config_file.c_str(),
            "Empty or Invalid XML Document?" );
        give_syslog_a_chance;
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
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
            THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_dataset_string() failed for path: " << a_path << ", value: "
                         << a_value )
        }
    }
}


/*! \brief Checks existence of a Nexus dataset location
 *
 * This method tries to open a Dataset path, to see if it exists.
 * Note: It First tries to open the enclosing Group path,
 * and then if the Dataset path is Non-Empty, it next checks that.
 */
void
NxGen::checkDataset
(
    const string &a_path,       ///< [in] Path in Nexus file to write string
    const string &a_dataset,    ///< [in] Name of dataset at specified path to receive string value (can be empty string)
    bool &a_exists              ///< [out] Does the dataset exist?
)
{
    if ( m_h5nx.H5NXcheck_dataset_path( a_path, a_dataset, a_exists )
            != SUCCEED )
    {
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
            "H5NXcheck_dataset_path() failed for path: " << a_path )
    }
}


/*! \brief Writes a string attribute to the specified Nexus path
 *
 * This method writes a string attribute value to the specified path
 * in the output Nexus file.
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_string() failed for path: " << a_path << ", attrib: "
                     << a_attrib << ", value: " << a_value )
    }
}


/*! \brief Checks the value of a string attribute at specified Nexus path
 *
 * This method writes a string attribute value to the specified path
 * in the output Nexus file IFF it does not already exist.
 *
 * Returns true/false whether Attribute was updated or not, resp.
 */
bool
NxGen::checkStringAttribute
(
    const string &a_path,       ///< [in] Path in Nexus file to write attribute
    const string &a_attrib,     ///< [in] Name of the attribute
    const string &a_value,      ///< [in] Value of the attribute
    string &a_attr_value        ///< [out] Any Existing Attribute Value...
)
{
    bool wasSet = false;
    if ( m_h5nx.H5NXcheck_attribute_string( a_path, a_attrib, a_value,
            a_attr_value, wasSet ) != SUCCEED )
    {
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXcheck_attribute_string() failed for path: " << a_path << ", attrib: "
                     << a_attrib << ", value: " << a_value )
    }
    return( !wasSet );
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_dataset_scalar() failed for path: " << a_path << ", name: "
                     << a_name << ", value: " << a_value )
    }

    if ( a_units.size() && a_units.compare("(unset)") )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name, "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_string() failed for path: " << a_path
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
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_scalar() failed for path: " << a_path << ", attrib: "
                     << a_attrib << ", value: " << a_value )
    }
}

// vim: expandtab

