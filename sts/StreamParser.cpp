#include "StreamParser.h"
#include "TransCompletePkt.h"
#include <iomanip>
#include <sstream>
#include <string.h>
#include <boost/algorithm/string.hpp>
#include <syslog.h>
#include "ADARAUtils.h"
#include "ADARAPackets.h"


using namespace std;

namespace STS {

/// This sets the size of the ADARA parser stream buffer in bytes
#define ADARA_IN_BUF_SIZE   0x1000000


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
    const string   &a_adara_out_file,           ///< [in] Filename of output ADARA stream file (disabled if empty)
    bool            a_strict,                   ///< [in] Controls strict processing of input stream
    bool            a_gather_stats,             ///< [in] Controls stream statistics gathering
    uint32_t        a_event_buf_write_thresh,   ///< [in] Event buffer write threshold
    uint32_t        a_anc_buf_write_thresh      ///< [in] Ancillary buffer write threshold
)
:
    POSIXParser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
    m_fd(a_fd_in),
    m_processing_state(PROCESSING_NOT_STARTED),
    m_pkt_recvd(0),
    m_pulse_id(0),
    m_pulse_count(0),
    m_event_buf_write_thresh(a_event_buf_write_thresh),
    m_anc_buf_write_thresh(a_anc_buf_write_thresh),
    m_info_rcvd(0),
    m_strict(a_strict),
    m_gen_adara(false),
    m_gather_stats(a_gather_stats),
    m_skipped_pkt_count(0),
    m_pulse_flag(0)
{
    if ( !a_adara_out_file.empty() )
    {
        m_gen_adara = true;
        m_ofs_adara.open( a_adara_out_file.c_str(), ios_base::out | ios_base::binary );
    }

    m_pulse_info.times.reserve(m_anc_buf_write_thresh);
    m_pulse_info.freqs.reserve(m_anc_buf_write_thresh);
}


/*! \brief Destructor for StreamParser class.
 *
 */
StreamParser::~StreamParser()
{
    if ( m_ofs_adara.is_open())
        m_ofs_adara.close();

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin();
            ibi != m_banks.end(); ++ibi ) {
        if ( *ibi )
            delete *ibi;
    }

    for ( vector<STS::DetectorBankSet *>::iterator dbs =
            m_bank_sets.begin(); dbs != m_bank_sets.end() ; ++dbs ) {
        if ( *dbs )
            delete *dbs;
    }

    for ( map<Identifier,MonitorInfo*>::iterator imi = m_monitors.begin();
            imi != m_monitors.end(); ++imi ) {
        if ( imi->second )
            delete imi->second;
    }

    m_monitor_config.clear();

    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs_by_key.begin();
            ipv != m_pvs_by_key.end(); ++ipv ) {
        if ( ipv->second )
            delete ipv->second;
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
        "StreamParser::processStream() can not be called more than once." )
    }

    try
    {
        initialize();
        m_processing_state = WAITING_FOR_RUN_START;

        while ( m_processing_state < DONE_PROCESSING )
        {
            // NOTE: This is POSIXParser::read()... ;-o
            if ( !read( m_fd, log_info ))
            {
                if ( m_processing_state != DONE_PROCESSING )
                {
                    syslog( LOG_ERR,
                    "[%i] STS failed %s: %s, Not Done Processing! (%s)",
                        g_pid, "processStream()", "Connection Failed",
                        log_info.c_str() );

                    if ( m_processing_state == PROCESSING_EVENTS )
                    {
                        syslog( LOG_ERR,
                            "[%i] %s %s: %s, Still Processing Events!",
                            g_pid, "STS Error:", "processStream()",
                            "Connection Failed" );

                        // On fatal error, flush buffers to Nexus
                        // before terminating
                        markerComment( m_pulse_info.last_time,
                            "Stream processing terminated abnormally." );
                        m_run_metrics.end_time = nsec_to_timespec(
                            m_pulse_info.start_time
                                + m_pulse_info.last_time );
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
        RETHROW_TRACE( e, "processStream() failed." )
    }
    catch ( exception &e )
    {
        THROW_TRACE( ERR_GENERAL_ERROR,
            "processStream() exception {" << e.what() << "}" )
    }
    catch ( ... )
    {
        THROW_TRACE( ERR_GENERAL_ERROR,
            "processStream() unexpected exception." )
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
    if ( m_processing_state & (s))      \
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
    if (( m_processing_state & (s)) && !(m_pkt_recvd & (x)))    \
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
        m_ofs_adara.write( (char *)a_pkt.packet(), a_pkt.packet_length());

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
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_PIXELMAP)

        case ADARA::PacketType::RUN_INFO_TYPE:
            PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,
                PKT_BIT_RUNINFO)

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
        case ADARA::PacketType::STREAM_ANNOTATION_TYPE:
            PROCESS_IN_STATES(PROCESSING_RUN_HEADER|PROCESSING_EVENTS)

        // These packets shall only be processed during event processing
        case ADARA::PacketType::BANKED_EVENT_TYPE:
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
                "[%i] Run Status Start-of-Run Received.", g_pid );
        }
        else
        {
            syslog( LOG_WARNING,
                "[%i] %s Run Status Error: Start-of-Run with state=0x%x.",
                g_pid, "STS Error:", m_processing_state );
            bad_state = true;
        }
    }
    else if ( a_pkt.status() == ADARA::RunStatus::END_RUN )
    {
        if ( m_processing_state == PROCESSING_EVENTS )
        {
            // Run "end time" is defined as time of last pulse
            // (which is nanoseconds epoch offset)
            m_run_metrics.end_time = nsec_to_timespec(
                m_pulse_info.start_time + m_pulse_info.last_time );

            finalizeStreamProcessing();
            m_processing_state = DONE_PROCESSING;

            // Dagnabbit, return "true" here to halt stream processing...
            // We've marked the processing state to "Done", but we still
            // need to forcibly terminate the POSIX read() loop, which in
            // our case _sometimes_ hangs on relentlessly... <sigh/>
            syslog( LOG_INFO,
                "[%i] Run Status End-of-Run Received.", g_pid );
            return true;
        }
        else
        {
            syslog( LOG_WARNING,
                "[%i] %s Run Status Error: End-of-Run with state=0x%x.",
                g_pid, "STS Error:", m_processing_state );
            bad_state = true;
        }
    }

    if ( bad_state )
    {
        THROW_TRACE( ERR_UNEXPECTED_INPUT,
            "Recvd Run Status pkt in wrong state.")
    }

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Pixel Mapping packet processing
//---------------------------------------------------------------------------------------------------------------------

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
    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload()
        + a_pkt.payload_length());

    uint32_t        base_logical;
    uint16_t        bank_id;
    uint16_t        pix_count;

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
        pix_count = (uint16_t)(*rpos2 & 0xFFFF);
        rpos2++;
        if ( bank_id > bank_count )
            bank_count = bank_id;
        rpos2 += pix_count;
    }

    m_banks.resize( bank_count + 1, 0 );

    // Now build banks and populate bank container
    while ( rpos < epos )
    {
        base_logical = *rpos++;
        bank_id = (uint16_t)(*rpos >> 16);
        pix_count = (uint16_t)(*rpos & 0xFFFF);
        rpos++;

        // Create New BankInfo...
        if ( !m_banks[bank_id] )
        {
            // Create BankInfo Instance
            m_banks[bank_id] = makeBankInfo( bank_id,
                m_event_buf_write_thresh, m_anc_buf_write_thresh );

            // Try to Associate Any Detector Bank Sets that
            // Contain This Bank Id...
            m_banks[bank_id]->m_bank_sets =
                getDetectorBankSets( static_cast<uint32_t>(bank_id) );
        }

        // Append This Section's Logical PixelIds...
        for (uint32_t i=0 ; i < pix_count ; ++i)
        {
            m_banks[bank_id]->m_logical_pixelids.push_back(
                base_logical + i );
        }

        // syslog( LOG_INFO,
            // "[%i] %s: bank_id=%u base_logical=%u count=%u tot=%lu",
            // g_pid, "PixelMappingPkt", bank_id, base_logical, pix_count,
            // m_banks[bank_id]->m_logical_pixelids.size() );

        // Next Section
        rpos += pix_count;
    }

    // The receipt of a pixel mapping packet allows state to progress
    // to event processing
    m_processing_state = PROCESSING_EVENTS;

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Banked Event packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Banked Event ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Banked Event packets. The processPulseInfo() method is called by this method with the
 * pulse data attached to the received Banked Event packet. The payload of the Banked Event packet is parsed for neutron
 * events which are then handled by the processBankEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BankedEventPkt &a_pkt      ///< [in] ADARA BankedEventPkt object to process
)
{
    // Ignore duplicate pulses
    if ( a_pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE )
        return false;

    // Pulse flag should be 0 (no data processed yet) or 2 (monitor data processed)
    // any other value indicates an error with SMS packet generation

    if ( m_pulse_flag == 0 )
    {
        // First packet of new pulse - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag |= 1;
    }
    else if ( m_pulse_flag == 2 )
        m_pulse_flag = 0;
    else
        THROW_TRACE( ERR_UNEXPECTED_INPUT, "Invalid banked event packet sequence received" )


    processPulseInfo( a_pkt );

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    rpos += 4; // Skip over pulse info

    uint32_t source_id;
    uint32_t bank_count;
    uint32_t bank_id;
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
            event_count = *rpos++;
            processBankEvents( bank_id, event_count, rpos );
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

        // Handle pulse sequence flag for this Oversized Packet
        // (so we don't get "out of sync" when we throw it away... :-)

        if ( hdr->base_type() == ADARA::PacketType::BANKED_EVENT_TYPE
                || hdr->base_type()
                    == ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
        {
            // Pulse flag should be 0 (no data processed yet) or 2 (monitor
            // data processed) any other value indicates an error with
            // SMS packet generation

            if ( m_pulse_flag == 0 )
            {
                // First packet of new pulse - count it and set flag
                // indicating it was counted
                ++m_pulse_count;
                if ( hdr->base_type()
                        == ADARA::PacketType::BANKED_EVENT_TYPE )
                {
                    m_pulse_flag |= 1;
                }
                else if ( hdr->base_type() ==
                        ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
                {
                    m_pulse_flag |= 2;
                }
            }
            else if ( ( hdr->base_type()
                            == ADARA::PacketType::BANKED_EVENT_TYPE
                        && m_pulse_flag == 2 )
                    || ( hdr->base_type() ==
                            ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE
                        && m_pulse_flag == 1 ) )
            {
                m_pulse_flag = 0;
            }
            else if ( hdr->base_type()
                    == ADARA::PacketType::BANKED_EVENT_TYPE )
            {
                THROW_TRACE( ERR_UNEXPECTED_INPUT,
                    "Invalid banked event packet sequence received" )
            }
            else if ( hdr->base_type() ==
                    ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE )
            {
                THROW_TRACE( ERR_UNEXPECTED_INPUT,
                    "Invalid beam monitor packet sequence received" )
            }
        }
    }

    // Log Oversized Packet (Next Chunk)
    else
    {
        syslog( LOG_WARNING,
            "[%i] OversizePkt: next chunk max=%u offset=%u len=%u",
            g_pid, ADARA_IN_BUF_SIZE, chunk_offset, chunk_len);
    }

    // Invoke the base handler, in case it ever does anything...
    return Parser::rxOversizePkt(hdr, chunk, chunk_offset, chunk_len);
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

        // It is (or should be) considered a fatal error if pulse times are not monotonically increasing
        if ( pulse_time < m_pulse_info.start_time )
        {
            syslog( LOG_INFO,
                "[%i] Unexpected input: %s at pulse #%ld ID=0x%lx, %s.",
                g_pid, "Pulse time went backwards",
                m_pulse_info.times.size(), a_pkt.pulseId(),
                "Clamping to zero" );
            pulse_time = 0;
        }
        else
            pulse_time -= m_pulse_info.start_time;

        m_pulse_info.times.push_back( pulse_time/1000000000.0 );
        m_pulse_info.freqs.push_back( 1000000000.0 / ( pulse_time - m_pulse_info.last_time ));
        m_run_metrics.freq_stats.push( m_pulse_info.freqs.back() );
        m_pulse_info.last_time = pulse_time;
    }
    else
    {
        m_pulse_info.start_time = timespec_to_nsec( a_pkt.timestamp() );
        m_pulse_info.last_time = 0;
        m_pulse_info.times.push_back(0);
        m_pulse_info.freqs.push_back(0);
        // Freq stats ignores first point since it can't be calculated

        // Run "start time" is defined as time of first pulse
        m_run_metrics.start_time = a_pkt.timestamp();
    }

    // Is is time to write pulse info?
    if ( m_pulse_info.times.size() == m_anc_buf_write_thresh )
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
 * the bankBuffersReady() virtual method. This method also detects pulse
 * gaps and corrects the event index as required (see handleBankPulseGap()
 * method for more details).
 */
void
StreamParser::processBankEvents
(
    uint32_t        a_bank_id,        ///< [in] Bank ID of detector bank to be processed
    uint32_t        a_event_count,    ///< [in] Number of events contained in stream buffer
    const uint32_t *a_rpos            ///< [in] Stream event buffer read pointer
)
{
    // Valid Detector Bank ID...
    if ( a_bank_id < m_banks.size() )
    {
        BankInfo *bi = m_banks[a_bank_id];

        // Make Sure Data has been (Late) Initialized...
        if ( !(bi->m_initialized) )
            bi->initializeBank( false );

        // Event-based Data Processing
        if ( bi->m_has_event )
        {
            // Detect gaps in event data and fill event index if present
            if ( bi->m_last_pulse_with_data < ( m_pulse_count - 1 ))
            {
                handleBankPulseGap( *bi,
                    ( m_pulse_count - 1 ) - bi->m_last_pulse_with_data );
            }

            size_t sz = bi->m_tof_buffer_size;

            // *** STS CRITICAL PATH OPTIMIZATION ***
            // *ONLY* Resize Vector if _Not_ Previously Resized...!
            // - Huge Savings for us, because resize() pre-initializes
            // the Values in the vector when resizing...! ;-D
            // - we track our own "in use" vector size in the
            // new "BankInfo::m_tof_buffer_size" field... :-D
            if ( sz + a_event_count > bi->m_tof_buffer.size() )
            {
                bi->m_tof_buffer.resize( sz + a_event_count,
                    (float) -1.0 );
                bi->m_pid_buffer.resize( sz + a_event_count,
                    (uint32_t) -1 );
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

            // Check to see if buffers are ready to write
            if ( bi->m_tof_buffer_size >= m_event_buf_write_thresh
                   || bi->m_index_buffer.size() >= m_anc_buf_write_thresh )
            {
                bankBuffersReady( *bi );

#ifdef PARANOID
                resetInUseVector<float>( bi->m_tof_buffer,
                    bi->m_tof_buffer_size );
                resetInUseVector<uint32_t>( bi->m_pid_buffer,
                    bi->m_tof_buffer_size );
#endif
                bi->m_tof_buffer_size = 0;

                bi->m_index_buffer.clear();
            }

            m_run_metrics.events_counted += a_event_count;
        }

        // Histogram-based Data Processing
        if ( bi->m_has_histo )
        {
            const uint32_t  *rpos = a_rpos;
            const uint32_t  *epos = a_rpos + (a_event_count<<1);

            // Find Detector Bank Set We're Using for Histogram
            // (Only *One* Histogram per Detector Bank (for now)...)
            for ( std::vector<STS::DetectorBankSet *>::iterator dbs =
                        bi->m_bank_sets.begin();
                    dbs != bi->m_bank_sets.end() ; ++dbs )
            {
                if ( !*dbs )
                    continue;

                // Histo-based Detector Bank
                if ( (*dbs)->flags
                        & ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
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
                                    g_pid, "STS Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Error",
                                    tof, tofbin, bi->m_num_tof_bins - 1 );
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
                                    g_pid, "STS Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Index Overflow",
                                    pid, tofbin, index,
                                    bi->m_logical_pixelids.size()
                                        * ( bi->m_num_tof_bins - 1 )
                                    );
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
                                    g_pid, "STS Error:",
                                    "Detector Bank", bi->m_id,
                                    "Histogram Offset Error",
                                    pid, bi->m_base_pid,
                                    bi->m_histo_pid_offset[
                                        pid - bi->m_base_pid ] );
                            }

                            (bi->m_histo_event_uncounted)++;
                        }

                    }   // event processing loop...

                    // Only *One* Histogram per Detector Bank (for now)...
                    break;

                }   // histo-based detector bank...

            }   // m_bank_sets loop...

        }   // m_has_histo

    }   // Valid Detector Bank ID...

    // Not a Valid Detector Bank ID...
    else
        m_run_metrics.events_uncounted += a_event_count;
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
        a_bi.initializeBank( false );

    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_bi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh )
    {
        a_bi.m_index_buffer.resize( a_bi.m_index_buffer.size() + a_count,
            a_bi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large - flush buffers & fill gap
        // Note: it is acceptable to call bankBuffersReady
        // even if they are empty.
        bankBuffersReady( a_bi );
        bankPulseGap( a_bi, a_count );

#ifdef PARANOID
        resetInUseVector<float>( a_bi.m_tof_buffer,
            a_bi.m_tof_buffer_size );
        resetInUseVector<uint32_t>( a_bi.m_pid_buffer,
            a_bi.m_tof_buffer_size );
#endif
        a_bi.m_tof_buffer_size = 0;

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
    if ( a_pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE )
        return false;

    // Pulse flag should be 0 (no data processed yet) or 1 (event data processed)
    // any other value indicates an error with SMS packet generation

    if ( m_pulse_flag == 0 )
    {
        // First packet of new pulse - count it and set flag indicating it was counted
        ++m_pulse_count;
        m_pulse_flag |= 2;
    }
    else if ( m_pulse_flag == 1 )
        m_pulse_flag = 0;
    else
        THROW_TRACE( ERR_UNEXPECTED_INPUT, "Invalid beam monitor packet sequence received" )

    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());

    // TODO What do we do with the pulse info from beam monitor packets?
    rpos += 4; // Skip over pulse info

    uint16_t  monitor_id;
    uint32_t event_count;

    // Process events per-bank
    while( rpos < epos )
    {
        monitor_id = *rpos >> 22;
        event_count = *rpos++ & 0x003FFFFF;

        // TODO What do we do with the source info from beam monitor packets?
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
 * stream adapter via the monitorBuffersReady() virtual method.
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
    map<Identifier,MonitorInfo*>::iterator imi =
        m_monitors.find( a_monitor_id );
    if ( imi == m_monitors.end())
    {
        bool known_monitor = true;
        STS::BeamMonitorConfig *config =
            getBeamMonitorConfig(a_monitor_id, known_monitor);
        MonitorInfo *mi = makeMonitorInfo( a_monitor_id,
            m_event_buf_write_thresh, m_anc_buf_write_thresh,
            config, known_monitor );
        imi = m_monitors.insert( m_monitors.begin(),
            pair<Identifier,MonitorInfo*>(a_monitor_id,mi));
    }

    const uint32_t *epos = a_rpos + a_event_count;

    // Histo-based Monitors...
    if ( imi->second->m_config != NULL )
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
            if ( tof >= imi->second->m_config->tofOffset
                    && tof < imi->second->m_config->tofMax )
            {
                // Calculate index into Histogram based on TOF...
                tofbin = ( tof - imi->second->m_config->tofOffset )
                    / imi->second->m_config->tofBin;

                // Sanity Test, Just to Be Sure... ;-b
                // (This should never happen, but the logic is confusing.)
                if ( tofbin >= imi->second->m_num_tof_bins - 1 )
                {
                    syslog( LOG_ERR,
                    "[%i] %s %s %u Histogram Error tof=%u index=%u >= %u",
                        g_pid, "STS Error:", "Beam Monitor",
                        imi->second->m_id, tof, tofbin,
                        imi->second->m_num_tof_bins - 1 );
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

        // *** STS CRITICAL PATH OPTIMIZATION ***
        // *ONLY* Resize Vector if _Not_ Previously Resized...!
        // - Huge Savings for us, because resize() pre-initializes
        // the Values in the vector when resizing...! ;-D
        // - we track our own "in use" vector size in the
        // new "MonitorInfo::m_tof_buffer_size" field... :-D
        if ( sz + a_event_count > imi->second->m_tof_buffer.size() )
        {
            imi->second->m_tof_buffer.resize( sz + a_event_count,
                (float) -1.0 );
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

        // Check to see if buffers are ready to write
        if ( imi->second->m_tof_buffer_size >= m_event_buf_write_thresh
          || imi->second->m_index_buffer.size() >= m_anc_buf_write_thresh )
        {
            monitorBuffersReady( *imi->second );
    
#ifdef PARANOID
            resetInUseVector<float>( imi->second->m_tof_buffer,
                imi->second->m_tof_buffer_size );
#endif
            imi->second->m_tof_buffer_size = 0;

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
 * deferred to the stream adatapter subclass via the monitorPulseGap()
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
    if ( a_mi.m_config != NULL )
        return;

    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_mi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh )
    {
        a_mi.m_index_buffer.resize( a_mi.m_index_buffer.size() + a_count,
            a_mi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large - flush current buffered data
        // & fill index directly
        // Note: it is acceptable to call monitorBuffersReady even if
        // they are empty.
        monitorBuffersReady( a_mi );
        monitorPulseGap( a_mi, a_count );

#ifdef PARANOID
        resetInUseVector<float>( a_mi.m_tof_buffer,
            a_mi.m_tof_buffer_size );
#endif
        a_mi.m_tof_buffer_size = 0;

        a_mi.m_index_buffer.clear();
    }
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Run Info packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Run Info ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Run Info packets. The processRunInfo() virtual method is used to communicate
 * run info data to the stream adapter subclass.
 */
bool
StreamParser::rxPacket
(
    const ADARA::RunInfoPkt &a_pkt  ///< [in] The ADARA Run Info Packet to process
)
{
    xmlDocPtr doc = xmlReadMemory( a_pkt.info().c_str(), a_pkt.info().length(), 0, 0, 0 );
    if ( doc )
    {
        string tag;
        string value;

        try
        {
            for ( xmlNode *node = xmlDocGetRootElement(doc)->children; node; node = node->next )
            {
                tag = (char*)node->name;
                getXmlNodeValue( node, value );

                if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
                    m_run_info.run_number = boost::lexical_cast<unsigned long>( value );
                else if ( xmlStrcmp( node->name, (const xmlChar*)"proposal_id" ) == 0)
                    m_run_info.proposal_id = value;
                else if ( xmlStrcmp( node->name, (const xmlChar*)"run_title" ) == 0)
                    m_run_info.run_title = value;
                else if (xmlStrcmp( node->name, (const xmlChar*) "facility_name") == 0)
                    m_run_info.facility_name = value;
                else if ( xmlStrcmp( node->name, (const xmlChar*)"sample" ) == 0)
                {
                    for ( xmlNode *sample_node = node->children; sample_node; sample_node = sample_node->next )
                    {
                        tag = (char*)sample_node->name;
                        getXmlNodeValue( sample_node, value );

                        if ( xmlStrcmp( sample_node->name, (const xmlChar*)"id" ) == 0)
                            m_run_info.sample_id = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"name" ) == 0)
                            m_run_info.sample_name = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"nature" ) == 0)
                            m_run_info.sample_nature = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"chemical_formula" ) == 0)
                            m_run_info.sample_formula = value;
                        else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"environment" ) == 0)
                            m_run_info.sample_environment = value;
                    }
                }
                else if ( xmlStrcmp( node->name, (const xmlChar*)"users" ) == 0)
                {
                    for ( xmlNode *user_node = node->children; user_node; user_node = user_node->next )
                    {
                        if ( xmlStrcmp( user_node->name, (const xmlChar*)"user" ) == 0)
                        {
                            UserInfo ui;

                            for ( xmlNode *uinfo_node = user_node->children; uinfo_node; uinfo_node = uinfo_node->next )
                            {
                                tag = (char*)uinfo_node->name;
                                getXmlNodeValue( uinfo_node, value );

                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"id" ) == 0)
                                    ui.id = (char*)uinfo_node->children->content;
                                if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"name" ) == 0)
                                    ui.name = (char*)uinfo_node->children->content;
                                else if (xmlStrcmp( uinfo_node->name, (const xmlChar*)"role" ) == 0)
                                    ui.role = (char*)uinfo_node->children->content;
                            }

                            m_run_info.users.push_back( ui );
                        }
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT, "Failed parsing RunInfo packet on tag: " << tag << ", value: " << value << "; " << e.what() )
        }
        catch( ... )
        {
            THROW_TRACE( ERR_UNEXPECTED_INPUT, "Failed parsing RunInfo packet on tag: " << tag << ", value: " << value )
        }

        xmlFreeDoc( doc );
    }

    if ( m_strict )
    {
        // Verify we received all required fields in Run Info pkt

        string msg;
        if ( !m_run_info.facility_name.size())
            msg = "Required facility_name missing from RunInfo.";
        else if ( !m_run_info.proposal_id.size())
            msg = "Required proposal_id missing from RunInfo.";
        else if ( m_run_info.run_number == 0 )
            msg = "Required run_number missing from RunInfo.";

        if ( msg.size())
            THROW_TRACE( ERR_UNEXPECTED_INPUT, msg )
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
    m_run_info.target_station_number = a_pkt.targetStationNumber();

    m_run_info.instr_id = a_pkt.id();
    m_run_info.instr_shortname = a_pkt.shortName();
    m_run_info.instr_longname = a_pkt.longName();

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
    syslog( LOG_INFO,
        "[%i] Beam Monitor Config Received: %u Histo Monitors",
        g_pid, a_pkt.beamMonCount() );

    for (uint32_t i=0 ; i < a_pkt.beamMonCount() ; i++) {

        STS::BeamMonitorConfig config;

        config.id = a_pkt.bmonId(i);
        config.tofOffset = a_pkt.tofOffset(i);
        config.tofMax = a_pkt.tofMax(i);
        config.tofBin = a_pkt.tofBin(i);
        config.distance = a_pkt.distance(i);

        syslog( LOG_INFO,
            "[%i] Beam Monitor %u: distance=%lf histo=(%u to %u by %u).",
            g_pid, config.id, config.distance,
            config.tofOffset, config.tofMax, config.tofBin );

        // Basic Sanity Check...
        if ( config.tofOffset >= config.tofMax )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %u Config Error: Offset %u >= Max %u",
                g_pid, "STS Error:", "Beam Monitor", config.id,
                config.tofOffset, config.tofMax );
            syslog( LOG_ERR,
                "[%i] %s Reverting to Beam Monitor Event Mode!",
                g_pid, "STS Error:" );
            m_monitor_config.clear();
            break;
        }

        // Make Sure Time Bin is > 0 ! (also checked in SMS... :)
        if ( config.tofBin < 1 )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %u Histogram Config Issue: Time Bin %u < 1",
                g_pid, "STS Error:", "Beam Monitor", config.id,
                config.tofBin );
            config.tofBin = 1;
        }

        m_monitor_config.push_back(config);
    }

    return false;
}


/*! \brief This method looks up any Histogram Config for a Beam Monitor
 *  \return Pointer to element of the BeamMonitorConfig vector or NULL
 *
 * This method looks for a Beam Monitor Histogramming Config amongst
 * any optionally received prologue information, to define proper
 * Histogramming parameters for processing/accumulating Beam Monitor data.
 */
STS::BeamMonitorConfig *
StreamParser::getBeamMonitorConfig
(
    Identifier a_monitor_id,    ///< [in] Beam Monitor Id (uint32_t)
    bool & known_monitor        ///< [in] Flag for "Unknown" Monitors...
)
{
    STS::BeamMonitorConfig *config = (STS::BeamMonitorConfig *) NULL;

    // Innocent until Presumed Guilty...
    // (or like, if there isn't any Beam Monitor Config info... :-)
    known_monitor = true;

    // Any Beam Monitor Histogramming Parameters...? (If not, we're done.)
    if (m_monitor_config.size() == 0)
        return(config); // NULL...

    // Look for a matching Beam Monitor Id in Any Config...
    for ( vector<STS::BeamMonitorConfig>::iterator bmc =
                m_monitor_config.begin();
            bmc != m_monitor_config.end() && config == NULL ; ++bmc )
    {
        if (bmc->id == a_monitor_id)
            config = &(*bmc);
    }

    // If we didn't find one, then there's Trouble... ;-b
    if (config == NULL)
    {
        // "Trouble"...
        syslog( LOG_ERR,
            "[%i] %s %s %d Missing in Histogramming Config! %s",
            g_pid, "STS Error:", "Beam Monitor", a_monitor_id,
            "[Unknown Monitor]" );

        // Now What?!
        // - flag this Beam Monitor as "Unknown"
        //   (still save events, just _Not_ in an official NXmonitor...)
        known_monitor = false;
    }

    return(config);
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

        STS::DetectorBankSet *set = new STS::DetectorBankSet;

        set->name = a_pkt.name(i);
        set->banklist = banklist;
        set->flags = a_pkt.flags(i);
        set->tofOffset = a_pkt.tofOffset(i);
        set->tofMax = a_pkt.tofMax(i);
        set->tofBin = a_pkt.tofBin(i);
        set->throttle = a_pkt.throttle(i);
        set->suffix = a_pkt.suffix(i);

        std::string format;
        if ( set->flags & ADARA::DetectorBankSetsPkt::EVENT_FORMAT
                && set->flags & ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
            format = "both";
        else if ( set->flags & ADARA::DetectorBankSetsPkt::EVENT_FORMAT )
            format = "event";
        else if ( set->flags & ADARA::DetectorBankSetsPkt::HISTO_FORMAT )
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

        // Basic Sanity Check...
        if ( set->tofOffset >= set->tofMax )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s Config Error: Offset %u >= Max %u",
                g_pid, "STS Error:", "Detector Bank Set",
                set->name.c_str(), set->tofOffset, set->tofMax );
            syslog( LOG_ERR,
                "[%i] %s Restricting Detector Bank Set to Event Mode!",
                g_pid, "STS Error:" );
            set->flags &= !(ADARA::DetectorBankSetsPkt::HISTO_FORMAT);
        }

        // Make Sure Time Bin is > 0 ! (also checked in SMS... :)
        if ( set->tofBin < 1 )
        {
            syslog( LOG_ERR,
                "[%i] %s %s %s Histogram Config Issue: Time Bin %u < 1",
                g_pid, "STS Error:", "Detector Bank Set",
                set->name.c_str(), set->tofBin );
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
vector<STS::DetectorBankSet *>
StreamParser::getDetectorBankSets
(
    uint32_t a_bank_id    ///< [in] Detector Bank Id (uint32_t)
)
{
    vector<STS::DetectorBankSet *> bank_sets;

    // Any Detector Bank Sets...? (If not, we're done.)
    if (m_bank_sets.size() == 0)
        return(bank_sets); // empty...

    // Look for Detector Bank Sets with matching Detector Bank Ids...
    for ( vector<STS::DetectorBankSet *>::iterator dbs =
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
    STS::DetectorBankSet *a_bank_set  ///< [in] Detector Bank Set (ptr)
)
{
    // Look for Detector Banks that are Listed in This Set...
    //    - append to given BankInfo Bank Sets vector

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin();
            ibi != m_banks.end(); ++ibi )
    {
        if ( !*ibi )
            continue;

        for ( vector<uint32_t>::iterator b = a_bank_set->banklist.begin();
                b != a_bank_set->banklist.end(); ++b )
        {
            if ((*b) == (*ibi)->m_id)
            {
                syslog( LOG_INFO,
                    "[%i] %s: Bank Set \"%s\" Associated with Bank Id %d.",
                    g_pid, "StreamParser::associateDetectorBankSet()",
                    a_bank_set->name.c_str(), (*ibi)->m_id );

                (*ibi)->m_bank_sets.push_back(a_bank_set);
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
 * shutdown() for the sending side of the SMS-STS socket,
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
        syslog( LOG_INFO, "[%i] Data Done Received.", g_pid );
    }

    else if ( m_processing_state != DONE_PROCESSING )
    {
        syslog( LOG_INFO,
            "[%i] STS failed: Data Done Received, Not Done Processing!",
            g_pid );

        if ( m_processing_state == PROCESSING_EVENTS )
        {
            syslog( LOG_INFO,
                "[%i] Data Done Received, Still Processing Events!",
                g_pid );

            // On fatal error, flush buffers to Nexus before terminating
            markerComment( m_pulse_info.last_time,
                "Stream processing terminated abnormally." );
            m_run_metrics.end_time = nsec_to_timespec(
                m_pulse_info.start_time + m_pulse_info.last_time );
            finalizeStreamProcessing();
        }

        THROW_TRACE( ERR_GENERAL_ERROR,
            "ADARA stream ended unexpectedly." );
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
                                                if (
                                                    !ienum->second[i].name.
                                                        compare( value ) )
                                                {
                                                    pv_enum_vector =
                                                       &(ienum->second);
                                                    pv_enum_index = i;
                                                }
                                            }
                                        }

                                        // We Didn't Find the Enum Type!
                                        if ( pv_enum_vector == NULL )
                                        {
                                            stringstream ss;
                                            ss << "STS Error: "
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
                                        }
                                    }
                                }

                                else if ( xmlStrcmp( pvnode->name,
                                        (const xmlChar*)"pv_units" ) == 0 )
                                {
                                    pv_units = value;
                                }
                            }

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
                                // or units of an previously defined PV
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
                                // eventually happen. The STS will try
                                // to handle name collisions without
                                // aborting, but the above behavior that
                                // tries to gracefully handle pvsd restarts
                                // enables a fatal error condition IF the
                                // PV being replaced is actually a
                                // duplicate PV that is still live.
                                // On the next value update of the
                                // replaced PV, the STS will detect that
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
                                // updates are received for the
                                // over-written definition, the STS will
                                // throw an error.

                                PVKey key(a_pkt.devId(),pv_id);
                                map<PVKey,PVInfoBase*>::iterator ipv =
                                    m_pvs_by_key.find( key );
                                map<string,PVKey>::iterator xref;

                                if ( ipv == m_pvs_by_key.end() )
                                {
                                    // This is a NEW key - see if there is
                                    // an existing entry (by dev:name)
                                    // that matches this PV EXACTLY

                                    bool create = true;
                                    xref = m_pv_name_xref.find(
                                        dev_name + ":" + pv_name
                                            + ":" + pv_connection );

                                    if ( xref != m_pv_name_xref.end() )
                                    {
                                        ipv = m_pvs_by_key.find(
                                            xref->second );
                                        if ( ipv != m_pvs_by_key.end())
                                        {
                                            if ( ipv->second->
                                                sameDefinition( dev_name,
                                                    pv_name,
                                                    pv_connection,
                                                    pv_type,
                                                    pv_enum_vector,
                                                    pv_enum_index,
                                                    pv_units ) )
                                            {
                                                // There is an existing
                                                // PVInfo instance with
                                                // EXACTLY the same
                                                // definition.
                                                // This will happen if
                                                // PVSD restarts during
                                                // a recording.

                                                // Re-use this entry
                                                create = false;

                                                // Update key mappings
                                                m_pvs_by_key[key] =
                                                    ipv->second;
                                                m_pvs_by_key.erase( ipv );

                                                // Update xref entry to
                                                // point to new key
                                                xref->second = key;
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

                                                // Existing entry differs,
                                                // flush and delete
                                                // old entry
                                                ipv->second->flushBuffers(
                                                    0 );
                                                delete ipv->second;

                                                // Update maps
                                                m_pvs_by_key.erase( ipv );
                                                m_pv_name_xref.erase(
                                                    xref );

                                                // New PVInfo entry will
                                                // be created below
                                            }
                                        }
                                        else
                                        {
                                            // This is an error - there
                                            // shouldn't be an xref entry
                                            // without a corresponding
                                            // PVInfo entry.
                                            // Delete bad xref entry
                                            m_pv_name_xref.erase( xref );
                                        }
                                    }

                                    // If no existing matching PV was found
                                    // make a new one
                                    if ( create )
                                    {
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
                                                pv_units );
                                        if ( info )
                                        {
                                            m_pvs_by_key[key] = info;
                                            m_pv_name_xref[
                                                dev_name + ":" + pv_name
                                                    + ":" + pv_connection ]
                                                = key;
                                        }
                                    }
                                }
                                else
                                {
                                    // PV Key is already in use -
                                    // An existing key will be received
                                    // here when the SMS re-broadcasts
                                    // DDPs at file boundaries (in which
                                    // case the definitions will match).
                                    // They can also be received if pvsd
                                    // is reconfigured (in which case the
                                    // definition can be different).
                                    // Pvsd tries to keep ID-to-Name
                                    // mappings consistent accross re-
                                    // configurations, but it may
                                    // not always be able to.

                                    if ( !ipv->second->sameDefinition(
                                            dev_name,
                                            pv_name,
                                            pv_connection,
                                            pv_type,
                                            pv_enum_vector,
                                            pv_enum_index,
                                            pv_units ) )
                                    {
                                        // Did the name change?
                                        if ( dev_name !=
                                                ipv->second->m_device_name
                                            || pv_name !=
                                                ipv->second->m_name
                                            || pv_connection !=
                                                ipv->second->m_connection )
                                        {
                                            // Delete existing name-key
                                            // xref entry
                                            xref = m_pv_name_xref.find(
                                                ipv->second->m_device_name
                                                + ":"
                                                + ipv->second->m_name
                                                + ":"
                                                + ipv->second->m_connection
                                            );
                                            if ( xref !=
                                                    m_pv_name_xref.end() )
                                            {
                                                m_pv_name_xref.erase(
                                                    xref );
                                            }

                                            // There is nothing the STS
                                            // can do if there is a name
                                            // conflict here, the existing
                                            // name-to-key entry will be
                                            // overwritten below
                                            // (if PV type is supported)
                                        }

                                        // PV definition has changed,
                                        // flush and replace existing
                                        // PVInfo object
                                        ipv->second->flushBuffers(0);
                                        delete ipv->second;

                                        // If PV type not supported,
                                        // makePVInfo will return NULL
                                        info = makePVInfo( dev_name,
                                            pv_name,
                                            pv_connection,
                                            a_pkt.devId(),
                                            pv_id,
                                            pv_type,
                                            pv_enum_vector,
                                            pv_enum_index,
                                            pv_units );
                                        if ( info )
                                        {
                                            m_pvs_by_key[key] = info;
                                            m_pv_name_xref[
                                                dev_name + ":" + pv_name
                                                    + ":" + pv_connection ]
                                                = key;
                                        }
                                        else
                                        {
                                            m_pvs_by_key.erase( ipv );
                                        }
                                    }
                                }
                            }
                            else
                            {
                                stringstream ss;
                                ss << "STS Error:"
                                    << " Skipping PV "
                                    << " devId=" << a_pkt.devId()
                                    << " pvId=" << pv_id;
                                syslog( LOG_ERR, "[%i] %s", g_pid,
                                    ss.str().c_str() );
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
                                        ss << "STS Error:"
                                            << " Skipping Incomplete"
                                            << " Enum Element"
                                            << " devId=" << a_pkt.devId()
                                            << " enumName=" << devEnum.name
                                            << " elemName=" << elem_name
                                            << " elemValue=" << elem_value;
                                        syslog( LOG_ERR, "[%i] %s", g_pid,
                                            ss.str().c_str() );
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
                                                &devEnum ) )
                                        {
                                            // Don't Log Duplicate Enums
                                            // (DDP is Duplicated at
                                            // _Every_ File Boundary! ;-)
                                            // stringstream ss;
                                            // ss << "STS Error:"
                                                // << " Device "
                                                // << a_pkt.devId()
                                                // << " Duplicate Enum "
                                                // << devEnum.name
                                                // << " Ignoring...";
                                            // syslog( LOG_ERR,
                                                // "[%i] %s", g_pid,
                                                // ss.str().c_str() );
                                            addEnum = false;
                                        }
                                        else if ( !ienum->second[i].name
                                                .compare( devEnum.name ) )
                                        {
                                            stringstream ss;
                                            ss << "STS Error:"
                                                << " Device "
                                                << a_pkt.devId()
                                                << " Enum " << devEnum.name
                                                << " Name Clash!"
                                                << " (Include Anyway)";
                                            syslog( LOG_ERR,
                                                "[%i] %s", g_pid,
                                                ss.str().c_str() );
                                            // Still Include Enum in File?
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

                                        ienum->second.push_back( devEnum );
                                    }
                                }
                            }
                            else
                            {
                                stringstream ss;
                                ss << "STS Error:"
                                    << " Skipping Incomplete Enum "
                                    << " devId=" << a_pkt.devId()
                                    << " enumName=" << devEnum.name;
                                syslog( LOG_ERR, "[%i] %s", g_pid,
                                    ss.str().c_str() );
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

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (uint32) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes unsigned integer Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA unsigned integer Variable Update packets. See the pvValueUpdate() template
 * method in StreamParser.h for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableU32Pkt &a_pkt  ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<uint32_t>( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp() );

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (double) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes double-prec floating point Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA double-precision floating point Variable Update packets. See the pvValueUpdate() template
 * method in StreamParser.h for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableDoublePkt &a_pkt ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<double>( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp() );

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Variable (string) packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes character string Variable Update ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA character string Variable Update packets. See the pvValueUpdate() template
 * method in StreamParser.h for more details.
 */
bool
StreamParser::rxPacket
(
    const ADARA::VariableStringPkt &a_pkt ///< [in] The ADARA Variable Update packet to process
)
{
    pvValueUpdate<string>( a_pkt.devId(), a_pkt.varId(), a_pkt.value(), a_pkt.timestamp() );

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
    double t = 0;

    // Note: if first pulse has not arrived, truncate all PV times to 0
    if ( m_pulse_info.start_time )
    {
        uint64_t t1 = timespec_to_nsec( a_pkt.timestamp() );
        // Truncate negative time offsets to 0
        if ( t1 > m_pulse_info.start_time )
            t = ( t1 - m_pulse_info.start_time ) / 1000000000.0;
    }

    // Switch on event type
    switch ( a_pkt.marker_type() )
    {
    case ADARA::MarkerType::GENERIC:
        markerComment( t, a_pkt.comment() );
        break;
    case ADARA::MarkerType::PAUSE:
        markerPause( t, a_pkt.comment() );
        break;
    case ADARA::MarkerType::RESUME:
        markerResume( t, a_pkt.comment() );
        break;
    case ADARA::MarkerType::SCAN_START:
        markerScanStart( t, a_pkt.scanIndex(), a_pkt.comment() );
        break;
    case ADARA::MarkerType::SCAN_STOP:
        markerScanStop( t, a_pkt.scanIndex(), a_pkt.comment() );
        break;
    case ADARA::MarkerType::OVERALL_RUN_COMMENT:
        runComment( a_pkt.comment() );
        break;
    }

    return false;
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

/*! \brief Processes state of received informational packets
 *
 * This method tracks which ADARA informational packets have been received and issues a processRunInfo() call once all
 * packets have been received.
 */
void
StreamParser::receivedInfo( InfoBit a_bit )
{
    m_info_rcvd |= a_bit;
    if ( m_info_rcvd == ALL_INFO_RCVD )
    {
        processRunInfo( m_run_info );
        m_info_rcvd |= INFO_SENT;

        syslog( LOG_INFO,
            "[%i] target_station: %u, beam: %s:%s, prop: %s, run: %lu",
            g_pid,
            m_run_info.target_station_number,
            m_run_info.facility_name.c_str(),
            m_run_info.instr_shortname.c_str(),
            m_run_info.proposal_id.c_str(),
            m_run_info.run_number );
    }
}


/*! \brief This method performs final stream processing.
 *
 * This method is called after the internal processing state progreses
 * to "DONE_PROCESSING" and permits a variety of final processing tasks
 * to be performed (primarily flushing data buffers). This method also
 * calls the virtual finalize() method to allow the stream adapter to
 * also perform final output operations.
 */
void
StreamParser::finalizeStreamProcessing()
{
    // Make sure neutron pulses were received

    if ( !m_run_metrics.charge_stats.count() && m_strict )
    {
        THROW_TRACE( ERR_UNEXPECTED_INPUT,
            "No neutron pulses received in stream.")
    }

    // Write any remaining data in bank buffers

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin();
            ibi != m_banks.end(); ++ibi )
    {
        if ( !*ibi )
            continue;

        // Make Sure Data has been (Late) Initialized...
        if ( !((*ibi)->m_initialized) )
            (*ibi)->initializeBank( true );

        // Detect gaps in bank data and fill event index if present
        if ( (*ibi)->m_last_pulse_with_data < m_pulse_count )
        {
            handleBankPulseGap( **ibi,
                m_pulse_count - (*ibi)->m_last_pulse_with_data );
        }

        // Flush bank buffers
        if ( (*ibi)->m_tof_buffer_size || (*ibi)->m_index_buffer.size() )
            bankBuffersReady( **ibi );

        bankFinalize( **ibi );
    }

    // Write any remaining data in monitor buffers

    for ( map<Identifier,MonitorInfo*>::iterator imi = m_monitors.begin();
            imi != m_monitors.end(); ++imi )
    {
        // Event-based Monitors Only...
        if ( imi->second->m_config == NULL )
        {
            // Detect gaps in monitor data and fill event index if present
            if ( imi->second->m_last_pulse_with_data < m_pulse_count )
            {
                handleMonitorPulseGap( *imi->second,
                    m_pulse_count - imi->second->m_last_pulse_with_data );
            }

            // Flush monitor buffers
            if ( imi->second->m_tof_buffer_size
                || imi->second->m_index_buffer.size() )
            {
                monitorBuffersReady( *imi->second );
            }
        }

        // All Beam Monitors...
        monitorFinalize( *imi->second );
    }

    // Write remaining pulse info and statistics

    if ( m_pulse_info.times.size())
        pulseBuffersReady( m_pulse_info );

    // Write any Enumerated Types, per DeviceId, into logs...

    for ( map<Identifier,std::vector<PVEnumeratedType> >::iterator ienum =
                m_enums_by_dev.begin();
            ienum != m_enums_by_dev.end(); ++ienum )
    {
        writeDeviceEnums( ienum->first, ienum->second );
    }

    // Write any remaining data in PV buffers

    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs_by_key.begin();
            ipv != m_pvs_by_key.end(); ++ipv )
    {
        // *Always* Flush Buffers at End, to Get Run Metrics for PVs...!
        ipv->second->flushBuffers( &m_run_metrics );
    }

    // Let adapter do anything else it wants to
    finalize( m_run_metrics );
}


/*! \brief This method retrieves tha human-readable name of an ADARA packet type.
 *
 * This method retrieves tha human-readable name of an ADARA packet type.
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
        case ADARA::PacketType::BEAM_MONITOR_EVENT_TYPE:
            ss << "Beam Monitor Event"; break;
        case ADARA::PacketType::PIXEL_MAPPING_TYPE:
            ss << "Pix Map"; break;
        case ADARA::PacketType::RUN_STATUS_TYPE:
            ss << "Run Stat"; break;
        case ADARA::PacketType::RUN_INFO_TYPE:
            ss << "Run Info"; break;
        case ADARA::PacketType::TRANS_COMPLETE_TYPE:
            ss << "Tran Comp"; break;
        case ADARA::PacketType::CLIENT_HELLO_TYPE:
            ss << "Cli Hello"; break;
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
    if ( boost::iequals( a_source, "integer" ))
        return PVT_INT;
    else if ( boost::iequals( a_source, "unsigned" ))
        return PVT_UINT;
    else if ( boost::iequals( a_source, "unsigned integer" ))
        return PVT_UINT;
    else if ( boost::iequals( a_source, "double" ))
        return PVT_DOUBLE;
    else if ( boost::iequals( a_source, "float" ))
        return PVT_FLOAT;
    else if ( boost::iequals( a_source, "string" ))
        return PVT_STRING;
    else if ( boost::istarts_with( a_source, "enum_" ))
        return PVT_ENUM;

    THROW_TRACE( ERR_UNEXPECTED_INPUT, "Invalid PV type." )
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

} // End namespace STS

// vim: expandtab

