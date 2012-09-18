#include "StreamParser.h"
#include "TransCompletePkt.h"
#include <iomanip>
#include <glog/logging.h>
#include <libxml/tree.h>
#include <boost/algorithm/string.hpp>


using namespace std;

namespace STS {

/// This sets the size of the ADARA parser stream buffer in bytes (bigger is faster)
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
    Parser(ADARA_IN_BUF_SIZE, ADARA_IN_BUF_SIZE),
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
    m_skipped_pkt_count(0)
{
    if ( !a_adara_out_file.empty() )
    {
        m_gen_adara = true;
        m_ofs_adara.open( a_adara_out_file.c_str(), ios_base::out | ios_base::binary );
    }

    m_pulse_info.times.reserve(m_anc_buf_write_thresh);
    m_pulse_info.freqs.reserve(m_anc_buf_write_thresh);
}


/*! \brief Destructor for StremParser class.
 *
 */
StreamParser::~StreamParser()
{
    if ( m_ofs_adara.is_open())
        m_ofs_adara.close();

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin(); ibi != m_banks.end(); ++ibi )
        if ( *ibi )
            delete *ibi;

    for ( map<Identifier,MonitorInfo*>::iterator imi = m_monitors.begin(); imi != m_monitors.end(); ++imi )
        delete imi->second;

    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
        if ( ipv->second )
            delete ipv->second;
}


/*! \brief This method initiates ADARA stream processing.
 *
 * This method initiates ADARA stream processing on the calling thread and does not return until the strem is fuly
 * translated, or an error occurs (an exception may be thrown). This method can only be called once for a given
 * StreamParser instance.
 */
void
StreamParser::processStream()
{
    // If anything goes wrong with translation, an exception will be thrown to caller of this method

    if ( m_processing_state != PROCESSING_NOT_STARTED )
        THROW_TRACE( ERR_INVALID_OPERATION, "StreamParser::processStream() can not be called more than once." )

    try
    {
        initialize();
        m_processing_state = WAITING_FOR_RUN_START;

        while( m_processing_state < DONE_PROCESSING )
        {
            read( m_fd, ADARA_IN_BUF_SIZE );
        }
    }
    catch ( TraceException &e )
    {
        RETHROW_TRACE( e, "processStream() failed." )
    }
    catch ( exception &e )
    {
        THROW_TRACE( ERR_GENERAL_ERROR, "processStream() exception {" << e.what() << "}" )
    }
    catch ( ... )
    {
        THROW_TRACE( ERR_GENERAL_ERROR, "processStream() unexpected exception." )
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
        a_os << endl << "Pkt Type\t\t\tCount     \tTotal KB  \tMin Size  \tMax Size  " << endl;
        for ( map<uint32_t,PktStats>::const_iterator i = m_stats.begin(); i != m_stats.end(); ++i )
        {
            a_os << hex << setw(8) << left << i->first << "\t" << setw(12) << getPktName((ADARA::PacketType::Enum)i->first)
                 << "\t" << dec << setw(10) << i->second.count << "\t" << setw(10) << (i->second.total_size >> 10)
                 << "\t" << setw(10) << i->second.min_pkt_size << "\t" << setw(10) << i->second.max_pkt_size << endl;
        }
        a_os << endl << "Packets skipped: " << m_skipped_pkt_count << endl;
        a_os << "Pulse charge stats: " << m_run_metrics.charge_stats << endl;
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
#define PKT_BIT_PIXELMAP    0x0001
#define PKT_BIT_RUNINFO     0x0002
#define PKT_BIT_BEAMINFO    0x0004
#define PKT_BIT_GEOMETRY    0x0008

#define PROCESS_IN_STATES(s)            \
    if ( m_processing_state & (s))      \
    {                                   \
        return Parser::rxPacket(a_pkt);   \
    }                                   \
    else                                \
    {                                   \
        if ( m_gather_stats )           \
            ++m_skipped_pkt_count;      \
        return false;                   \
    }

#define PROCESS_IN_STATES_ONCE(s,x)     \
    if (( m_processing_state & (s)) && !(m_pkt_recvd & x))    \
    {                                   \
        m_pkt_recvd |= x;               \
        return Parser::rxPacket(a_pkt);   \
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

    switch (a_pkt.type())
    {
    // These packets shall always be processed
    case ADARA::PacketType::RUN_STATUS_V0:
        return Parser::rxPacket(a_pkt);

    // These packets shall be processed ONCE during header and event processing
    // Note: these should arrive before event processing, but it is no guaranteed.
    case ADARA::PacketType::PIXEL_MAPPING_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_PIXELMAP)

    case ADARA::PacketType::RUN_INFO_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_RUNINFO)

    case ADARA::PacketType::GEOMETRY_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_GEOMETRY)

    case ADARA::PacketType::BEAMLINE_INFO_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_BEAMINFO)

    // These packets shall be processed during header & event processing
    case ADARA::PacketType::DEVICE_DESC_V0:
    case ADARA::PacketType::VAR_VALUE_U32_V0:
    case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
        PROCESS_IN_STATES(PROCESSING_RUN_HEADER|PROCESSING_EVENTS)

    // These packets shall only be processed during event processing
    case ADARA::PacketType::BANKED_EVENT_V0:
    case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
        PROCESS_IN_STATES(PROCESSING_EVENTS)

    // Packet types that are not processes by StreamParser
    case ADARA::PacketType::RAW_EVENT_V0:
    case ADARA::PacketType::RTDL_V0:
    case ADARA::PacketType::SOURCE_LIST_V0:
    case ADARA::PacketType::TRANS_COMPLETE_V0:
    case ADARA::PacketType::CLIENT_HELLO_V0:
    case ADARA::PacketType::STATS_RESET_V0:
    case ADARA::PacketType::SYNC_V0:
    case ADARA::PacketType::HEARTBEAT_V0:
    case ADARA::PacketType::VAR_VALUE_STRING_V0:
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
            m_processing_state = PROCESSING_RUN_HEADER;
        else
            bad_state = true;
    }
    else if ( a_pkt.status() == ADARA::RunStatus::END_RUN )
    {
        if ( m_processing_state == PROCESSING_EVENTS )
        {
            // Run "end time" is defined as time of last pulse (which is nanoseconds epoch offset)
            m_run_metrics.end_time = nsec_to_timespec( m_pulse_info.start_time + m_pulse_info.last_time );

            finalizeStreamProcessing();
            m_processing_state = DONE_PROCESSING;
        }
        else
            bad_state = true;
    }

    if ( m_strict && bad_state )
        THROW_TRACE( ERR_UNEXPECTED_INPUT, "Recvd Run Status pkt in wrong state.")

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Pixel Mapping packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Pixel Mapping ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA PixelMapping packets. Detector source and bank information is extracted from the received
 * packet and BankInfo instances are created (using the makeBankInfo() virtual factory method) to capture essential bank
 * parameters need for subsequent bnked event processing. The receipt of a Pixel Mapping packets also triggers
 * progression to the internal event processing state.
 */
bool
StreamParser::rxPacket
(
    const ADARA::PixelMappingPkt &a_pkt     ///< [in] ADARA PixelMappingPkt object to process
)
{
    const uint32_t *rpos = (const uint32_t*)a_pkt.payload();
    const uint32_t *epos = (const uint32_t*)(a_pkt.payload() + a_pkt.payload_length());
    //uint32_t        bank_logical_id;
    uint16_t        bank_id;
    uint16_t        pix_count;

    // Note: a vector is used for BankInfo instances where the bank_id is the offset into the vector. This is safe
    // as bank IDs are monotonically increasing integers starting at 0. IF this ever changes, then the bank
    // container will need to be changed to a map (which would result in a slight performance drop). Also note that
    // the current code accommodates gaps in the banks by zeroing and subsequently checking entries when iterating
    // over the container.

    // Count number of banks (largest bank id) in payload and reserve bank container storage
    uint16_t bank_count = 0;
    const uint32_t *rpos2 = rpos;

    while( rpos2 < epos )
    {
        rpos2++;
        bank_id = (uint16_t)(*rpos2 >> 16);
        pix_count = (uint16_t)(*rpos2 & 0xFFFF);
        rpos2++;
        if ( bank_id > bank_count )
            bank_count = bank_id;
        rpos2 += pix_count;
    }

    m_banks.resize(bank_count+1,0);

    // Now build banks and populate bank container
    while( rpos < epos )
    {
        rpos++;  // TODO This infomation is not currently processed.
        bank_id = (uint16_t)(*rpos >> 16);
        pix_count = (uint16_t)(*rpos & 0xFFFF);
        rpos++;

        if ( !m_banks[bank_id] )
            m_banks[bank_id] = makeBankInfo( bank_id, pix_count, m_event_buf_write_thresh, m_anc_buf_write_thresh );

        rpos += pix_count;
    }

    // The receipt of a pixel mapping packet allows state to progress to event processing
    m_processing_state = PROCESSING_EVENTS;

    return false;
}


//---------------------------------------------------------------------------------------------------------------------
// ADARA Banked Event packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Banked Event ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Banked Event packets. The processPulseInfo() method is called by ths method with the
 * pulse data attached to the received Banked Event packet. The payload of the Banked Event packet is parsed for neutron
 * events which are then handled by the processBankEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BankedEventPkt &a_pkt      ///< [in] ADARA BankedEventPkt object to process
)
{
    processPulseID( a_pkt.pulseId() );
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
    m_run_metrics.total_charge += a_pkt.pulseCharge();
    m_pulse_info.charges.push_back( a_pkt.pulseCharge() );
    m_run_metrics.charge_stats.push( a_pkt.pulseCharge() );

    if ( m_pulse_info.start_time )
    {
        uint64_t pulse_time = timespec_to_nsec( a_pkt.timestamp() ) - m_pulse_info.start_time;
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
        m_pulse_info.charges.clear();
    }
}


/*! \brief This method processes the neutron events for a specific detector bank.
 *
 * This method processes incoming neutron events for a specified detector bank. The events are read from the packet
 * and placed into internal event buffers (units are converted for event time of flight). When the event buffers are
 * full, they are flushed to a subclassed stream adapter via the bankBuffersReady() virtual method. This method also
 * detects pulse gaps and corrects the event index as required (see handleBankPulseGap() method for more details).
 */
void
StreamParser::processBankEvents
(
    uint32_t        a_bank_id,        ///< [in] Bank ID of detector bank to be processed
    uint32_t        a_event_count,    ///< [in] Number of events contained in stream buffer
    const uint32_t *a_rpos            ///< [in] Stream event buffer read pointer
)
{
    if ( a_bank_id < m_banks.size() )
    {
        BankInfo *bi = m_banks[a_bank_id];

        // Detect gaps in event data and fill event index if present
        if ( bi->m_last_pulse_with_data < ( m_pulse_count - 1 ))
            handleBankPulseGap( *bi, ( m_pulse_count - 1 ) - bi->m_last_pulse_with_data );

        size_t sz = bi->m_tof_buffer.size();

        bi->m_tof_buffer.resize( sz + a_event_count );
        bi->m_pid_buffer.resize( sz + a_event_count );

        float           *tof_ptr = &bi->m_tof_buffer[sz];
        uint32_t        *pid_ptr = &bi->m_pid_buffer[sz];
        const uint32_t  *epos = a_rpos + (a_event_count<<1);

        while ( a_rpos != epos )
        {
            // ADARA TOF values are in units of 100 ns - convert to microseconds
            *tof_ptr++ = *a_rpos++ / 10.0;
            *pid_ptr++ = *a_rpos++;
        }

        // Cache event index until large enough to write
        bi->m_index_buffer.push_back( bi->m_event_count );
        bi->m_event_count += a_event_count;

        bi->m_last_pulse_with_data = m_pulse_count;

        // Check to see if buffers are ready to write
        if ( bi->m_tof_buffer.size() >= m_event_buf_write_thresh || bi->m_index_buffer.size() >= m_anc_buf_write_thresh )
        {
            bankBuffersReady( *bi );

            bi->m_tof_buffer.clear();
            bi->m_pid_buffer.clear();
            bi->m_index_buffer.clear();
        }

        m_run_metrics.events_counted += a_event_count;
    }
    else
        m_run_metrics.events_uncounted += a_event_count;
}

/*! \brief This method handles pulse gaps for a specified detector bank
 *
 * This method handles pulse gaps in the event stream for the specified detectpr bank. When a gap is detected, the event
 * index for the bank must be corrected for the missing pulses to keep in synchronized with the event stream. If a small
 * gap is detected, values are inserted directly into the internal index buffer; otherwise, gap processing is deferred
 * to the stream adatapter subclass via the bankPulseGap() virtual method. (It is expected that the virtual method
 * should write index values directly into the destination format to prevent excessive memory consumption that would
 * be caused by buffering the corrected index.)
 */
void
StreamParser::handleBankPulseGap
(
    BankInfo &a_bi,     ///< [in] A BankInfo instance with a pulse gap
    uint64_t a_count    ///< [in] The size of the pulse gap
)
{
    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_bi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh )
    {
        a_bi.m_index_buffer.resize( a_bi.m_index_buffer.size() + a_count, a_bi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large - flush buffers & fill gap
        // Note: it is acceptable to call bankBuffersReady even if they are empty.
        bankBuffersReady( a_bi );
        bankPulseGap( a_bi, a_count );

        a_bi.m_tof_buffer.clear();
        a_bi.m_pid_buffer.clear();
        a_bi.m_index_buffer.clear();
    }
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Beam Monitor packet processing
//---------------------------------------------------------------------------------------------------------------------

/*! \brief This method processes Monitor Event ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Monitor Event packets. The payload of the Monitor Event packet is parsed for neutron
 * events which are then handled by the processMonitorEvents() method.
 */
bool
StreamParser::rxPacket
(
    const ADARA::BeamMonitorPkt &a_pkt  ///< [in] ADARA BankedEventPkt object to process
)
{
    processPulseID( a_pkt.pulseId() );

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

        // TODO What do we do with the pulse info from beam monitor packets?
        rpos += 2; // Skip over source info (source ID & tof offset)

        processMonitorEvents( monitor_id, event_count, rpos );
        rpos += event_count;
    }

    return false;
}


/*! \brief This method processes the neutron events for a specific monitor.
 *
 * This method processes incoming neutron events for a specified monitor. The events are read from the packet
 * and placed into internal event buffers (units are converted for event time of flight). When the event buffers are
 * full, they are flushed to a subclassed stream adapter via the monitorBuffersReady() virtual method. This method also
 * detects pulse gaps and corrects the event index as required (see handleMonitorPulseGap() method for more details).
 * Note that unlike banked events, monitors events do not contain a pixel ID field (pid) as all events originate from
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
    map<Identifier,MonitorInfo*>::iterator imi = m_monitors.find( a_monitor_id );
    if ( imi == m_monitors.end())
    {
        MonitorInfo *mi = makeMonitorInfo( a_monitor_id, m_event_buf_write_thresh, m_anc_buf_write_thresh );
        imi = m_monitors.insert( m_monitors.begin(), pair<Identifier,MonitorInfo*>(a_monitor_id,mi));
    }

    // Detect gaps in event data and fill event index if present
    if ( imi->second->m_last_pulse_with_data < ( m_pulse_count - 1 ))
        handleMonitorPulseGap( *imi->second, ( m_pulse_count - 1 ) - imi->second->m_last_pulse_with_data );

    size_t sz = imi->second->m_tof_buffer.size();

    imi->second->m_tof_buffer.resize( sz + a_event_count );

    float           *tof_ptr = &imi->second->m_tof_buffer[sz];
    const uint32_t  *epos = a_rpos + a_event_count;

    while ( a_rpos != epos )
    {
        // ADARA TOF values are in units of 100 ns - convert to microseconds
        // TOF values is lower 21 bits
        *tof_ptr++ = ((*a_rpos++)&0x1fffff) / 10.0;
    }

    // Cache event index until large enough to write
    imi->second->m_index_buffer.push_back( imi->second->m_event_count );
    imi->second->m_event_count += a_event_count;
    imi->second->m_last_pulse_with_data = m_pulse_count;

    // Check to see if buffers are ready to write
    if ( imi->second->m_tof_buffer.size() >= m_event_buf_write_thresh || imi->second->m_index_buffer.size() >= m_anc_buf_write_thresh )
    {
        monitorBuffersReady( *imi->second );

        imi->second->m_index_buffer.clear();
        imi->second->m_tof_buffer.clear();
    }
}


/*! \brief This method handles pulse gaps for a specified monitor
 *
 * This method handles pulse gaps in the event stream for the specified monitor. When a gap is detected, the event
 * index for the monitor must be corrected for the missing pulses to keep in synchronized with the event stream. If a
 * small gap is detected, values are inserted directly into the internal index buffer; otherwise, gap processing is
 * deferred to the stream adatapter subclass via the monitorPulseGap() virtual method. (It is expected that the virtual
 * method should write index values directly into the destination format to prevent excessive memory consumption that
 * would be caused by buffering the corrected index.)
 */
void
StreamParser::handleMonitorPulseGap
(
    MonitorInfo    &a_mi,       ///< [in] A MonitorInfo instance with a pulse gap
    uint64_t        a_count     ///< [in] The size of the pulse gap
)
{
    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_mi.m_index_buffer.size() + a_count < m_anc_buf_write_thresh )
    {
        a_mi.m_index_buffer.resize( a_mi.m_index_buffer.size() + a_count, a_mi.m_event_count );
    }
    else
    {
        // Otherwise, if the gap is too large - flush current buffered data & fill index directly
        // Note: it is acceptable to call monitorBuffersReady even if they are empty.
        monitorBuffersReady( a_mi );
        monitorPulseGap( a_mi, a_count );

        a_mi.m_tof_buffer.clear();
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
        for ( xmlNode *node = xmlDocGetRootElement(doc)->children; node; node = node->next )
        {
            if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
                m_run_info.run_number = boost::lexical_cast<unsigned long>( (char*)node->children->content );
            else if ( xmlStrcmp( node->name, (const xmlChar*)"proposal_id" ) == 0)
                m_run_info.proposal_id = (char*)node->children->content;
            else if ( xmlStrcmp( node->name, (const xmlChar*)"run_title" ) == 0)
                m_run_info.run_title = (char*)node->children->content;
            else if (xmlStrcmp( node->name, (const xmlChar*) "facility_name") == 0)
                m_run_info.facility_name = (char*) node->children->content;
            else if ( xmlStrcmp( node->name, (const xmlChar*)"sample" ) == 0)
            {
                for ( xmlNode *sample_node = node->children; sample_node; sample_node = sample_node->next )
                {
                    if ( xmlStrcmp( sample_node->name, (const xmlChar*)"id" ) == 0)
                        m_run_info.sample_id = (char*)sample_node->children->content;
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"name" ) == 0)
                        m_run_info.sample_name = (char*)sample_node->children->content;
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"nature" ) == 0)
                        m_run_info.sample_nature = (char*)sample_node->children->content;
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"chemical_formula" ) == 0)
                        m_run_info.sample_formula = (char*)sample_node->children->content;
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"environment" ) == 0)
                        m_run_info.sample_environment = (char*)sample_node->children->content;
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
    m_run_info.instr_id =  a_pkt.id();
    m_run_info.instr_shortname =  a_pkt.shortName();
    m_run_info.instr_longname =  a_pkt.longName();

    receivedInfo( INSTR_INFO_BIT );

    return false;
}

//---------------------------------------------------------------------------------------------------------------------
// ADARA Device Descriptor packet processing
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method processes Device Descriptor ADARA packets
 *  \return Always returns false to allow parsing to continue
 *
 * This method processes ADARA Device Descriptor packets. Process variable information is extracted from the XML payload
 * of the packet and used to create type-specific PVInfo instances via the makePVInfo() virtual factory method. (A
 * stream adapter subclass should perform all required format-specific initialization within this factory method.)
 */
bool
StreamParser::rxPacket
(
    const ADARA::DeviceDescriptorPkt &a_pkt     ///< [in] The ADARA Device Descriptor Packet to process
)
{
    const string &xml =  a_pkt.description();

    xmlDocPtr doc = xmlReadMemory( xml.c_str(), xml.length(), 0, 0, 0 /* XML_PARSE_NOERROR | XML_PARSE_NOWARNING */ );
    if ( doc )
    {
        Identifier  pv_id = 0;
        string      pv_name;
        string      pv_units;
        PVType      pv_type = PVT_INT;
        short       found;

        xmlNode *root = xmlDocGetRootElement( doc );

        for ( xmlNode* lev1 = root->children; lev1 != 0; lev1 = lev1->next )
        {
            if ( xmlStrcmp( lev1->name, (const xmlChar*)"process_variables" ) == 0)
            {
                xmlNode *pvnode;

                for ( xmlNode *pvsnode = lev1->children; pvsnode; pvsnode = pvsnode->next )
                {
                    if ( xmlStrcmp( pvsnode->name, (const xmlChar*)"process_variable" ) == 0)
                    {
                        pv_units = "";
                        found = 0;

                        for ( pvnode = pvsnode->children; pvnode; pvnode = pvnode->next )
                        {
                            if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_name" ) == 0)
                            {
                                found |= 1;
                                pv_name = (char*)pvnode->children->content;
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_id" ) == 0)
                            {
                                found |= 2;
                                pv_id = boost::lexical_cast<Identifier>((char*)pvnode->children->content);
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_type" ) == 0)
                            {
                                found |= 4;
                                pv_type = toPVType( (char*)pvnode->children->content );
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_units" ) == 0)
                            {
                                pv_units = (char*)pvnode->children->content;
                            }
                        }

                        if ( found == 7 )
                        {
                            PVKey   key(a_pkt.devId(),pv_id);

                            if ( m_pvs.find(key) == m_pvs.end() )
                            {
                                m_pvs[key] = makePVInfo( pv_name, a_pkt.devId(), pv_id, pv_type, pv_units );
                            }
                        }
                        else
                        {
                           //TODO Log this: "Skipping PV " << a_pkt.devId() << "." << pv_id << endl;
                        }
                    }
                }
            }
            else if ( xmlStrcmp( lev1->name, (const xmlChar*)"enumerations" ) == 0)
            {
                // TODO Handle enumeration definitions
            }
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
// ADARA support methods
//---------------------------------------------------------------------------------------------------------------------


/*! \brief This method handles pules ID processing.
 *
 * This method examines a received pulse ID (parameter) and determines if it represents a new pulse (ID differs from
 * current pulse ID). If so, the pulse count is incremented and the new pulse ID retained. This method is needed to
 * decouple packet ordering dependencies between banked event packets and monitor packets.
 */
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
    }
}


/*! \brief This method performs final stream processing.
 *
 * This method is called after the internal processing state progreses to "DONE_PROCESSING" and permits a variety of
 * final processing tasks to be performed (primarily flushing data buffers). This method also calls the virtual
 * finalize() method to allow the stream adapter to also perform final output operations.
 */
void
StreamParser::finalizeStreamProcessing()
{
    // Make sure neutron pulses were received

    if ( !m_run_metrics.charge_stats.count() )
        THROW_TRACE( ERR_UNEXPECTED_INPUT, "No neutron pulses received in stream.")

    // Write any remaining data in bank buffers

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin(); ibi != m_banks.end(); ++ibi )
    {
        if ( !*ibi )
            continue;

        // Detect gaps in bank data and fill event index if present
        if ( (*ibi)->m_last_pulse_with_data < m_pulse_count )
            handleBankPulseGap( **ibi, m_pulse_count - (*ibi)->m_last_pulse_with_data );

        // Flush bank buffers
        if ( (*ibi)->m_tof_buffer.size() || (*ibi)->m_index_buffer.size() )
            bankBuffersReady( **ibi );

        bankFinalize( **ibi );
    }

    // Write any remaining data in monitor buffers

    for ( map<Identifier,MonitorInfo*>::iterator imi = m_monitors.begin(); imi != m_monitors.end(); ++imi )
    {
        // Detect gaps in monitor data and fill event index if present
        if ( imi->second->m_last_pulse_with_data < m_pulse_count )
            handleMonitorPulseGap( *imi->second, m_pulse_count - imi->second->m_last_pulse_with_data );

        // Flush monitor buffers
        if ( imi->second->m_tof_buffer.size() || imi->second->m_index_buffer.size() )
            monitorBuffersReady( *imi->second );

        monitorFinalize( *imi->second );
    }
    

    // Write remaining pulse info and statistics

    if ( m_pulse_info.times.size())
        pulseBuffersReady( m_pulse_info );

    // Write any remaining data in PV buffers

    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
    {
        if ( ipv->second->m_time_buffer.size() > 0 )
            ipv->second->flushBuffers( true );
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
    ADARA::PacketType::Enum a_pkt_type   ///< [in] An ADARA packet type (defined in ADARA.h)
) const
{
    // Mask out packet version number
    switch ( a_pkt_type & 0xFFFFFF00 )
    {
    case ADARA::PacketType::RAW_EVENT_V0:
        return "Raw Event";
    case ADARA::PacketType::RTDL_V0:
        return "RTDL";
    case ADARA::PacketType::SOURCE_LIST_V0:
        return "Src List";
    case ADARA::PacketType::BANKED_EVENT_V0:
        return "Bank Event";
    case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
        return "Beam Mon";
    case ADARA::PacketType::PIXEL_MAPPING_V0:
        return "Pix Map";
    case ADARA::PacketType::RUN_STATUS_V0:
        return "Run Stat";
    case ADARA::PacketType::RUN_INFO_V0:
        return "Run Info";
    case ADARA::PacketType::TRANS_COMPLETE_V0:
        return "Tran Comp";
    case ADARA::PacketType::CLIENT_HELLO_V0:
        return "Cli Hello";
    case ADARA::PacketType::STATS_RESET_V0:
        return "Stat Reset";
    case ADARA::PacketType::SYNC_V0:
        return "Sync";
    case ADARA::PacketType::HEARTBEAT_V0:
        return "Heart";
    case ADARA::PacketType::GEOMETRY_V0:
        return "Geom Info";
    case ADARA::PacketType::BEAMLINE_INFO_V0:
        return "Beam Info";
    case ADARA::PacketType::DEVICE_DESC_V0:
        return "DDP";
    case ADARA::PacketType::VAR_VALUE_U32_V0:
        return "VVP U32";
    case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
        return "VVP DBL";
    case ADARA::PacketType::VAR_VALUE_STRING_V0:
        return "VVP STR";
    }

    return "Unknown";
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
    else if ( boost::iequals( a_source, "double" ))
        return PVT_DOUBLE;
    else if ( boost::iequals( a_source, "float" ))
        return PVT_FLOAT;
    else if ( boost::istarts_with( a_source, "enum_" ))
        return PVT_ENUM;

    THROW_TRACE( ERR_UNEXPECTED_INPUT, "Invalid PV type." )
}


} // End namespace STS
