#include "StreamParser.h"
#include "TransCompletePkt.h"
#include <iomanip>
#include <sstream>
#include <string>
#include <string.h>
#include <boost/algorithm/string.hpp>
#include <unistd.h>
#include <syslog.h>
#include <time.h>
#include "NxGen.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"


using namespace std;

namespace STC {

RateLimitedLogging::History RLLHistory_StreamParserH;

// Rate-Limited Logging IDs...
#define RLL_PV_VALUE_UPDATE_WEIRD            0
#define RLL_PV_VALUE_UPDATE_SAWTOOTH         1
#define RLL_PV_VALUE_UPDATE_NEG_TIME_CLAMP   2
#define RLL_PV_VALUE_UPDATE_EARLY            3
#define RLL_ANNOTATION_SAWTOOTH              4
#define RLL_ANNOTATION_NEG_TIME_CLAMP        5
#define RLL_ANNOTATION_EARLY                 6

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x3000000  // For Old "Direct" PixelMap Pkt!


//---------------------------------------------------------------------------------------------------------------------
// Public StreamParser methods
//---------------------------------------------------------------------------------------------------------------------


/*! \brief Constructor for StremParser class.
 *
 * This constructor builds a StreamParser instance using the options specified. If the adara filename is empty, then no
 * adara output stream is produced. The buffer thresholds should be set to sane values based on the specific stream
 * adapter in use (i.e. multiples of a given chunk size or an hdf5 implementation).
 */
StreamParser::StreamParser
(
    int             a_fd_in,                    ///< [in] File descriptor of input ADARA byte stream
    const string   &a_work_root,                ///< [in] Work Directory Root
    const string   &a_work_base,                ///< [in] Work Directory Base
    const string   &a_adara_out_file,           ///< [in] Filename of output ADARA stream file (disabled if empty)
    const string   &a_config_file,              ///< [in] Path to STC Config file
    bool            a_strict,                   ///< [in] Controls strict processing of input stream
    bool            a_gather_stats,             ///< [in] Controls stream statistics gathering
    uint32_t        a_event_buf_write_thresh,   ///< [in] Event buffer write threshold (number of elements)
    uint32_t        a_anc_buf_write_thresh,     ///< [in] Ancillary buffer write threshold (number of elements)
    uint32_t        a_verbose_level             ///< [in] STC Verbosity Level
)
:
    POSIXParser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
    m_total_bytes_count(0),
    m_fd(a_fd_in),
    m_processing_state(PROCESSING_NOT_STARTED),
    m_pkt_recvd(0),
    m_pulse_id(0),
    m_pulse_count(0),
    m_banks_arr(NULL),
    m_banks_arr_size(0),
    m_maxBank(0),   // Maximum Detector Bank ID
    m_numStates(1), // Total Number of States (Including State 0)
    m_event_buf_write_thresh(a_event_buf_write_thresh),
    m_anc_buf_write_thresh(a_anc_buf_write_thresh),
    m_info_rcvd(0),
    m_work_root(a_work_root),
    m_work_base(a_work_base),
    m_work_dir(""),
    m_do_rename(true),
    m_adara_out_file(a_adara_out_file),
    m_config_file(a_config_file),
    m_strict(a_strict),
    m_gen_adara(false),
    m_gather_stats(a_gather_stats),
    m_skipped_pkt_count(0),
    m_pulse_flag(0),
    m_verbose_level(a_verbose_level),
    m_pause_has_non_normalized(false),
    m_scan_has_non_normalized(false),
    m_comment_has_non_normalized(false)
{
    // Capture Default Run "Start Time"...
    // (In case there are No Neutron Pulses, for Faking It...! ;-D)
    clock_gettime( CLOCK_REALTIME, &m_default_run_start_time );

    if ( !m_adara_out_file.empty() )
    {
        m_gen_adara = true;

        // If We Already Have the Working Directory,
        // Proceed with Opening the ADARA Output Stream File...
        if ( isWorkingDirectoryReady() )
        {
            string adara_file_path = m_work_dir + m_adara_out_file;
            syslog( LOG_INFO, "[%i] Creating ADARA Stream File: %s",
                g_pid, adara_file_path.c_str() );
            give_syslog_a_chance;
            m_ofs_adara.open( adara_file_path.c_str(),
                ios_base::out | ios_base::binary );
        }
    }

    m_pulse_info.times.reserve(m_anc_buf_write_thresh);
    m_pulse_info.freqs.reserve(m_anc_buf_write_thresh);

    // Insert initial "not in scan" value:
    //     - Current Nexus scan log calls for 0
    //     to be used for all scan stops
    //     - Just use 0 for the Starting Non-Normalized Nanosecond
    //     Time Stamp (guaranteed to be 1st Chronologically...! ;-D)
    m_last_scan_multimap_it = m_scan_multimap.insert(
        std::pair< uint64_t, std::pair<double, uint32_t> >(
            0, std::pair<double, uint32_t>(0.0, 0) ) );
    m_run_metrics.scan_stats.push( 0 );
    m_last_scan_comment = "";

    // Insert initial "not paused" value
    m_last_pause_multimap_it = m_pause_multimap.insert(
        std::pair< uint64_t, std::pair<double, uint16_t> >(
            0, std::pair<double, uint32_t>(0.0, 0) ) );
    m_last_pause_comment = "";

    // Initialize last comment iterator
    m_last_comment_multimap_it = m_comment_multimap.end();
}


/*! \brief Destructor for StreamParser class.
 *
 */
StreamParser::~StreamParser()
{
    if ( m_ofs_adara.is_open() )
        m_ofs_adara.close();

    for ( uint32_t i=0 ; i < m_banks_arr_size ; i++ )
    {
        BankInfo *bi = m_banks_arr[i];

        if ( bi != NULL )
            delete m_banks_arr[i];
    }

    for ( vector<STC::DetectorBankSet *>::iterator dbs =
            m_bank_sets.begin(); dbs != m_bank_sets.end() ; ++dbs ) {
        if ( *dbs )
            delete *dbs;
    }

    for ( map<Identifier, MonitorInfo*>::iterator imi = m_monitors.begin();
            imi != m_monitors.end(); ++imi ) {
        if ( imi->second )
            delete imi->second;
    }

    m_monitor_config.clear();

    for ( vector<PVInfoBase*>::iterator ipv = m_pvs_list.begin();
            ipv != m_pvs_list.end(); ++ipv ) {
        if ( *ipv ) {
            if ( m_verbose_level > 2 ) {
                syslog( LOG_ERR,
                    "[%i] %s: Erasing Device %s: %s (%s)",
                    g_pid, "~StreamParser()",
                    (*ipv)->m_device_name.c_str(),
                    (*ipv)->m_name.c_str(),
                    (*ipv)->m_connection.c_str() );
                give_syslog_a_chance;
            }
            delete *ipv;
        }
    }
}


bool
StreamParser::isWorkingDirectoryReady()
{
    return ( m_work_dir.size() > 0
        || ( m_work_root.size() == 0 && m_work_base.size() == 0 ) );
}


bool
StreamParser::constructWorkingDirectory( bool a_force_init, string caller )
{
    // Do We Need to Construct a Working Directory Path...?
    if ( m_work_dir.size() == 0
            && ( m_work_root.size() > 0 || m_work_base.size() > 0 ) )
    {
        // Do We Have the Necessary Pieces
        // to Construct the Working Directory?
        if ( getFacilityName().size() > 0
                && getBeamShortName().size() > 0 )
        {
            // Note: Work Root Must End in "/",
            // as it might be _Just_ "/"...! ;-D
            m_work_dir = ( m_work_root.size() > 0 ) ? m_work_root : "/";

            m_work_dir += getFacilityName() + "/"
                + getBeamShortName() + "/";

            if ( m_work_base.size() > 0 )
                m_work_dir += m_work_base + "/";

            syslog( LOG_INFO,
                "[%i] %s: Working Directory Constructed %s: [%s]",
                g_pid, "StreamParser::constructWorkingDirectory()",
                "from Run/Beamline Info", m_work_dir.c_str() );
            give_syslog_a_chance;

            return( true );
        }
        else if ( a_force_init )
        {
            syslog( LOG_INFO, "[%i] %s: %s by Caller %s",
                g_pid, "StreamParser::constructWorkingDirectory()",
                "Forcing Initialization of Nexus File", caller.c_str() );
            give_syslog_a_chance;

            // In a pinch, Steal STC Config File Directory as Scratch...!
            size_t last_slash = m_config_file.find_last_of("/");
            if ( last_slash != string::npos )
                m_work_dir = m_config_file.substr( 0, last_slash );
            else
                m_work_dir = ".";

            m_work_dir += "/";

            m_do_rename = false;

            syslog( LOG_ERR,
                "[%i] %s %s: %s %s (%s): [%s] (Set doRename=%s)",
                g_pid, "STC Error:",
                "StreamParser::constructWorkingDirectory()", "FORCE INIT",
                "Working Directory Constructed from STC Config File Path",
                m_config_file.c_str(), m_work_dir.c_str(),
                ( m_do_rename ? "true" : "false" ) );
            give_syslog_a_chance;

            return( true );
        }
        else
        {
            syslog( LOG_WARNING,
                "[%i] %s: %s: %s=[%s] %s=[%s]",
                g_pid, "StreamParser::constructWorkingDirectory()",
                "Still Missing Info for Working Directory Construction",
                "FacilityName", getFacilityName().c_str(),
                "BeamShortName", getBeamShortName().c_str() );
            give_syslog_a_chance;

            return( false );
        }
    }

    // We Didn't Newly Construct Any Working Directory Here... ;-D
    return( false );
}


void
StreamParser::flushAdaraStreamBuffer()
{
    // Open the ADARA Stream File if Working Directory Now Constructed...
    if ( isWorkingDirectoryReady() && ! m_ofs_adara.is_open() )
    {
        string adara_file_path = m_work_dir + m_adara_out_file;
        syslog( LOG_INFO, "[%i] %s, Creating ADARA Stream File: %s",
            g_pid, "Working Directory Created", adara_file_path.c_str() );
        give_syslog_a_chance;
        m_ofs_adara.open( adara_file_path.c_str(),
            ios_base::out | ios_base::binary );
    }

    // Dump Any Queued Packets to the ADARA Stream File...
    if ( m_ofs_adara.is_open() && m_adara_queue.size() > 0 )
    {
        syslog( LOG_INFO, "[%i] Dumping %d %s to ADARA Stream File: %s%s",
            g_pid, (int) m_adara_queue.size(), "Queued Packets",
            m_work_dir.c_str(), m_adara_out_file.c_str() );
        give_syslog_a_chance;

        for ( std::vector<ADARA::Packet *>::iterator pkt =
                    m_adara_queue.begin();
                pkt != m_adara_queue.end() ; ++pkt )
        {
            m_ofs_adara.write(
                (char *)(*pkt)->packet(), (*pkt)->packet_length() );

            delete (*pkt);
        }

        m_adara_queue.clear();
    }
}


/*! \brief This method initiates ADARA stream processing.
 *
 * This method initiates ADARA stream processing on the calling thread and does not return until the stream is fully
 * translated, or an error occurs (an exception may be thrown). This method can only be called once for a given
 * StreamParser instance.
 */
void
StreamParser::processStream()
{
    // If anything goes wrong with translation, an exception will be thrown to caller of this method

    std::string log_info;

    if ( m_processing_state != PROCESSING_NOT_STARTED )
    {
        THROW_TRACE( ERR_INVALID_OPERATION,
            "StreamParser::processStream() cannot be called more than once"
                << " (Processing State = " << getProcessingStateString()
                << ")." )
    }

    try
    {
        initialize();
        m_processing_state = WAITING_FOR_RUN_START;

        while ( m_processing_state < DONE_PROCESSING )
        {
            // NOTE: This is POSIXParser::read()... ;-o
            if ( !read( m_fd, log_info ) )
            {
                if ( m_processing_state != DONE_PROCESSING )
                {
                    stringstream ss;
                    ss << m_run_info.facility_name
                        << " " << m_beamline_info.instr_longname
                        << " (" << m_beamline_info.instr_shortname << ")"
                        << " Run " << m_run_info.run_number
                        << " " << m_run_info.proposal_id;

                    syslog( LOG_ERR,
                    "[%i] STC failed %s: %s, %s (%s = %s)! %s [%s]",
                        g_pid, "processStream()", "Connection Failed",
                        "Not Done Processing", "Processing State",
                        getProcessingStateString().c_str(),
                        ss.str().c_str(),
                        log_info.c_str() );

                    if ( m_processing_state == PROCESSING_EVENTS )
                    {
                        syslog( LOG_ERR,
                            "[%i] %s %s: %s, %s (%s = %s)! %s",
                            g_pid, "STC Error:", "processStream()",
                            "Connection Failed", "Still Processing Events",
                            "Processing State",
                            getProcessingStateString().c_str(),
                            ss.str().c_str() );

                        // On fatal error, flush buffers to Nexus
                        // before terminating
                        markerComment(
                            ( m_pulse_info.max_time
                                    - m_pulse_info.start_time )
                                / NANO_PER_SECOND_D,
                            m_pulse_info.start_time
                                + m_pulse_info.max_time,
                            "Stream processing terminated abnormally." );
                        m_run_metrics.run_end_time = nsec_to_timespec(
                            m_pulse_info.start_time
                                + m_pulse_info.max_time );
                        finalizeStreamProcessing();
                    }

                    THROW_TRACE( ERR_GENERAL_ERROR,
                        "ADARA parser stopped unexpectedly." );
                }
            }
        }
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "processStream() failed"
            << " (Processing State = " << getProcessingStateString()
            << ")." )
    }
    catch ( exception &e )
    {
        THROW_TRACE( ERR_GENERAL_ERROR,
            "processStream() exception {" << e.what() << "} "
                << " (Processing State = " << getProcessingStateString()
                << ")." )
    }
    catch ( ... )
    {
        THROW_TRACE( ERR_GENERAL_ERROR,
            "processStream() unexpected exception"
                << " (Processing State = " << getProcessingStateString()
                << ")." )
    }
}


/*! \brief This method prints stream statistics.
 *
 * This method prints human-readbale stream statistics to the specified output stream if statistics gathering was
 * enabled. The output statistics may not be accurate if the processStream method exits abnormally.
 */
void
StreamParser::printStats
(
    ostream &a_os   ///< [in] An output stream to write statistics to.
) const
{
    if ( m_gather_stats )
    {
        a_os << endl
             << "Pkt Type\t\t\tCount     \tTotal KB  \t"
             << "Min Size  \tMax Size  " << endl;
        for ( map<uint32_t,PktStats>::const_iterator i = m_stats.begin();
                i != m_stats.end(); ++i )
        {
            a_os << hex << setw(8) << left << i->first << "\t" << setw(12)
                 << getPktName( i->first ) << "\t"
                 << dec << setw(10) << i->second.count << "\t" << setw(10)
                 << (i->second.total_size >> 10) << "\t" << setw(10)
                 << i->second.min_pkt_size << "\t" << setw(10)
                 << i->second.max_pkt_size << endl;
        }
        a_os << endl << "Packets skipped: " << m_skipped_pkt_count << endl;
        a_os << "Pulse charge stats: "
             << m_run_metrics.charge_stats << endl;
        a_os << "Pulse freq stats: " << m_run_metrics.freq_stats << endl;
        a_os << "Scan stats: " << m_run_metrics.scan_stats << endl;
    }
    else
    {
        a_os << "Statistics were not gathered." << endl;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// General ADARA packet processing methods
//---------------------------------------------------------------------------------------------------------------------

// These bitmasks are used to monitor one-time processing of the associated packet type
#define PKT_BIT_PIXELMAP                0x0001
#define PKT_BIT_RUNINFO                 0x0002
#define PKT_BIT_BEAMINFO                0x0004
#define PKT_BIT_GEOMETRY                0x0008
#define PKT_BIT_BEAM_MONITOR_CONFIG     0x0010
#define PKT_BIT_DETECTOR_BANK_SETS      0x0020

#define PROCESS_IN_STATES(s)            \
    if ( m_processing_state & (s) )     \
    {                                   \
        return Parser::rxPacket(a_pkt); \
    }                                   \
    else                                \
    {                                   \
        if ( m_gather_stats )           \
            ++m_skipped_pkt_count;      \
        return false;                   \
    }

#define PROCESS_IN_STATES_ONCE(s,x)     \
    if ( ( m_processing_state & (s) ) && !( m_pkt_recvd & (x) ) )    \
    {                                   \
        m_pkt_recvd |= (x);             \
        return Parser::rxPacket(a_pkt); \
    }                                   \
    else                                \
    {                                   \
        if ( m_gather_stats )           \
            ++m_skipped_pkt_count;      \
        return false;                   \
    }

/*! \brief This method controls processing of incoming ADARA stream packets.
 *
 * This method is called by the ADARAParser base class to allow a subclass to control which ADARA packet types will be
 * further processed. If a packet type is to be processed, Parser::rxPacket() is called; otherwise no action is taken.
 * This method examines the current processing state and a set of macros to determine which packets are processed. This
 * is also the point at which stream statistics are gathered.
 */
bool
StreamParser::rxPacket
(
    const ADARA::Packet &a_pkt    ///< [in] An ADARA packet
)
{
    if ( m_gather_stats )
        gatherStats( a_pkt );

    if ( m_gen_adara )
    {
        if ( m_ofs_adara.is_open() )
        {
            m_ofs_adara.write(
                (char *)a_pkt.packet(), a_pkt.packet_length() );
        }
        else
        {
            m_adara_queue.push_back( new ADARA::Packet( a_pkt ) );
        }
    }

    // Accumulate Total Input Stream Byte Count (for Testing/Diagnostics)
    m_total_bytes_count += a_pkt.packet_length();

    switch (a_pkt.base_type())
    {
        // These packets shall always be processed
        case ADARA::PacketType::RUN_STATUS_TYPE:
        case ADARA::PacketType::DATA_DONE_TYPE:
            return Parser::rxPacket(a_pkt);

        // These packets shall be processed ONCE during header and
        // event processing
        // Note: these should arrive before event processing,
        // but it is no guaranteed.
        case ADARA::PacketType::PIXEL_MAPPING_TYPE:
        case ADARA::PacketType::PIXEL_MAPPING_ALT_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_PIXELMAP)

        // Allow & Process Multiple RunInfoPkt Per Run Now...! :-O
        case ADARA::PacketType::RUN_INFO_TYPE:
            PROCESS_IN_STATES(PROCESSING_RUN_HEADER|PROCESSING_EVENTS)

        case ADARA::PacketType::GEOMETRY_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_GEOMETRY)

        case ADARA::PacketType::BEAMLINE_INFO_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_BEAMINFO)

        case ADARA::PacketType::BEAM_MONITOR_CONFIG_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_BEAM_MONITOR_CONFIG)

        case ADARA::PacketType::DETECTOR_BANK_SETS_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_DETECTOR_BANK_SETS)

        // These packets shall be processed during header and
        // event processing
        case ADARA::PacketType::DEVICE_DESC_TYPE:
        case ADARA::PacketType::VAR_VALUE_U32_TYPE:
        case ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE:
        case ADARA::PacketType::VAR_VALUE_STRING_TYPE:
        case ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE:
        case ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE:
        case ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE:
        case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE:
        case ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE:
        case ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE:
        case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE:
        case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
            PROCESS_IN_STATES(PROCESSING_RUN_HEADER|PROCESSING_EVENTS)

        // These packets shall only be processed during event processing
        case ADARA::PacketType::BANKED_EVENT_TYPE:
        case ADARA::PacketType::BANKED_EVENT_STATE_TYPE:
        case ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE:
            PROCESS_IN_STATES(PROCESSING_EVENTS)

        // Packet types that are not processes by StreamParser
        case ADARA::PacketType::RAW_EVENT_TYPE:
        case ADARA::PacketType::MAPPED_EVENT_TYPE:
        case ADARA::PacketType::RTDL_TYPE:
        case ADARA::PacketType::SOURCE_LIST_TYPE:
        case ADARA::PacketType::TRANS_COMPLETE_TYPE:
        case ADARA::PacketType::CLIENT_HELLO_TYPE:
        case ADARA::PacketType::SYNC_TYPE:
        case ADARA::PacketType::HEARTBEAT_TYPE:
            if ( m_gather_stats )
                ++m_skipped_pkt_count;
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Run Status packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Run Status ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Run Status packets. The first Run Status indicating "NEW_RUN" causes processing to
 * proceed from the initial state to header processing state. Processing continues until an "END_RUN" status is
 * received, at which point processing is halted within the StreamParser. Note: if strict processing is enabled and run
 * status values do not align with current processing states, an exception will be thrown.
 */
bool
StreamParser::rxPacket
(
    const ADARA::RunStatusPkt &a_pkt    ///< [in] ADARA RunStatusPkt object to process
)
{
   bool bad_state = false;

    if ( a_pkt.status() == ADARA::RunStatus::NEW_RUN )
    {
        if ( m_processing_state == WAITING_FOR_RUN_START )
        {
            m_processing_state = PROCESSING_RUN_HEADER;
            syslog( LOG_INFO,
            "[%i] Run Status Start-of-Run Received at %u.%09u (%s = %s).",
                g_pid,
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                "Processing State", getProcessingStateString().c_str() );
            give_syslog_a_chance;

            // Because We Got A Valid RunStatus Packet for the
            // Start of the Run, Go Ahead and Use This Packet's
            // TimeStamp as the Backup/"Default" Run Start Time,
            // In Case We Never Receive Any Neutron Pulses/Data...
            // (Do This _Instead_ of Using the Wallclock TimeStamp,
            // Which Could Easily Be Bogus if This is a "Replay"... ;-D)
            m_default_run_start_time = a_pkt.timestamp();
        }
        else
        {
            syslog( LOG_ERR, "[%i] %s Run Status Error: %s = %s.",
                g_pid, "STC Error:", "Start-of-Run with Processing State",
                getProcessingStateString().c_str() );
            give_syslog_a_chance;
            bad_state = true;
        }
    }

    else if ( a_pkt.status() == ADARA::RunStatus::END_RUN )
    {
        if ( m_processing_state == PROCESSING_EVENTS )
        {
            // Run "end time" is defined as time of last pulse
            // (which is nanoseconds epoch offset)
            m_run_metrics.run_end_time = nsec_to_timespec(
                m_pulse_info.start_time + m_pulse_info.max_time );

            finalizeStreamProcessing();
            m_processing_state = DONE_PROCESSING;

            // Dagnabbit, return "true" here to halt stream processing...
            // We've marked the processing state to "Done", but we still
            // need to forcibly terminate the POSIX read() loop, which in
            // our case _sometimes_ hangs on relentlessly... <sigh/>
            syslog( LOG_INFO,
                "[%i] Run Status End-of-Run Received (%s = %s).",
                g_pid, "Processing State",
                getProcessingStateString().c_str() );
            give_syslog_a_chance;
            return true;
        }
        else
        {
            syslog( LOG_ERR, "[%i] %s Run Status Error: %s = %s.",
                g_pid, "STC Error:", "End-of-Run with Processing State",
                getProcessingStateString().c_str() );
            give_syslog_a_chance;
            bad_state = true;
        }
    }

    else if ( a_pkt.status() == ADARA::RunStatus::RUN_BOF )
    {
        // Decode Any Mode Index from the File Index (SMS After 1.7.0)
        uint32_t fileNum = a_pkt.fileNumber();
        uint32_t modeNum = 0;

        // Decode Any Paused Mode or Addendum File Index (SMS After 1.8.1)
        uint32_t pauseFileNum = a_pkt.pauseFileNumber();
        uint32_t paused = a_pkt.paused();
        uint32_t addendumFileNum = a_pkt.addendumFileNumber();
        uint32_t addendum = a_pkt.addendum();

        // Embedded Mode Number...?
        if ( fileNum > 0xfff )
        {
            modeNum = ( fileNum >> 12 ) & 0xfff;
            fileNum &= 0xfff;

            syslog( LOG_INFO,
               "[%i] %s, %s%d, %s%d, %s%d (%s=%d), %s%d (%s=%d), %s = %s.",
                g_pid, "Run Status",
                "Mode Index #", modeNum,
                "File Index #", fileNum,
                "Pause Index #", pauseFileNum,
                "paused", paused,
                "Addendum Index #", addendumFileNum,
                "addendum", addendum,
                "Processing State", getProcessingStateString().c_str() );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_INFO,
               "[%i] %s, %s%d, %s%d (%s=%d), %s%d (%s=%d), %s = %s.",
                g_pid, "Run Status",
                "File Index #", fileNum,
                "Pause Index #", pauseFileNum,
                "paused", paused,
                "Addendum Index #", addendumFileNum,
                "addendum", addendum,
                "Processing State", getProcessingStateString().c_str() );
            give_syslog_a_chance;
        }
    }

    // We don't really need to log the _End_ of the file...
    // Just log the Start of the _Next_ file... ;-D
    // ( a_pkt.status() == ADARA::RunStatus::RUN_EOF )

    if ( bad_state )
    {
        THROW_TRACE( ERR_UNEXPECTED_INPUT,
            "Recvd Run Status pkt in Wrong Processing State = "
                << getProcessingStateString() << "." )
    }

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Pixel Mapping packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Pixel Mapping Alt ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA PixelMappingAlt packets. Detector source and
 * bank information is extracted from the received packet and BankInfo
 * instances are created (using the makeBankInfo() virtual factory method)
 * to capture essential bank parameters need for subsequent banked event
 * processing. The receipt of a Pixel Mapping packets also triggers
 * progression to the internal event processing state.
 */
bool
StreamParser::rxPacket
(
    const ADARA::PixelMappingAltPkt &a_pkt     ///< [in] ADARA PixelMappingAltPkt object to process
)
{
    BankInfo *bi;

    uint32_t bsindex;

    const uint32_t *rpos = (const uint32_t *) a_pkt.mappingData();
    const uint32_t *epos = (const uint32_t *)
        ( a_pkt.mappingData() + a_pkt.payload_length()
            - sizeof(uint32_t) );

    int32_t physical_start, physical_stop;
    int32_t physical_step;

    int32_t logical_start, logical_stop;
    int32_t logical_step;

    int32_t physical, logical;

    uint16_t bank_id;
    uint16_t is_shorthand;
    uint16_t pixel_count;

    int16_t cnt;

    // Note: a vector is used for BankInfo instances where the bank_id
    // is the offset into the vector. This is safe as bank IDs are
    // monotonically increasing integers starting at 0. IF this ever
    // changes, then the bank container will need to be changed to a map
    // (which would result in a slight performance drop). Also note that
    // the current code accommodates gaps in the banks by zeroing and
    // subsequently checking entries when iterating over the container.

    // Get number of banks (largest bank id) explicitly from packet and
    // reserve bank container storage

    uint16_t bank_count = (uint16_t) a_pkt.numBanks();

    syslog( LOG_INFO, "[%i] %s: Max Bank = %u", g_pid,
        "PixelMappingAltPkt", bank_count );
    give_syslog_a_chance;

    // Check/Re-Allocate the Detector Banks Array...
    // For Initial Creation of BankInfo Map, Just Do All as State=0...

    reallocateBanksArray( bank_count, 0 );

    // Now build banks and populate bank container

    uint32_t skip_pixel_count = 0;
    uint32_t tot_pixel_count = 0;
    uint32_t skip_sections = 0;
    uint32_t section_count = 0;

    while ( rpos < epos )
    {
        // Base/Starting Physical PixelId
        physical_start = *rpos++;

        // BankID, 0/1=Direct/Shorthand Bit, Pixel Count...
        bank_id = (uint16_t)(*rpos >> 16);
        is_shorthand = (uint16_t)((*rpos & 0x8000) >> 15);
        pixel_count = (uint16_t)(*rpos & 0x7FFF);
        rpos++;

        if ( m_verbose_level > 1 ) {
            syslog( LOG_INFO,
                "[%i] %s: %s = %d, %s = %u, %s = %u, %s = %u",
                g_pid, "PixelMappingAltPkt",
                "Base/Starting Physical", physical_start,
                "Bank ID", bank_id,
                "Is Shorthand", is_shorthand,
                "Count", pixel_count );
            give_syslog_a_chance;
        }

        // Set Up BankInfo Map for Any *Mapped* Sections of Pixel Map...!
        if ( bank_id != (uint16_t) UNMAPPED_BANK )
        {
            // For Initial Creation of BankInfo Map,
            // Just Do All as State=0...
            // Note: Max Bank is Maximum Detector Bank ID...
            bsindex = NUM_SPECIAL_BANKS
                + bank_id + ( 0 * ( m_maxBank + 1 ) );

            bi = m_banks_arr[ bsindex ];

            // Create New BankInfo...
            if ( bi == NULL )
            {
                // Create BankInfo Instance
                m_banks_arr[ bsindex ] = makeBankInfo( bank_id, 0,
                    m_event_buf_write_thresh, m_anc_buf_write_thresh );

                bi = m_banks_arr[ bsindex ];

                // Try to Associate Any Detector Bank Sets that
                // Contain This Bank Id...
                bi->m_bank_sets =
                    getDetectorBankSets( static_cast<uint32_t>(bank_id) );
            }
        }

        // Process Shorthand PixelId Section...
        if ( is_shorthand )
        {
            // Stopping Physical PixelId
            physical_stop = *rpos++;

            // Base/Starting Logical PixelId
            logical_start = *rpos++;

            // Physical and Logical Step
            physical_step = (int16_t)(*rpos >> 16);
            logical_step = (int16_t)(*rpos & 0xFFFF);
            rpos++;

            // Stopping Logical PixelId
            logical_stop = *rpos++;

            if ( m_verbose_level > 1 ) {
                syslog( LOG_INFO,
                    "[%i] %s: %s %s=%d/%d/%d %s=%d/%d/%d",
                    g_pid, "PixelMappingAltPkt", "Shorthand Section",
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step );
                give_syslog_a_chance;
            }

            // Verify Physical PixelId Count Versus Section Count...
            cnt = ( physical_stop - physical_start + physical_step )
                / physical_step;
            if ( cnt != (int32_t) pixel_count )
            {
                syslog( LOG_INFO,
                    "[%i] %s: %s %s=%d/%d/%d %s=%d/%d/%d: %d != %d - %s",
                    g_pid, "PixelMappingAltPkt",
                    "Physical PixelId Count Mismatch",
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step,
                    cnt, pixel_count,
                    "Skip Section..." );
                give_syslog_a_chance;

                // Next Section
                skip_pixel_count += pixel_count;
                skip_sections++;
                continue;
            }

            // Verify Logical PixelId Count Versus Section Count...
            cnt = ( logical_stop - logical_start + logical_step )
                / logical_step;
            if ( cnt != (int32_t) pixel_count )
            {
                syslog( LOG_INFO,
                    "[%i] %s: %s %s=%d/%d/%d %s=%d/%d/%d: %d != %d - %s",
                    g_pid, "PixelMappingAltPkt",
                    "Logical PixelId Count Mismatch",
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step,
                    cnt, pixel_count,
                    "Skip Section..." );
                give_syslog_a_chance;

                // Next Section
                skip_pixel_count += pixel_count;
                skip_sections++;
                continue;
            }

            // Note: Assume Physical and Logical Counts Match Each Other
            // If They Both Match the Same Internal Section Count... ;-D

            // Skip Unmapped Sections of Pixel Map...!
            if ( bank_id == (uint16_t) UNMAPPED_BANK )
            {
                syslog( LOG_INFO,
                    "[%i] %s: Unmapped Shorthand Section - Skip...",
                    g_pid, "PixelMappingAltPkt" );
                give_syslog_a_chance;

                // Next Section
                skip_pixel_count += pixel_count;
                skip_sections++;
                continue;
            }

            // Append This Section's Logical (& Physical) PixelIds...
            for ( physical=physical_start, logical=logical_start ;
                    physical != physical_stop && logical != logical_stop ;
                    physical += physical_step, logical += logical_step )
            {
                bi->m_logical_pixelids.push_back( (uint32_t) logical );

                bi->m_physical_pixelids.push_back( (uint32_t) physical );
            }

            // Append "Stop" PixelIds Too...! ;-D

            bi->m_logical_pixelids.push_back( (uint32_t) logical_stop );

            bi->m_physical_pixelids.push_back( (uint32_t) physical_stop );

            // Done with This Shorthand Section

            tot_pixel_count += pixel_count;

            if ( m_verbose_level > 0 ) {
                syslog( LOG_INFO,
                 "[%i] %s: %s, %s=%u %s=%d/%d/%d %s=%d/%d/%d %s=%u %s=%lu",
                    g_pid, "PixelMappingAltPkt",
                    "Done with Shorthand Section",
                    "bank_id", bank_id,
                    "physical",
                    physical_start, physical_stop, physical_step,
                    "logical",
                    logical_start, logical_stop, logical_step,
                    "count", pixel_count,
                    "total", bi->m_logical_pixelids.size() );
                give_syslog_a_chance;
            }
        }

        // Process Direct PixelId Section...
        else
        {
            // Skip Unmapped Sections of Pixel Map...!
            if ( bank_id == (uint16_t) UNMAPPED_BANK )
            {
                syslog( LOG_INFO,
                    "[%i] %s: Unmapped Direct Section - Skip...",
                    g_pid, "PixelMappingAltPkt" );
                give_syslog_a_chance;

                // Next Section
                skip_pixel_count += pixel_count;
                rpos += pixel_count;
                skip_sections++;
                continue;
            }

            // Append This Section's Logical (& Physical) PixelIds...

            const uint32_t *epos2 = rpos + pixel_count;

            tot_pixel_count += pixel_count;

            while ( rpos < epos2 )
            {
                bi->m_logical_pixelids.push_back( *rpos++ );

                bi->m_physical_pixelids.push_back(
                    (uint32_t) physical_start++ );
            }

            if ( m_verbose_level > 0 ) {
                syslog( LOG_INFO,
                    "[%i] %s: %s, %s=%u %s=%d %s=%u count=%u tot=%lu",
                    g_pid, "PixelMappingAltPkt",
                    "Done with Direct Section",
                    "bank_id", bank_id, "physical_start", physical_start,
                    "Last Logical PixelId",
                    bi->m_logical_pixelids.back(),
                    pixel_count, bi->m_logical_pixelids.size() );
                give_syslog_a_chance;
            }
        }

        section_count++;
    }

    // Also Add Unmapped and Error BankInfo to Map, as State=0...

    // Unmapped BankInfo

    bi = m_banks_arr[ UNMAPPED_BANK_INDEX ];

    // Create New BankInfo...
    if ( bi == NULL )
    {
        // Create BankInfo Instance
        m_banks_arr[ UNMAPPED_BANK_INDEX ] = makeBankInfo(
            (uint16_t) UNMAPPED_BANK, 0,
            m_event_buf_write_thresh, m_anc_buf_write_thresh );

        bi = m_banks_arr[ UNMAPPED_BANK_INDEX ];

        // No Detector Bank Sets for Unmapped Bank...
    }

    // Error BankInfo

    bi = m_banks_arr[ ERROR_BANK_INDEX ];

    // Create New BankInfo...
    if ( bi == NULL )
    {
        // Create BankInfo Instance
        m_banks_arr[ ERROR_BANK_INDEX ] = makeBankInfo(
            (uint16_t) ERROR_BANK, 0,
            m_event_buf_write_thresh, m_anc_buf_write_thresh );

        bi = m_banks_arr[ ERROR_BANK_INDEX ];

        // No Detector Bank Sets for Error Bank...
    }

    // Done

    syslog( LOG_INFO,
        "[%i] %s: %s, PixelIds Tot=%u Skip=%u, Sections Used=%u Skip=%u",
        g_pid, "PixelMappingAltPkt", "Done with Packet",
        tot_pixel_count, skip_pixel_count, section_count, skip_sections );
    give_syslog_a_chance;

    // The receipt of a pixel mapping packet allows state to progress
    // to event processing
    m_processing_state = PROCESSING_EVENTS;

    return false;
}


/*! \brief This method processes Pixel Mapping ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA PixelMapping packets. Detector source and
 * bank information is extracted from the received packet and BankInfo
 * instances are created (using the makeBankInfo() virtual factory method)
 * to capture essential bank parameters need for subsequent banked event
 * processing. The receipt of a Pixel Mapping packets also triggers
 * progression to the internal event processing state.
 */
bool
StreamParser::rxPacket
(
    const ADARA::PixelMappingPkt &a_pkt     ///< [in] ADARA PixelMappingPkt object to process
)
{
    BankInfo *bi;

    uint32_t bsindex;

    const uint32_t *rpos = (const uint32_t *) a_pkt.mappingData();
    const uint32_t *epos = (const uint32_t *)
        ( a_pkt.mappingData() + a_pkt.payload_length() );

    uint32_t        base_logical;
    uint16_t        bank_id;
    uint16_t        pixel_count;

    // Note: a vector is used for BankInfo instances where the bank_id
    // is the offset into the vector. This is safe as bank IDs are
    // monotonically increasing integers starting at 0. IF this ever
    // changes, then the bank container will need to be changed to a map
    // (which would result in a slight performance drop). Also note that
    // the current code accommodates gaps in the banks by zeroing and
    // subsequently checking entries when iterating over the container.

    // Count number of banks (largest bank id) in payload and
    // reserve bank container storage

    uint16_t bank_count = 0;
    const uint32_t *rpos2 = rpos;

    // Determine Max Bank ID...
    while ( rpos2 < epos )
    {
        rpos2++;
        bank_id = (uint16_t)(*rpos2 >> 16);
        pixel_count = (uint16_t)(*rpos2 & 0xFFFF);
        rpos2++;
        syslog( LOG_INFO,
            "[%i] %s: bank_id=%u pixel_count=%u", g_pid, "PixelMappingPkt",
            bank_id, pixel_count );
        give_syslog_a_chance;
        if ( bank_id != (uint16_t) UNMAPPED_BANK && bank_id > bank_count )
        {
            bank_count = bank_id;
            syslog( LOG_INFO,
                "[%i] %s: bank_count -> %u", g_pid, "PixelMappingPkt",
                bank_count );
            give_syslog_a_chance;
        }
        rpos2 += pixel_count;
    }

    syslog( LOG_INFO,
        "[%i] %s: Max Bank = %u", g_pid, "PixelMappingPkt", bank_count );
    give_syslog_a_chance;

    // Check/Re-Allocate the Detector Banks Array...
    // For Initial Creation of BankInfo Map, Just Do All as State=0...

    reallocateBanksArray( bank_count, 0 );

    // Now build banks and populate bank container
    while ( rpos < epos )
    {
        base_logical = *rpos++;
        bank_id = (uint16_t)(*rpos >> 16);
        pixel_count = (uint16_t)(*rpos & 0xFFFF);
        rpos++;

        // Skip Unmapped Sections of Pixel Map...!
        if ( bank_id == (uint16_t) UNMAPPED_BANK )
        {
            // Next Section
            rpos += pixel_count;
            continue;
        }

        // For Initial Creation of BankInfo Map, Just Do All as State=0...
        // Note: Max Bank is Maximum Detector Bank ID...
        bsindex = NUM_SPECIAL_BANKS
            + bank_id + ( 0 * ( m_maxBank + 1 ) );

        bi = m_banks_arr[ bsindex ];

        // Create New BankInfo...
        if ( bi == NULL )
        {
            // Create BankInfo Instance
            m_banks_arr[ bsindex ] = makeBankInfo( bank_id, 0,
                m_event_buf_write_thresh, m_anc_buf_write_thresh );

            bi = m_banks_arr[ bsindex ];

            // Try to Associate Any Detector Bank Sets that
            // Contain This Bank Id...
            bi->m_bank_sets =
                getDetectorBankSets( static_cast<uint32_t>(bank_id) );
        }

        // Append This Section's Logical (& Physical) PixelIds...
        for (uint32_t i=0 ; i < pixel_count ; ++i)
        {
            bi->m_logical_pixelids.push_back( base_logical + i );
            bi->m_physical_pixelids.push_back( *rpos++ );
        }

        // syslog( LOG_INFO,
            // "[%i] %s: bank_id=%u base_logical=%u count=%u tot=%lu",
            // g_pid, "PixelMappingPkt",
            // bank_id, base_logical, pixel_count,
            // bi->m_logical_pixelids.size() );
        // give_syslog_a_chance;
    }

    // Also Add Unmapped and Error BankInfo to Map, as State=0...

    // Unmapped BankInfo

    bi = m_banks_arr[ UNMAPPED_BANK_INDEX ];

    // Create New BankInfo...
    if ( bi == NULL )
    {
        // Create BankInfo Instance
        m_banks_arr[ UNMAPPED_BANK_INDEX ] = makeBankInfo(
            (uint16_t) UNMAPPED_BANK, 0,
            m_event_buf_write_thresh, m_anc_buf_write_thresh );

        bi = m_banks_arr[ UNMAPPED_BANK_INDEX ];

        // No Detector Bank Sets for Unmapped Bank...
    }

    // Error BankInfo

    bi = m_banks_arr[ ERROR_BANK_INDEX ];

    // Create New BankInfo...
    if ( bi == NULL )
    {
        // Create BankInfo Instance
        m_banks_arr[ ERROR_BANK_INDEX ] = makeBankInfo(
            (uint16_t) ERROR_BANK, 0,
            m_event_buf_write_thresh, m_anc_buf_write_thresh );

        bi = m_banks_arr[ ERROR_BANK_INDEX ];

        // No Detector Bank Sets for Error Bank...
    }

    // Done

    // The receipt of a pixel mapping packet allows state to progress
    // to event processing
    m_processing_state = PROCESSING_EVENTS;
    syslog( LOG_INFO,
        "[%i] %s: Pixel Mapping Table Processed (%s = %s).",
        g_pid, "StreamParser::rxPacket( ADARA::PixelMappingPkt )",
        "Processing State", getProcessingStateString().c_str() );
    give_syslog_a_chance;

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Banked Event packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Banked Event ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Banked Event packets.
 * The processPulseInfo() method is called by this method with the
 * pulse data attached to the received Banked Event packet.
 * The payload of the Banked Event packet is parsed for neutron
 * events which are then handled by the processBankEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BankedEventPkt &a_pkt      ///< [in] ADARA BankedEventPkt object to process
)
{
    // Ignore duplicate pulses
    if ( a_pkt.flags() & ADARA::DUPLICATE_PULSE )
    {
        // Log Duplicate Pulse if it had Any Events...!
        uint32_t nbytes = a_pkt.payload_length() - 16;
        if ( nbytes > 16 ) // appears to be minimal "empty"-ish packet...
        {
            syslog( LOG_ERR,
                "[%i] %s %s %u.%09u with Events (%u %s) in %s - Ignoring!",
                g_pid, "STC Error:", "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                nbytes, "Payload Bytes",
                "BankedEventPkt" );
            give_syslog_a_chance;
        }
        // Else Just Note the Dropped Duplicate Pulse
        // (if for no other reason than diagnostics and testing :-)
        else
        {
            syslog( LOG_INFO,
                "[%i] %s %u.%09u (%u %s) in %s - Ignoring!",
                g_pid, "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                nbytes, "Payload Bytes",
                "BankedEventPkt" );
            give_syslog_a_chance;
        }
        return false;
    }

    // (Current) Pulse flag should be 0 (no data processed yet)
    // or 2 (monitor data processed).
    // Any other value indicates an error with SMS packet generation.

    if ( m_pulse_flag == 0 )
    {
        // First packet of new pulse
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag |= 1;
    }
    else if ( m_pulse_flag == 2 ) {
        // "Complete" Pulse, Got Both Some Neutron & Beam Monitor Data...
        m_pulse_flag = 0;
    }
    else {
        // *** No Intervening Beam Monitor Packet for Last Pulse?!
        // _Assume_ First Packet of _New_ Pulse...
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag = 1;
        // *Don't* Throw Exception Here, Log Profusely & Move On...
        // (No need to throw out baby with the bath water...! ;-)
        syslog( LOG_ERR,
            "[%i] %s %s Received at %u.%09u, %s %s Continuing...",
            g_pid, "STC Error:",
            "Invalid Banked Event Packet Sequence",
            (uint32_t) a_pkt.timestamp().tv_sec
                - ADARA::EPICS_EPOCH_OFFSET,
            (uint32_t) a_pkt.timestamp().tv_nsec,
            "No Intervening Beam Monitor Packet!",
            "(LOST EXPERIMENT DATA?)" );
        give_syslog_a_chance;
    }

    processPulseInfo( a_pkt );

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)
        (a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t source_id;
    uint32_t bank_count;
    uint32_t bank_id;
    uint32_t event_count;

    // Process banks per-source
    while ( rpos < epos )
    {
        source_id = *rpos++;
        rpos += 2; // TODO For now, skip over source-specific pulse info.
                   // Should eventually process this data
        bank_count = *rpos++;

        // Process events per-bank
        while( bank_count-- )
        {
            bank_id = *rpos++;
            event_count = *rpos++;
            processBankEvents( bank_id, /* state */ 0, event_count, rpos );
            rpos += event_count << 1;
        }
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Banked Event State packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Banked Event State ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Banked Event State packets.
 * The processPulseInfo() method is called by this method with the
 * pulse data attached to the received Banked Event State packet.
 * The payload of the Banked Event State packet is parsed for neutron
 * events which are then handled by the processBankEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BankedEventStatePkt &a_pkt     ///< [in] ADARA BankedEventStatePkt object to process
)
{
    // Ignore duplicate pulses
    if ( a_pkt.flags() & ADARA::DUPLICATE_PULSE )
    {
        // Log Duplicate Pulse if it had Any Events...!
        uint32_t nbytes = a_pkt.payload_length() - 16;
        if ( nbytes > 16 ) // appears to be minimal "empty"-ish packet...
        {
            syslog( LOG_ERR,
                "[%i] %s %s %u.%09u with Events (%u %s) in %s - Ignoring!",
                g_pid, "STC Error:", "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                nbytes, "Payload Bytes",
                "BankedEventStatePkt" );
            give_syslog_a_chance;
        }
        // Else Just Note the Dropped Duplicate Pulse
        // (if for no other reason than diagnostics and testing :-)
        else
        {
            syslog( LOG_INFO,
                "[%i] %s %u.%09u (%u %s) in %s - Ignoring!",
                g_pid, "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                nbytes, "Payload Bytes",
                "BankedEventStatePkt" );
            give_syslog_a_chance;
        }
        return false;
    }

    // (Current) Pulse flag should be 0 (no data processed yet)
    // or 2 (monitor data processed).
    // Any other value indicates an error with SMS packet generation.

    if ( m_pulse_flag == 0 )
    {
        // First packet of new pulse
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag |= 1;
    }
    else if ( m_pulse_flag == 2 ) {
        // "Complete" Pulse, Got Both Some Neutron & Beam Monitor Data...
        m_pulse_flag = 0;
    }
    else {
        // *** No Intervening Beam Monitor Packet for Last Pulse?!
        // _Assume_ First Packet of _New_ Pulse...
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag = 1;
        // *Don't* Throw Exception Here, Log Profusely & Move On...
        // (No need to throw out baby with the bath water...! ;-)
        syslog( LOG_ERR,
            "[%i] %s %s Received at %u.%09u, %s %s Continuing...",
            g_pid, "STC Error:",
            "Invalid Banked Event State Packet Sequence",
            (uint32_t) a_pkt.timestamp().tv_sec
                - ADARA::EPICS_EPOCH_OFFSET,
            (uint32_t) a_pkt.timestamp().tv_nsec,
            "No Intervening Beam Monitor Packet!",
            "(LOST EXPERIMENT DATA?)" );
        give_syslog_a_chance;
    }

    processPulseInfo( a_pkt );

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t source_id;
    uint32_t bank_count;
    uint32_t bank_id;
    uint32_t state;
    uint32_t event_count;

    // Process banks per-source
    while ( rpos < epos )
    {
        source_id = *rpos++;
        rpos += 2; // TODO For now, skip over source-specific pulse info. Should eventually process this data
        bank_count = *rpos++;

        // Process events per-bank
        while( bank_count-- )
        {
            bank_id = *rpos++;
            state = *rpos++;
            event_count = *rpos++;
            processBankEvents( bank_id, state, event_count, rpos );
            rpos += event_count << 1;
        }
    }

    return false;
}


/*! \brief This method throws away the contents of an Oversized Packet received as part of the ADARA Stream.
 *
 * This method hopefully squawks to the log file when an Oversized Packet
 * arrives from the SMS, rather than silently dropping it on the floor.
 * It is unlikely that such packets will occur frequently, however in
 * the ADARA Test Harness these can be generated when an Event Index
 * is way off, and _All_ the events for a Data Source show up in 1 Pulse.
 * And if such oversized packets do start cropping up, we really should
 * look into it, since we simply throw these packets on the floor... :-b
 */
bool
StreamParser::rxOversizePkt
(
    const ADARA::PacketHeader *hdr,  ///< [in] ADARA PacketHeader object for Oversized Packet (1st Invocation Only, then NULL for rest of packet)
    const uint8_t *chunk,  ///< [in] Chunk of Oversized Message Data
    unsigned int chunk_offset,  ///< [in] Offset of This Oversized Chunk
    unsigned int chunk_len  ///< [in] Length of this Oversized Chunk (in bytes)
)
{
    // NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o

    // Log Oversized Packet (with Header)
    if ( hdr != NULL )
    {
        syslog( LOG_WARNING,
        "[%i] %s %u.%09u type=0x%x payload_len=%u max=%u offset=%u len=%u",
            g_pid, "OversizePkt:",
            (uint32_t) hdr->timestamp().tv_sec - ADARA::EPICS_EPOCH_OFFSET,
            (uint32_t) hdr->timestamp().tv_nsec,
            hdr->type(), hdr->payload_length(), ADARA_IN_BUF_SIZE,
            chunk_offset, chunk_len);
        give_syslog_a_chance;

        // Handle pulse sequence flag for this Oversized Packet
        // (so we don't get "out of sync" when we throw it away... :-)

        if ( hdr->base_type() == ADARA::PacketType::BANKED_EVENT_TYPE
                || hdr->base_type()
                    == ADARA::PacketType::BANKED_EVENT_STATE_TYPE
                || hdr->base_type()
                    == ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
        {
            // (Current) Pulse flag should be 0 (no data processed yet)
            // or 1 (event data processed) [If This is Beam Monitor Pkt]
            // or 2 (monitor data processed) [If This is Banked Event Pkt]
            // Any other mismatching value indicates an error with
            // SMS packet generation.

            if ( m_pulse_flag == 0 )
            {
                // First packet of new pulse - count it and set flag
                // indicating it was counted
                ++m_pulse_count;
                if ( hdr->base_type()
                        == ADARA::PacketType::BANKED_EVENT_TYPE
                    || hdr->base_type()
                        == ADARA::PacketType::BANKED_EVENT_STATE_TYPE )
                {
                    m_pulse_flag |= 1;
                }
                else if ( hdr->base_type() ==
                        ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
                {
                    m_pulse_flag |= 2;
                }
            }
            else if ( ( ( hdr->base_type()
                            == ADARA::PacketType::BANKED_EVENT_TYPE
                        || hdr->base_type()
                            == ADARA::PacketType::BANKED_EVENT_STATE_TYPE )
                        && m_pulse_flag == 2 )
                    || ( hdr->base_type() ==
                            ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE
                        && m_pulse_flag == 1 ) )
            {
                // "Complete" Pulse,
                // Got Both Some Neutron & Beam Monitor Data...
                m_pulse_flag = 0;
            }
            else if ( hdr->base_type()
                    == ADARA::PacketType::BANKED_EVENT_TYPE
                || hdr->base_type()
                    == ADARA::PacketType::BANKED_EVENT_STATE_TYPE )
            {
                // *** No Intervening Beam Monitor Packet for Last Pulse?!
                // _Assume_ First Packet of _New_ Pulse...
                // - count it and set flag indicating it was counted
                ++m_pulse_count;
                m_pulse_flag = 1;
                // *Don't* Throw Exception Here, Log Profusely & Move On...
                // (No need to throw out baby with the bath water...! ;-)
                syslog( LOG_ERR,
                    "[%i] %s %s Received at %u.%09u, %s %s Continuing...",
                    g_pid, "STC Error:",
                    "Invalid (Oversized) Banked Event Packet Sequence",
                    (uint32_t) hdr->timestamp().tv_sec
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (uint32_t) hdr->timestamp().tv_nsec,
                    "No Intervening Beam Monitor Packet!",
                    "(LOST EXPERIMENT DATA?)" );
                give_syslog_a_chance;
            }
            else if ( hdr->base_type() ==
                    ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
            {
                // *** No Intervening Banked Event Packet for Last Pulse?!
                // _Assume_ First Packet of _New_ Pulse...
                // - count it and set flag indicating it was counted
                ++m_pulse_count;
                m_pulse_flag = 2;
                // *Don't* Throw Exception Here, Log Profusely & Move On...
                // (No need to throw out baby with the bath water...! ;-)
                syslog( LOG_ERR,
                    "[%i] %s %s Received at %u.%09u, %s %s Continuing...",
                    g_pid, "STC Error:",
                    "Invalid (Oversized) Beam Monitor Packet Sequence",
                    (uint32_t) hdr->timestamp().tv_sec
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (uint32_t) hdr->timestamp().tv_nsec,
                    "No Intervening Banked Event Packet!",
                    "(LOST EXPERIMENT DATA?)" );
                give_syslog_a_chance;
            }
        }
    }

    // Log Oversized Packet (Next Chunk)
    else
    {
        syslog( LOG_WARNING,
            "[%i] OversizePkt: next chunk max=%u offset=%u len=%u",
            g_pid, ADARA_IN_BUF_SIZE, chunk_offset, chunk_len);
        give_syslog_a_chance;
    }

    // Invoke the base handler, in case it ever does anything...
    return Parser::rxOversizePkt(hdr, chunk, chunk_offset, chunk_len);
}

/*! \brief This method checks and reallocates as necessary the Array of Detector Bank Info instances.
 *
 * This method compares the State and Bank ID to the Max State and
 * Max Bank ID, respectively, and Re-Allocates the Bank-State-Indexed
 * BankInfo* Array.
 */
void
StreamParser::reallocateBanksArray
(
    uint32_t        a_bank_id,        ///< [in] Bank ID of detector bank to be processed
    uint32_t        a_state           ///< [in] State of detector bank to be processed
)
{
    // Check State Versus Total Number of States,
    // Check Bank ID Versus Max Bank,
    // Re-Allocate Banks Array As Needed...
    // Note: "Number" of States Includes State 0...
    // Note: Max Bank is Maximum Detector Bank ID...
    if ( ( a_state + 1 > m_numStates ) || ( a_bank_id > m_maxBank ) )
    {
        // Calculate New Banks Array Size (Including Special Banks)
        uint32_t new_banks_arr_size = NUM_SPECIAL_BANKS
            + ( ( a_state + 1 ) * ( a_bank_id + 1 ) );

        syslog( LOG_INFO,
          "[%i] %s(): %s = %u > %s = %u and/or %s = %u > %s = %u, %s = %u",
            g_pid, "reallocateBanksArray",
            "New State (+1 for 0)", a_state,
            "Num States", m_numStates,
            "New Bank ID", a_bank_id,
            "Max Bank", m_maxBank,
            "New Banks Array Size", new_banks_arr_size );
        give_syslog_a_chance;

        // Allocate New Banks Array
        BankInfoPtr *new_banks_arr = new BankInfoPtr[ new_banks_arr_size ];

        // Make Sure All Elements are Initialized... ;-D
        for ( uint32_t i=0 ; i < new_banks_arr_size ; i++ )
            new_banks_arr[i] = NULL;

        // Swap Over Original State-Bank BankInfo Array to New Array...
        if ( m_banks_arr != NULL )
        {
            // Swap Over Existing Special BankInfo Instances...
            // (e.g. UNMAPPED_BANK and ERROR_BANK)
            for ( uint32_t i=0 ; i < NUM_SPECIAL_BANKS ; i++ )
            {
                new_banks_arr[ i ] = m_banks_arr[ i ];
                m_banks_arr[ i ] = (BankInfo *) NULL;
            }

            // Swap Over Existing Regular State-Bank BankInfo Instances...
            // Note: "Number" of States Includes State 0...
            // Note: Max Bank is Maximum Detector Bank ID...
            for ( uint32_t s=0 ; s < m_numStates ; s++ )
            {
                for ( uint32_t b=0 ; b < m_maxBank + 1 ; b++ )
                {
                    uint32_t bsindex_old = NUM_SPECIAL_BANKS
                        + b + ( s * ( m_maxBank + 1 ) );
                    uint32_t bsindex_new = NUM_SPECIAL_BANKS
                        + b + ( s * ( a_bank_id + 1 ) );
    
                    new_banks_arr[ bsindex_new ] =
                        m_banks_arr[ bsindex_old ];
                    m_banks_arr[ bsindex_old ] = (BankInfo *) NULL;
                }
            }

            // Free Original State-Bank bankInfo Array...
            delete[] m_banks_arr;
        }

        // Assign New Array and Update State/Bank Parameters
        // Note: "Number" of States Includes State 0...
        // Note: Max Bank is Maximum Detector Bank ID...
        m_banks_arr = new_banks_arr;
        m_banks_arr_size = new_banks_arr_size;
        m_numStates = a_state + 1;
        m_maxBank = a_bank_id;
    }
}

/*! \brief This method processes the neutron pulse data associated with a Banked Event packet.
 *
 * This method accumulates pulse charge, time, and frequency data associated with a Banked Event packet. The various
 * data are collected in ancillary buffers until ready to be flushed to a subclassed stream adapter via the
 * pulseBuffersReady() virtual method.
 */
void
StreamParser::processPulseInfo
(
    const ADARA::BankedEventPkt &a_pkt      ///< [in] ADARA BankedEventPkt object to process
)
{
    // accumulate pulse charge
    double charge = a_pkt.pulseCharge()*10.0; // ADARA charge is in units of 10 pC
    m_run_metrics.total_charge += charge;
    m_pulse_info.charges.push_back( charge );
    m_run_metrics.charge_stats.push( charge );

    // Accumulate flags
    m_pulse_info.flags.push_back( a_pkt.flags() );

    // Handle time and frequency info
    if ( m_pulse_info.start_time )
    {
        uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

        // It is (or should be) considered a fatal error if
        // pulse times are not monotonically increasing
        if ( pulse_time < m_pulse_info.start_time )
        {
            syslog( LOG_INFO,
                "[%i] Unexpected input: %s at pulse #%ld ID=0x%lx, %s.",
                g_pid, "Pulse time went backwards",
                m_pulse_info.times.size(), a_pkt.pulseId(),
                "Clamping to zero" );
            give_syslog_a_chance;
            pulse_time = 0;
        }
        else
            pulse_time -= m_pulse_info.start_time;

        m_pulse_info.times.push_back( pulse_time / NANO_PER_SECOND_D );
        m_pulse_info.freqs.push_back( NANO_PER_SECOND_D
            / ( pulse_time - m_pulse_info.last_time ) );
        m_run_metrics.freq_stats.push( m_pulse_info.freqs.back() );
        m_pulse_info.last_time = pulse_time;
        if ( pulse_time > m_pulse_info.max_time )
            m_pulse_info.max_time = pulse_time;
    }
    else
    {
        m_pulse_info.start_time = timespec_to_nsec( a_pkt.timestamp() );
        m_pulse_info.last_time = 0;
        m_pulse_info.max_time = 0;
        m_pulse_info.times.push_back(0);
        m_pulse_info.freqs.push_back(0);
        // Freq stats ignores first point since it can't be calculated

        // Run "start time" is defined as time of first pulse
        m_run_metrics.run_start_time = a_pkt.timestamp();
    }

    // Is is time to write pulse info?
    // (And is the Working Directory/NeXus Data File Ready for Data...?)
    if ( m_pulse_info.times.size() == m_anc_buf_write_thresh
            && isWorkingDirectoryReady() )
    {
        pulseBuffersReady( m_pulse_info );

        m_pulse_info.times.clear();
        m_pulse_info.freqs.clear();
        m_pulse_info.flags.clear();
        m_pulse_info.charges.clear();
    }
}


/*! \brief This method processes the neutron pulse data associated with a Banked Event State packet.
 *
 * This method accumulates pulse charge, time, and frequency data associated with a Banked Event State packet (the same but different from the Banked Event packet ;-D). The various
 * data are collected in ancillary buffers until ready to be flushed to a subclassed stream adapter via the
 * pulseBuffersReady() virtual method.
 */
void
StreamParser::processPulseInfo
(
    const ADARA::BankedEventStatePkt &a_pkt      ///< [in] ADARA BankedEventStatePkt object to process
)
{
    // accumulate pulse charge
    double charge = a_pkt.pulseCharge()*10.0; // ADARA charge is in units of 10 pC
    m_run_metrics.total_charge += charge;
    m_pulse_info.charges.push_back( charge );
    m_run_metrics.charge_stats.push( charge );

    // Accumulate flags
    m_pulse_info.flags.push_back( a_pkt.flags() );

    // Handle time and frequency info
    if ( m_pulse_info.start_time )
    {
        uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() );

        // It is (or should be) considered a fatal error if
        // pulse times are not monotonically increasing
        if ( pulse_time < m_pulse_info.start_time )
        {
            syslog( LOG_INFO,
                "[%i] Unexpected input: %s at pulse #%ld ID=0x%lx, %s.",
                g_pid, "Pulse time went backwards",
                m_pulse_info.times.size(), a_pkt.pulseId(),
                "Clamping to zero" );
            give_syslog_a_chance;
            pulse_time = 0;
        }
        else
            pulse_time -= m_pulse_info.start_time;

        m_pulse_info.times.push_back( pulse_time / NANO_PER_SECOND_D );
        m_pulse_info.freqs.push_back( NANO_PER_SECOND_D
            / ( pulse_time - m_pulse_info.last_time ) );
        m_run_metrics.freq_stats.push( m_pulse_info.freqs.back() );
        m_pulse_info.last_time = pulse_time;
        if ( pulse_time > m_pulse_info.max_time )
            m_pulse_info.max_time = pulse_time;
    }
    else
    {
        m_pulse_info.start_time = timespec_to_nsec( a_pkt.timestamp() );
        m_pulse_info.last_time = 0;
        m_pulse_info.max_time = 0;
        m_pulse_info.times.push_back(0);
        m_pulse_info.freqs.push_back(0);
        // Freq stats ignores first point since it can't be calculated

        // Run "start time" is defined as time of first pulse
        m_run_metrics.run_start_time = a_pkt.timestamp();
    }

    // Is is time to write pulse info?
    // (And is the Working Directory/NeXus Data File Ready for Data...?)
    if ( m_pulse_info.times.size() == m_anc_buf_write_thresh
            && isWorkingDirectoryReady() )
    {
        pulseBuffersReady( m_pulse_info );

        m_pulse_info.times.clear();
        m_pulse_info.freqs.clear();
        m_pulse_info.flags.clear();
        m_pulse_info.charges.clear();
    }
}


/*! \brief This method processes the neutron events for a specific detector bank.
 *
 * This method processes incoming neutron events for a specified detector
 * bank. The events are read from the packet and placed into internal event
 * buffers (units are converted for event time of flight). When the event
 * buffers are full, they are flushed to a subclassed stream adapter via
 * the now-split dual bankPidTOFBuffersReady() and bankIndexBuffersReady()
 * virtual methods. This method also detects pulse gaps and corrects the
 * event index as required (see handleBankPulseGap() method for
 * more details).
 */
void
StreamParser::processBankEvents
(
    uint32_t        a_bank_id,        ///< [in] Bank ID of detector bank to be processed
    uint32_t        a_state,          ///< [in] State of detector bank to be processed
    uint32_t        a_event_count,    ///< [in] Number of events contained in stream buffer
    const uint32_t *a_rpos            ///< [in] Stream event buffer read pointer
)
{
    // Check/Re-Allocate the Detector Banks Array...
    // For Initial Creation of BankInfo Map, Just Do All as State=0...

    // Make Sure We At Least Allocate 1 Bank, Plus the UNMAPPED/ERROR Banks
    if ( a_bank_id == UNMAPPED_BANK || a_bank_id == ERROR_BANK )
    {
        reallocateBanksArray( 1, a_state );
    }
    // Check/Allocate the Regular Detector Bank Slots...
    else
    {
        reallocateBanksArray( a_bank_id, a_state );
    }

    // Determine State-Bank BankInfo Array Index...
    uint32_t bsindex;
    if ( a_bank_id == UNMAPPED_BANK )
    {
        bsindex = UNMAPPED_BANK_INDEX;
    }
    else if ( a_bank_id == ERROR_BANK )
    {
        bsindex = ERROR_BANK_INDEX;
    }
    else
    {
        // Note: Max Bank is Maximum Detector Bank ID...
        bsindex = NUM_SPECIAL_BANKS
            + a_bank_id + ( a_state * ( m_maxBank + 1 ) );
    }

    BankInfo *bi = m_banks_arr[ bsindex ];

    // No BankInfo for This Detector Bank ID/State Yet...?
    if ( bi == NULL ) {

        syslog( LOG_ERR,
            "[%i] %s: No BankInfo Found for bank_id=%u state=%u",
            g_pid, "StreamParser::processBankEvents()",
            a_bank_id, a_state );
        give_syslog_a_chance;

        if ( a_bank_id == UNMAPPED_BANK )
        {
            // Create BankInfo Instance
            m_banks_arr[ UNMAPPED_BANK_INDEX ] = makeBankInfo(
                (uint16_t) UNMAPPED_BANK, 0,
                m_event_buf_write_thresh, m_anc_buf_write_thresh );

            bi = m_banks_arr[ UNMAPPED_BANK_INDEX ];
        }

        else if ( a_bank_id == ERROR_BANK )
        {
            // Create BankInfo Instance
            m_banks_arr[ ERROR_BANK_INDEX ] = makeBankInfo(
                (uint16_t) ERROR_BANK, 0,
                m_event_buf_write_thresh, m_anc_buf_write_thresh );

            bi = m_banks_arr[ ERROR_BANK_INDEX ];
        }

        else
        {
            // Search for the State=0 BankInfo...

            uint32_t bsindex0 = NUM_SPECIAL_BANKS
                + a_bank_id + ( 0 * ( m_maxBank + 1 ) );

            BankInfo *bi0 = m_banks_arr[ bsindex0 ];

            // Valid State=0 Detector Bank ID...
            if ( bi0 != NULL ) {

                syslog( LOG_ERR,
                 "[%i] %s: Found State=0 BankInfo for bank_id=%u state=%u",
                    g_pid, "StreamParser::processBankEvents()",
                    a_bank_id, a_state );
                give_syslog_a_chance;

                // Carefully "Clone" the Base State=0 BankInfo
                // for This State...

                // Create BankInfo Instance
                m_banks_arr[ bsindex ] = makeBankInfo( a_bank_id, a_state,
                    m_event_buf_write_thresh, m_anc_buf_write_thresh );

                bi = m_banks_arr[ bsindex ];

                // Try to Associate Any Detector Bank Sets that
                // Contain This Bank Id...
                bi->m_bank_sets =
                    getDetectorBankSets(
                        static_cast<uint32_t>(a_bank_id) );

                // Copy Any Saved Logical PixelIds for This Detector Bank
                bi->m_logical_pixelids = bi0->m_logical_pixelids;
                bi->m_physical_pixelids = bi0->m_physical_pixelids;

                // Copy Any Saved Detector Bank Sets for This Detector Bank
                bi->m_bank_sets = bi0->m_bank_sets;
            }

            else {
                syslog( LOG_ERR,
                 "[%i] %s: No State=0 BankInfo Found! bank_id=%u state=%u",
                    g_pid, "StreamParser::processBankEvents()",
                    a_bank_id, a_state );
                give_syslog_a_chance;
            }
        }
    }

    // Valid Detector Bank ID...
    if ( bi != NULL )
    {
        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( false, m_verbose_level );

        // Event-based Data Processing
        if ( bi->m_has_event )
        {
            // Detect gaps in event data and fill event index if present
            if ( bi->m_last_pulse_with_data < ( m_pulse_count - 1 ) )
            {
                handleBankPulseGap( *bi,
                    ( m_pulse_count - 1 ) - bi->m_last_pulse_with_data );
            }

            size_t sz = bi->m_tof_buffer_size;

            // *** STC CRITICAL PATH OPTIMIZATION ***
            // *ONLY* Resize Vector if _Not_ Previously Resized...!
            // - Huge Savings for us, because resize() pre-initializes
            // the Values in the vector when resizing...! ;-D
            // - we track our own "in use" vector size in the
            // new "BankInfo::m_tof_buffer_size" field... :-D
            // Update: Don't Resize Incrementally with 1000 Razor Blades,
            // Do Exponential Growth (Like This Helps... ;-D)
            if ( sz + a_event_count > bi->m_tof_buffer.size() )
            {
                size_t resz;
                // If Buffer Hasn't Yet Been Allocated,
                // Start with 10x What We Need Right Now...
                if ( bi->m_tof_buffer.size() == 0 )
                {
                    resz = 10 * ( sz + a_event_count );
                }
                // If Buffer Exists But We've Filled It Up,
                // Just Double the Size of the Existing Buffer.
                // (We don't wanna spend our lives resizing... ;-b)
                else
                {
                    resz = 2 * ( sz + a_event_count );
                }
                // Don't Blow Over the Event Buffer Write Threshold,
                // Otherwise Limit Buffer Resize to "Just What We Need"...
                // (As Long as Working Directory has been Fully Resolved!)
                if ( isWorkingDirectoryReady()
                        && resz > m_event_buf_write_thresh )
                {
                    resz = m_event_buf_write_thresh;
                    if ( sz + a_event_count > resz )
                        resz = sz + a_event_count;
                }

                if ( m_verbose_level > 0 ) {
                    syslog( LOG_INFO,
                      "[%i] %s: %s=%u %s=%u %s %s=%lu %s=%u %s=%lu %s=%lu",
                        g_pid, "StreamParser::processBankEvents",
                        "BankID", a_bank_id,
                        "State", a_state,
                        "Resizing Event Buffers",
                        "m_tof_buffer_size", bi->m_tof_buffer_size,
                        "event_count", a_event_count,
                        "m_tof_buffer.size()", bi->m_tof_buffer.size(),
                        "resz", resz );
                    give_syslog_a_chance;
                }

                // Now Resize the Buffers to this Max Resize Size...
                bi->m_tof_buffer.resize( resz, (float) -1.0 );
                bi->m_pid_buffer.resize( resz, (uint32_t) -1 );
            }

            float           *tof_ptr = &bi->m_tof_buffer[sz];
            uint32_t        *pid_ptr = &bi->m_pid_buffer[sz];
            const uint32_t  *rpos = a_rpos;
            const uint32_t  *epos = a_rpos + (a_event_count<<1);

            while ( rpos != epos )
            {
                // ADARA TOF values are in units of 100 ns
                // - convert to microseconds
                *tof_ptr++ = *rpos++ / 10.0;
                *pid_ptr++ = *rpos++;
            }

            // Manually Track "In Use" Vector Data Buffer Size (see above!)
            bi->m_tof_buffer_size += a_event_count;

            // Cache event index until large enough to write
            bi->m_index_buffer.push_back( bi->m_event_count );
            bi->m_event_count += a_event_count;

            bi->m_last_pulse_with_data = m_pulse_count;

            // Check to see if Pid/TOF buffers are ready to write
            // (And is the Working Directory/NeXus Data File
            // Ready for Data...?)
            if ( bi->m_tof_buffer_size >= m_event_buf_write_thresh
                    && isWorkingDirectoryReady() )
            {
                bankPidTOFBuffersReady( *bi );

#ifdef PARANOID
                resetInUseVector<float>( bi->m_tof_buffer,
                    bi->m_tof_buffer_size );
                resetInUseVector<uint32_t>( bi->m_pid_buffer,
                    bi->m_tof_buffer_size );
#endif
                bi->m_tof_buffer_size = 0;
            }

            // Check to see if Index buffers are ready to write
            // (And is the Working Directory/NeXus Data File
            // Ready for Data...?)
            if ( bi->m_index_buffer.size() >= m_anc_buf_write_thresh
                    && isWorkingDirectoryReady() )
            {
                bankIndexBuffersReady( *bi, false );

                bi->m_index_buffer.clear();
            }

            if ( a_bank_id != UNMAPPED_BANK && a_bank_id != ERROR_BANK )
                m_run_metrics.events_counted += a_event_count;
            else
                m_run_metrics.events_uncounted += a_event_count;
        }

        // Histogram-based Data Processing
        if ( bi->m_has_histo )
        {
            const uint32_t  *rpos = a_rpos;
            const uint32_t  *epos = a_rpos + (a_event_count<<1);

            // Find Detector Bank Set We're Using for Histogram
            // (Only *One* Histogram per Detector Bank (for now)...)
            for ( std::vector<STC::DetectorBankSet *>::iterator dbs =
                        bi->m_bank_sets.begin();
                    dbs != bi->m_bank_sets.end() ; ++dbs )
            {
                if ( !*dbs )
                    continue;

                // Histo-based Detector Bank
                if ( (*dbs)->flags & ADARA::HISTO_FORMAT )
                {
                    uint32_t tofbin;
                    uint32_t index;
                    uint32_t tof;
                    uint32_t pid;

                    while ( rpos != epos )
                    {
                        // ADARA TOF values are in units of 100 ns,
                        //    convert to microseconds
                        tof = *rpos++ / 10;
                        pid = *rpos++;

                        // Ignore TOF Less than Minimum Offset
                        //    and Greater than or Equal to Maximum TOF...
                        //    (Non-Inclusive Max...! ;-D)
                        if ( tof >= (*dbs)->tofOffset
                                && tof < (*dbs)->tofMax
                                && bi->m_histo_pid_offset[
                                    pid - bi->m_base_pid ] >= 0 )
                        {
                            // Calculate index into Histogram based on TOF
                            tofbin = ( tof - (*dbs)->tofOffset )
                                / bi->m_tof_bin_size;

                            // TOF Sanity Test, Just to Be Sure... ;-b
                            // (This should never happen,
                            //    but the logic is confusing.)
                            if ( tofbin >= bi->m_num_tof_bins - 1 )
                            {
                                syslog( LOG_ERR,
                                "[%i] %s %s %u %s tof=%u tofbin=%u >= %u",
                                    g_pid, "STC Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Error",
                                    tof, tofbin, bi->m_num_tof_bins - 1 );
                                give_syslog_a_chance;
                                // Count Uncounted Detector Histo Events...
                                (bi->m_histo_event_uncounted)++;
                                continue;
                            }

                            // Calculate Overall Histogram Index
                            index = bi->m_histo_pid_offset[
                                        pid - bi->m_base_pid ]
                                    + tofbin;

                            // Index Sanity Test, Just to Be Sure... ;-b
                            // (This should never happen...)
                            if ( index >= ( bi->m_logical_pixelids.size()
                                    * ( bi->m_num_tof_bins - 1 ) ) )
                            {
                                syslog( LOG_ERR,
                             "[%i] %s %s %u %s pid=%u tofbin=%u %u >= %lu",
                                    g_pid, "STC Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Index Overflow",
                                    pid, tofbin, index,
                                    bi->m_logical_pixelids.size()
                                        * ( bi->m_num_tof_bins - 1 )
                                    );
                                give_syslog_a_chance;
                                // Count Uncounted Detector Histo Events...
                                (bi->m_histo_event_uncounted)++;
                                continue;
                            }

                            // Increment Histogram Time Slot...
                            (bi->m_data_buffer[ index ])++;

                            // Count Detector Events for Histogram Mode Too
                            (bi->m_histo_event_count)++;
                        }

                        // Count Uncounted Detector Events for Histogram...
                        else
                        {
                            // Log Any Bogus PixelId Offsets...?
                            // (TODO definitely need to be rate-limited ;-)
                            if ( bi->m_histo_pid_offset[
                                    pid - bi->m_base_pid ] < 0 )
                            {
                                syslog( LOG_ERR,
                           "[%i] %s %s %u %s pid=%u base=%u offset=%u < 0",
                                    g_pid, "STC Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Offset Error",
                                    pid, bi->m_base_pid,
                                    bi->m_histo_pid_offset[
                                        pid - bi->m_base_pid ] );
                                give_syslog_a_chance;
                            }

                            (bi->m_histo_event_uncounted)++;
                        }

                    }   // event processing loop...

                    // Only *One* Histogram per Detector Bank (for now)...
                    break;

                }   // histo-based detector bank...

            }   // m_bank_sets loop...

        }   // m_has_histo

        // Collect Event Count Stats for Unmapped & Error Banks...!
        if ( a_bank_id == UNMAPPED_BANK )
            m_run_metrics.events_unmapped += a_event_count;
        else if ( a_bank_id == ERROR_BANK )
            m_run_metrics.events_error += a_event_count;

    }   // Valid Detector Bank ID...

    // Not a Valid Detector Bank ID...
    else
    {
        // Add to Uncounted Events Count...
        m_run_metrics.events_uncounted += a_event_count;

        // Also Add to Special Bank Ids Counts... (REMOVE)
        if ( a_bank_id == UNMAPPED_BANK )
            m_run_metrics.events_unmapped += a_event_count;
        else if ( a_bank_id == ERROR_BANK )
            m_run_metrics.events_error += a_event_count;
    }
}

/*! \brief This method handles pulse gaps for a specified detector bank
 *
 * This method handles pulse gaps in the event stream for the specified
 * detectpr bank. When a gap is detected, the event index for the bank
 * must be corrected for the missing pulses to keep in synchronized with
 * the event stream. If a small gap is detected, values are inserted
 * directly into the internal index buffer; otherwise, gap processing is
 * deferred to the stream adatapter subclass via the bankPulseGap() virtual
 * method. (It is expected that the virtual method should write index
 * values directly into the destination format to prevent excessive memory
 * consumption that would be caused by buffering the corrected index.)
 */
void
StreamParser::handleBankPulseGap
(
    BankInfo &a_bi,     ///< [in] A BankInfo instance with a pulse gap
    uint64_t a_count    ///< [in] The size of the pulse gap
)
{
    // Make Sure Data has been (Late) Initialized...
    if ( !(a_bi.m_initialized) )
        a_bi.initializeBank( false, m_verbose_level );

    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    // (OR, If Working Directory Not Yet Resolved...!)
    if ( a_bi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh
            || !isWorkingDirectoryReady() )
    {
        a_bi.m_index_buffer.resize( a_bi.m_index_buffer.size() + a_count,
            a_bi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large
        //    - flush current buffered data & fill index directly
        // Note: it is acceptable to call bankIndexBuffersReady()
        //    even if the associated buffer is empty.
        bankIndexBuffersReady( a_bi, true );
        bankPulseGap( a_bi, a_count );

        a_bi.m_index_buffer.clear();
    }
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Beam Monitor packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Beam Monitor Event ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Beam Monitor Event packets. The payload
 * of the Monitor Event packet is parsed for neutron events which are
 * then handled by the processMonitorEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BeamMonitorPkt &a_pkt  ///< [in] ADARA BeamMonitorPkt object to process
)
{
    // Ignore duplicate pulses
    if ( a_pkt.flags() & ADARA::DUPLICATE_PULSE )
    {
        // Log Duplicate Pulse if it had Any Events...!
        uint32_t nevents = ( a_pkt.payload_length() / 4 ) - 4;
        if ( nevents )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %u.%09u with %u Events in %s - Ignoring!",
                g_pid, "STC Error:", "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                nevents, "BeamMonitorPkt" );
            give_syslog_a_chance;
        }
        // Else Just Note the Dropped Duplicate Pulse
        // (if for no other reason than diagnostics and testing :-)
        else
        {
            syslog( LOG_INFO,
                "[%i] %s %u.%09u in %s - Ignoring!",
                g_pid, "Duplicate Pulse",
                (uint32_t) a_pkt.timestamp().tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) a_pkt.timestamp().tv_nsec,
                "BeamMonitorPkt" );
            give_syslog_a_chance;
        }
        return false;
    }

    // (Current) Pulse flag should be 0 (no data processed yet)
    // or 1 (event data processed).
    // Any other value indicates an error with SMS packet generation.

    if ( m_pulse_flag == 0 )
    {
        // First packet of new pulse
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag |= 2;
    }
    else if ( m_pulse_flag == 1 ) {
        // "Complete" Pulse, Got Both Some Neutron & Beam Monitor Data...
        m_pulse_flag = 0;
    }
    else {
        // *** No Intervening Banked Event Packet for Last Pulse?!
        // _Assume_ First Packet of _New_ Pulse...
        // - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag = 2;
        // *Don't* Throw Exception Here, Log Profusely & Move On...
        // (No need to throw out baby with the bath water...! ;-)
        syslog( LOG_ERR,
            "[%i] %s %s Received at %u.%09u, %s %s Continuing...",
            g_pid, "STC Error:",
            "Invalid Beam Monitor Packet Sequence",
            (uint32_t) a_pkt.timestamp().tv_sec
                - ADARA::EPICS_EPOCH_OFFSET,
            (uint32_t) a_pkt.timestamp().tv_nsec,
            "No Intervening Banked Event Packet!",
            "(LOST EXPERIMENT DATA?)" );
        give_syslog_a_chance;
    }

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload()
        + a_pkt.payload_length());

    // TODO What do we do with the pulse info from beam monitor packets?
    rpos += 4; // Skip over pulse info

    uint16_t  monitor_id;
    uint32_t event_count;

    // Process events per-bank
    while( rpos < epos )
    {
        monitor_id = *rpos >> 22;
        event_count = *rpos++ & 0x003FFFFF;

        // TODO What do we do with source info from beam monitor packets?
        rpos += 2; // Skip over source info (source ID & tof offset)

        processMonitorEvents( monitor_id, event_count, rpos );
        rpos += event_count;

        // Monitor events are counted as non-events
        m_run_metrics.non_events_counted += event_count;
    }

    return false;
}


/*! \brief This method processes the neutron events for a specific monitor.
 *
 * This method processes incoming neutron events for a specified monitor.
 * The events are read from the packet and placed into internal
 * event buffers (units are converted for event time of flight).
 * When the event buffers are full, they are flushed to a subclassed
 * stream adapter via the now-split dual monitorTOFBuffersReady() and
 * monitorIndexBuffersReady() virtual methods.
 * This method also detects pulse gaps and corrects the event index
 * as required (see handleMonitorPulseGap() method for more details).
 * Note that unlike banked events, monitors events do not contain a
 * pixel ID field (pid) as all events originate from
 * one source (i.e. the monitor).
 */
void
StreamParser::processMonitorEvents
(
    Identifier      a_monitor_id,     ///< [in] Monitor ID of monitor to be processed
    uint32_t        a_event_count,    ///< [in] Number of events contained in stream buffer
    const uint32_t *a_rpos            ///< [in] Stream event buffer read pointer
)
{
    map<Identifier, MonitorInfo*>::iterator imi =
        m_monitors.find( a_monitor_id );
    if ( imi == m_monitors.end() )
    {
        STC::BeamMonitorConfig config;
        getBeamMonitorConfig( a_monitor_id, config );
        std::stringstream ss_new;
        ss_new << "["
            << "format=" << config.format << " ("
            << ( ( config.format & ADARA::EVENT_FORMAT ) ?
                "Event" : "Histo" ) << ")"
            << " tofOffset=" << config.tofOffset
            << " tofMax=" << config.tofMax
            << " tofBin=" << config.tofBin
            << " distance=" << config.distance
            << "]";
        syslog( LOG_INFO, "[%i] %s: Adding New Beam Monitor %u: %s",
            g_pid, "StreamParser::processMonitorEvents()",
            a_monitor_id, ss_new.str().c_str() );
        MonitorInfo *mi = makeMonitorInfo( a_monitor_id,
            m_event_buf_write_thresh, m_anc_buf_write_thresh, config );
        imi = m_monitors.insert( m_monitors.begin(),
            pair<Identifier, MonitorInfo*>( a_monitor_id, mi ) );
    }

    const uint32_t *epos = a_rpos + a_event_count;

    // Histo-based Monitors...
    if ( imi->second->m_config.format == ADARA::HISTO_FORMAT )
    {
        // Process Monitor Events (into Histogram)... :-D

        uint32_t tofbin;
        uint32_t tof;

        while ( a_rpos != epos )
        {
            // ADARA TOF values are in units of 100 ns,
            //    convert to microseconds
            // TOF values is lower 21 bits
            tof = ((*a_rpos++) & 0x1fffff) / 10;

            // Ignore TOF Less than Minimum Offset
            //    and Greater than or Equal to Maximum TOF...
            //    (Non-Inclusive Max...! ;-D)
            if ( tof >= imi->second->m_config.tofOffset
                    && tof < imi->second->m_config.tofMax )
            {
                // Calculate index into Histogram based on TOF...
                tofbin = ( tof - imi->second->m_config.tofOffset )
                    / imi->second->m_config.tofBin;

                // Sanity Test, Just to Be Sure... ;-b
                // (This should never happen, but the logic is confusing.)
                if ( tofbin >= imi->second->m_num_tof_bins - 1 )
                {
                    syslog( LOG_ERR,
                    "[%i] %s %s %u Histogram Error tof=%u index=%u >= %u",
                        g_pid, "STC Error:", "Beam Monitor",
                        imi->second->m_id, tof, tofbin,
                        imi->second->m_num_tof_bins - 1 );
                    give_syslog_a_chance;
                    // Count Uncounted Beam Monitor Events...
                    (imi->second->m_event_uncounted)++;
                    continue;
                }

                // Increment Histogram Time Slot...
                (imi->second->m_data_buffer[tofbin])++;

                // Count Beam Monitor Events for Histogram Mode Too... ;-D
                (imi->second->m_event_count)++;
            }

            // Count Uncounted Beam Monitor Events for Histogram Mode...
            else
                (imi->second->m_event_uncounted)++;
        }
    }

    // Event-based Monitors...
    else
    {
        // Detect gaps in event data and fill event index if present
        if ( imi->second->m_last_pulse_with_data < ( m_pulse_count - 1 ) )
        {
            handleMonitorPulseGap( *imi->second,
                ( m_pulse_count - 1 )
                    - imi->second->m_last_pulse_with_data );
        }

        // Process Monitor Events...

        size_t sz = imi->second->m_tof_buffer_size;

        // *** STC CRITICAL PATH OPTIMIZATION ***
        // *ONLY* Resize Vector if _Not_ Previously Resized...!
        // - Huge Savings for us, because resize() pre-initializes
        // the Values in the vector when resizing...! ;-D
        // - we track our own "in use" vector size in the
        // new "MonitorInfo::m_tof_buffer_size" field... :-D
        // Update: Don't Resize Incrementally with 1000 Razor Blades,
        // Do Exponential Growth (Like This Helps... ;-D)
        if ( sz + a_event_count > imi->second->m_tof_buffer.size() )
        {
            size_t resz;
            // If Buffer Hasn't Yet Been Allocated,
            // Start with 10x What We Need Right Now...
            if ( imi->second->m_tof_buffer.size() == 0 )
            {
                resz = 10 * ( sz + a_event_count );
            }
            // If Buffer Exists But We've Filled It Up,
            // Just Double the Size of the Existing Buffer.
            // (We don't wanna spend our lives resizing... ;-b)
            else
            {
                resz = 2 * ( sz + a_event_count );
            }
            // Don't Blow Over the Event Buffer Write Threshold,
            // Otherwise Limit Buffer Resize to "Just What We Need"...
            // (As Long as Working Directory has been Fully Resolved!)
            if ( isWorkingDirectoryReady()
                    && resz > m_event_buf_write_thresh )
            {
                resz = m_event_buf_write_thresh;
                if ( sz + a_event_count > resz )
                    resz = sz + a_event_count;
            }

            if ( m_verbose_level > 0 ) {
                syslog( LOG_INFO,
                    "[%i] %s: %s=%u %s %s=%lu %s=%u %s=%lu %s=%lu",
                    g_pid, "StreamParser::processMonitorEvents",
                    "MonitorID", a_monitor_id,
                    "Resizing Event Buffers",
                    "m_tof_buffer_size", imi->second->m_tof_buffer_size,
                    "event_count", a_event_count,
                    "m_tof_buffer.size()",
                        imi->second->m_tof_buffer.size(),
                    "resz", resz );
                give_syslog_a_chance;
            }

            // Now Resize the Buffers to this Max Resize Size...
            imi->second->m_tof_buffer.resize( resz, (float) -1.0 );
        }

        float *tof_ptr = &imi->second->m_tof_buffer[sz];

        while ( a_rpos != epos )
        {
            // ADARA TOF values are in units of 100 ns,
            //    convert to microseconds
            // TOF values is lower 21 bits
            *tof_ptr++ = ((*a_rpos++) & 0x1fffff) / 10.0;
        }

        // Manually Track "In Use" Vector Data Buffer Size (see above!)
        imi->second->m_tof_buffer_size += a_event_count;

        // Cache event index until large enough to write
        imi->second->m_index_buffer.push_back(
            imi->second->m_event_count );
        imi->second->m_event_count += a_event_count;
        imi->second->m_last_pulse_with_data = m_pulse_count;

        // Write Monitor TOF and Event Index Buffers Independently
        //    to Enable Chunk Size Override Optimizations...! ;-D

        // Check to see if TOF buffers are ready to write
        // (And is the Working Directory/NeXus Data File
        // Ready for Data...?)
        if ( imi->second->m_tof_buffer_size >= m_event_buf_write_thresh
                && isWorkingDirectoryReady() )
        {
            monitorTOFBuffersReady( *imi->second );
    
#ifdef PARANOID
            resetInUseVector<float>( imi->second->m_tof_buffer,
                imi->second->m_tof_buffer_size );
#endif
            imi->second->m_tof_buffer_size = 0;
        }

        // Check to see if Event Index buffers are ready to write
        // (And is the Working Directory/NeXus Data File
        // Ready for Data...?)
        if ( imi->second->m_index_buffer.size() >= m_anc_buf_write_thresh
                && isWorkingDirectoryReady() )
        {
            monitorIndexBuffersReady( *imi->second, false );
    
            imi->second->m_index_buffer.clear();
        }
    }
}


/*! \brief This method handles pulse gaps for a specified monitor
 *
 * This method handles pulse gaps in the event stream for the specified
 * monitor. When a gap is detected, the event index for the monitor
 * must be corrected for the missing pulses to keep in synchronized
 * with the event stream. If a small gap is detected, values are inserted
 * directly into the internal index buffer; otherwise, gap processing is
 * deferred to the stream adapter subclass via the monitorPulseGap()
 * virtual method. (It is expected that the virtual method should write
 * index values directly into the destination format to prevent excessive
 * memory consumption that would be caused by buffering the corrected
 * index.)
 */
void
StreamParser::handleMonitorPulseGap
(
    MonitorInfo    &a_mi,       ///< [in] A MonitorInfo instance with a pulse gap
    uint64_t        a_count     ///< [in] The size of the pulse gap
)
{
    // Event-based Monitors Only...
    if ( a_mi.m_config.format == ADARA::HISTO_FORMAT )
    {
        return;
    }

    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    // (OR, If Working Directory Not Yet Resolved...!)
    if ( a_mi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh
            || !isWorkingDirectoryReady() )
    {
        a_mi.m_index_buffer.resize( a_mi.m_index_buffer.size() + a_count,
            a_mi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large
        //    - flush current buffered data & fill index directly
        // Note: it is acceptable to call monitorIndexBuffersReady()
        //    even if the associated buffer is empty.
        monitorIndexBuffersReady( a_mi, true );
        monitorPulseGap( a_mi, a_count );

        a_mi.m_index_buffer.clear();
    }
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Run Info packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Run Info ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Run Info packets.
 * The processRunInfo() virtual method is used to communicate
 * run info data to the stream adapter subclass.
 */
bool
StreamParser::rxPacket
(
    const ADARA::RunInfoPkt &a_pkt  ///< [in] ADARA RunInfo pkt to process
)
{
    xmlDocPtr doc = xmlReadMemory(
        a_pkt.info().c_str(), a_pkt.info().length(), 0, 0, 0 );

    if ( doc )
    {
        syslog( LOG_INFO,
            "[%i] %s: Proceed to Parse RunInfo XML - %lu Bytes, [%s]",
            g_pid, "rxPacket(RunInfoPkt)",
            a_pkt.info().length(), a_pkt.info().c_str() );
        give_syslog_a_chance;

        // Temporary RunInfo Holder...
        // (to Enable Duplicate RunInfoPkt Comparisons... ;-D)
        RunInfo tmp_run_info;

        string tag;
        string value;

        try
        {
            for ( xmlNode *node = xmlDocGetRootElement(doc)->children;
                    node; node = node->next )
            {
                tag = (char*)node->name;
                getXmlNodeValue( node, value );

                if ( xmlStrcmp( node->name,
                        (const xmlChar*)"run_number" ) == 0 )
                {
                    tmp_run_info.run_number =
                        boost::lexical_cast<uint32_t>( value );
                }
                else if ( xmlStrcmp( node->name,
                        (const xmlChar*)"proposal_id" ) == 0 )
                {
                    tmp_run_info.proposal_id = value;
                }
                else if ( xmlStrcmp( node->name,
                        (const xmlChar*)"proposal_title" ) == 0 )
                {
                    tmp_run_info.proposal_title = value;
                }
                else if ( xmlStrcmp( node->name,
                        (const xmlChar*)"run_title" ) == 0 )
                {
                    tmp_run_info.run_title = value;
                }
                else if (xmlStrcmp( node->name,
                        (const xmlChar*) "das_version") == 0 )
                {
                    tmp_run_info.das_version = value;
                }
                else if (xmlStrcmp( node->name,
                        (const xmlChar*) "facility_name") == 0 )
                {
                    tmp_run_info.facility_name = value;
                }
                else if (xmlStrcmp( node->name,
                        (const xmlChar*) "no_sample_info") == 0 )
                {
                    tmp_run_info.no_sample_info = true;
                }
                else if (xmlStrcmp( node->name,
                        (const xmlChar*) "save_pixel_map") == 0 )
                {
                    tmp_run_info.save_pixel_map = true;
                }
                else if (xmlStrcmp( node->name,
                        (const xmlChar*) "run_notes_updates_enabled")
                            == 0 )
                {
                    tmp_run_info.run_notes_updates_enabled = true;
                }
                else if ( xmlStrcmp( node->name,
                        (const xmlChar*)"sample" ) == 0 )
                {
                    for ( xmlNode *sample_node = node->children;
                            sample_node; sample_node = sample_node->next )
                    {
                        tag = (char*)sample_node->name;
                        getXmlNodeValue( sample_node, value );

                        if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"id" ) == 0 )
                        {
                            tmp_run_info.sample_id = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"name" ) == 0 )
                        {
                            tmp_run_info.sample_name = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"nature" ) == 0 )
                        {
                            tmp_run_info.sample_nature = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"chemical_formula" ) == 0 )
                        {
                            tmp_run_info.sample_formula = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"mass" ) == 0 )
                        {
                            tmp_run_info.sample_mass =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"mass_units" ) == 0 )
                        {
                            tmp_run_info.sample_mass_units = value;
                        }
                        // TODO Delete When Phased Out... ;-Q
                        // ("Density" is really "Number Density"...!)
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"density" ) == 0 )
                        {
                            tmp_run_info.sample_mass_density =
                                boost::lexical_cast<double>( value );
                        }
                        // TODO Delete When Phased Out... ;-Q
                        // ("Density Units" is really
                        //    "Number Density Units"...!)
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"density_units" ) == 0 )
                        {
                            tmp_run_info.sample_mass_density_units = value;
                        }
                        // TODO Delete Re-Name Leftover When Phased Out...
                        // They Changed Their Minds... Again... ;-Q
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"number_density" ) == 0 )
                        {
                            tmp_run_info.sample_mass_density =
                                boost::lexical_cast<double>( value );
                        }
                        // TODO Delete Re-Name Leftover When Phased Out...
                        // They Changed Their Minds... Again... ;-Q
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"number_density_units" )
                                    == 0 )
                        {
                            tmp_run_info.sample_mass_density_units = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"mass_density" ) == 0 )
                        {
                            tmp_run_info.sample_mass_density =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"mass_density_units" )
                                    == 0 )
                        {
                            tmp_run_info.sample_mass_density_units = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"container_id" ) == 0 )
                        {
                            tmp_run_info.sample_container_id = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                    (const xmlChar*)"container_name" ) == 0
                                // Minor Backwards Compat...
                                || xmlStrcmp( sample_node->name,
                                    (const xmlChar*)"container" ) == 0 )
                        {
                            tmp_run_info.sample_container_name = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"can_indicator" ) == 0 )
                        {
                            tmp_run_info.sample_can_indicator = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"can_barcode" ) == 0 )
                        {
                            tmp_run_info.sample_can_barcode = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"can_name" ) == 0 )
                        {
                            tmp_run_info.sample_can_name = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"can_materials" ) == 0 )
                        {
                            tmp_run_info.sample_can_materials = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"description" ) == 0 )
                        {
                            tmp_run_info.sample_description = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"comments" ) == 0 )
                        {
                            tmp_run_info.sample_comments = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "height_in_container" ) == 0 )
                        {
                            tmp_run_info.sample_height_in_container =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "height_in_container_units" ) == 0 )
                        {
                            tmp_run_info.sample_height_in_container_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "interior_diameter" ) == 0 )
                        {
                            tmp_run_info.sample_interior_diameter =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "interior_diameter_units" ) == 0 )
                        {
                            tmp_run_info.sample_interior_diameter_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"interior_height" ) == 0 )
                        {
                            tmp_run_info.sample_interior_height =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "interior_height_units" ) == 0 )
                        {
                            tmp_run_info.sample_interior_height_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"interior_width" ) == 0 )
                        {
                            tmp_run_info.sample_interior_width =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "interior_width_units" ) == 0 )
                        {
                            tmp_run_info.sample_interior_width_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"interior_depth" ) == 0 )
                        {
                            tmp_run_info.sample_interior_depth =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "interior_depth_units" ) == 0 )
                        {
                            tmp_run_info.sample_interior_depth_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"outer_diameter" ) == 0 )
                        {
                            tmp_run_info.sample_outer_diameter =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "outer_diameter_units" ) == 0 )
                        {
                            tmp_run_info.sample_outer_diameter_units =
                                value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"outer_height" ) == 0 )
                        {
                            tmp_run_info.sample_outer_height =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "outer_height_units" ) == 0 )
                        {
                            tmp_run_info.sample_outer_height_units = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"outer_width" ) == 0 )
                        {
                            tmp_run_info.sample_outer_width =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "outer_width_units" ) == 0 )
                        {
                            tmp_run_info.sample_outer_width_units = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"outer_depth" ) == 0 )
                        {
                            tmp_run_info.sample_outer_depth =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "outer_depth_units" ) == 0 )
                        {
                            tmp_run_info.sample_outer_depth_units = value;
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)"volume_cubic" ) == 0 )
                        {
                            tmp_run_info.sample_volume_cubic =
                                boost::lexical_cast<double>( value );
                        }
                        else if ( xmlStrcmp( sample_node->name,
                                (const xmlChar*)
                                    "volume_cubic_units" ) == 0 )
                        {
                            tmp_run_info.sample_volume_cubic_units = value;
                        }
                    }
                }
                else if ( xmlStrcmp( node->name,
                        (const xmlChar*)"users" ) == 0 )
                {
                    for ( xmlNode *user_node = node->children;
                            user_node; user_node = user_node->next )
                    {
                        if ( xmlStrcmp( user_node->name,
                                (const xmlChar*)"user" ) == 0 )
                        {
                            UserInfo ui;

                            for ( xmlNode *uinfo_node =
                                        user_node->children;
                                    uinfo_node;
                                    uinfo_node = uinfo_node->next )
                            {
                                tag = (char*)uinfo_node->name;
                                getXmlNodeValue( uinfo_node, value );

                                if ( xmlStrcmp( uinfo_node->name,
                                        (const xmlChar*)"id" ) == 0 )
                                {
                                    ui.id = (char*)
                                        uinfo_node->children->content;
                                }
                                else if ( xmlStrcmp( uinfo_node->name,
                                        (const xmlChar*)"name" ) == 0 )
                                {
                                    ui.name = (char*)
                                        uinfo_node->children->content;
                                }
                                else if (xmlStrcmp( uinfo_node->name,
                                        (const xmlChar*)"role" ) == 0 )
                                {
                                    ui.role = (char*)
                                        uinfo_node->children->content;
                                }
                            }

                            tmp_run_info.users.push_back( ui );
                        }
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT,
                "Failed parsing RunInfo packet on tag: " << tag
                    << ", value: " << value << "; " << e.what() )
        }
        catch( ... )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT,
                "Failed parsing RunInfo packet on tag: " << tag
                    << ", value: " << value )
        }

        xmlFreeDoc( doc );

        // First RunInfoPkt...
        if ( !( m_pkt_recvd & (PKT_BIT_RUNINFO) ) )
        {
            syslog( LOG_INFO, "[%i] %s: First RunInfoPkt - %s...",
                g_pid, "rxPacket(RunInfoPkt)", "Utilizing Values" );
            give_syslog_a_chance;

            m_pkt_recvd |= (PKT_BIT_RUNINFO);

            m_run_info = tmp_run_info;
        }

        // Duplicate RunInfoPkt...
        else
        {
            syslog( LOG_INFO, "[%i] %s: Duplicate RunInfoPkt - %s...",
                g_pid, "rxPacket(RunInfoPkt)", "Updating" );
            give_syslog_a_chance;

            updateRunInfo( tmp_run_info );
        }
    }

    else
    {
        syslog( LOG_ERR,
            "[%i] %s %s: Error Parsing RunInfo XML - %lu Bytes, [%s]",
            g_pid, "STC Error:", "rxPacket(RunInfoPkt)",
            a_pkt.info().length(), a_pkt.info().c_str() );
        give_syslog_a_chance;
    }

    if ( m_strict )
    {
        // Verify we received all required fields in Run Info pkt

        string msg;
        if ( !m_run_info.facility_name.size() )
            msg = "Required facility_name missing from RunInfo.";
        else if ( !m_run_info.proposal_id.size() )
            msg = "Required proposal_id missing from RunInfo.";
        else if ( m_run_info.run_number == 0 )
            msg = "Required run_number missing from RunInfo.";

        // *DON'T* Throw Exception Yet Here...
        // - Give it a Chance to be Fixed Later in the Run
        // with an Updated RunInfo...
        // -> But Log It as an Error, so Maybe Someone Will See It in Time!
        if ( msg.size() ) {
            syslog( LOG_ERR,
                "[%i] %s %s: Strictly Missing RunInfo - %s...",
                g_pid, "STC Error:", "rxPacket(RunInfoPkt)", msg.c_str() );
            give_syslog_a_chance;
        }
    }

    if ( !isWorkingDirectoryReady() )
    {
        if ( constructWorkingDirectory() )
            flushAdaraStreamBuffer();
    }

    receivedInfo( RUN_INFO_BIT );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Geometry packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Geometry ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Geometry packets. The processGeometry() virtual method is used to communicate
 * geometry data to the stream adapter subclass.
 */
bool
StreamParser::rxPacket
(
    const ADARA::GeometryPkt &a_pkt     ///< [in] The ADARA Geometry Packet to process
)
{
    processGeometry( a_pkt.info() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Beam Line Info packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Beamline Info ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Beamline Info packets. The processBeamLineInfo() virtual method is used to communicate
 * beamline data to the stream adapter subclass.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BeamlineInfoPkt &a_pkt     ///< [in] The ADARA Beamline Info Packet to process
)
{
    m_beamline_info.target_station_number = a_pkt.targetStationNumber();

    m_beamline_info.instr_id = a_pkt.id();
    m_beamline_info.instr_shortname = a_pkt.shortName();
    m_beamline_info.instr_longname = a_pkt.longName();

    if ( !isWorkingDirectoryReady() )
    {
        if ( constructWorkingDirectory() )
            flushAdaraStreamBuffer();
    }

    receivedInfo( INSTR_INFO_BIT );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Beam Monitor Config packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Beam Monitor Config ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Beam Monitor Config packets,
 * to optionally define Histogramming parameters for
 * processing/accumulating Beam Monitor data.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BeamMonitorConfigPkt &a_pkt     ///< [in] The ADARA Beam Monitor Config Packet to process
)
{
    map<Identifier, MonitorInfo*>::iterator imi;

    MonitorInfo *mi;

    syslog( LOG_INFO,
        "[%i] %s: Beam Monitor Config Received: %u Beam Monitors",
        g_pid, "BeamMonitorConfigPkt", a_pkt.beamMonCount() );
    give_syslog_a_chance;

    // Count Number of Histo vs. Event Beam Monitors,
    // So We Can Clamp to "All" One or the Other... ;-D
    uint32_t numEvent = 0;
    uint32_t numHisto = 0;
    a_pkt.countFormats(numEvent, numHisto);

    // If Mixed Formatting for Beam Monitors, Default to Event Format...
    // (Because You Can Generate Histo from Events, But Not Vice Versa! ;-)
    bool forceEvent = false;
    if ( numEvent > 0 && numHisto > 0 ) {
        syslog( LOG_ERR,
            "[%i] %s %s: %s: %s %s=%u %s=%u - %s",
            g_pid, "STC Error:", "BeamMonitorConfigPkt",
            "Beam Monitor Config Error",
            "Mixed Event and Histo Beam Monitor Formats",
            "numEvent", numEvent, "numHisto", numHisto,
            "Forcing All Beam Monitors to Event Format!" );
        forceEvent = true;
    }

    for (uint32_t i=0 ; i < a_pkt.beamMonCount() ; i++) {

        STC::BeamMonitorConfig config;

        config.id = a_pkt.bmonId(i);

        if ( forceEvent )
            config.format = ADARA::EVENT_FORMAT;
        else
            config.format = a_pkt.format(i);

        config.tofOffset = a_pkt.tofOffset(i);
        config.tofMax = a_pkt.tofMax(i);
        config.tofBin = a_pkt.tofBin(i);

        config.distance = a_pkt.distance(i);

        std::stringstream ss_new;
        ss_new << "["
            << "format=" << config.format << " ("
            << ( ( config.format & ADARA::EVENT_FORMAT ) ?
                "Event" : "Histo" )
            << ( forceEvent ? " [FORCED!]" : "" ) << ")"
            << " tofOffset=" << config.tofOffset
            << " tofMax=" << config.tofMax
            << " tofBin=" << config.tofBin
            << " distance=" << config.distance
            << "]";

        syslog( LOG_INFO, "[%i] %s: %s %u Config: %s",
            g_pid, "BeamMonitorConfigPkt",
            "Beam Monitor", config.id, ss_new.str().c_str() );
        give_syslog_a_chance;

        // Basic Sanity Check...
        if ( config.tofOffset >= config.tofMax ) {
            syslog( LOG_ERR, "[%i] %s %s: %s %u %s: Offset %u >= Max %u",
                g_pid, "STC Error:", "BeamMonitorConfigPkt",
                "Beam Monitor", config.id, "Histogram Config Error",
                config.tofOffset, config.tofMax );
            if ( config.format == ADARA::HISTO_FORMAT ) {
                syslog( LOG_ERR,
                    "[%i] %s %s: Reverting Beam Monitor %u to Event Mode!",
                    g_pid, "STC Error:", "BeamMonitorConfigPkt",
                    config.id );
                give_syslog_a_chance;
                config.format = ADARA::EVENT_FORMAT;
            } else {
                syslog( LOG_WARNING,
                    "[%i] %s: Beam Monitor %u Stays in Event Mode...",
                    g_pid, "BeamMonitorConfigPkt", config.id );
                give_syslog_a_chance;
            }
        }

        // Make Sure Time Bin is > 0 ! (also checked in SMS... :)
        if ( config.tofBin < 1 ) {
            syslog( LOG_ERR,
                "[%i] %s %s: %s %u %s: Time Bin %u < 1 - Setting to 1.",
                g_pid, "STC Error:", "BeamMonitorConfigPkt",
                "Beam Monitor", config.id,
                "Histogram Config Error", config.tofBin );
            give_syslog_a_chance;
            config.tofBin = 1;
        }

        m_monitor_config.push_back(config);

        // Go Ahead & Create Beam Monitor Info for
        // This Expected Beam Monitor's Definition...
        imi = m_monitors.find( config.id );
        if ( imi == m_monitors.end() ) {
            // Add New Monitor...
            syslog( LOG_INFO, "[%i] %s: Adding New Beam Monitor %u: %s",
                g_pid, "BeamMonitorConfigPkt",
                config.id, ss_new.str().c_str() );
            give_syslog_a_chance;
            mi = makeMonitorInfo( config.id,
                m_event_buf_write_thresh, m_anc_buf_write_thresh, config );
            m_monitors.insert( m_monitors.begin(),
                pair<Identifier, MonitorInfo*>( config.id, mi ) );
        }
        else {
            // Duplicate Beam Monitor Found...?
            // Compare Fields...
            if ( imi->second->m_config.format != config.format
                    || imi->second->m_config.tofOffset != config.tofOffset
                    || imi->second->m_config.tofMax != config.tofMax
                    || imi->second->m_config.tofBin != config.tofBin
                    || !approximatelyEqual(
                            imi->second->m_config.distance,
                            config.distance, STC_DOUBLE_EPSILON ) )
            {
                std::stringstream ss_orig;
                ss_orig << "["
                    << "format=" << imi->second->m_config.format << " ("
                    << ( ( imi->second->m_config.format
                            & ADARA::EVENT_FORMAT ) ?
                        "Event" : "Histo" ) << ")"
                    << " tofOffset=" << imi->second->m_config.tofOffset
                    << " tofMax=" << imi->second->m_config.tofMax
                    << " tofBin=" << imi->second->m_config.tofBin
                    << " distance=" << imi->second->m_config.distance
                    << "]";
                syslog( LOG_ERR,
                    "[%i] %s %s: %s %u - %s: New=%s != Orig=%s - %s...!",
                    g_pid, "STC Error:", "BeamMonitorConfigPkt",
                    "Duplicate Beam Monitor", config.id,
                    "Doesn't Match Existing Definition",
                    ss_new.str().c_str(), ss_orig.str().c_str(),
                    "Ignoring" );
                give_syslog_a_chance;
            } else {
                syslog( LOG_ERR, "[%i] %s %s: %s %u - %s: %s - %s...",
                    g_pid, "STC Error:", "BeamMonitorConfigPkt",
                    "Duplicate Beam Monitor", config.id,
                    "Matches Existing Definition", ss_new.str().c_str(),
                    "Ignoring" );
                give_syslog_a_chance;
            }
        }
    }

    return false;
}


/*! \brief This method looks for Any Beam Monitor Config
 *  \return Pointer to element of the BeamMonitorConfig vector or NULL
 *
 * This method looks for a Beam Monitor Config amongst any
 * optionally received prologue information, e.g. to define proper
 * Histogramming parameters for processing/accumulating Beam Monitor data.
 */
void
StreamParser::getBeamMonitorConfig
(
    Identifier a_monitor_id,            ///< [in] Beam Monitor Id (uint32_t)
    STC::BeamMonitorConfig &a_config    ///< [out] Beam Monitor Config
)
{
    // Look for a matching Beam Monitor Id in Any Config...
    bool found = false;
    for ( vector<STC::BeamMonitorConfig>::iterator bmc =
                m_monitor_config.begin();
            bmc != m_monitor_config.end() && found == false ; ++bmc )
    {
        if ( bmc->id == a_monitor_id )
        {
            a_config = *bmc;
            found = true;
        }
    }

    // Beam Monitor Config Not Found - Use Default Config Values...
    if ( found == false )
    {
        a_config.id = a_monitor_id;
        a_config.format = ADARA::EVENT_FORMAT;
        a_config.tofOffset = 0;
        a_config.tofMax = (uint32_t) -1;
        a_config.tofBin = (uint32_t) -1; // "One Big Bin"... ;-D
        a_config.distance = 0.0;

        std::stringstream ss_mon;
        ss_mon << "["
            << "format=" << a_config.format << " ("
            << ( ( a_config.format & ADARA::EVENT_FORMAT ) ?
                "Event" : "Histo" ) << ")"
            << " tofOffset=" << a_config.tofOffset
            << " tofMax=" << a_config.tofMax
            << " tofBin=" << a_config.tofBin
            << " distance=" << a_config.distance
            << "]";
        syslog( LOG_ERR,
            "[%i] %s %s %u %s Beam Monitor Configs[%lu]! %s %s - %s",
            g_pid, "STC Error:", "Beam Monitor", a_monitor_id,
            "Missing from", m_monitor_config.size(),
            "Setting Beam Monitor Config to Defaults",
            ss_mon.str().c_str(),
            "Please Add Missing Beam Monitor to Beamline Config!" );
        give_syslog_a_chance;
    }
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Detector Bank Sets packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Detector Bank Sets ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Detector Bank Sets packets,
 * to optionally define Histogramming & Rate-Throttled Event parameters
 * for processing/accumulating special subsets of Neutron Detector Banks.
 */
bool
StreamParser::rxPacket
(
    const ADARA::DetectorBankSetsPkt &a_pkt     ///< [in] The ADARA Detector Bank Sets Packet to process
)
{
    syslog( LOG_INFO,
        "[%i] Detector Bank Sets Packet Received: %u Detector Bank Sets",
        g_pid, a_pkt.detBankSetCount() );
    give_syslog_a_chance;

    std::vector<uint32_t> banklist;

    for (uint32_t i=0 ; i < a_pkt.detBankSetCount() ; i++) {

        banklist.clear();

        const uint32_t *rawBanklist = a_pkt.banklist(i);
        std::stringstream ss;
        bool first = true;
        ss << "[";
        for (uint32_t b=0 ; b < a_pkt.bankCount(i) ; b++) {
            if ( first ) first = false;
            else ss << ",";
            ss << rawBanklist[b];
            banklist.push_back( rawBanklist[b] );
        }
        ss << "]";

        STC::DetectorBankSet *set = new STC::DetectorBankSet;

        set->name = a_pkt.name(i);
        set->banklist = banklist;
        set->flags = a_pkt.flags(i);
        set->tofOffset = a_pkt.tofOffset(i);
        set->tofMax = a_pkt.tofMax(i);
        set->tofBin = a_pkt.tofBin(i);
        set->throttle = a_pkt.throttle(i);
        set->suffix = a_pkt.suffix(i);

        std::string format;
        if ( set->flags & ADARA::EVENT_FORMAT
                && set->flags & ADARA::HISTO_FORMAT )
            format = "both";
        else if ( set->flags & ADARA::EVENT_FORMAT )
            format = "event";
        else if ( set->flags & ADARA::HISTO_FORMAT )
            format = "histo";
        else
            format = "[None/Unknown!]";  // "None"... (implies *NO DATA*!)

        syslog( LOG_INFO,
        "[%i] %s %s (%u=%s): %s=%u (%s) %s=(%u to %u by %u) %s=%lf (%s).",
            g_pid, "Detector Bank Set", set->name.c_str(),
            a_pkt.bankCount(i), ss.str().c_str(),
            "flags", set->flags, format.c_str(),
            "histo", set->tofOffset, set->tofMax, set->tofBin,
            "throttle", set->throttle, set->suffix.c_str() );
        give_syslog_a_chance;

        // Basic Sanity Check...
        if ( set->tofOffset >= set->tofMax )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s Config Error: Offset %u >= Max %u",
                g_pid, "STC Error:", "Detector Bank Set",
                set->name.c_str(), set->tofOffset, set->tofMax );
            syslog( LOG_ERR,
                "[%i] %s Restricting Detector Bank Set to Event Mode!",
                g_pid, "STC Error:" );
            give_syslog_a_chance;
            set->flags &= !(ADARA::HISTO_FORMAT);
        }

        // Make Sure Time Bin is > 0 ! (also checked in SMS... :)
        if ( set->tofBin < 1 )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s Histogram Config Issue: Time Bin %u < 1",
                g_pid, "STC Error:", "Detector Bank Set",
                set->name.c_str(), set->tofBin );
            give_syslog_a_chance;
            set->tofBin = 1;
        }

        // Associate This Detector Bank Set with Any Existing BankInfo...
        associateDetectorBankSet( set );

        // Save for Posterity
        m_bank_sets.push_back(set);
    }

    return false;
}


/*! \brief This method looks up any Bank Sets for a Neutron Detector
 *  \return Vector of pointers to elements of the DetectorBankSets vector
 *
 * This method looks for a Detector Bank Set amongst
 * any optionally received prologue information, to define proper
 * Histogramming parameters for processing/accumulating Neutron Detector
 * data.
 */
vector<STC::DetectorBankSet *>
StreamParser::getDetectorBankSets
(
    uint32_t a_bank_id    ///< [in] Detector Bank Id (uint32_t)
)
{
    vector<STC::DetectorBankSet *> bank_sets;

    // Any Detector Bank Sets...? (If not, we're done.)
    if (m_bank_sets.size() == 0)
        return(bank_sets); // empty...

    // Look for Detector Bank Sets with matching Detector Bank Ids...
    for ( vector<STC::DetectorBankSet *>::iterator dbs =
                m_bank_sets.begin();
            dbs != m_bank_sets.end() ; ++dbs )
    {
        if ( !*dbs )
            continue;

        for ( vector<uint32_t>::iterator b = (*dbs)->banklist.begin();
                b != (*dbs)->banklist.end(); ++b )
        {
            if ((*b) == a_bank_id)
            {
                syslog( LOG_INFO,
                    "[%i] %s: Bank Id %d Found in \"%s\" Bank Set.",
                    g_pid, "StreamParser::getDetectorBankSets()",
                    a_bank_id, (*dbs)->name.c_str() );
                give_syslog_a_chance;

                bank_sets.push_back(*dbs);
            }
        }
    }

    return(bank_sets);
}


/*! \brief This method associates Detector Bank Set with Any Included Banks
 *
 * This method looks for BankInfo instances which match a Bank Id in the
 * given Detector Bank Set, and append the Set to the vector for the Bank.
 */
void
StreamParser::associateDetectorBankSet
(
    STC::DetectorBankSet *a_bank_set  ///< [in] Detector Bank Set (ptr)
)
{
    // Look for Detector Banks that are Listed in This Set...
    //    - append to given BankInfo Bank Sets vector

    for ( uint32_t i=0 ; i < m_banks_arr_size ; i++ )
    {
        BankInfo *bi = m_banks_arr[i];

        if ( bi == NULL )
            continue;

        for ( vector<uint32_t>::iterator b = a_bank_set->banklist.begin();
                b != a_bank_set->banklist.end(); ++b )
        {
            if ( (*b) == bi->m_id )
            {
                syslog( LOG_INFO,
                    "[%i] %s: Bank Set \"%s\" Associated with Bank Id %d.",
                    g_pid, "StreamParser::associateDetectorBankSet()",
                    a_bank_set->name.c_str(), bi->m_id );
                give_syslog_a_chance;

                bi->m_bank_sets.push_back(a_bank_set);
            }
        }
    }
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Data Done packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method handles the Data Done ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method handles the ADARA Data Done packets.
 * This is the "direct" way of the SMS telling us that
 * there is "No More Data" to stream; much better than using
 * shutdown() for the sending side of the SMS-STC socket,
 * which seems to cause a _total_ socket disconnect here,
 * due to some aspect of the networking setup (firewall?).
 */
bool
StreamParser::rxPacket
(
    const ADARA::DataDonePkt &UNUSED(a_pkt)  ///< [in] The ADARA Data Done Packet to process
)
{
    // Basically, mark the run as "Done"...
    // (tho check for the normal completion status & squawk... :-)
    if ( m_processing_state == DONE_PROCESSING )
    {
        syslog( LOG_INFO, "[%i] Data Done Received (%s = %s).",
            g_pid, "Processing State",
            getProcessingStateString().c_str() );
        give_syslog_a_chance;
    }

    else if ( m_processing_state != DONE_PROCESSING )
    {
        stringstream ss;
        ss << m_run_info.facility_name
            << " " << m_beamline_info.instr_longname
            << " (" << m_beamline_info.instr_shortname << ")"
            << " Run " << m_run_info.run_number
            << " " << m_run_info.proposal_id;

        syslog( LOG_INFO,
            "[%i] STC failed: Data Done Received, %s (%s = %s)! %s",
            g_pid, "Not Done Processing", "Processing State",
            getProcessingStateString().c_str(),
            ss.str().c_str() );
        give_syslog_a_chance;

        if ( m_processing_state == PROCESSING_EVENTS )
        {
            syslog( LOG_INFO,
                "[%i] Data Done Received, %s (%s = %s)!",
                g_pid, "Still Processing Events", "Processing State",
                getProcessingStateString().c_str() );
            give_syslog_a_chance;

            // On fatal error, flush buffers to Nexus before terminating
            markerComment(
                ( m_pulse_info.max_time - m_pulse_info.start_time )
                    / NANO_PER_SECOND_D,
                m_pulse_info.start_time + m_pulse_info.max_time,
                "Stream processing terminated abnormally." );
            m_run_metrics.run_end_time = nsec_to_timespec(
                m_pulse_info.start_time + m_pulse_info.max_time );
            finalizeStreamProcessing();
        }

        THROW_TRACE( ERR_GENERAL_ERROR,
            "ADARA stream ended unexpectedly"
                << " (Processing State = " << getProcessingStateString()
                << ")." )
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Device Descriptor packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Device Descriptor ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Device Descriptor packets. Process variable
 * information is extracted from the XML payload of the packet and used
 * to create type-specific PVInfo instances via the makePVInfo() virtual
 * factory method. (A stream adapter subclass should perform all required
 * format-specific initialization within this factory method.)
 */
bool
StreamParser::rxPacket
(
    const ADARA::DeviceDescriptorPkt &a_pkt     ///< [in] The ADARA Device Descriptor Packet to process
)
{
    const string &xml =  a_pkt.description();

    xmlDocPtr doc = xmlReadMemory( xml.c_str(), xml.length(), 0, 0,
        0 /* XML_PARSE_NOERROR | XML_PARSE_NOWARNING */ );

    if ( doc )
    {
        string              dev_name;
        Identifier          pv_id = 0;
        string              pv_name;
        string              pv_connection;
        string              pv_units;
        bool                pv_ignore;
        PVType              pv_type = PVT_INT;
        std::vector<PVEnumeratedType> *pv_enum_vector = NULL;
        uint32_t            pv_enum_index = (uint32_t) -1;
        short               found;
        string              tag;
        string              value;
        PVInfoBase         *info;

        try
        {
            xmlNode *root = xmlDocGetRootElement( doc );

            for ( xmlNode* lev1 = root->children; lev1 != 0;
                    lev1 = lev1->next )
            {
                if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"device_name" ) == 0 )
                {
                    getXmlNodeValue( lev1, dev_name );
                }

                else if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"process_variables" ) == 0 )
                {
                    for ( xmlNode *pvsnode = lev1->children; pvsnode;
                            pvsnode = pvsnode->next )
                    {
                        if ( xmlStrcmp( pvsnode->name,
                                (const xmlChar*)"process_variable" ) == 0 )
                        {
                            pv_enum_vector = NULL;
                            pv_enum_index = (uint32_t) -1;
                            pv_units = "";
                            pv_ignore = false;
                            found = 0;

                            for ( xmlNode *pvnode = pvsnode->children;
                                    pvnode; pvnode = pvnode->next )
                            {
                                tag = (char*)pvnode->name;
                                getXmlNodeValue( pvnode, value );

                                if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_name" ) == 0 )
                                {
                                    found |= 1;
                                    pv_name = value;
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_connection" )
                                        == 0 )
                                {
                                    found |= 1;
                                    pv_connection = value;
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_id" ) == 0 )
                                {
                                    found |= 2;
                                    pv_id =
                                        boost::lexical_cast<Identifier>(
                                            value );
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_type" ) == 0 )
                                {
                                    found |= 4;
                                    pv_type = toPVType( value.c_str() );

                                    // Match Up Any Enumerated Types!
                                    if ( pv_type == PVT_ENUM )
                                    {
                                        map<Identifier,
                                            std::vector<PVEnumeratedType> >
                                                ::iterator ienum =
                                                    m_enums_by_dev.find(
                                                        a_pkt.devId() );
                                        if ( ienum
                                                != m_enums_by_dev.end() )
                                        {
                                            for ( uint32_t i=0 ;
                                                i < ienum->second.size() ;
                                                i++ )
                                            {
                                                // Capture LAST Enum Match!
                                                // (in case of Name Clash,
                                                // the *Last* One will have
                                                // _Just_ been added for
                                                // the Device we're just
                                                // now parsing... "Lucky"!
                                                if (
                                                    !ienum->second[i].name.
                                                        compare( value ) )
                                                {
                                                    /*
                                                     * Commemorative
                                                     * "Lucky Day" Log! :-D
                                                     * 11/20/2015 - Jeeem
                                                    stringstream ss;
                                                    ss << "STC Error: "
                                                        << "Device "
                                                        << a_pkt.devId()
                                                        << " Enum "
                                                        << value
                                                        << " Found for PV "
                                                        << pv_name
                                                        << "("
                                                        << pv_connection
                                                        << ")"
                                                        << " index="
                                                        << i;
                                                    syslog( LOG_ERR,
                                                        "[%i] %s", g_pid,
                                                        ss.str().c_str() );
                                                    give_syslog_a_chance;
                                                    */

                                                    pv_enum_vector =
                                                       &(ienum->second);
                                                    pv_enum_index = i;
                                                    // *Don't* Stop Search,
                                                    // Want *Last* Match!
                                                }
                                            }
                                        }

                                        // We Didn't Find the Enum Type!
                                        if ( pv_enum_vector == NULL )
                                        {
                                            stringstream ss;
                                            ss << "STC Error: "
                                                << "Device "
                                                << a_pkt.devId()
                                                << " Enum "
                                                << value
                                                << " Not Found for PV "
                                                << pv_name
                                                << "("
                                                << pv_connection
                                                << ")";
                                            syslog( LOG_ERR,
                                                "[%i] %s", g_pid,
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                    }
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_units" ) == 0 )
                                {
                                    pv_units = value;
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_ignore" )
                                            == 0 )
                                {
                                    pv_ignore =
                                        boost::lexical_cast<bool>( value );
                                }
                            }

                            // Found Enough Pieces to Make a Device/PV...!
                            if ( found == 7 )
                            {
                                // Handle Various Name/Connection String
                                // Combinations... ;-D
                                // (Make Sure We Always Have "Both"...)

                                // No Name, Just Connection String...
                                // -> Set Name to Connection String.
                                if ( pv_name.empty()
                                        && !pv_connection.empty() )
                                {
                                    pv_name = pv_connection;
                                }

                                // Just Name, No Connection String...
                                // -> Set Connection String to Name.
                                else if ( !pv_name.empty()
                                        && pv_connection.empty() )
                                {
                                    pv_connection = pv_name;
                                }

                                // Else *Both* Name & Connection String
                                // Were Specified (May or May Not Match!)
                                // (Could be a PV Alias...)

                                // Note: due to the possibility of certain
                                // uncontrollable external events relating
                                // to beam-line re-configuration, it is no
                                // longer possible for ADARA to guarantee
                                // that the name and/or definition of a PV
                                // will remain constant throughout a run.
                                // PV values are stored in name-indexed
                                // logs in the /DASlogs entry of the
                                // generated Nexus file.
                                // IF a previously defined ID-to-PV mapping
                                // changes, and there are no differences
                                // with the PV itself (name, type, units),
                                // then the internal ID-to-name mapping
                                // will be updated. However, if the type
                                // or units of a previously defined PV
                                // ever change, then a new PV must be
                                // dynamically created to prevent an error
                                // in the Nexus output.
                                // To avoid collisions with other PVs, the
                                // name will be algorithmically generated
                                // by appending a numeric suffix (i.e.
                                // "(N)", where N is the next available,
                                // non-conflicting suffix).

                                // Note that PV name ~should~ be unique
                                // within the stream, but currently the
                                // SMS does not perform any checks to
                                // prevent DataSources from publishing
                                // duplicate PV names; thus it will
                                // eventually happen. The STC will try
                                // to handle name collisions without
                                // aborting, but the above behavior that
                                // tries to gracefully handle pvsd restarts
                                // enables a fatal error condition IF the
                                // PV being replaced is actually a
                                // duplicate PV that is still live.
                                // On the next value update of the
                                // replaced PV, the STC will detect that
                                // there is no KEY present for that
                                // variable. This is a ~very~ unlikely
                                // scenario given that there is no use case
                                // for running multiple pvsd instances,
                                // and even if there were, they would have
                                // to be publishing the exact same DDP
                                // content. Therefore, this code will
                                // assume a PV with a new ID but identical
                                // content is due to a pvsd restart
                                // condition.

                                // Also note that the following code favors
                                // the most recently received configuration
                                // data when collisions do occur. If there
                                // is a name conflict, the most recently
                                // received definition will overwrite the
                                // previous definition, and if value
                                // updates are received for the former
                                // over-written definition, the STC will
                                // throw an error.

                                PVKey key(a_pkt.devId(),pv_id);
                                map<PVKey,PVInfoBase*>::iterator ipk =
                                    m_pvs_by_key.find( key );
                                map<string,PVKey>::iterator xref;

                                // This is a NEW Device/PV Key...!
                                if ( ipk == m_pvs_by_key.end() )
                                {
                                    // See if there is an existing
                                    // Name XRef entry (by "dev:name:conn")
                                    // that matches this PV EXACTLY...

                                    bool create = true;
                                    xref = m_pvs_by_name_xref.find(
                                        dev_name + ":" + pv_name
                                            + ":" + pv_connection );

                                    // Ok, We Found the Name XRef Entry
                                    // for this Device/PV...
                                    if ( xref != m_pvs_by_name_xref.end() )
                                    {
                                        // See if this XRef Points to a
                                        // Valid PV-by-Key PVInfo Entry...
                                        ipk = m_pvs_by_key.find(
                                            xref->second );

                                        // Yep, Found a PVInfo Instance...
                                        if ( ipk != m_pvs_by_key.end() )
                                        {
                                            // Does PVInfo Instance Match?
                                            // If So, then the Device/PV
                                            // Key was *Re-Numbered*...!
                                            if ( ipk->second->
                                                sameDefinition( dev_name,
                                                    pv_name,
                                                    pv_connection,
                                                    pv_type,
                                                    pv_enum_vector,
                                                    pv_enum_index,
                                                    pv_units,
                                                    pv_ignore ) )
                                            {
                                                // There is an existing
                                                // PVInfo instance with
                                                // EXACTLY the same defn
                                                // (yet with a Different
                                                // Device/PV Key), i.e.
                                                // it's been *Re-Numbered*.
                                                // This will happen if
                                                // PVSD restarts during
                                                // a recording.

                                                stringstream ss;
                                                ss << "STC Error:"
                                                    << " PV ID Re-Numbered"
                                                    << " [Device "
                                                    << ipk->second
                                                        ->m_device_name
                                                    << ": "
                                                    << ipk->second->m_name
                                                    << " ("
                                                    << ipk->second
                                                        ->m_connection
                                                    << ")]"
                                                    << " devId="
                                                    << ipk->first.first
                                                    << " pvId="
                                                    << ipk->first.second
                                                    << " - Re-Use Existing"
                                                    << " Definition"
                                                    << " With New"
                                                    << " devId="
                                                    << a_pkt.devId()
                                                    << " pvId=" << pv_id;
                                                syslog( LOG_ERR, "[%i] %s",
                                                    g_pid,
                                                    ss.str().c_str() );
                                                give_syslog_a_chance;

                                                // Re-Use this PVInfo Entry
                                                create = false;

                                                // *ADD* Key Mapping from
                                                // New Key to this Existing
                                                // PVInfo Instance Entry...
                                                m_pvs_by_key[key] =
                                                    ipk->second;

                                                // Update Name XRef entry
                                                // to point to New Key...
                                                xref->second = key;

                                                // *Don't* Erase Original
                                                // Key Mapping Tho, Btw.
                                                // Leave It Hanging Around
                                                // for Any Straggling
                                                // Variable Value Updates!

                                                stringstream ss2;
                                                ss2 << "NOTE:"
                                                    << " Still Retaining"
                                                    << " Original Mapping"
                                                    << " to Device "
                                                    << ipk->second
                                                        ->m_device_name
                                                    << ": "
                                                    << ipk->second->m_name
                                                    << " ("
                                                    << ipk->second
                                                        ->m_connection
                                                    << ")"
                                                    << " Under Former Key "
                                                    << ipk->first.first
                                                    << "."
                                                    << ipk->first.second;
                                                syslog( LOG_ERR,
                                                    "[%i] %s(%s): %s",
                                                    g_pid,
                                                    "rxPacket",
                                                    "DeviceDescriptor",
                                                    ss2.str().c_str()
                                                );
                                                give_syslog_a_chance;
                                            }
                                            else
                                            {
                                                // There is an existing
                                                // PVInfo instance with a
                                                // different definition.
                                                // This will happen if
                                                // PVSD is reconfigured
                                                // and restarted during
                                                // a recording.

                                                stringstream ss;
                                                ss << "STC Error:"
                                                    << " PV ID Re-Defined"
                                                    << " - Former"
                                                    << " Definition"
                                                    << " [Device "
                                                    << ipk->second
                                                        ->m_device_name
                                                    << ": "
                                                    << ipk->second->m_name
                                                    << " ("
                                                    << ipk->second
                                                        ->m_connection
                                                    << ")]"
                                                    << " devId="
                                                    << ipk->first.first
                                                    << " pvId="
                                                    << ipk->first.second
                                                    << " Pushed Out of Way"
                                                    << " by New"
                                                    << " [Device "
                                                    << dev_name
                                                    << ": " << pv_name
                                                    << " ("
                                                    << pv_connection
                                                    << ")]"
                                                    << " devId="
                                                    << a_pkt.devId()
                                                    << " pvId=" << pv_id;
                                                syslog( LOG_ERR, "[%i] %s",
                                                    g_pid,
                                                    ss.str().c_str() );
                                                give_syslog_a_chance;

                                                // Existing entry differs;
                                                // *Don't* flush or delete
                                                // old PVInfo entry, tho.
                                                // Save it for later...
                                                // (It could pop back up!)

                                                // Now, Can We Find This
                                                // PV's Definition in List,
                                                // Already in Existence...?

                                                bool found = false;

                                                for ( vector<PVInfoBase*>
                                                    ::iterator ipv =
                                                        m_pvs_list.begin();
                                                    ipv !=
                                                        m_pvs_list.end();
                                                    ++ipv )
                                                {
                                                    if ( (*ipv)->
                                                        sameDefinition(
                                                            dev_name,
                                                            pv_name,
                                                            pv_connection,
                                                            pv_type,
                                                            pv_enum_vector,
                                                            pv_enum_index,
                                                            pv_units,
                                                            pv_ignore ) )
                                                    {
                                                        // Found Match!
                                                        // Use Existing
                                                        // PVInfo Instance
                                                        // for New Key!

                                                        stringstream ss2;
                                                        ss2 << "Found"
                                                            << " Exact"
                                                            << " Match"
                                                            << " With"
                                                            << " Existing"
                                                            << " PVInfo"
                                                            << " - Re-Use"
                                                            << " Device "
                                                            << (*ipv)->
                                                              m_device_name
                                                            << ": "
                                                            << (*ipv)->
                                                                m_name
                                                            << " ("
                                                            << (*ipv)->
                                                               m_connection
                                                            << ")";
                                                        syslog( LOG_ERR,
                                                         "[%i] %s(%s): %s",
                                                            g_pid,
                                                            "rxPacket",
                                                        "DeviceDescriptor",
                                                            ss2.str().
                                                                c_str()
                                                        );
                                                        give_syslog_a_chance;

                                                        // *Reset* Key
                                                        // Mapping from
                                                        // New Key to this
                                                        // Existing PVInfo
                                                        // Instance Entry
                                                        m_pvs_by_key[key] =
                                                            *ipv;

                                                        // Update Name
                                                        // XRef entry
                                                        // to point to
                                                        // New Key...
                                                        xref->second = key;

                                                        // Re-Use this
                                                        // Existing PVInfo
                                                        create = false;

                                                        found = true;

                                                        break;
                                                    }
                                                }

                                                // No Matching PVInfo
                                                // Found, Must Be New
                                                // or Modified PV Defn...
                                                if ( !found )
                                                {
                                                    // Just Update Maps
                                                    // (Remove Old Key/Name
                                                    // References...)
                                                    m_pvs_by_key.erase(
                                                        ipk );
                                                    m_pvs_by_name_xref
                                                        .erase( xref );

                                                    // New PVInfo entry
                                                    // will be created
                                                    // below...
                                                }
                                            }
                                        }
                                        else
                                        {
                                            // This is an error - there
                                            // shouldn't be an xref entry
                                            // without a corresponding
                                            // PVInfo entry.

                                            stringstream ss;
                                            ss << "STC Error:"
                                                << " Internal Error -"
                                                << " Dangling PV"
                                                << " Xref Entry"
                                                << " [Device " << dev_name
                                                << ": " << pv_name
                                                << " (" << pv_connection
                                                << ")]"
                                                << " Without Corresponding"
                                                << " PVInfo Entry"
                                                << " devId="
                                                << a_pkt.devId()
                                                << " pvId=" << pv_id;
                                            syslog( LOG_ERR, "[%i] %s",
                                                g_pid, ss.str().c_str() );
                                            give_syslog_a_chance;

                                            // Delete bad xref entry
                                            m_pvs_by_name_xref.erase(
                                                xref );
                                        }
                                    }

                                    // There's No Name XRef Entry
                                    // for this Device/PV...
                                    // So Must Be Totally New Key,
                                    // No Record Of It Yet...
                                    else
                                    {
                                        // Tho Perhaps Can We Find This
                                        // PV's Definition in List,
                                        // Already in Existence...?

                                        for ( vector<PVInfoBase*>
                                                ::iterator ipv =
                                                    m_pvs_list.begin();
                                                ipv !=
                                                    m_pvs_list.end();
                                                ++ipv )
                                        {
                                            if ( (*ipv)->
                                                sameDefinition(
                                                    dev_name,
                                                    pv_name,
                                                    pv_connection,
                                                    pv_type,
                                                    pv_enum_vector,
                                                    pv_enum_index,
                                                    pv_units,
                                                    pv_ignore ) )
                                            {
                                                // Found Match!
                                                // Use Existing PVInfo
                                                // Instance for New Key!

                                                stringstream ss2;
                                                ss2 << "New Un-Used Key "
                                                    << key.first
                                                    << "."
                                                    << key.second
                                                    << ", But Found"
                                                    << " Exact Match"
                                                    << " With Existing"
                                                    << " PVInfo"
                                                    << " - Re-Use Device "
                                                    << (*ipv)->
                                                      m_device_name
                                                    << ": "
                                                    << (*ipv)->
                                                        m_name
                                                    << " ("
                                                    << (*ipv)->
                                                       m_connection
                                                    << ")";
                                                syslog( LOG_ERR,
                                                    "[%i] %s(%s): %s",
                                                    g_pid,
                                                    "rxPacket",
                                                    "DeviceDescriptor",
                                                    ss2.str().
                                                        c_str()
                                                );
                                                give_syslog_a_chance;

                                                // *Reset* Key
                                                // Mapping from
                                                // New Key to this
                                                // Existing PVInfo
                                                // Instance Entry
                                                m_pvs_by_key[key] =
                                                    *ipv;

                                                // Update Name
                                                // XRef entry
                                                // to point to
                                                // New Key...
                                                m_pvs_by_name_xref[
                                                    dev_name + ":"
                                                        + pv_name
                                                        + ":"
                                                        + pv_connection ]
                                                    = key;

                                                // Re-Use this
                                                // Existing PVInfo
                                                create = false;

                                                break;
                                            }
                                        }
                                    }

                                    // If no existing matching PV was found
                                    // make a new one
                                    if ( create )
                                    {
                                        stringstream ss;
                                        ss << "[Device "
                                            << dev_name
                                            << ": " << pv_name
                                            << " ("
                                            << pv_connection
                                            << ")]"
                                            << " devId="
                                            << a_pkt.devId()
                                            << " pvId=" << pv_id;

                                        // If adapter doesn't support
                                        // PV type, makePVInfo will
                                        // return NULL
                                        info = makePVInfo( dev_name,
                                                pv_name,
                                                pv_connection,
                                                a_pkt.devId(),
                                                pv_id,
                                                pv_type,
                                                pv_enum_vector,
                                                pv_enum_index,
                                                pv_units,
                                                pv_ignore );
                                        if ( info )
                                        {
                                            // REMOVE ME
                                            //syslog( LOG_INFO,
                                                //"[%i] %s %s", g_pid,
                                                //"New PV",
                                                //ss.str().c_str() );
                                            //give_syslog_a_chance;

                                            m_pvs_list.push_back( info );

                                            m_pvs_by_key[key] = info;

                                            m_pvs_by_name_xref[
                                                dev_name + ":" + pv_name
                                                    + ":" + pv_connection ]
                                                = key;

                                            if ( m_verbose_level > 2 ) {
                                                stringstream ss2;
                                                ss2 << "Adding New Key "
                                                    << key.first
                                                    << "."
                                                    << key.second
                                                    << " - Device "
                                                    << info->m_device_name
                                                    << ": "
                                                    << info->m_name
                                                    << " ("
                                                    << info->m_connection
                                                    << ")";
                                                // Note: PV Name and
                                                // Connection String
                                                // Could Have Been
                                                // Modified in
                                                // makePVInfo()...! :-D
                                                syslog( LOG_ERR,
                                                    "[%i] %s(%s): %s",
                                                    g_pid,
                                                    "rxPacket",
                                                    "DeviceDescriptor",
                                                    ss2.str().c_str()
                                                );
                                                give_syslog_a_chance;
                                            }
                                        }
                                        else
                                        {
                                            syslog( LOG_ERR,
                                                "[%i] %s %s", g_pid,
                                                "Failed to Create New PV",
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                        }
                                    }
                                }

                                // PV Key is already in use -
                                // An existing key will be received
                                // here when the SMS re-broadcasts
                                // DDPs at file boundaries (in which
                                // case the definitions will match).
                                // They can also be received if pvsd
                                // is reconfigured (in which case the
                                // definition can be different).
                                // Pvsd tries to keep ID-to-Name
                                // mappings consistent across re-
                                // configurations, but it may
                                // not always be able to.
                                else
                                {
                                    // _Not_ the Same Definition...! Uh-Oh!
                                    if ( !ipk->second->sameDefinition(
                                            dev_name,
                                            pv_name,
                                            pv_connection,
                                            pv_type,
                                            pv_enum_vector,
                                            pv_enum_index,
                                            pv_units,
                                            pv_ignore ) )
                                    {
                                        stringstream ss;
                                        ss << "STC Error:"
                                            << " PV Dev/ID Key Re-Used!"
                                            << " Former Definition"
                                            << " [Device "
                                            << ipk->second->m_device_name
                                            << ": "
                                            << ipk->second->m_name
                                            << " ("
                                            << ipk->second->m_connection
                                            << ")"
                                            << " type="
                                            << ipk->second->m_type
                                            << " units="
                                            << ipk->second->m_units
                                            << " ignore="
                                            << ipk->second->m_ignore
                                            << "]"
                                            << " devId="
                                            << ipk->first.first
                                            << " pvId="
                                            << ipk->first.second
                                            << " Pushed Out of Way"
                                            << " by New"
                                            << " [Device " << dev_name
                                            << ": " << pv_name
                                            << " (" << pv_connection
                                            << ")"
                                            << " type=" << pv_type
                                            << " units=" << pv_units
                                            << " ignore=" << pv_ignore
                                            << "]"
                                            << " devId="
                                            << a_pkt.devId()
                                            << " pvId=" << pv_id;
                                        syslog( LOG_ERR, "[%i] %s",
                                            g_pid,
                                            ss.str().c_str() );
                                        give_syslog_a_chance;

                                        // Did the name change?
                                        // Clean Up the Old XRef Entry...
                                        if ( dev_name !=
                                                ipk->second->m_device_name
                                            || pv_name !=
                                                ipk->second->m_name
                                            || pv_connection !=
                                                ipk->second->m_connection )
                                        {
                                            // Delete existing name-key
                                            // xref entry
                                            xref = m_pvs_by_name_xref.find(
                                                ipk->second->m_device_name
                                                + ":"
                                                + ipk->second->m_name
                                                + ":"
                                                + ipk->second->m_connection
                                            );
                                            if ( xref !=
                                                    m_pvs_by_name_xref
                                                        .end() )
                                            {
                                                m_pvs_by_name_xref.erase(
                                                    xref );
                                            }

                                            // There is nothing the STC
                                            // can do if there is a name
                                            // conflict here, the existing
                                            // name-to-key entry will be
                                            // overwritten below
                                            // (if PV type is supported)
                                        }

                                        // A New PV is Steamrolling Us
                                        // Using Our Device/PV Key...
                                        // New PV definition is different;
                                        // *Don't* flush or delete the
                                        // old PVInfo entry, tho.
                                        // Save it for later...
                                        // (It could pop back up!)

                                        // Now, Can We Find This PV's
                                        // Definition in List,
                                        // Already in Existence...?

                                        bool found = false;

                                        for ( vector<PVInfoBase*>
                                                ::iterator ipv =
                                                    m_pvs_list.begin();
                                                ipv !=
                                                    m_pvs_list.end();
                                                ++ipv )
                                        {
                                            if ( (*ipv)->sameDefinition(
                                                    dev_name,
                                                    pv_name,
                                                    pv_connection,
                                                    pv_type,
                                                    pv_enum_vector,
                                                    pv_enum_index,
                                                    pv_units,
                                                    pv_ignore ) )
                                            {
                                                // Found Match!
                                                // Use Existing PVInfo
                                                // Instance for Re-Used
                                                // Key!

                                                stringstream ss2;
                                                ss2 << "Found Exact Match"
                                                    << " With Existing"
                                                    << " PVInfo"
                                                    << " - Re-Use Device "
                                                    << (*ipv)->
                                                        m_device_name
                                                    << ": "
                                                    << (*ipv)->m_name
                                                    << " ("
                                                    << (*ipv)->
                                                        m_connection
                                                    << ")";
                                                syslog( LOG_ERR,
                                                    "[%i] %s(%s): %s",
                                                    g_pid,
                                                    "rxPacket",
                                                    "DeviceDescriptor",
                                                    ss2.str().c_str()
                                                );
                                                give_syslog_a_chance;

                                                // *Reset* Mapping from
                                                // Re-Used Key to Existing
                                                // PVInfo Instance
                                                m_pvs_by_key[key] = *ipv;
                                                // map[] = always
                                                // overwrites!

                                                // Update Name XRef entry
                                                // to point to this Key...
                                                m_pvs_by_name_xref[
                                                    dev_name + ":"
                                                        + pv_name
                                                        + ":"
                                                        + pv_connection ]
                                                    = key;
                                                // map[] = always
                                                // overwrites!

                                                found = true;

                                                break;
                                            }
                                        }

                                        // No Matching PVInfo Found,
                                        // Must Be New or Modified
                                        // PV Defn...
                                        if ( !found )
                                        {
                                            // Make New PVInfo object...
                                            // If PV type bogus/not
                                            // supported, makePVInfo will
                                            // return NULL...!
                                            // (Else, it should work... :-)
                                            info = makePVInfo( dev_name,
                                                pv_name,
                                                pv_connection,
                                                a_pkt.devId(),
                                                pv_id,
                                                pv_type,
                                                pv_enum_vector,
                                                pv_enum_index,
                                                pv_units,
                                                pv_ignore );
                                            if ( info )
                                            {
                                                m_pvs_list.push_back(
                                                    info );

                                                m_pvs_by_key[key] = info;
                                                // map[] = always
                                                // overwrites!

                                                m_pvs_by_name_xref[
                                                    dev_name + ":"
                                                        + pv_name
                                                        + ":"
                                                        + pv_connection ]
                                                    = key;
                                                // map[] = always
                                                // overwrites!

                                                if ( m_verbose_level > 2 )
                                                {
                                                    stringstream ss2;
                                                    ss2 << "Adding"
                                                        << " Revised Key "
                                                        << key.first
                                                        << "."
                                                        << key.second
                                                        << " - Device "
                                                        << info->
                                                            m_device_name
                                                        << ": "
                                                        << info->m_name
                                                        << " ("
                                                        << info
                                                            ->m_connection
                                                        << ")";
                                                    // Note: PV Name and
                                                    // Connection String
                                                    // Could Have Been
                                                    // Modified in
                                                    // makePVInfo()...! :-D
                                                    syslog( LOG_ERR,
                                                        "[%i] %s(%s): %s",
                                                        g_pid,
                                                        "rxPacket",
                                                        "DeviceDescriptor",
                                                        ss2.str().c_str()
                                                    );
                                                    give_syslog_a_chance;
                                                }
                                            }
                                            else
                                            {
                                                stringstream ss;
                                                ss << "[Device "
                                                    << dev_name
                                                    << ": " << pv_name
                                                    << " ("
                                                    << pv_connection
                                                    << ")]"
                                                    << " devId="
                                                    << a_pkt.devId()
                                                    << " pvId=" << pv_id;

                                                syslog( LOG_ERR,
                                                    "[%i] %s %s %s", g_pid,
                                                    "Failed to Create",
                                                    "New PV",
                                                    ss.str().c_str() );
                                                give_syslog_a_chance;

                                                stringstream ss2;
                                                ss2 << "Erasing Old Key "
                                                    << ipk->first.first
                                                    << "."
                                                    << ipk->first.second
                                                    << " - Device "
                                                    << ipk->second
                                                        ->m_device_name
                                                    << ": "
                                                    << ipk->second->m_name
                                                    << " ("
                                                    << ipk->second
                                                        ->m_connection
                                                    << ")";
                                                syslog( LOG_ERR,
                                                    "[%i] %s(%s): %s",
                                                    g_pid, "rxPacket",
                                                    "DeviceDescriptor",
                                                    ss2.str().c_str()
                                                );
                                                give_syslog_a_chance;

                                                m_pvs_by_key.erase( ipk );
                                            }

                                            // Btw, Just Leave Old PV As Is
                                            // - Any Duplicates will have
                                            // been Handled Already by
                                            // makePVInfo() Above, and
                                            // Either Separated as a
                                            // Name Clash or Will Be Merged
                                        }
                                    }
                                    // Else, Same Definition... That's Ok!
                                    // Just Let It Ride... ;-D
                                }
                            }

                            // Not Enough Pieces Found for Device/PV...
                            else
                            {
                                stringstream ss;
                                ss << "STC Error:"
                                    << " Skipping PV - Missing Fields!"
                                    << " devId=" << a_pkt.devId()
                                    << " pvId=" << pv_id
                                    << " found=0x"
                                    << std::hex << found << std::dec;
                                syslog( LOG_ERR, "[%i] %s", g_pid,
                                    ss.str().c_str() );
                                give_syslog_a_chance;
                            }

                            // Reset PV Name and Connection String...!
                            // (So we start with a clean state there... :-)
                            pv_name.clear();
                            pv_connection.clear();
                        }
                    }
                }

                else if ( xmlStrcmp( lev1->name,
                        (const xmlChar*)"enumerations" ) == 0 )
                {
                    for ( xmlNode *enumsnode = lev1->children; enumsnode;
                            enumsnode = enumsnode->next )
                    {
                        if ( xmlStrcmp( enumsnode->name,
                                (const xmlChar*)"enumeration" ) == 0 )
                        {
                            PVEnumeratedType devEnum;
                            devEnum.name = "(unset)";

                            found = 0;

                            for ( xmlNode *enumnode = enumsnode->children;
                                    enumnode; enumnode = enumnode->next )
                            {
                                tag = (char*)enumnode->name;
                                getXmlNodeValue( enumnode, value );

                                if ( xmlStrcmp( enumnode->name,
                                        (const xmlChar*)"enum_name" )
                                            == 0 )
                                {
                                    found |= 1;
                                    devEnum.name = value;
                                }
                                else if ( xmlStrcmp( enumnode->name,
                                        (const xmlChar*)"enum_element" )
                                            == 0 )
                                {
                                    std::string elem_name = "(unset)";
                                    Identifier elem_value = -1;
                                    short elem_found = 0;

                                    for ( xmlNode *elemnode =
                                                enumnode->children;
                                            elemnode;
                                            elemnode = elemnode->next )
                                    {
                                        tag = (char*)elemnode->name;
                                        getXmlNodeValue( elemnode, value );

                                        if ( xmlStrcmp( elemnode->name,
                                                (const xmlChar*)
                                                    "enum_element_name" )
                                                        == 0 )
                                        {
                                            elem_found |= 1;
                                            elem_name = value;
                                        }
                                        else if ( xmlStrcmp(
                                                elemnode->name,
                                                (const xmlChar*)
                                                    "enum_element_value" )
                                                        == 0 )
                                        {
                                            elem_found |= 2;
                                            elem_value =
                                                boost::lexical_cast<
                                                    Identifier>( value );
                                        }
                                    }

                                    if ( elem_found == 3 )
                                    {
                                        devEnum.element_names.push_back(
                                            elem_name );
                                        devEnum.element_values.push_back(
                                            elem_value );
                                        found |= 2;
                                    }
                                    else
                                    {
                                        stringstream ss;
                                        ss << "STC Error:"
                                            << " Skipping Incomplete"
                                            << " Enum Element"
                                            << " devId=" << a_pkt.devId()
                                            << " enumName=" << devEnum.name
                                            << " elemName=" << elem_name
                                            << " elemValue=" << elem_value;
                                        syslog( LOG_ERR, "[%i] %s", g_pid,
                                            ss.str().c_str() );
                                        give_syslog_a_chance;
                                    }
                                }
                            }

                            if ( found == 3 )
                            {
                                map<Identifier,
                                    std::vector<PVEnumeratedType> >
                                        ::iterator ienum =
                                            m_enums_by_dev.find(
                                                a_pkt.devId() );

                                // First Enum for This Device...
                                if ( ienum == m_enums_by_dev.end() )
                                {
                                    stringstream ss;
                                    ss << "Device " << a_pkt.devId()
                                        << " First Enum " << devEnum.name;
                                    syslog( LOG_INFO, "[%i] %s", g_pid,
                                        ss.str().c_str() );
                                    give_syslog_a_chance;

                                    std::vector<PVEnumeratedType> enumVec;
                                    enumVec.push_back( devEnum );
                                    m_enums_by_dev[ a_pkt.devId() ] =
                                        enumVec;
                                }

                                // Check for Duplicates...
                                // Add Any New Enums
                                else
                                {
                                    bool addEnum = true;

                                    for ( uint32_t i=0 ;
                                            i < ienum->second.size()
                                                && addEnum ;
                                            i++ )
                                    {
                                        if ( ienum->second[i].sameEnum(
                                                &devEnum, true ) )
                                        {
                                            // Don't Log Duplicate Enums
                                            // (DDP is Duplicated at
                                            // _Every_ File Boundary! ;-)
                                            // stringstream ss;
                                            // ss << "STC Error:"
                                                // << " Device "
                                                // << a_pkt.devId()
                                                // << " Duplicate Enum "
                                                // << devEnum.name
                                                // << " Ignoring...";
                                            // syslog( LOG_ERR,
                                                // "[%i] %s", g_pid,
                                                // ss.str().c_str() );
                                            // give_syslog_a_chance;
                                            addEnum = false;
                                        }
                                        else if ( !ienum->second[i].name
                                                .compare( devEnum.name ) )
                                        {
                                            stringstream ss;
                                            ss << "STC Error:"
                                                << " Device "
                                                << a_pkt.devId()
                                                << " Enum " << devEnum.name
                                                << " Name Clash!"
                                                << " (Include Anyway)";
                                            syslog( LOG_ERR,
                                                "[%i] %s", g_pid,
                                                ss.str().c_str() );
                                            give_syslog_a_chance;
                                            // Still Include Enum in File.
                                        }
                                    }

                                    // New Enum, Add It...
                                    if ( addEnum )
                                    {
                                        stringstream ss;
                                        ss << "Device " << a_pkt.devId()
                                            << " Add Enum "
                                            << devEnum.name;
                                        syslog( LOG_INFO, "[%i] %s", g_pid,
                                            ss.str().c_str() );
                                        give_syslog_a_chance;

                                        ienum->second.push_back( devEnum );
                                    }
                                }
                            }
                            else
                            {
                                stringstream ss;
                                ss << "STC Error:"
                                    << " Skipping Incomplete Enum "
                                    << " devId=" << a_pkt.devId()
                                    << " enumName=" << devEnum.name;
                                syslog( LOG_ERR, "[%i] %s", g_pid,
                                    ss.str().c_str() );
                                give_syslog_a_chance;
                            }
                        }
                    }
                }
            }
        }
        catch( TraceException &e )
        {
            RETHROW_TRACE( e,
                "Failed parsing Device Descriptor packet on tag: "
                    << tag << ", value: " << value )
        }
        catch( std::exception &e )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT,
                "Failed parsing Device Descriptor packet on tag: "
                    << tag << ", value: " << value << "; " << e.what() )
        }
        catch( ... )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT,
                "Failed parsing Device Descriptor packet on tag: "
                    << tag << ", value: " << value )
        }

        xmlFreeDoc( doc );
    }
    else
    {
        syslog( LOG_ERR, "[%i] %s %s [%s]", g_pid, "STC Error:",
            "Error Parsing Device Descriptor", xml.c_str() );
        give_syslog_a_chance;
    }

    return false;
}


//------------------------------------------------------------------------
// StreamParser Inline / Template Method Implementations
//------------------------------------------------------------------------

/*! \brief Processes a process variable value update from the input stream.
 *  \param a_device_id - Device ID of process variable
 *  \param a_pv_id - Process variable ID
 *  \param a_value - Value of process variable
 *  \param a_timestamp - Timestamp of value update from stream
 *
 * This method processes value updates for process variables (PVs) from the
 * input ADARA stream. If the PV has been defined by a device descriptor
 * packet (DDP), then an entry will be present in the StreamParser PV
 * container - allowing the specified value to be stored in the associated
 * value buffer of the PV. This buffer will be flushed to the stream
 * adapter when full. Statistics for the PV are also updated when this
 * method is called.
 */
template<class T>
void
StreamParser::pvValueUpdate
(
    Identifier      a_device_id,
    Identifier      a_pv_id,
    T               a_value,
    const timespec &a_timestamp
)
{
    PVKey   key(a_device_id,a_pv_id);

    std::map<PVKey,PVInfoBase*>::iterator ipk = m_pvs_by_key.find(key);
    if ( ipk == m_pvs_by_key.end() )
    {
        THROW_TRACE( ERR_PV_NOT_DEFINED,
            "pvValueUpdate() failed - PV " << a_device_id << "." << a_pv_id
                << " not defined." )
    }

    PVInfo<T> *pvinfo = dynamic_cast<PVInfo<T>*>( ipk->second );
    if ( !pvinfo )
    {
        THROW_TRACE( ERR_CAST_FAILED,
            "pvValueUpdate() failed - PV " << a_device_id << "." << a_pv_id
                << " not of correct type." )
    }

    // Did the PV Value *Change* with This Update...?
    bool value_changed = false;
    bool new_value = false;
    if ( pvinfo->m_last_value_set )
    {
        value_changed =
            !( pvinfo->valuesEqual( pvinfo->m_last_value, a_value ) );
    }
    else new_value = true;

    uint64_t ts_nano = timespec_to_nsec( a_timestamp );

    // Check this update to see if the timestamp is newer than the
    // last time. (m_last_time is initialized to 0, so first real update
    // will be Ok.) This will *Log* PV updates that are at negative time
    // displacements and filter-out duplicate updates caused by
    // SMS file boundary crossings.

    // *** Otherwise, Pass the Data Thru! *** [Change in Behavior! 1/2018.]

    // 1/2018 Note: Because the SMS _Repeats_ the Last Value for
    // Each PV in the Prologue of Each New Local ADARA Stream File,
    // we see Lots of "Duplicate" PV Value Updates for the "Same Time";
    // However, if the PV Value Update Frequency is Sub-Pulse, then
    // we _Still_ Need to Include a given PV Value Update
    // IFF the PV Value has *Changed* since the Last Update...! ;-D

    // Here, Both the PV Time and Value are the Same as the Last Update,
    // So Assume SMS File Boundary Duplicate - Ignore PV Update...
    // (*Don't* Log, Frequent Occurrence...)
    if ( ts_nano == pvinfo->m_last_time
            && pvinfo->m_last_value_set && !value_changed )
    {
        return;
    }

    // Log Value Update if Time Stamp Goes into the Past...! ;-D
    // (But Still Pass Thru Any PV Value Data...!)
    else if ( ts_nano < pvinfo->m_last_time )
    {
        /* Rate-limited logging of tachyon PV update times */
        std::stringstream ss;
        ss << a_device_id << "." << a_pv_id;
        std::string log_info;
        if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                RLL_PV_VALUE_UPDATE_SAWTOOTH, ss.str(),
                60, 10, 100, log_info ) ) {
            std::stringstream ssinfo;
            ssinfo << "devId=" << a_device_id
                << " (" << pvinfo->m_device_name << ")";
            ssinfo << " pvId=" << a_pv_id << " (" << pvinfo->m_name
                << " [" << pvinfo->m_connection << "]" << ")";
            ssinfo << " = " << pvinfo->valueToString( a_value ) << " @";
            syslog( LOG_ERR,
        "[%i] %s %s%s %s %s %lu.%09lu (%s=%lu) < %lu.%09lu (%s=%lu) %s %s",
                g_pid, "STC Error:", log_info.c_str(),
                "StreamParser::pvValueUpdate()",
                "Variable Value Update SAWTOOTH",
                ssinfo.str().c_str(),
                a_timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                a_timestamp.tv_nsec,
                "ts_nano", ts_nano,
                (unsigned long)(pvinfo->m_last_time / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(pvinfo->m_last_time % NANO_PER_SECOND_LL),
                "ts_nano", pvinfo->m_last_time,
                ( ( value_changed ) ? "(Value Changed)"
                    : ( ( new_value ) ? "(New Value)" : "(Same Value)" ) ),
                "- Pass Thru Anyway..."
            );
            give_syslog_a_chance;
        }
    }

    // Now Proceed to Process PV Value Update, Regardless of Errors! ;-D

    // Relative time of update in seconds from first pulse of run.
    // Note: If First Pulse has Not Arrived, Save This Value Update
    // Until Later for Normalization (Set PV Time to -1.0)...
    // (Will Only Save _Last_ of Pre-Pulse PV Value Data...)
    // Otherwise, Truncate All Pre-First-Pulse PV Times to 0.
    double t = 0.0;

    bool save_abs_time = false;

    // First Pulse Has Arrived...
    if ( m_pulse_info.start_time )
    {
        // Positive Time Update, Normalize PV Time to 1st Pulse...
        if ( ts_nano > m_pulse_info.start_time )
        {
            t = ( ts_nano - m_pulse_info.start_time )
                / NANO_PER_SECOND_D;
        }

        // Truncate Any Negative Time Offsets to 0...
        // Only Purge _Initial_ Negative Time Offset PV Values:
        // - Always Leave at Least *One* Pre-First-Pulse Value Tho...!
        // (It Could be the Only Value We Get!)
        // - Otherwise, any incidental Negative Time Offsets
        // could simply be some transient error, pass it thru...
        else
        {
            // For rate-limited logging...
            std::stringstream ss;
            ss << a_device_id << "." << a_pv_id;
            std::string log_info;
            std::stringstream ssinfo;
            ssinfo << "Truncate Negative Variable Value Update";
            ssinfo << " Time to Zero",
            ssinfo << " devId=" << a_device_id
                << " (" << pvinfo->m_device_name << ")";
            ssinfo << " pvId=" << a_pv_id << " (" << pvinfo->m_name
                << " [" << pvinfo->m_connection << "]" << ")";
            ssinfo << " = " << pvinfo->valueToString( a_value ) << " @";

            // If there's Only *One* PV Value before this one,
            // and it's _Also_ a Negative Time Offset, then
            // throw That One away and keep This One instead... ;-D
            if ( pvinfo->m_value_buffer.size() == 1
                    && pvinfo->m_last_time < m_pulse_info.start_time )
            {
                // Verbose Logging Level 1 or Above...
                if ( m_verbose_level > 0 ) {
                    // Rate-limited logging of
                    // truncated negative update times
                    if ( RateLimitedLogging::checkLog(
                            RLLHistory_StreamParserH,
                            RLL_PV_VALUE_UPDATE_NEG_TIME_CLAMP, ss.str(),
                            60, 10, 100, log_info ) ) {
                        // Only Log Negative Time Truncation as Error
                        // After 1st PV Value...
                        // (Otherwise we get spammed for nearly
                        // every PV in the run! ;-D)
                        std::string log_hdr = "";
                        int log_type = LOG_INFO;
                        // Don't Log _Any_ of These as "Errors",
                        // Just Too Much Spam...! ;-b
                        // if ( pvinfo->m_last_value_set ) {
                            // log_type = LOG_ERR;
                            // log_hdr = "STC Error: ";
                        // }
                        std::stringstream ss2;
                        ss2 << "Discard Previous Pre-First-Pulse Value ";
                        ss2 << pvinfo->valueToString(
                            pvinfo->m_value_buffer[0] );
                        ss2 << " @ " << pvinfo->m_last_time;
                        ss2 << " - Keep New Value";
                        ss2 << ": " << ssinfo.str();
                        syslog( log_type,
               "[%i] %s%s%s %s %lu.%09lu (%s=%lu) < %lu.%09lu (%s=%lu) %s",
                            g_pid, log_hdr.c_str(), log_info.c_str(),
                            "StreamParser::pvValueUpdate()",
                            ss2.str().c_str(),
                            a_timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                            a_timestamp.tv_nsec,
                            "ts_nano", ts_nano,
                            (unsigned long)(m_pulse_info.start_time
                                    / NANO_PER_SECOND_LL)
                                - ADARA::EPICS_EPOCH_OFFSET,
                            (unsigned long)(m_pulse_info.start_time
                                % NANO_PER_SECOND_LL),
                            "ts_nano", m_pulse_info.start_time,
                            ( ( value_changed ) ? "(Value Changed)"
                                : ( ( new_value )
                                    ? "(New Value)" : "(Same Value)" ) )
                        );
                        give_syslog_a_chance;
                    }
                }

                // Purge Existing Pre-Pulse PV Data...
                pvinfo->m_value_buffer.clear();
                pvinfo->m_abs_time_buffer.clear();
                pvinfo->m_time_buffer.clear();
                pvinfo->m_stats.reset();
            }

            else
            {
                // Rate-limited logging of truncated negative update times
                if ( RateLimitedLogging::checkLog(
                        RLLHistory_StreamParserH,
                        RLL_PV_VALUE_UPDATE_NEG_TIME_CLAMP, ss.str(),
                        60, 10, 100, log_info ) ) {
                    // Only Log Negative Time Truncation as Error
                    // After 1st PV Value...
                    // (Otherwise we get spammed for nearly
                    // every PV in the run! ;-D)
                    std::string log_hdr = "";
                    int log_type = LOG_INFO;
                    // Don't Log _Any_ of These as "Errors",
                    // Just Too Much Spam...! ;-b
                    // if ( pvinfo->m_last_value_set ) {
                        // log_type = LOG_ERR;
                        // log_hdr = "STC Error: ";
                    // }
                    syslog( log_type,
               "[%i] %s%s%s %s %lu.%09lu (%s=%lu) < %lu.%09lu (%s=%lu) %s",
                        g_pid, log_hdr.c_str(), log_info.c_str(),
                        "StreamParser::pvValueUpdate()",
                        ssinfo.str().c_str(),
                        a_timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                        a_timestamp.tv_nsec,
                        "ts_nano", ts_nano,
                        (unsigned long)(m_pulse_info.start_time
                                / NANO_PER_SECOND_LL)
                            - ADARA::EPICS_EPOCH_OFFSET,
                        (unsigned long)(m_pulse_info.start_time
                            % NANO_PER_SECOND_LL),
                        "ts_nano", m_pulse_info.start_time,
                        ( ( value_changed ) ? "(Value Changed)"
                            : ( ( new_value )
                                ? "(New Value)" : "(Same Value)" ) )
                    );
                    give_syslog_a_chance;
                }
            }
        }
    }

    // No Pulse Has Arrived as Yet, So Save This Value Update Til Later...
    // Set PV Time to -1.0, So We Know It's Not Yet Normalized...
    // (Later, Will Only Save _Last_ of Pre-Pulse PV Value Data...)
    else
    {
        // Verbose Logging Level 1 or Above...
        if ( m_verbose_level > 1 ) {
            /* Rate-limited logging of pre-pulse variable value updates */
            std::stringstream ss;
            ss << a_device_id << "." << a_pv_id;
            std::string log_info;
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_PV_VALUE_UPDATE_EARLY, ss.str(),
                    60, 10, 100, log_info ) ) {
                // If Logging, Include Actual Purged PV Values...
                // (the times have all been zeroed out... ;-)
                std::stringstream ssinfo;
                ssinfo << "devId=" << a_device_id
                    << " (" << pvinfo->m_device_name << ")";
                ssinfo << " pvId=" << a_pv_id << " (" << pvinfo->m_name
                    << " [" << pvinfo->m_connection << "]" << ")";
                syslog( LOG_INFO,
                    "[%i] %s%s %s %s = %s @ %lu.%09lu (%s=%lu) %s",
                    g_pid, log_info.c_str(),
                    "StreamParser::pvValueUpdate()",
                    "Got Pre-Pulse Variable Value Update",
                    ssinfo.str().c_str(),
                    pvinfo->valueToString( a_value ).c_str(),
                    a_timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                    a_timestamp.tv_nsec,
                    "ts_nano", ts_nano,
                    ( ( value_changed ) ? "(Value Changed)"
                        : ( ( new_value )
                            ? "(New Value)" : "(Same Value)" ) )
                );
                give_syslog_a_chance;
            }
        }

        pvinfo->m_has_non_normalized = true;
        save_abs_time = true;
        t = -1.0;
    }

    // Set "Last Value" for PV, It's Ok if Overwritten in flushBuffers()...
    // - if it does get overwritten, it will be the same value... ;-D
    // - sometimes we need to "make sure" the Last Value is Set,
    // like for a "Duplicate" PV that never actually flushes its buffers!
    pvinfo->m_last_time = ts_nano;
    pvinfo->m_last_value = a_value;
    pvinfo->m_last_value_set = true;
    pvinfo->m_value_changed = value_changed;

    pvinfo->m_value_buffer.push_back(a_value);
    if ( save_abs_time ) pvinfo->m_abs_time_buffer.push_back(ts_nano);
    pvinfo->m_time_buffer.push_back(t);

    if ( t >= 0.0 )
        pvinfo->addToStats(a_value);

    // Check for buffer write
    // (And is the Working Directory/NeXus Data File Ready for Data...?)
    if ( pvinfo->m_value_buffer.size() >= m_anc_buf_write_thresh
            && isWorkingDirectoryReady() )
    {
        pvinfo->flushBuffers( m_pulse_info.start_time, 0 );
    }
}

template void StreamParser::pvValueUpdate<uint32_t>(
    Identifier a_device_id, Identifier a_pv_id,
    uint32_t a_value, const timespec &a_timestamp );
template void StreamParser::pvValueUpdate<double>(
    Identifier a_device_id, Identifier a_pv_id,
    double a_value, const timespec &a_timestamp );
template void StreamParser::pvValueUpdate< std::vector<uint32_t> >(
    Identifier a_device_id, Identifier a_pv_id,
    std::vector<uint32_t> a_value, const timespec &a_timestamp );
template void StreamParser::pvValueUpdate< std::vector<double> >(
    Identifier a_device_id, Identifier a_pv_id,
    std::vector<double> a_value, const timespec &a_timestamp );


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (uint32) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes unsigned integer Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA unsigned integer Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableU32Pkt &a_pkt  ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (double) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes double-prec floating point Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA double-precision floating point Variable
 * Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableDoublePkt &a_pkt ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (string) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes character string Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA character string Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableStringPkt &a_pkt ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<string>( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (uint32 array) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes unsigned integer array Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA unsigned integer array Variable Update
 * packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableU32ArrayPkt &a_pkt  ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate< vector<uint32_t> >( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (double array) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes double-prec floating point array Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA double-precision floating point array
 * Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableDoubleArrayPkt &a_pkt ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate< vector<double> >( a_pkt.devId(), a_pkt.varId(),
        a_pkt.value(), a_pkt.timestamp() );

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Multiple Variable (uint32) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes unsigned integer Multiple Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA unsigned integer Multiple Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::MultVariableU32Pkt &a_pkt  ///< [in] The ADARA Multiple Variable Update packet to process
)
{
    vector<uint32_t>::const_iterator it_vals = a_pkt.values().begin();
    vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                g_pid, "STC Error:", "MultVariableU32Pkt()",
                "devId", a_pkt.devId(),
                "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            give_syslog_a_chance;
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts );
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Multiple Variable (double) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes double-prec floating point Multiple Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA double-precision floating point Multiple Variable
 * Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::MultVariableDoublePkt &a_pkt ///< [in] The ADARA Multiple Variable Update packet to process
)
{
    std::vector<double>::const_iterator it_vals = a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                g_pid, "STC Error:", "MultVariableDoublePkt()",
                "devId", a_pkt.devId(),
                "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            give_syslog_a_chance;
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts );
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Multiple Variable (string) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes character string Multiple Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA character string Multiple Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::MultVariableStringPkt &a_pkt ///< [in] The ADARA Multiple Variable Update packet to process
)
{
    std::vector<string>::const_iterator it_vals = a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                g_pid, "STC Error:", "MultVariableStringPkt()",
                "devId", a_pkt.devId(),
                "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            give_syslog_a_chance;
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate<string>( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts );
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Multiple Variable (uint32 array) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes unsigned integer array Multiple Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA unsigned integer array Multiple Variable Update
 * packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::MultVariableU32ArrayPkt &a_pkt  ///< [in] The ADARA Multiple Variable Update packet to process
)
{
    std::vector< vector<uint32_t> >::const_iterator it_vals =
        a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                g_pid, "STC Error:", "MultVariableU32ArrayPkt()",
                "devId", a_pkt.devId(),
                "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            give_syslog_a_chance;
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate< vector<uint32_t> >( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts );
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Multiple Variable (double array) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes double-prec floating point array Multiple Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA double-precision floating point array
 * Multiple Variable Update packets.
 * See the pvValueUpdate() template method in StreamParser.h
 * for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::MultVariableDoubleArrayPkt &a_pkt ///< [in] The ADARA Multiple Variable Update packet to process
)
{
    std::vector< vector<double> >::const_iterator it_vals =
        a_pkt.values().begin();
    std::vector<uint32_t>::const_iterator it_tofs = a_pkt.tofs().begin();

    uint32_t numVals = a_pkt.numValues();

    for ( uint32_t i=0 ; i < numVals ; i++ )
    {
        struct timespec ts = a_pkt.timestamp();

        // Make Sure We Don't Run Out of Values or TOFs... ;-D
        if ( it_vals == a_pkt.values().end()
                || it_tofs == a_pkt.tofs().end() )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s=%u %s=%u %s i=%u/%u at %lu.%09lu",
                g_pid, "STC Error:", "MultVariableDoubleArrayPkt()",
                "devId", a_pkt.devId(),
                "varId", a_pkt.varId(),
                "Premature End of Value or TOF Vector", i, numVals,
                ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET, ts.tv_nsec );
            give_syslog_a_chance;
            return false;
        }

        ts.tv_nsec += *it_tofs++;

        while (ts.tv_nsec >= NANO_PER_SECOND_LL) {
            ts.tv_nsec -= NANO_PER_SECOND_LL;
            ts.tv_sec++;
        }

        pvValueUpdate< vector<double> >( a_pkt.devId(), a_pkt.varId(),
            *it_vals++, ts );
    }

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Stream Annotation packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Stream Marker ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Stream Marker packets.
 */
bool
StreamParser::rxPacket
(
    const ADARA::AnnotationPkt &a_pkt     ///< [in] The ADARA Annotation Packet to process
)
{
    struct timespec timestamp = a_pkt.timestamp();

    uint64_t ts_nano = timespec_to_nsec( timestamp );

    double t = 0.0;

    // First Pulse Has Arrived...
    if ( m_pulse_info.start_time )
    {
        // Positive Time Update, Normalize PV Time to 1st Pulse...
        if ( ts_nano > m_pulse_info.start_time )
        {
            t = ( ts_nano - m_pulse_info.start_time ) / NANO_PER_SECOND_D;
        }

        // Truncate Any Negative Time Offsets to 0...
        // Always Pass Thru Any Annotation Comments, Tho.
        else
        {
            // For rate-limited logging...
            std::stringstream ss;
            ss << "Annot" << a_pkt.marker_type();
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_NEG_TIME_CLAMP, ss.str(),
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
      "[%i] %s %s%s %s %s=%d [%s] %lu.%09lu (%s=%lu) < %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::rxPacket(AnnotationPkt)",
                    "Truncate Negative Annotation Timestamp to Zero",
                    "type", a_pkt.marker_type(),
                    a_pkt.comment().c_str(),
                    timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                    timestamp.tv_nsec,
                    "ts_nano", ts_nano,
                    (unsigned long)(m_pulse_info.start_time
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_pulse_info.start_time
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_pulse_info.start_time );
                give_syslog_a_chance;
            }
        }
    }

    // No Pulse Has Arrived as Yet, So Save This Annotation Til Later...
    // Set Time to -1.0, So We Know It's Not Yet Normalized...
    else
    {
        // For rate-limited logging...
        std::stringstream ss;
        ss << "Annot" << a_pkt.marker_type();
        std::string log_info;
        // Rate-limited logging of truncated negative annotation times
        if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                RLL_ANNOTATION_EARLY, ss.str(),
                60, 10, 100, log_info ) )
        {
            syslog( LOG_ERR,
      "[%i] %s %s%s %s %s=%d [%s] %lu.%09lu (%s=%lu) < %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", log_info.c_str(),
                "StreamParser::rxPacket(AnnotationPkt)",
                "Got Pre-Pulse Annotation Timestamp",
                "type", a_pkt.marker_type(),
                a_pkt.comment().c_str(),
                timestamp.tv_sec - ADARA::EPICS_EPOCH_OFFSET,
                timestamp.tv_nsec,
                "ts_nano", ts_nano,
                (unsigned long)(m_pulse_info.start_time
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_pulse_info.start_time
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_pulse_info.start_time );
            give_syslog_a_chance;
        }

        t = -1.0;
    }

    // Switch on event type
    switch ( a_pkt.marker_type() )
    {
    case ADARA::MarkerType::GENERIC:
        markerComment( t, ts_nano, a_pkt.comment() );
        break;
    case ADARA::MarkerType::PAUSE:
        markerPause( t, ts_nano, a_pkt.comment() );
        break;
    case ADARA::MarkerType::RESUME:
        markerResume( t, ts_nano, a_pkt.comment() );
        break;
    case ADARA::MarkerType::SCAN_START:
        markerScanStart( t, ts_nano, a_pkt.scanIndex(), a_pkt.comment() );
        break;
    case ADARA::MarkerType::SCAN_STOP:
        markerScanStop( t, ts_nano, a_pkt.scanIndex(), a_pkt.comment() );
        break;
    case ADARA::MarkerType::OVERALL_RUN_COMMENT:
        runComment( t, ts_nano, a_pkt.comment() );
        break;
    case ADARA::MarkerType::SYSTEM:
        // Just Log System Comments, Don't Insert Into NeXus...
        syslog( LOG_INFO, "[%i] %s %s %lu.%09lu (%s=%lu) [%s]",
            g_pid, "StreamParser::rxPacket(AnnotationPkt)",
            "System Annotation Comment",
            (unsigned long)(ts_nano / NANO_PER_SECOND_LL)
                - ADARA::EPICS_EPOCH_OFFSET,
            (unsigned long)(ts_nano % NANO_PER_SECOND_LL),
            "ts_nano", ts_nano,
            a_pkt.comment().c_str() );
        give_syslog_a_chance;
        break;
    }

    return false;
}


/*! \brief Inserts a pause marker into Nexus file
 *
 * This method inserts a pause marker into the marker logs of the Nexus file.
 */
void
StreamParser::markerPause
(
    double a_time,              ///< [in] Time associated with marker
    uint64_t a_ts_nano,         ///< [in] Actual Timestamp in Nanoseconds
    const string &a_comment     ///< [in] Comment associated with marker
)
{
    if ( a_time < 0.0 )
        m_pause_has_non_normalized = true;

    // Did the Pause/Resume State *Change* with This Annotation...?
    bool state_changed = false;
    // We have a Last Pause/Resume State...
    if ( m_last_pause_multimap_it != m_pause_multimap.end() )
    {
        if ( m_last_pause_multimap_it->second.second != 1 )
            state_changed = true;
    }
    // Starting Pause/Resume State is Always "Not Paused"...
    else state_changed = true;
    // REMOVEME
    syslog( LOG_ERR, "[%i] %s %s %s 1=[%s] %lu.%09lu (%s=%lu) %s=%d",
        g_pid, "STC Error:", "StreamParser::markerPause()",
        "Note Pause Annotation",
        a_comment.c_str(),
        (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
            - ADARA::EPICS_EPOCH_OFFSET,
        (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
        "ts_nano", a_ts_nano,
        "state_changed", state_changed );
    give_syslog_a_chance;

    // Have We Previously Had A Pause Annotation to Compare Against...?
    if ( m_last_pause_multimap_it != m_pause_multimap.end() )
    {
        // If Timestamp Matches and State has Not Changed,
        // Assume SMS File Boundary Duplicate - Ignore Annotation...
        if ( a_ts_nano == m_last_pause_multimap_it->first
                && !m_last_pause_comment.compare( a_comment )
                && !state_changed )
        {
            syslog( LOG_ERR, "[%i] %s %s %s 1=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerPause()",
                "Ignoring Duplicate Pause Annotation with Identical Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano );
            give_syslog_a_chance;
            return;
        }
        // Log Annotation if Time Stamp Goes into the Past...! ;-D
        // (But Still Pass Thru Annotation...!)
        else if ( a_ts_nano < m_last_pause_multimap_it->first )
        {
            // For rate-limited logging...
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_SAWTOOTH, "Pause",
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
  "[%i] %s %s%s %s 1=[%s] %lu.%09lu (%s=%lu) < %d=[%s] %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::markerPause()",
                    "Pause Annotation SAWTOOTH",
                    a_comment.c_str(),
                    (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                    "ts_nano", a_ts_nano,
                    m_last_pause_multimap_it->second.second,
                    m_last_pause_comment.c_str(),
                    (unsigned long)(m_last_pause_multimap_it->first
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_last_pause_multimap_it->first
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_last_pause_multimap_it->first );
                give_syslog_a_chance;
            }
        }
        // REMOVEME
        /*
        else
        {
            syslog( LOG_ERR,
  "[%i] %s %s %s 1=[%s] %lu.%09lu (%s=%lu) vs. %d=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerPause()",
                "Note Distinct Pause Annotation and/or Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                m_last_pause_multimap_it->second.second,
                m_last_pause_comment.c_str(),
                (unsigned long)(m_last_pause_multimap_it->first
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_last_pause_multimap_it->first
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_last_pause_multimap_it->first );
            give_syslog_a_chance;
        }
        */
    }

    try
    {
        // Current Nexus Pause log calls for 1 to be Used for Pause
        m_last_pause_multimap_it = m_pause_multimap.insert(
            std::pair< uint64_t, std::pair<double, uint16_t> >( a_ts_nano,
                std::pair<double, uint16_t>( a_time, 1 ) ) );

        if ( a_comment.size() )
        {
            m_last_pause_comment = a_comment;
            markerComment( a_time, a_ts_nano, a_comment );
        }
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
StreamParser::markerResume
(
    double a_time,              ///< [in] Time associated with marker
    uint64_t a_ts_nano,         ///< [in] Actual Timestamp in Nanoseconds
    const string &a_comment     ///< [in] Comment associated with marker
)
{
    if ( a_time < 0.0 )
        m_pause_has_non_normalized = true;

    // Did the Pause/Resume State *Change* with This Annotation...?
    bool state_changed = false;
    // We have a Last Pause/Resume State...
    if ( m_last_pause_multimap_it != m_pause_multimap.end() )
    {
        if ( m_last_pause_multimap_it->second.second != 0 )
            state_changed = true;
    }
    // Starting Pause/Resume State is Always "Not Paused"...
    else state_changed = true;
    // REMOVEME
    syslog( LOG_ERR, "[%i] %s %s %s 0=[%s] %lu.%09lu (%s=%lu) %s=%d",
        g_pid, "STC Error:", "StreamParser::markerResume()",
        "Note Pause Annotation",
        a_comment.c_str(),
        (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
            - ADARA::EPICS_EPOCH_OFFSET,
        (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
        "ts_nano", a_ts_nano,
        "state_changed", state_changed );
    give_syslog_a_chance;

    // Have We Previously Had A Pause Annotation to Compare Against...?
    if ( m_last_pause_multimap_it != m_pause_multimap.end() )
    {
        // If Timestamp Matches and State has Not Changed,
        // Assume SMS File Boundary Duplicate - Ignore Annotation...
        if ( a_ts_nano == m_last_pause_multimap_it->first
                && !m_last_pause_comment.compare( a_comment )
                && !state_changed )
        {
            // For now, Just Log It...! ;-D
            syslog( LOG_ERR, "[%i] %s %s %s 0=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerResume()",
                "Ignoring Duplicate Pause Annotation with Identical Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano );
            give_syslog_a_chance;
            return;
        }
        // Log Annotation if Time Stamp Goes into the Past...! ;-D
        // (But Still Pass Thru Annotation...!)
        else if ( a_ts_nano < m_last_pause_multimap_it->first )
        {
            // For rate-limited logging...
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_SAWTOOTH, "Resume",
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
  "[%i] %s %s%s %s 0=[%s] %lu.%09lu (%s=%lu) < %d=[%s] %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::markerResume()",
                    "Resume Annotation SAWTOOTH",
                    a_comment.c_str(),
                    (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                    "ts_nano", a_ts_nano,
                    m_last_pause_multimap_it->second.second,
                    m_last_pause_comment.c_str(),
                    (unsigned long)(m_last_pause_multimap_it->first
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_last_pause_multimap_it->first
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_last_pause_multimap_it->first );
                give_syslog_a_chance;
            }
        }
        // REMOVEME
        /*
        else
        {
            syslog( LOG_ERR,
  "[%i] %s %s %s 0=[%s] %lu.%09lu (%s=%lu) vs. %d=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerResume()",
                "Note Distinct Pause Annotation and/or Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                m_last_pause_multimap_it->second.second,
                m_last_pause_comment.c_str(),
                (unsigned long)(m_last_pause_multimap_it->first
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_last_pause_multimap_it->first
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_last_pause_multimap_it->first );
            give_syslog_a_chance;
        }
        */
    }

    try
    {
        // Current Nexus Pause log calls for 0 to be Used for Resume
        m_last_pause_multimap_it = m_pause_multimap.insert(
            std::pair< uint64_t, std::pair<double, uint16_t> >( a_ts_nano,
                std::pair<double, uint16_t>( a_time, 0 ) ) );

        if ( a_comment.size() )
        {
            m_last_pause_comment = a_comment;
            markerComment( a_time, a_ts_nano, a_comment );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerResume() failed." )
    }
}


/*! \brief Inserts a scan start marker into Nexus file
 *
 * This method inserts a scan start marker into the marker logs
 * of the Nexus file.
 */
void
StreamParser::markerScanStart
(
    double a_time,                      ///< [in] Time associated with marker
    uint64_t a_ts_nano,                 ///< [in] Actual Timestamp in Nanoseconds
    uint32_t a_scan_index,              ///< [in] Scan index associated with scan
    const string &a_comment             ///< [in] Comment associated with scan
)
{
    if ( a_time < 0.0 )
        m_scan_has_non_normalized = true;

    // Did the Scan State *Change* with This Annotation...?
    bool state_changed = false;
    // We have a Last Scan State...
    if ( m_last_scan_multimap_it != m_scan_multimap.end() )
    {
        if ( m_last_scan_multimap_it->second.second != a_scan_index )
            state_changed = true;
    }
    // Starting Scan Index is Always "Zero"...
    else if ( a_scan_index != 0 )
        state_changed = true;
    // REMOVEME
    syslog( LOG_ERR, "[%i] %s %s %s %u=[%s] %lu.%09lu (%s=%lu) %s=%d",
        g_pid, "STC Error:", "StreamParser::markerScanStart()",
        "Note Scan Annotation",
        a_scan_index, a_comment.c_str(),
        (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
            - ADARA::EPICS_EPOCH_OFFSET,
        (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
        "ts_nano", a_ts_nano,
        "state_changed", state_changed );
    give_syslog_a_chance;

    // Have We Previously Had A Scan Annotation to Compare Against...?
    if ( m_last_scan_multimap_it != m_scan_multimap.end() )
    {
        // If Timestamp Matches and State has Not Changed,
        // Assume SMS File Boundary Duplicate - Ignore Annotation...
        if ( a_ts_nano == m_last_scan_multimap_it->first
                && !m_last_scan_comment.compare( a_comment )
                && !state_changed )
        {
            // For now, Just Log It...! ;-D
            syslog( LOG_ERR, "[%i] %s %s %s %u=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerScanStart()",
                "Ignoring Duplicate Scan Annotation with Identical Time",
                a_scan_index, a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano );
            give_syslog_a_chance;
            return;
        }
        // Log Annotation if Time Stamp Goes into the Past...! ;-D
        // (But Still Pass Thru Annotation...!)
        else if ( a_ts_nano < m_last_scan_multimap_it->first )
        {
            // For rate-limited logging...
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_SAWTOOTH, "Start",
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
 "[%i] %s %s%s %s %u=[%s] %lu.%09lu (%s=%lu) < %u=[%s] %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::markerScanStart()",
                    "Scan Start Annotation SAWTOOTH",
                    a_scan_index, a_comment.c_str(),
                    (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                    "ts_nano", a_ts_nano,
                    m_last_scan_multimap_it->second.second,
                    m_last_scan_comment.c_str(),
                    (unsigned long)(m_last_scan_multimap_it->first
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_last_scan_multimap_it->first
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_last_scan_multimap_it->first );
                give_syslog_a_chance;
            }
        }
        // REMOVEME
        /*
        else
        {
            syslog( LOG_ERR,
 "[%i] %s %s %s %u=[%s] %lu.%09lu (%s=%lu) vs. %u=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerScanStart()",
                "Note Distinct Scan Annotation, Index and/or Time",
                a_scan_index, a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                m_last_scan_multimap_it->second.second,
                m_last_scan_comment.c_str(),
                (unsigned long)(m_last_scan_multimap_it->first
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_last_scan_multimap_it->first
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_last_scan_multimap_it->first );
            give_syslog_a_chance;
        }
        */
    }

    try
    {
        m_last_scan_multimap_it = m_scan_multimap.insert(
            std::pair< uint64_t, std::pair<double, uint32_t> >( a_ts_nano,
                std::pair<double, uint32_t>( a_time, a_scan_index ) ) );

        m_run_metrics.scan_stats.push( a_scan_index );

        if ( a_comment.size() )
        {
            m_last_scan_comment = a_comment;
            markerComment( a_time, a_ts_nano, a_comment );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerScanStart() failed." )
    }
}


/*! \brief Inserts a scan stop marker into Nexus file
 *
 * This method inserts a scan stop marker into the marker logs
 * of the Nexus file.
 */
void
StreamParser::markerScanStop
(
    double a_time,                      ///< [in] Time associated with marker
    uint64_t a_ts_nano,                 ///< [in] Actual Timestamp in Nanoseconds
    uint32_t a_scan_index,              ///< [in] Scan index associated with scan
    const string &a_comment             ///< [in] Comment associated with scan
)
{
    if ( a_time < 0.0 )
        m_scan_has_non_normalized = true;

    // Did the Scan State *Change* with This Annotation...?
    bool state_changed = false;
    // We have a Last Scan State...
    if ( m_last_scan_multimap_it != m_scan_multimap.end() )
    {
        // When Stopping a Scan, the Index _Always_ Gets Set to Zero...
        if ( m_last_scan_multimap_it->second.second != 0 )
            state_changed = true;
    }
    // Starting Scan State is Always "Stopped"...
    // REMOVEME
    syslog( LOG_ERR, "[%i] %s %s %s %u(0)=[%s] %lu.%09lu (%s=%lu) %s=%d",
        g_pid, "STC Error:", "StreamParser::markerScanStop()",
        "Note Scan Annotation",
        a_scan_index, a_comment.c_str(),
        (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
            - ADARA::EPICS_EPOCH_OFFSET,
        (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
        "ts_nano", a_ts_nano,
        "state_changed", state_changed );
    give_syslog_a_chance;

    // Have We Previously Had A Scan Annotation to Compare Against...?
    if ( m_last_scan_multimap_it != m_scan_multimap.end() )
    {
        // If Timestamp Matches and State has Not Changed,
        // Assume SMS File Boundary Duplicate - Ignore Annotation...
        if ( a_ts_nano == m_last_scan_multimap_it->first
                && !m_last_scan_comment.compare( a_comment )
                && !state_changed )
        {
            // For now, Just Log It...! ;-D
            syslog( LOG_ERR, "[%i] %s %s %s %u(0)=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerScanStop()",
                "Ignoring Duplicate Scan Annotation with Identical Time",
                a_scan_index, a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano );
            give_syslog_a_chance;
            return;
        }
        // Log Annotation if Time Stamp Goes into the Past...! ;-D
        // (But Still Pass Thru Annotation...!)
        else if ( a_ts_nano < m_last_scan_multimap_it->first )
        {
            // For rate-limited logging...
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_SAWTOOTH, "Stop",
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
                "[%i] %s %s%s %s %u(0)=[%s] %lu.%09lu (%s=%lu) < %u=[%s] %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::markerScanStop()",
                    "Scan Stop Annotation SAWTOOTH",
                    a_scan_index, a_comment.c_str(),
                    (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                    "ts_nano", a_ts_nano,
                    m_last_scan_multimap_it->second.second,
                    m_last_scan_comment.c_str(),
                    (unsigned long)(m_last_scan_multimap_it->first
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_last_scan_multimap_it->first
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_last_scan_multimap_it->first );
                give_syslog_a_chance;
            }
        }
        // REMOVEME
        /*
        else
        {
            syslog( LOG_ERR,
            "[%i] %s %s %s %u(0)=[%s] %lu.%09lu (%s=%lu) vs. %u=[%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerScanStop()",
                "Note Distinct Scan Annotation, Index and/or Time",
                a_scan_index, a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                m_last_scan_multimap_it->second.second,
                m_last_scan_comment.c_str(),
                (unsigned long)(m_last_scan_multimap_it->first
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_last_scan_multimap_it->first
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_last_scan_multimap_it->first );
            give_syslog_a_chance;
        }
        */
    }

    try
    {
        // Current NeXus Scan Log calls for
        // 0 to be used for All Scan Stops.
        m_last_scan_multimap_it = m_scan_multimap.insert(
            std::pair< uint64_t, std::pair<double, uint32_t> >( a_ts_nano,
                std::pair<double, uint32_t>( a_time, 0 ) ) );

        m_run_metrics.scan_stats.push( 0 );

        if ( a_comment.size() )
        {
            m_last_scan_comment = a_comment;
            markerComment( a_time, a_ts_nano, a_comment );
        }
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
StreamParser::markerComment
(
    double a_time,                      ///< [in] Time associated with marker
    uint64_t a_ts_nano,                 ///< [in] Actual Timestamp in Nanoseconds
    const std::string &a_comment        ///< [in] Comment to insert
)
{
    if ( a_time < 0.0 )
        m_comment_has_non_normalized = true;

    // Have We Previously Had A Comment Annotation to Compare Against...?
    if ( m_last_comment_multimap_it != m_comment_multimap.end() )
    {
        // If Timestamp Matches and Comment has Not Changed,
        // Assume SMS File Boundary Duplicate - Ignore Annotation...
        if ( a_ts_nano == m_last_comment_multimap_it->first
                && !m_last_comment_multimap_it->second.second.compare(
                    a_comment ) )
        {
            // For now, Just Log It...! ;-D
            syslog( LOG_ERR, "[%i] %s %s %s [%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerComment()",
                "Ignoring Duplicate Annotation with Identical Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano );
            give_syslog_a_chance;
            return;
        }
        // Log Annotation if Time Stamp Goes into the Past...! ;-D
        // (But Still Pass Thru Annotation...!)
        else if ( a_ts_nano < m_last_comment_multimap_it->first )
        {
            // For rate-limited logging...
            std::string log_info;
            // Rate-limited logging of truncated negative annotation times
            if ( RateLimitedLogging::checkLog( RLLHistory_StreamParserH,
                    RLL_ANNOTATION_SAWTOOTH, "Comment",
                    60, 10, 100, log_info ) )
            {
                syslog( LOG_ERR,
       "[%i] %s %s%s %s [%s] %lu.%09lu (%s=%lu) < [%s] %lu.%09lu (%s=%lu)",
                    g_pid, "STC Error:", log_info.c_str(),
                    "StreamParser::markerComment()",
                    "Comment Annotation SAWTOOTH",
                    a_comment.c_str(),
                    (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                    "ts_nano", a_ts_nano,
                    m_last_comment_multimap_it->second.second.c_str(),
                    (unsigned long)(m_last_comment_multimap_it->first
                            / NANO_PER_SECOND_LL)
                        - ADARA::EPICS_EPOCH_OFFSET,
                    (unsigned long)(m_last_comment_multimap_it->first
                        % NANO_PER_SECOND_LL),
                    "ts_nano", m_last_comment_multimap_it->first );
                give_syslog_a_chance;
            }
        }
        // REMOVEME
        /*
        else
        {
            syslog( LOG_ERR,
       "[%i] %s %s %s [%s] %lu.%09lu (%s=%lu) vs. [%s] %lu.%09lu (%s=%lu)",
                g_pid, "STC Error:", "StreamParser::markerComment()",
                "Note Distinct Annotation and/or Time",
                a_comment.c_str(),
                (unsigned long)(a_ts_nano / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(a_ts_nano % NANO_PER_SECOND_LL),
                "ts_nano", a_ts_nano,
                m_last_comment_multimap_it->second.second.c_str(),
                (unsigned long)(m_last_comment_multimap_it->first
                        / NANO_PER_SECOND_LL)
                    - ADARA::EPICS_EPOCH_OFFSET,
                (unsigned long)(m_last_comment_multimap_it->first
                    % NANO_PER_SECOND_LL),
                "ts_nano", m_last_comment_multimap_it->first );
            give_syslog_a_chance;
        }
        */
    }

    try
    {
        if ( a_comment.size() )
        {
            // Geez, this got complicated fast... Lol... ;-D
            m_last_comment_multimap_it = m_comment_multimap.insert(
                std::pair< uint64_t, std::pair<double, std::string> >(
                    a_ts_nano,
                    std::pair<double, std::string>( a_time, a_comment ) )
            );
        }
    }
    catch( TraceException &e )
    {
        RETHROW_TRACE( e, "markerComment() failed." )
    }
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA support methods
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method handles pules ID processing.
 *
 * This method examines a received pulse ID (parameter) and determines if it represents a new pulse (ID differs from
 * current pulse ID). If so, the pulse count is incremented and the new pulse ID retained. This method is needed to
 * decouple packet ordering dependencies between banked event packets and monitor packets.
 */
/*
void
StreamParser::processPulseID
(
    uint64_t a_pulse_id     ///< [in] Pulse ID extracted from ADARA packet header
)
{
    if ( a_pulse_id != m_pulse_id || !m_pulse_count )
    {
        m_pulse_id = a_pulse_id;
        ++m_pulse_count;
    }
}
*/


/*! \brief Compare and Update RunInfo from Duplicate RunInfoPkt Packets
 *
 * This method compares each Meta-Data Field of a Duplicate incoming
 * RunInfoPkt Packet, and Updates the Saved RunInfo struct Meta-Data,
 * to enable Multiple RunInfo Meta-Data Updates _During_ a Run...! ;-D
 *
 * Log Any Actual Changes to the RunInfo Meta-Data... ;-D
 *
 * (Note that we naturally get *Lots* of Duplicate RunInfoPkt Packets
 * anyway, due to the redundancy at the start of each new SMS Data File.)
 */
void
StreamParser::updateRunInfo( const RunInfo &a_run_info )
{
    // ILLEGAL UPDATE (Meta-Data Used in Preliminary ComBus Messaging!)
    if ( a_run_info.run_number != m_run_info.run_number ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Illegal Attempt to Update RunInfo %s! (%u != %u)",
            g_pid, "STC Error:", "updateRunInfo()", "Run Number",
            a_run_info.run_number, m_run_info.run_number );
        give_syslog_a_chance;
    }
    if ( m_run_info.run_title.compare( a_run_info.run_title ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Run Title",
            m_run_info.run_title.c_str(), a_run_info.run_title.c_str() );
        give_syslog_a_chance;
        m_run_info.run_title = a_run_info.run_title;
    }
    if ( m_run_info.proposal_id.compare( a_run_info.proposal_id ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Proposal Id",
            m_run_info.proposal_id.c_str(),
            a_run_info.proposal_id.c_str() );
        give_syslog_a_chance;
        m_run_info.proposal_id = a_run_info.proposal_id;
    }
    if ( m_run_info.proposal_title.compare( a_run_info.proposal_title ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Proposal Title",
            m_run_info.proposal_title.c_str(),
            a_run_info.proposal_title.c_str() );
        give_syslog_a_chance;
        m_run_info.proposal_title = a_run_info.proposal_title;
    }
    if ( m_run_info.das_version.compare( a_run_info.das_version ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "DAS Version",
            m_run_info.das_version.c_str(),
            a_run_info.das_version.c_str() );
        give_syslog_a_chance;
        m_run_info.das_version = a_run_info.das_version;
    }
    // ILLEGAL UPDATE (Meta-Data Used in Preliminary ComBus Messaging!)
    if ( m_run_info.facility_name.compare( a_run_info.facility_name ) ) {
        syslog( LOG_ERR,
        "[%i] %s %s: Illegal Attempt to Update RunInfo %s! ([%s] != [%s])",
            g_pid, "STC Error:", "updateRunInfo()", "Facility Name",
            m_run_info.facility_name.c_str(),
            a_run_info.facility_name.c_str() );
        give_syslog_a_chance;
    }
    if ( a_run_info.no_sample_info != m_run_info.no_sample_info ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "No Sample Info",
            (m_run_info.no_sample_info) ? "True" : "False",
            (a_run_info.no_sample_info) ? "True" : "False" );
        give_syslog_a_chance;
        m_run_info.no_sample_info = a_run_info.no_sample_info;
    }
    if ( a_run_info.save_pixel_map != m_run_info.save_pixel_map ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Save Pixel Map",
            (m_run_info.save_pixel_map) ? "True" : "False",
            (a_run_info.save_pixel_map) ? "True" : "False" );
        give_syslog_a_chance;
        m_run_info.save_pixel_map = a_run_info.save_pixel_map;
    }
    if ( a_run_info.run_notes_updates_enabled
            != m_run_info.run_notes_updates_enabled ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Run Notes Updates Enabled",
            (m_run_info.run_notes_updates_enabled) ? "True" : "False",
            (a_run_info.run_notes_updates_enabled) ? "True" : "False" );
        give_syslog_a_chance;
        m_run_info.run_notes_updates_enabled =
            a_run_info.run_notes_updates_enabled;
    }
    if ( m_run_info.sample_id.compare( a_run_info.sample_id ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Id",
            m_run_info.sample_id.c_str(), a_run_info.sample_id.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_id = a_run_info.sample_id;
    }
    if ( m_run_info.sample_name.compare( a_run_info.sample_name ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Name",
            m_run_info.sample_name.c_str(),
            a_run_info.sample_name.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_name = a_run_info.sample_name;
    }
    if ( m_run_info.sample_nature.compare( a_run_info.sample_nature ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Nature",
            m_run_info.sample_nature.c_str(),
            a_run_info.sample_nature.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_nature = a_run_info.sample_nature;
    }
    if ( m_run_info.sample_formula.compare( a_run_info.sample_formula ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Formula",
            m_run_info.sample_formula.c_str(),
            a_run_info.sample_formula.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_formula = a_run_info.sample_formula;
    }
    if ( !approximatelyEqual( a_run_info.sample_mass,
            m_run_info.sample_mass, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Mass",
            m_run_info.sample_mass, a_run_info.sample_mass );
        give_syslog_a_chance;
        m_run_info.sample_mass = a_run_info.sample_mass;
    }
    if ( m_run_info.sample_mass_units.compare(
            a_run_info.sample_mass_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Mass Units",
            m_run_info.sample_mass_units.c_str(),
            a_run_info.sample_mass_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_mass_units = a_run_info.sample_mass_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_mass_density,
            m_run_info.sample_mass_density, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Mass Density",
            m_run_info.sample_mass_density,
            a_run_info.sample_mass_density );
        give_syslog_a_chance;
        m_run_info.sample_mass_density = a_run_info.sample_mass_density;
    }
    if ( m_run_info.sample_mass_density_units.compare(
            a_run_info.sample_mass_density_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Mass Density Units",
            m_run_info.sample_mass_density_units.c_str(),
            a_run_info.sample_mass_density_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_mass_density_units =
            a_run_info.sample_mass_density_units;
    }
    if ( m_run_info.sample_container_id.compare(
            a_run_info.sample_container_id ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Container Id",
            m_run_info.sample_container_id.c_str(),
            a_run_info.sample_container_id.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_container_id = a_run_info.sample_container_id;
    }
    if ( m_run_info.sample_container_name.compare(
            a_run_info.sample_container_name ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Container Name",
            m_run_info.sample_container_name.c_str(),
            a_run_info.sample_container_name.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_container_name =
            a_run_info.sample_container_name;
    }
    if ( m_run_info.sample_can_indicator.compare(
            a_run_info.sample_can_indicator ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Can Indicator",
            m_run_info.sample_can_indicator.c_str(),
            a_run_info.sample_can_indicator.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_can_indicator =
            a_run_info.sample_can_indicator;
    }
    if ( m_run_info.sample_can_barcode.compare(
            a_run_info.sample_can_barcode ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Can Barcode",
            m_run_info.sample_can_barcode.c_str(),
            a_run_info.sample_can_barcode.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_can_barcode = a_run_info.sample_can_barcode;
    }
    if ( m_run_info.sample_can_name.compare(
            a_run_info.sample_can_name ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Can Name",
            m_run_info.sample_can_name.c_str(),
            a_run_info.sample_can_name.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_can_name = a_run_info.sample_can_name;
    }
    if ( m_run_info.sample_can_materials.compare(
            a_run_info.sample_can_materials ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Can Materials",
            m_run_info.sample_can_materials.c_str(),
            a_run_info.sample_can_materials.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_can_materials = a_run_info.sample_can_materials;
    }
    if ( m_run_info.sample_description.compare(
            a_run_info.sample_description ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Description",
            m_run_info.sample_description.c_str(),
            a_run_info.sample_description.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_description = a_run_info.sample_description;
    }
    if ( m_run_info.sample_comments.compare(
            a_run_info.sample_comments ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Comments",
            m_run_info.sample_comments.c_str(),
            a_run_info.sample_comments.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_comments = a_run_info.sample_comments;
    }
    if ( !approximatelyEqual( a_run_info.sample_height_in_container,
            m_run_info.sample_height_in_container, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Height In Container",
            m_run_info.sample_height_in_container,
            a_run_info.sample_height_in_container );
        give_syslog_a_chance;
        m_run_info.sample_height_in_container =
            a_run_info.sample_height_in_container;
    }
    if ( m_run_info.sample_height_in_container_units.compare(
            a_run_info.sample_height_in_container_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Height in Container Units",
            m_run_info.sample_height_in_container_units.c_str(),
            a_run_info.sample_height_in_container_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_height_in_container_units =
            a_run_info.sample_height_in_container_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_interior_diameter,
            m_run_info.sample_interior_diameter, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Diameter",
            m_run_info.sample_interior_diameter,
            a_run_info.sample_interior_diameter );
        give_syslog_a_chance;
        m_run_info.sample_interior_diameter =
            a_run_info.sample_interior_diameter;
    }
    if ( m_run_info.sample_interior_diameter_units.compare(
            a_run_info.sample_interior_diameter_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Diameter Units",
            m_run_info.sample_interior_diameter_units.c_str(),
            a_run_info.sample_interior_diameter_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_interior_diameter_units =
            a_run_info.sample_interior_diameter_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_interior_height,
            m_run_info.sample_interior_height, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Height",
            m_run_info.sample_interior_height,
            a_run_info.sample_interior_height );
        give_syslog_a_chance;
        m_run_info.sample_interior_height =
            a_run_info.sample_interior_height;
    }
    if ( m_run_info.sample_interior_height_units.compare(
            a_run_info.sample_interior_height_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Height Units",
            m_run_info.sample_interior_height_units.c_str(),
            a_run_info.sample_interior_height_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_interior_height_units =
            a_run_info.sample_interior_height_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_interior_width,
            m_run_info.sample_interior_width, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Width",
            m_run_info.sample_interior_width,
            a_run_info.sample_interior_width );
        give_syslog_a_chance;
        m_run_info.sample_interior_width =
            a_run_info.sample_interior_width;
    }
    if ( m_run_info.sample_interior_width_units.compare(
            a_run_info.sample_interior_width_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Width Units",
            m_run_info.sample_interior_width_units.c_str(),
            a_run_info.sample_interior_width_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_interior_width_units =
            a_run_info.sample_interior_width_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_interior_depth,
            m_run_info.sample_interior_depth, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Depth",
            m_run_info.sample_interior_depth,
            a_run_info.sample_interior_depth );
        give_syslog_a_chance;
        m_run_info.sample_interior_depth =
            a_run_info.sample_interior_depth;
    }
    if ( m_run_info.sample_interior_depth_units.compare(
            a_run_info.sample_interior_depth_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Interior Depth Units",
            m_run_info.sample_interior_depth_units.c_str(),
            a_run_info.sample_interior_depth_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_interior_depth_units =
            a_run_info.sample_interior_depth_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_outer_diameter,
            m_run_info.sample_outer_diameter, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Outer Diameter",
            m_run_info.sample_outer_diameter,
            a_run_info.sample_outer_diameter );
        give_syslog_a_chance;
        m_run_info.sample_outer_diameter =
            a_run_info.sample_outer_diameter;
    }
    if ( m_run_info.sample_outer_diameter_units.compare(
            a_run_info.sample_outer_diameter_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Outer Diameter Units",
            m_run_info.sample_outer_diameter_units.c_str(),
            a_run_info.sample_outer_diameter_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_outer_diameter_units =
            a_run_info.sample_outer_diameter_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_outer_height,
            m_run_info.sample_outer_height, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Outer Height",
            m_run_info.sample_outer_height,
            a_run_info.sample_outer_height );
        give_syslog_a_chance;
        m_run_info.sample_outer_height = a_run_info.sample_outer_height;
    }
    if ( m_run_info.sample_outer_height_units.compare(
            a_run_info.sample_outer_height_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Outer Height Units",
            m_run_info.sample_outer_height_units.c_str(),
            a_run_info.sample_outer_height_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_outer_height_units =
            a_run_info.sample_outer_height_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_outer_width,
            m_run_info.sample_outer_width, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Outer Width",
            m_run_info.sample_outer_width,
            a_run_info.sample_outer_width );
        give_syslog_a_chance;
        m_run_info.sample_outer_width = a_run_info.sample_outer_width;
    }
    if ( m_run_info.sample_outer_width_units.compare(
            a_run_info.sample_outer_width_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Outer Width Units",
            m_run_info.sample_outer_width_units.c_str(),
            a_run_info.sample_outer_width_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_outer_width_units =
            a_run_info.sample_outer_width_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_outer_depth,
            m_run_info.sample_outer_depth, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Outer Depth",
            m_run_info.sample_outer_depth,
            a_run_info.sample_outer_depth );
        give_syslog_a_chance;
        m_run_info.sample_outer_depth = a_run_info.sample_outer_depth;
    }
    if ( m_run_info.sample_outer_depth_units.compare(
            a_run_info.sample_outer_depth_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Outer Depth Units",
            m_run_info.sample_outer_depth_units.c_str(),
            a_run_info.sample_outer_depth_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_outer_depth_units =
            a_run_info.sample_outer_depth_units;
    }
    if ( !approximatelyEqual( a_run_info.sample_volume_cubic,
            m_run_info.sample_volume_cubic, STC_DOUBLE_EPSILON ) ) {
        syslog( LOG_ERR,
            "[%i] %s %s: Updating RunInfo %s: %.17lg -> %.17lg",
            g_pid, "STC Error:", "updateRunInfo()", "Sample Volume Cubic",
            m_run_info.sample_volume_cubic,
            a_run_info.sample_volume_cubic );
        give_syslog_a_chance;
        m_run_info.sample_volume_cubic = a_run_info.sample_volume_cubic;
    }
    if ( m_run_info.sample_volume_cubic_units.compare(
            a_run_info.sample_volume_cubic_units ) ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: [%s] -> [%s]",
            g_pid, "STC Error:", "updateRunInfo()",
            "Sample Volume Cubic Units",
            m_run_info.sample_volume_cubic_units.c_str(),
            a_run_info.sample_volume_cubic_units.c_str() );
        give_syslog_a_chance;
        m_run_info.sample_volume_cubic_units =
            a_run_info.sample_volume_cubic_units;
    }

    bool update_users = false;
    if ( a_run_info.users.size() != m_run_info.users.size() ) {
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: %lu -> %lu",
            g_pid, "STC Error:", "updateRunInfo()",
            "Users Vector Size Changed",
            m_run_info.users.size(),
            a_run_info.users.size() );
        give_syslog_a_chance;
        update_users = true;
    }
    else {
        for ( uint32_t i=0 ; i < a_run_info.users.size() ; i++ ) {
            if ( m_run_info.users[i].id.compare(
                    a_run_info.users[i].id ) ) {
                syslog( LOG_ERR,
                    "[%i] %s %s: Updating RunInfo %s #%u %s: [%s] -> [%s]",
                    g_pid, "STC Error:", "updateRunInfo()",
                    "User", i, "Id Changed",
                    m_run_info.users[i].id.c_str(),
                    a_run_info.users[i].id.c_str() );
                give_syslog_a_chance;
                update_users = true;
            }
            if ( m_run_info.users[i].name.compare(
                    a_run_info.users[i].name ) ) {
                syslog( LOG_ERR,
                    "[%i] %s %s: Updating RunInfo %s #%u %s: [%s] -> [%s]",
                    g_pid, "STC Error:", "updateRunInfo()",
                    "User", i, "Name Changed",
                    m_run_info.users[i].name.c_str(),
                    a_run_info.users[i].name.c_str() );
                give_syslog_a_chance;
                update_users = true;
            }
            if ( m_run_info.users[i].role.compare(
                    a_run_info.users[i].role ) ) {
                syslog( LOG_ERR,
                    "[%i] %s %s: Updating RunInfo %s #%u %s: [%s] -> [%s]",
                    g_pid, "STC Error:", "updateRunInfo()",
                    "User", i, "Role Changed",
                    m_run_info.users[i].role.c_str(),
                    a_run_info.users[i].role.c_str() );
                give_syslog_a_chance;
                update_users = true;
            }
        }
    }
    if ( update_users ) {
        // Old Users Info...
        std::stringstream ss_old;
        ss_old << "Old Users = [";
        for ( uint32_t i=0 ; i < m_run_info.users.size() ; i++ ) {
            if ( i ) ss_old << ", ";
            ss_old << "(" << m_run_info.users[i].id
                << "/" << m_run_info.users[i].name
                << "/" << m_run_info.users[i].role << ")";
        }
        ss_old << "]";
        // New Users Info...
        std::stringstream ss_new;
        ss_new << "New Users = [";
        for ( uint32_t i=0 ; i < a_run_info.users.size() ; i++ ) {
            if ( i ) ss_new << ", ";
            ss_new << "(" << a_run_info.users[i].id
                << "/" << a_run_info.users[i].name
                << "/" << a_run_info.users[i].role << ")";
        }
        ss_new << "]";
        syslog( LOG_ERR, "[%i] %s %s: Updating RunInfo %s: %s -> %s",
            g_pid, "STC Error:", "updateRunInfo()", "Users",
            ss_old.str().c_str(), ss_new.str().c_str() );
        give_syslog_a_chance;
        m_run_info.users = a_run_info.users;
    }
}


/*! \brief Processes state of received informational packets
 *
 * This method tracks which ADARA informational packets have been received
 * and issues a processBeamlineInfo() call once all packets have
 * been received.
 */
void
StreamParser::receivedInfo( InfoBit a_bit )
{
    m_info_rcvd |= a_bit;
    if ( m_info_rcvd == ALL_INFO_RCVD )
    {
        processBeamlineInfo( m_beamline_info );
        m_info_rcvd |= INFO_SENT;

        syslog( LOG_INFO,
            "[%i] %s: %u, %s: %s:%s, %s: %s, %s: %u", g_pid,
            "Target Station", m_beamline_info.target_station_number,
            "Beamline", m_run_info.facility_name.c_str(),
            m_beamline_info.instr_shortname.c_str(),
            "Proposal", m_run_info.proposal_id.c_str(),
            "Run", m_run_info.run_number );
        give_syslog_a_chance;
    }
}


/*! \brief This method performs final stream processing.
 *
 * This method is called after the internal processing state progresses
 * to "DONE_PROCESSING" and permits a variety of final processing tasks
 * to be performed (primarily flushing data buffers). This method also
 * calls the virtual finalize() method to allow the stream adapter to
 * also perform final output operations.
 */
void
StreamParser::finalizeStreamProcessing()
{
    // We're at the End of the Run Now,
    // So No More Time to Wait for the Working Directory to resolve...!
    // (Hopefully we at least have the Beamline Short Name by now...! ;-D)
    // Force the Creation of "Some" Working Directory and then
    // Try to Proceed to Create and Write a NeXus Data File...! ;-D
    if ( !isWorkingDirectoryReady() )
    {
        if ( constructWorkingDirectory( true,
                "StreamParser::finalizeStreamProcessing()" ) )
        {
            flushAdaraStreamBuffer();
        }

        else
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:", "NxGen::finalizeStreamProcessing()",
                "Failed to Force Construction of Working Directory",
                "EPIC FAIL, This Was Our Last Chance...!! Bailing..." );
            give_syslog_a_chance;
            return;
        }
    }

    // Make Sure the BeamlineInfo has been Recorded/Processed...
    // (In case the Working Directory hadn't been resolved previously...)
    processBeamlineInfo( m_beamline_info, true );

    // NOW Record/Process RunInfo Meta-Data...!
    // - The Run is Over/Stopped, so there can be No More RunInfo Updates!
    processRunInfo( m_run_info, m_strict );

    // Make sure neutron pulses were received
    // Just Log It, Don't Throw Exception... ;-b
    // (it happens all too much and is often annoying...)
    if ( !m_run_metrics.charge_stats.count() )
    {
        if ( m_strict )
        {
            syslog( LOG_ERR,
               "[%i] %s %s. %s: %u, %s: %s:%s, %s: %s, %s: %u, %s %u.%09u",
                g_pid, "STC Error:",
                "No Neutron Pulses Received in Stream",
                "Target Station", m_beamline_info.target_station_number,
                "Beamline", m_run_info.facility_name.c_str(),
                m_beamline_info.instr_shortname.c_str(),
                "Proposal", m_run_info.proposal_id.c_str(),
                "Run", m_run_info.run_number,
                "Using Default Run Start Time as",
                (uint32_t) m_default_run_start_time.tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) m_default_run_start_time.tv_nsec );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_WARNING,
                "[%i] %s. %s: %u, %s: %s:%s, %s: %s, %s: %u, %s %u.%09u",
                g_pid, "No Neutron Pulses Received in Stream",
                "Target Station", m_beamline_info.target_station_number,
                "Beamline", m_run_info.facility_name.c_str(),
                m_beamline_info.instr_shortname.c_str(),
                "Proposal", m_run_info.proposal_id.c_str(),
                "Run", m_run_info.run_number,
                "Using Default Run Start Time as",
                (uint32_t) m_default_run_start_time.tv_sec
                    - ADARA::EPICS_EPOCH_OFFSET,
                (uint32_t) m_default_run_start_time.tv_nsec );
            give_syslog_a_chance;
        }

        // Make Sure We Have "Reasonable" Run Time Values... (Fake It!)
        struct timespec run_end_time;
        clock_gettime( CLOCK_REALTIME, &run_end_time );
        m_run_metrics.run_start_time = m_default_run_start_time;
        m_run_metrics.run_end_time = run_end_time;
    }

    // Write remaining pulse info and statistics
    // (*Must* Do This _Before_ Detector Banks and Beam Monitors,
    //    as they "Borrow" the Pulse Time series possibly created herein,
    //    via various makeLink() calls...)
    //
    // Call pulseBuffersReady() even if m_pulse_info.times.size() == 0,
    //    as we _Always_ need _Some_ Pulse Time dataset for linking...!

    pulseBuffersReady( m_pulse_info );

    // Write any remaining data in bank buffers

    for ( uint32_t i=0 ; i < m_banks_arr_size ; i++ )
    {
        BankInfo *bi = m_banks_arr[i];

        if ( bi == NULL ) {
            syslog( LOG_WARNING,
              "[%i] %s: %s Bank ID %u State %u (i=%u)",
                g_pid, "StreamParser::finalizeStreamProcessing()",
                "Can't Finalize Empty Detector",
                ( i - NUM_SPECIAL_BANKS ) % ( m_maxBank + 1 ), // bank_id
                ( i - NUM_SPECIAL_BANKS ) / ( m_maxBank + 1 ), // state
                i );
            give_syslog_a_chance;
            continue;
        }

        syslog( LOG_INFO,
            "[%i] %s: Finalizing Detector Bank ID %u State %u",
            g_pid, "StreamParser::finalizeStreamProcessing()",
            bi->m_id, bi->m_state );
        give_syslog_a_chance;

        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( true, m_verbose_level );

        // Detect gaps in bank data and fill event index if present
        if ( bi->m_last_pulse_with_data < m_pulse_count )
        {
            handleBankPulseGap( *bi,
                m_pulse_count - bi->m_last_pulse_with_data );
        }

        // Write Bank Pid/TOF and Event Index Buffers Independently
        //    to Enable Chunk Size Override Optimizations...! ;-D

        // _Always_ Flush Pid/TOF bank buffers in the end,
        //    to create any Dummy/Empty Datasets...
        bankPidTOFBuffersReady( *bi );

        // _Always_ Flush Event Index bank buffers in the end,
        //    to create any Dummy/Empty Datasets...
        bankIndexBuffersReady( *bi, false );

        bankFinalize( *bi );
    }

    // Write any remaining data in monitor buffers

    for ( map<Identifier, MonitorInfo*>::iterator imi = m_monitors.begin();
            imi != m_monitors.end(); ++imi )
    {
        std::stringstream ss_mon;
        ss_mon << "["
            << "format=" << imi->second->m_config.format << " ("
            << ( ( imi->second->m_config.format & ADARA::EVENT_FORMAT ) ?
                "Event" : "Histo" ) << ")"
            << " tofOffset=" << imi->second->m_config.tofOffset
            << " tofMax=" << imi->second->m_config.tofMax
            << " tofBin=" << imi->second->m_config.tofBin
            << " distance=" << imi->second->m_config.distance
            << "]";
        syslog( LOG_INFO,
            "[%i] %s: Finalizing Beam Monitor %u: %s",
            g_pid, "StreamParser::finalizeStreamProcessing()",
            imi->second->m_id, ss_mon.str().c_str() );
        give_syslog_a_chance;

        // Event-based Monitors Only...
        if ( imi->second->m_config.format == ADARA::EVENT_FORMAT )
        {
            // Detect gaps in monitor data and fill event index if present
            if ( imi->second->m_last_pulse_with_data < m_pulse_count )
            {
                handleMonitorPulseGap( *imi->second,
                    m_pulse_count - imi->second->m_last_pulse_with_data );
            }

            // Write Monitor TOF and Event Index Buffers Independently
            //    to Enable Chunk Size Override Optimizations...! ;-D

            // _Always_ Flush TOF monitor buffers in the end,
            //    to create any Dummy/Empty Datasets...
            monitorTOFBuffersReady( *imi->second );

            // _Always_ Flush Event Index monitor buffers in the end,
            //    to create any Dummy/Empty Datasets...
            monitorIndexBuffersReady( *imi->second, false );
        }

        // All Beam Monitors...
        monitorFinalize( *imi->second );
    }

    // Reconcile Any Duplicate PV Connection Logs...
    collapseDuplicatePVs();

    // Write any Enumerated Types, per DeviceId, into logs...

    for ( map<Identifier,std::vector<PVEnumeratedType> >::iterator ienum =
                m_enums_by_dev.begin();
            ienum != m_enums_by_dev.end(); ++ienum )
    {
        writeDeviceEnums( ienum->first, ienum->second );
    }

    // Write Any Remaining Data in PV Buffers

    for ( vector<PVInfoBase*>::iterator ipv = m_pvs_list.begin();
            ipv != m_pvs_list.end(); ++ipv )
    {
        // *Always* Flush Buffers at End, to Get Run Metrics for PVs...!
        // (Use Guaranteed-Set Run Metrics Time (convert to nanoseconds).)
        (*ipv)->flushBuffers(
            timespec_to_nsec( m_run_metrics.run_start_time ),
            &m_run_metrics );
    }

    // Write Any Conditional STC Config Group Elements,
    // Now that All the PV Buffers have been Processed,
    // and Any Conditional Flags have been Set... :-D

    for ( vector<PVInfoBase*>::iterator ipv = m_pvs_list.begin();
            ipv != m_pvs_list.end(); ++ipv )
    {
        (*ipv)->createSTCConfigConditionalGroups();
    }

    // Now Globally Write Any Captured PV Units Attributes,
    // Using All Saved "Units Paths" String Sets & Units Values,
    // Now That All PV Values have been Processed...
    checkSTCConfigElementUnitsPaths();

    // Let adapter do anything else it wants to
    finalize( m_run_metrics, m_run_info );
}


/*! \brief This method tries to Merge/Collapse Duplicate PV Logs...!
 *
 * This method is called just prior to actually writing out all the
 * final PV Logs into DASlogs, and tries to Reconcile Any Duplicate PVs
 * (i.e. PVs with the _Same_ Connection Information).
 * We try to Merge Differing Sections that hopefully Don't Overlap in Time,
 * thereby Collapsing Away Any Duplicate PV Logs...! ;-D
 */
void
StreamParser::collapseDuplicatePVs()
{
    PVInfo<uint32_t> *pvinfoU32, *pvinfoDupU32;
    PVInfo<double> *pvinfoDbl, *pvinfoDupDbl;
    PVInfo<string> *pvinfoStr, *pvinfoDupStr;
    PVInfo< vector<uint32_t> > *pvinfoU32Arr, *pvinfoDupU32Arr;
    PVInfo< vector<double> > *pvinfoDblArr, *pvinfoDupDblArr;

    uint64_t start_time = timespec_to_nsec( m_run_metrics.run_start_time );

    std::string cast_fail = "collapseDuplicatePVs()";
    cast_fail += " Failed to Cast PVInfoBase to PVInfo for ";

    // Search PV List for "Duplicates"...
    for ( vector<PVInfoBase*>::iterator ipv = m_pvs_list.begin();
            ipv != m_pvs_list.end(); ++ipv )
    {
        // Found a PV Marked as Duplicate...!
        if ( (*ipv)->m_duplicate && !((*ipv)->m_ignore) )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:",
                "StreamParser::collapseDuplicatePVs()",
                "Found PV Marked as Duplicate",
                (*ipv)->m_device_pv_str.c_str() );
            give_syslog_a_chance;

            // Search Remaining List of PVs for Matching Connections,
            // and Try to Absorb/Subsume Any Duplicate PV Values...
            for ( vector<PVInfoBase*>::iterator ipvDup = ipv + 1;
                    ipvDup != m_pvs_list.end(); ++ipvDup )
            {
                // Found a Matching PV Log, try to Absorb/Subsume It...!
                if ( (*ipv)->sameDefinitionPVConn( *ipvDup,
                    false /* Not "Exact" Match (Device Enum Name)... */ ) )
                {
                    syslog( LOG_ERR, "[%i] %s %s: %s %s - %s",
                        g_pid, "STC Error:",
                        "StreamParser::collapseDuplicatePVs()",
                        "Found Matching Duplicate PV",
                        (*ipvDup)->m_device_pv_str.c_str(),
                        "Try to Subsume Its Values..." );
                    give_syslog_a_chance;

                    switch ( (*ipv)->m_type )
                    {
                        case STC::PVT_INT:  // ADARA only supports uint32_t
                        case STC::PVT_ENUM:
                        case STC::PVT_UINT:

                            pvinfoU32 =
                                dynamic_cast<PVInfo<uint32_t>*>( *ipv );
                            if ( !pvinfoU32 ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipv)->m_device_pv_str )
                            }

                            // Make Sure This PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoU32->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Main PV Duplicate Not Normalized",
                                    (*ipv)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoU32->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDupU32 =
                                dynamic_cast<PVInfo<uint32_t>*>( *ipvDup );
                            if ( !pvinfoDupU32 ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipvDup)->m_device_pv_str )
                            }

                            // Make Sure Matching PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDupU32->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Matching PV Isn't Normalized",
                                    (*ipvDup)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDupU32->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoU32->subsumeValues(
                                pvinfoDupU32->m_value_buffer,
                                pvinfoDupU32->m_time_buffer );

                            // This Duplicate PV's Values have
                            // Now Been Subsumed, So Mark It to Ignore...
                            // (We Don't Want to Write This PV Any More!)
                            pvinfoDupU32->m_value_buffer.clear();
                            pvinfoDupU32->m_time_buffer.clear();
                            pvinfoDupU32->m_ignore = true;

                            break;

                        case STC::PVT_FLOAT: // ADARA only supports double
                        case STC::PVT_DOUBLE:

                            pvinfoDbl =
                                dynamic_cast<PVInfo<double>*>( *ipv );
                            if ( !pvinfoDbl ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipv)->m_device_pv_str )
                            }

                            // Make Sure This PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDbl->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Main PV Duplicate Not Normalized",
                                    (*ipv)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDbl->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDupDbl =
                                dynamic_cast<PVInfo<double>*>( *ipvDup );
                            if ( !pvinfoDupDbl ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipvDup)->m_device_pv_str )
                            }

                            // Make Sure Matching PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDupDbl->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Matching PV Isn't Normalized",
                                    (*ipvDup)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDupDbl->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDbl->subsumeValues(
                                pvinfoDupDbl->m_value_buffer,
                                pvinfoDupDbl->m_time_buffer );

                            // This Duplicate PV's Values have
                            // Now Been Subsumed, So Mark It to Ignore...
                            // (We Don't Want to Write This PV Any More!)
                            pvinfoDupDbl->m_value_buffer.clear();
                            pvinfoDupDbl->m_time_buffer.clear();
                            pvinfoDupDbl->m_ignore = true;

                            break;

                        case STC::PVT_STRING:

                            pvinfoStr =
                                dynamic_cast<PVInfo<string>*>( *ipv );
                            if ( !pvinfoStr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipv)->m_device_pv_str )
                            }

                            // Make Sure This PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoStr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Main PV Duplicate Not Normalized",
                                    (*ipv)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoStr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDupStr =
                                dynamic_cast<PVInfo<string>*>( *ipvDup );
                            if ( !pvinfoDupStr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipvDup)->m_device_pv_str )
                            }

                            // Make Sure Matching PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDupStr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Matching PV Isn't Normalized",
                                    (*ipvDup)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDupStr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoStr->subsumeValues(
                                pvinfoDupStr->m_value_buffer,
                                pvinfoDupStr->m_time_buffer );

                            // This Duplicate PV's Values have
                            // Now Been Subsumed, So Mark It to Ignore...
                            // (We Don't Want to Write This PV Any More!)
                            pvinfoDupStr->m_value_buffer.clear();
                            pvinfoDupStr->m_time_buffer.clear();
                            pvinfoDupStr->m_ignore = true;

                            break;

                        case STC::PVT_UINT_ARRAY:

                            pvinfoU32Arr =
                                dynamic_cast<PVInfo< vector<uint32_t> >*>(
                                    *ipv );
                            if ( !pvinfoU32Arr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipv)->m_device_pv_str )
                            }

                            // Make Sure This PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoU32Arr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Main PV Duplicate Not Normalized",
                                    (*ipv)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoU32Arr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDupU32Arr =
                                dynamic_cast<PVInfo< vector<uint32_t> >*>(
                                    *ipvDup );
                            if ( !pvinfoDupU32Arr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipvDup)->m_device_pv_str )
                            }

                            // Make Sure Matching PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDupU32Arr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Matching PV Isn't Normalized",
                                    (*ipvDup)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDupU32Arr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoU32Arr->subsumeValues(
                                pvinfoDupU32Arr->m_value_buffer,
                                pvinfoDupU32Arr->m_time_buffer );

                            // This Duplicate PV's Values have
                            // Now Been Subsumed, So Mark It to Ignore...
                            // (We Don't Want to Write This PV Any More!)
                            pvinfoDupU32Arr->m_value_buffer.clear();
                            pvinfoDupU32Arr->m_time_buffer.clear();
                            pvinfoDupU32Arr->m_ignore = true;

                            break;

                        case STC::PVT_DOUBLE_ARRAY:

                            pvinfoDblArr =
                                dynamic_cast<PVInfo< vector<double> >*>(
                                    *ipv );
                            if ( !pvinfoDblArr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipv)->m_device_pv_str )
                            }

                            // Make Sure This PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDblArr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Main PV Duplicate Not Normalized",
                                    (*ipv)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDblArr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDupDblArr =
                                dynamic_cast<PVInfo< vector<double> >*>(
                                    *ipvDup );
                            if ( !pvinfoDupDblArr ) {
                                THROW_TRACE( ERR_CAST_FAILED, cast_fail
                                        << (*ipvDup)->m_device_pv_str )
                            }

                            // Make Sure Matching PV's Timestamps
                            // Have Been Normalized...!
                            if ( pvinfoDupDblArr->m_has_non_normalized )
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s - %s %s %s, %s",
                                    g_pid, "STC Error:",
                                    "StreamParser::collapseDuplicatePVs()",
                                    "Matching PV Isn't Normalized",
                                    (*ipvDup)->m_device_pv_str.c_str(),
                                    "Normalizing PV Value Times",
                                    "with First Pulse Time",
                                    "Now Available" );
                                give_syslog_a_chance;

                                pvinfoDupDblArr->normalizeTimestamps(
                                    start_time, m_verbose_level );
                            }

                            pvinfoDblArr->subsumeValues(
                                pvinfoDupDblArr->m_value_buffer,
                                pvinfoDupDblArr->m_time_buffer );

                            // This Duplicate PV's Values have
                            // Now Been Subsumed, So Mark It to Ignore...
                            // (We Don't Want to Write This PV Any More!)
                            pvinfoDupDblArr->m_value_buffer.clear();
                            pvinfoDupDblArr->m_time_buffer.clear();
                            pvinfoDupDblArr->m_ignore = true;

                            break;
                    }

                    // Leave Subsumed PV Log Marked as "Duplicate",
                    // So We Won't Try to Write Its Values to DASlogs...
                }
            }

            // This PV has Now Subsumed All Other Duplicate PV Logs,
            // So We Can Let It Thru into the DASlogs Output...
            (*ipv)->m_duplicate = false;

            syslog( LOG_ERR, "[%i] %s %s: %s %s - %s",
                g_pid, "STC Error:",
                "StreamParser::collapseDuplicatePVs()",
                "PV Has Subsumed All Duplicate PV Log Values",
                (*ipv)->m_device_pv_str.c_str(),
                "Cleared for Writing to DASlogs." );
            give_syslog_a_chance;
        }

        // This Duplicate has Already Been Subsumed...! ;-D
        else if ( (*ipv)->m_duplicate )
        {
            syslog( LOG_ERR, "[%i] %s %s: %s - %s",
                g_pid, "STC Error:",
                "StreamParser::collapseDuplicatePVs()",
                "Ignoring Already Subsumed PV Duplicate",
                (*ipv)->m_device_pv_str.c_str() );
            give_syslog_a_chance;
        }
    }
}


/*! \brief This method retrieves the human-readable name of an ADARA packet type.
 *
 * This method retrieves the human-readable name of an ADARA packet type.
 */
const char*
StreamParser::getPktName(
    uint32_t a_pkt_type   ///< [in] ADARA packet type (defined in ADARA.h)
) const
{
    // Mask out packet version number
    uint32_t type = ADARA_BASE_PKT_TYPE( a_pkt_type );
    uint32_t version = ADARA_PKT_VERSION( a_pkt_type );

    stringstream ss;
    switch ( type )
    {
        case ADARA::PacketType::RAW_EVENT_TYPE:
            ss << "Raw Event"; break;
        case ADARA::PacketType::MAPPED_EVENT_TYPE:
            ss << "Mapped Event"; break;
        case ADARA::PacketType::RTDL_TYPE:
            ss << "RTDL"; break;
        case ADARA::PacketType::SOURCE_LIST_TYPE:
            ss << "Src List"; break;
        case ADARA::PacketType::BANKED_EVENT_TYPE:
            ss << "Banked Event"; break;
        case ADARA::PacketType::BANKED_EVENT_STATE_TYPE:
            ss << "Banked Event State"; break;
        case ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE:
            ss << "Beam Monitor Event"; break;
        case ADARA::PacketType::PIXEL_MAPPING_TYPE:
            ss << "Pixel Map"; break;
        case ADARA::PacketType::PIXEL_MAPPING_ALT_TYPE:
            ss << "Pixel Map Alt"; break;
        case ADARA::PacketType::RUN_STATUS_TYPE:
            ss << "Run Stat"; break;
        case ADARA::PacketType::RUN_INFO_TYPE:
            ss << "Run Info"; break;
        case ADARA::PacketType::TRANS_COMPLETE_TYPE:
            ss << "Tran Comp"; break;
        case ADARA::PacketType::CLIENT_HELLO_TYPE:
            ss << "Client Hello"; break;
        case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
            ss << "Annotation"; break;
        case ADARA::PacketType::SYNC_TYPE:
            ss << "Sync"; break;
        case ADARA::PacketType::HEARTBEAT_TYPE:
            ss << "Heartbeat"; break;
        case ADARA::PacketType::GEOMETRY_TYPE:
            ss << "Geom Info"; break;
        case ADARA::PacketType::BEAMLINE_INFO_TYPE:
            ss << "Beamline Info"; break;
        case ADARA::PacketType::BEAM_MONITOR_CONFIG_TYPE:
            ss << "Beam Monitor Config"; break;
        case ADARA::PacketType::DETECTOR_BANK_SETS_TYPE:
            ss << "Detector Bank Sets"; break;
        case ADARA::PacketType::DATA_DONE_TYPE:
            ss << "Data Done"; break;
        case ADARA::PacketType::DEVICE_DESC_TYPE:
            ss << "DDP"; break;
        case ADARA::PacketType::VAR_VALUE_U32_TYPE:
            ss << "VVP U32"; break;
        case ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE:
            ss << "VVP DBL"; break;
        case ADARA::PacketType::VAR_VALUE_STRING_TYPE:
            ss << "VVP STR"; break;
        case ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE:
            ss << "VVP U32 ARRAY"; break;
        case ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE:
            ss << "VVP DBL ARRAY"; break;
        case ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE:
            ss << "Mult VVP U32"; break;
        case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE:
            ss << "Mult VVP DBL"; break;
        case ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE:
            ss << "Mult VVP STR"; break;
        case ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE:
            ss << "Mult VVP U32 ARRAY"; break;
        case ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE:
            ss << "Mult VVP DBL ARRAY"; break;
        default:
            ss << "Unknown (0x" << hex << type << dec << ")";
            break;
    }

    // Append Version
    ss << " V" << version;

    return ss.str().c_str();
}


/*! \brief This method converts a text-based variable type to a PVType
 *  \return PVType based on input text
 *
 * This method converts a text-based process variable type (from a device descriptor) to a PVType. If the conversion is
 * not possible, an expcetion is thrown.
 */
PVType
StreamParser::toPVType
(
    const char *a_source    ///< [in] Text-based variable type to convert
) const
{
    if ( boost::iequals( a_source, "integer" ) )
        return PVT_INT;
    else if ( boost::iequals( a_source, "unsigned" ) )
        return PVT_UINT;
    else if ( boost::iequals( a_source, "unsigned integer" ) )
        return PVT_UINT;
    else if ( boost::iequals( a_source, "double" ) )
        return PVT_DOUBLE;
    else if ( boost::iequals( a_source, "float" ) )
        return PVT_FLOAT;
    else if ( boost::iequals( a_source, "string" ) )
        return PVT_STRING;
    else if ( boost::iequals( a_source, "integer array" ) )
        return PVT_UINT_ARRAY;
    else if ( boost::iequals( a_source, "double array" ) )
        return PVT_DOUBLE_ARRAY;
    else if ( boost::istarts_with( a_source, "enum_" ) )
        return PVT_ENUM;

    std::string err = "Invalid PV type [";
    err += a_source;
    err += "]";
    THROW_TRACE( ERR_UNEXPECTED_INPUT, err )
}


/*! \brief Method to retrieve an XML node's value with whitespace trimmed
 *  \param a_node - The xml node containing the value to retrieve
 *  \param a_value - A string to receive the value (empty if no value defined)
 */
void
StreamParser::getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const
{
    if ( a_node->children && a_node->children->content )
    {
        a_value = (char*)a_node->children->content;
        boost::algorithm::trim( a_value );
    }
    else
        a_value = "";
}


/*! \brief Resets "In Use" Portion of Critical Path Data Buffer Vectors.
 *  \param a_buffer - Data buffer vector
 *  \param a_buffer_size - "In Use" size of data buffer vector
 *
 * This method quickly resets the currently "In Use" data elements
 * in a Critical Path data buffer vector, by spewing in "-1"s
 * to overwrite any potentially existing data values.
 * (This method isn't strictly required, if we do our "size" bookkeeping
 * correctly, but it's a sure-fire indicator if we do screw things up! :-)
 */
template<class T>
void
StreamParser::resetInUseVector
(
    std::vector<T>   a_buffer,
    uint64_t         a_buffer_size
)
{
    T *ptr = &a_buffer[0];

    memset( (void *) ptr, 0xff, a_buffer_size * sizeof( T ) );
}


/*! \brief Gathers statistics from the specified ADARA packet.
 *  \param a_pkt - An ADARA packet to analyze
 *
 * If stream statistics gathering is enabled, this method collects a number
 * of metrics for the stream and each packet type (total packet count, min/
 * max packet size with payload, total byte count for each packet type.
 */
inline void
StreamParser::gatherStats( const ADARA::Packet &a_pkt ) const
{
    PktStats &stats = m_stats[a_pkt.type()];
    ++stats.count;
    if ( a_pkt.packet_length() < stats.min_pkt_size
            || !stats.min_pkt_size )
    {
        stats.min_pkt_size = a_pkt.packet_length();
    }
    if ( a_pkt.packet_length() > stats.max_pkt_size )
        stats.max_pkt_size = a_pkt.packet_length();
    stats.total_size += a_pkt.packet_length();
}

} // End namespace STC

// vim: expandtab

