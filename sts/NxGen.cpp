#include <stdexcept>
#include <string.h>
#include <libxml/tree.h>
#include "NxGen.h"
#include "TraceException.h"

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
    m_chunk_size(a_chunk_size),
    m_h5nx(a_compression_level),
    m_pulse_info_slab_size(0),
    m_pulse_vetoes_slab_size(0),
    m_comment_last_offset(0)
{
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
 * This method constructs Nexus-specific PVInfoBase objects for use by the generalizes process variable hanlders in the
 * StreamParser class. Due to ADARA protocol limitations, only uint32 and double types are supported (others are
 * mapped to these).
 */
STS::PVInfoBase*
NxGen::makePVInfo
(
    const string       &a_name,         ///< [in] Name of PV
    STS::Identifier     a_device_id,    ///< [in] ID of device that owns the PV
    STS::Identifier     a_pv_id,        ///< [in] ID of the PV
    STS::PVType         a_type,         ///< [in] Type of PV
    const std::string  &a_units         ///< [in] Units of PV (empty if not needed)
)
{
    switch ( a_type )
    {
    case STS::PVT_INT:  // TODO ADARA only supports uint32_t currently
    case STS::PVT_ENUM:
    case STS::PVT_UINT:
        return new NxPVInfo<uint32_t>( a_name, a_device_id, a_pv_id, a_type, a_units, *this );
    case STS::PVT_FLOAT: // TODO ADARA only supports double currently
    case STS::PVT_DOUBLE:
        return new NxPVInfo<double>( a_name, a_device_id, a_pv_id, a_type, a_units, *this );
    }

    THROW_TRACE( STS::ERR_UNEXPECTED_INPUT, "makePVInfo() failed - invalid PV type: " << a_type );
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
    uint16_t a_pixel_count,     ///< [in] Pixel count of bank
    uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
)
{
    try
    {
        NxBankInfo* bi = new NxBankInfo( a_id, a_pixel_count, a_buf_reserve, a_idx_buf_reserve );

        if ( m_gen_nexus)
        {
            // Instrument bank group
            string instr_path = string("/entry/instrument/") + bi->m_name;
            makeGroup( instr_path, "NXdetector" );
            makeDataset( instr_path, "event_time_offset", NeXus::FLOAT32, TIME_USEC_UNITS );
            makeDataset( instr_path, "event_id", NeXus::UINT32 );
            makeDataset( instr_path, "event_index", NeXus::UINT64 );

            // Event data group
            string event_path = string("/entry/") + bi->m_eventname;
            makeGroup( event_path, "NXevent_data" );
            makeLink( instr_path + "/event_time_offset", event_path + "/event_time_offset" );
            makeLink( instr_path + "/event_id", event_path + "/event_id" );
            makeLink( instr_path + "/event_index", event_path + "/event_index" );

            // Link pulse time to bank event times
            makeLink( "/entry/DASlogs/frequency/time", instr_path + "/event_time_zero" );
            makeLink( "/entry/DASlogs/frequency/time", event_path + "/event_time_zero" );
        }

        return bi;
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "makeBankInfo (bank: " << a_id << ") failed." )
    }
}


/*! \brief Factory method for MonitorInfo instances
 *  \return A new MonitorInfo derived instance
 *
 * This method constructs Nexus-specific MonitorInfo objects. The Nexus-specific NxMonitorInfo class extends the
 * BankInfo class to include a number of attributes needed for writing monito event data efficiently to a Nexus file.
 */
STS::MonitorInfo*
NxGen::makeMonitorInfo
(
    uint16_t a_id,              ///< [in] ID of detector bank
    uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
    uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
)
{
    try
    {
        NxMonitorInfo* mi = new NxMonitorInfo( a_id, a_buf_reserve, a_idx_buf_reserve );

        if ( m_gen_nexus)
        {
            // create instrument/bank# group
            string path = "/entry/" + mi->m_name;

            makeGroup( path, "NXmonitor" );
            makeDataset( path, "event_time_offset", NeXus::FLOAT32, TIME_USEC_UNITS );
            makeDataset( path, "event_index", NeXus::UINT64 );

            makeLink( "/entry/DASlogs/frequency/time", path + "/event_time_zero" );
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
 * This method performs Nexus-specific initialization (creates file and several HDF5 entries).
 */
void
NxGen::initialize()
{
    if (!m_gen_nexus)
        return;

    try
    {
        m_h5nx.H5NXcreate_file( m_nexus_filename );

        // Create general Nexus entries
        makeGroup( "/entry", "NXentry" );
        makeGroup( "/entry/instrument", "NXinstrument" );
        makeGroup( "/entry/DASlogs", "NXcollection" );

        // Create pulse frequency log
        makeGroup( "/entry/DASlogs/frequency", "NXlog" );
        makeDataset( "/entry/DASlogs/frequency", "time", NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( "/entry/DASlogs/frequency", "value", NeXus::FLOAT64, FREQ_UNITS );

        // Create proton charge log (time same as pulse frequency)
        makeGroup( "/entry/DASlogs/proton_charge", "NXlog" );
        makeDataset( "/entry/DASlogs/proton_charge", "value", NeXus::FLOAT64, CHARGE_UNITS );
        makeLink( "/entry/DASlogs/frequency/time", "/entry/DASlogs/proton_charge/time" );

        // Create pulse veto log
        makeGroup( "/entry/DASlogs/Veto_pulse", "NXcollection" );
        makeDataset( "/entry/DASlogs/Veto_pulse", "veto_pulse_time", NeXus::FLOAT64, TIME_SEC_UNITS );

        // Create pause event log
        makeGroup( "/entry/DASlogs/pause", "NXlog" );
        makeDataset( "/entry/DASlogs/pause/", "time", NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( "/entry/DASlogs/pause/", "value", NeXus::UINT16 );

        // Create scan event log
        makeGroup( "/entry/DASlogs/scan_index", "NXlog" );
        makeDataset( "/entry/DASlogs/scan_index/", "time", NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( "/entry/DASlogs/scan_index/", "value", NeXus::UINT32 );

        // Creare comment event log
        makeGroup( "/entry/DASlogs/comments", "NXcollection" );
        makeDataset( "/entry/DASlogs/comments/", "time", NeXus::FLOAT64, TIME_SEC_UNITS );
        makeDataset( "/entry/DASlogs/comments/", "offset", NeXus::UINT32 );
        makeDataset( "/entry/DASlogs/comments/", "length", NeXus::UINT32 );
        makeDataset( "/entry/DASlogs/comments/", "data", NeXus::CHAR );

        // Insert initial "not in scan" value
        m_scan_time.push_back( 0.0 );
        m_scan_value.push_back( 0 );

        // Insert initial "not paused" value
        m_pause_time.push_back( 0.0 );
        m_pause_value.push_back( 0 );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "initialization of nexus file (" << m_nexus_filename << ") failed." )
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
        writeString( "/entry/", "definition", "NXsnsevent" );

        writeScalar( "/entry/DASlogs/frequency", "minimum_value", a_run_metrics.freq_stats.min(), FREQ_UNITS );
        writeScalar( "/entry/DASlogs/frequency", "maximum_value", a_run_metrics.freq_stats.max(), FREQ_UNITS );
        writeScalar( "/entry/DASlogs/frequency", "average_value", a_run_metrics.freq_stats.mean(), FREQ_UNITS );
        writeScalar( "/entry/DASlogs/frequency", "average_value_error", a_run_metrics.freq_stats.stdDev(), FREQ_UNITS );

        writeScalar( "/entry/DASlogs/proton_charge", "minimum_value", a_run_metrics.charge_stats.min(), CHARGE_UNITS );
        writeScalar( "/entry/DASlogs/proton_charge", "maximum_value", a_run_metrics.charge_stats.max(), CHARGE_UNITS );
        writeScalar( "/entry/DASlogs/proton_charge", "average_value", a_run_metrics.charge_stats.mean(), CHARGE_UNITS );
        writeScalar( "/entry/DASlogs/proton_charge", "average_value_error", a_run_metrics.charge_stats.stdDev(), CHARGE_UNITS );

        // Flush any remaining pulse vetoes
        if ( m_pulse_vetoes.size() )
        {
            writeSlab( "/entry/DASlogs/Veto_pulse/veto_pulse_time", m_pulse_vetoes, m_pulse_vetoes_slab_size );
            m_pulse_vetoes_slab_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
        }

        // Flush stream marker data
        flushPauseData();
        flushScanData();
        flushCommentData();

        float duration = calcDiffSeconds( a_run_metrics.end_time, a_run_metrics.start_time );

        writeScalar( "/entry/", "duration", duration, TIME_SEC_UNITS );
        writeScalar( "/entry/", "total_pulses", a_run_metrics.charge_stats.count(), "" );
        // Link raw_frames to total_pulses for backward compatibility
        makeLink( "/entry/total_pulses", "/entry/raw_frames" );
        writeScalar( "/entry/", "total_counts", a_run_metrics.events_counted, "" );
        writeScalar( "/entry/", "total_uncounted_counts", a_run_metrics.events_uncounted, "" );
        writeScalar( "/entry/", "total_other_counts", a_run_metrics.non_events_counted, "" );
        writeScalar( "/entry/", "proton_charge", a_run_metrics.total_charge, CHARGE_UNITS );

        // Start time
        string time = timeToISO8601( a_run_metrics.start_time );
        writeString( "/entry/", "start_time", time );

        // Add start time (offset) properties to all time axis in DAS logs
        writeStringAttribute( "/entry/DASlogs/frequency/time", "offset", time );
        writeScalarAttribute( "/entry/DASlogs/frequency/time", "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( "/entry/DASlogs/frequency/time", "offset_nanoseconds", (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( "/entry/DASlogs/pause/time", "start", time );
        writeScalarAttribute( "/entry/DASlogs/pause/time", "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( "/entry/DASlogs/pause/time", "offset_nanoseconds", (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( "/entry/DASlogs/scan_index/time", "start", time );
        writeScalarAttribute( "/entry/DASlogs/scan_index/time", "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( "/entry/DASlogs/scan_index/time", "offset_nanoseconds", (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( "/entry/DASlogs/comments/time", "start", time );
        writeScalarAttribute( "/entry/DASlogs/comments/time", "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( "/entry/DASlogs/comments/time", "offset_nanoseconds", (uint32_t)a_run_metrics.start_time.tv_nsec );

        writeStringAttribute( "/entry/DASlogs/Veto_pulse/veto_pulse_time", "start", time );
        writeScalarAttribute( "/entry/DASlogs/Veto_pulse/veto_pulse_time", "offset_seconds", (uint32_t)a_run_metrics.start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
        writeScalarAttribute( "/entry/DASlogs/Veto_pulse/veto_pulse_time", "offset_nanoseconds", (uint32_t)a_run_metrics.start_time.tv_nsec );

        // End time
        time = timeToISO8601( a_run_metrics.end_time );
        writeString( "/entry/", "end_time", time );

        m_h5nx.H5NXclose_file();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "finalization of nexus file failed." )
    }
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
        writeString( "/entry/instrument", "beamline", a_run_info.instr_id );

        if ( a_run_info.instr_longname.size())
        {
            writeString( "/entry/instrument", "name", a_run_info.instr_longname );

            if ( a_run_info.instr_shortname.size())
                writeStringAttribute( "/entry/instrument/name", "short_name", a_run_info.instr_shortname );
        }

        string group_path = "/entry";

        string tmp = boost::lexical_cast<string>(a_run_info.run_number);
        writeString( group_path, "run_number", tmp );
        writeString( group_path, "/entry_identifier", tmp );

        writeString( group_path, "experiment_identifier", a_run_info.proposal_id );
        writeString( group_path, "title", a_run_info.run_title );

        makeGroup( "/entry/sample", "NXsample" );
        writeString( "/entry/sample", "identifier", a_run_info.sample_id );
        writeString( "/entry/sample", "name", a_run_info.sample_name );
        writeString( "/entry/sample", "nature", a_run_info.sample_nature );
        writeString( "/entry/sample", "chemical_formula", a_run_info.sample_formula );
        writeString( "/entry/sample", "environment", a_run_info.sample_environment );

        size_t user_count = 0;
        string path;
        for ( vector<STS::UserInfo>::const_iterator u = a_run_info.users.begin(); u != a_run_info.users.end(); ++u )
        {
            path = group_path + "/user" + boost::lexical_cast<string>(++user_count);
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
        makeGroup( "/entry/instrument/instrument_xml", "NXnote" );
        writeString( "/entry/instrument/instrument_xml", "description", "XML contents of the instrument IDF" );
        writeString( "/entry/instrument/instrument_xml", "type", "text/xml" );
        writeString( "/entry/instrument/instrument_xml", "data", a_xml );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "processGeometry() failed." )
    }
}


/*! \brief Writes pulse buffers to Nexus file
 *
 * This method writes time, frequency, and charge data in pulse buffers to the Nexus file.
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
        writeSlab( "/entry/DASlogs/frequency/time", a_pulse_info.times, m_pulse_info_slab_size );
        writeSlab( "/entry/DASlogs/frequency/value", a_pulse_info.freqs, m_pulse_info_slab_size );
        writeSlab( "/entry/DASlogs/proton_charge/value", a_pulse_info.charges, m_pulse_info_slab_size );

        m_pulse_info_slab_size += a_pulse_info.times.size();

        // Must process pulse flags linearly
        vector<double>::iterator t = a_pulse_info.times.begin();
        for ( vector<uint32_t>::iterator f = a_pulse_info.flags.begin(); f != a_pulse_info.flags.end(); ++f, ++t )
        {
            // Write pulse vetoes to DASlog
            if ( *f & ADARA::BankedEventPkt::PULSE_VETO )
                m_pulse_vetoes.push_back( *t );
        }

        if ( m_pulse_vetoes.size() > m_chunk_size )
        {
            writeSlab( "/entry/DASlogs/Veto_pulse/veto_pulse_time", m_pulse_vetoes, m_pulse_vetoes_slab_size );
            m_pulse_vetoes_slab_size +=  m_pulse_vetoes.size();
            m_pulse_vetoes.clear();
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid bank object passed to bankBuffers()" )

        writeSlab( bi->m_tof_slab_path, a_bank.m_tof_buffer, bi->m_event_slab_size );
        writeSlab( bi->m_pid_slab_path, a_bank.m_pid_buffer, bi->m_event_slab_size );

        bi->m_event_slab_size += a_bank.m_tof_buffer.size();

        writeSlab( bi->m_index_slab_path, a_bank.m_index_buffer, bi->m_index_slab_size );

        bi->m_index_slab_size += a_bank.m_index_buffer.size();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankBuffersReady() failed for bank id: " << a_bank.m_id )
    }
}


/*! \brief Fills pulse gaps in bank index slab
 *
 * This method fills pulse gaps in the index slab for a given bank in the Nexus file.
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid bank object passed to bankPulseGap()" )

        fillSlab( bi->m_index_slab_path, bi->m_event_count, a_count, bi->m_index_slab_size );
        bi->m_index_slab_size += a_count;
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankPulseGap() failed for bank id: " << a_bank.m_id << ", gap count: " << a_count )
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid bank object passed to bankFinalize()" )

        string total_path = "/entry/instrument/" + bi->m_name;
        writeScalar( total_path, "total_counts", bi->m_event_count, "" );
        makeLink( total_path + "/total_counts", "/entry/" + bi->m_eventname + "/total_counts" );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "bankFinalize() failed for bank id: " << a_bank.m_id )
    }
}


/*! \brief Writes monitor event buffers to Nexus file
 *
 * This method writes time of flight and index data in monitor buffers to the Nexus file.
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid monitor object passed to monitorBuffersReady()" )

        writeSlab( mi->m_tof_slab_path, a_monitor.m_tof_buffer, mi->m_event_slab_size );
        mi->m_event_slab_size += a_monitor.m_tof_buffer.size();

        writeSlab( mi->m_index_slab_path, a_monitor.m_index_buffer, mi->m_index_slab_size );
        mi->m_index_slab_size += a_monitor.m_index_buffer.size();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorBuffersReady() failed for monitor id: " << a_monitor.m_id )
    }
}


/*! \brief Fills pulse gaps in monitor index slab
 *
 * This method fills pulse gaps in the index slab for a given monitor in the Nexus file.
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid monitor object passed to monitorPulseGap()" )

        fillSlab( mi->m_index_slab_path, mi->m_event_count, a_count, mi->m_index_slab_size );
        mi->m_index_slab_size += a_count;
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorPulseGap() failed for monitor id: " << a_monitor.m_id << ", gap count: " << a_count )
    }
}


/*! \brief Finalizes monitor data in Nexus file
 *
 * This method writes event counts for the specified monitor to the Nexus file.
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
            THROW_TRACE( STS::ERR_CAST_FAILED, "Invalid monitor object passed to monitorFinalize()" )

        writeScalar( string("/entry/") + mi->m_name, "total_counts", mi->m_event_count, "" );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "monitorFinalize() failed for monitor id: " << a_monitor.m_id )
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
    try
    {
        writeString( "/entry/", "notes", a_comment );
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "runComment() failed." )
    }
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
    unsigned long a_scan_index,         ///< [in] Scan index associated with scan
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
        if ( a_comment.size())
        {
            m_comment_time.push_back( a_time );

            m_comment_offset.push_back( m_comment_last_offset );
            m_comment_last_offset += a_comment.size();
            m_comment_length.push_back( a_comment.size() );

            m_comment_data.reserve( m_comment_data.size() + a_comment.size() );
            m_comment_data.insert( m_comment_data.end(), a_comment.begin(), a_comment.end()) ;
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
        writeSlab( "/entry/DASlogs/pause/time", m_pause_time, 0 );
        writeSlab( "/entry/DASlogs/pause/value", m_pause_value, 0 );

        m_pause_time.clear();
        m_pause_value.clear();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "flushPauseData() failed." )
    }
}


/*! \brief Writes buffered pause data into Nexus file
 *
 * This method flushes buffered pause data to the logs of the Nexus file.
 */
void
NxGen::flushScanData()
{
    try
    {
        writeSlab( "/entry/DASlogs/scan_index/time", m_scan_time, 0 );
        writeSlab( "/entry/DASlogs/scan_index/value", m_scan_value, 0 );

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
        writeSlab( "/entry/DASlogs/comments/time", m_comment_time, 0 );
        writeSlab( "/entry/DASlogs/comments/offset", m_comment_offset, 0 );
        writeSlab( "/entry/DASlogs/comments/length", m_comment_length, 0 );

        if ( m_comment_data.size())
            writeSlab( "/entry/DASlogs/comments/data", m_comment_data, 0 );

        m_comment_time.clear();
        m_comment_offset.clear();
        m_comment_length.clear();
        m_comment_data.clear();
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "flushCommentData() failed." )
    }
}


/*! \brief Converts a PVType to a Nexus NXnumtype
 *  \return The most appropriate Nxnumtype for the provided PVType
 *
 * This method converts the provided PVType to a Nexus NXnumtype. Throws an exception for unkown / unsupported inputs.
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


/*! \brief Creates a Nexus dataset
 *
 * This method creates a Nexus dataset with the specified type and (optional) units in the output Nexus file.
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
    if ( m_h5nx.H5NXcreate_dataset_extend( a_path, a_name, a_type, m_chunk_size ) != SUCCEED )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXcreate_dataset_extend() failed for path: " << a_path << ", name: "
                     << a_name )
    }

    if ( a_units.size() )
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name, "units", a_units ) != SUCCEED )
        {
            THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_attribute_string() failed for path: " << a_path
                         << ", name: " << a_name )
        }
    }
}


/*! \brief Creates a Nexus link
 *
 * This method creates a link from the source path to the destination path in the output Nexus file.
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
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXmake_link() failed for source: " << a_source_path << ", dest: "
                     << a_dest_name )
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



