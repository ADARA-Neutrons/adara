#ifndef STREAMPARSER_H
#define	STREAMPARSER_H

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <libxml/tree.h>
#include "POSIXParser.h"
#include "Utils.h"
#include "stsdefs.h"
#include "TraceException.h"

namespace STS {


/*! \brief Base class that provides ADARA-specific parsing and translation
 *
 * The StreamParser class provides ADARA-specific parsing of an incoming data
 * stream and serves as a base class for format-specific adapter classes that
 * output the extracted data. The StreamParser class communicates with a sub-
 * class via the methods defined on the IStreamAdapter interface. The Stream-
 * Parser class operates on posix file descriptors (for stream input and
 * also output of a filtered adara file).
 */
class StreamParser : public ADARA::POSIXParser, public IStreamAdapter
{
public:
    StreamParser( int a_fd, const std::string & a_adara_out_file, bool a_strict, bool a_gather_stats = false,
                  uint32_t a_event_buf_write_thresh = 40960, uint32_t a_ancillary_buf_write_thresh = 4096 );

    virtual ~StreamParser();

    void    processStream();
    void    printStats( std::ostream &a_os ) const;

    std::string             getFacilityName() const { return m_run_info.facility_name; }
    std::string             getBeamShortName() const { return m_run_info.instr_shortname; }
    std::string             getProposalID() const { return m_run_info.proposal_id; }
    unsigned long           getRunNumber() const { return m_run_info.run_number; }
    bool                    infoReady() const { return (m_info_rcvd & INFO_SENT); }

private:
    /// Defines internal stream processing states of StreamParser class
    enum ProcessingState
    {
        PROCESSING_NOT_STARTED  = 0x0000,
        WAITING_FOR_RUN_START   = 0x0001,
        PROCESSING_RUN_HEADER   = 0x0002,
        PROCESSING_EVENTS       = 0x0004,
        DONE_PROCESSING         = 0x0008
    };

    /// Defines packet statistics that can be gathers and displayed
    struct PktStats
    {
        PktStats() : count(0), min_pkt_size(0), max_pkt_size(0), total_size(0) {}

        uint64_t    count;
        uint64_t    min_pkt_size;
        uint64_t    max_pkt_size;
        uint64_t    total_size;
    };

    /// Used to track reception of informational packets
    enum InfoBit
    {
        RUN_INFO_BIT    = 0x0001,
        INSTR_INFO_BIT  = 0x0002,
        ALL_INFO_RCVD   = 0x0003,
        INFO_SENT       = 0x1000
    };

    bool        rxPacket( const ADARA::Packet &a_pkt );
    bool        rxPacket( const ADARA::RunStatusPkt &a_pkt );
    bool        rxPacket( const ADARA::BankedEventPkt &a_pkt );
    bool        rxPacket( const ADARA::PixelMappingPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamMonitorPkt &a_pkt );
    bool        rxPacket( const ADARA::RunInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::GeometryPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamlineInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::AnnotationPkt &a_pkt );

    using ADARA::POSIXParser::rxPacket; // Shunt remaining rxPacket flavors to base class implementations

    void        processPulseInfo( const ADARA::BankedEventPkt &a_pkt );
    void        processBankEvents( uint32_t a_bank_id, uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleBankPulseGap( BankInfo &a_bi, uint64_t a_count );
    void        processMonitorEvents( Identifier a_monitor_id, uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleMonitorPulseGap( MonitorInfo &a_mi, uint64_t a_count );
    template<class T>
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id, T a_value, const timespec &a_timestamp );
    void        processPulseID( uint64_t a_pulse_id );
    void        receivedInfo( InfoBit a_bit );
    void        finalizeStreamProcessing();
    PVType      toPVType( const char *a_source ) const;
    inline void gatherStats( const ADARA::Packet &a_pkt ) const;
    const char* getPktName( ADARA::PacketType::Enum a_pkt_type ) const;
    void        getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const;

    int                                     m_fd;                       ///< Input ADARA stream file descriptor
    ProcessingState                         m_processing_state;         ///< Current (internal) processing state
    uint32_t                                m_pkt_recvd;                ///< Packet received-status bit mask
    std::ofstream                           m_ofs_adara;                ///< ADARA output file stream
    uint64_t                                m_pulse_id;                 ///< ID of current pulse
    uint64_t                                m_pulse_count;              ///< Internal pulse counter
    PulseInfo                               m_pulse_info;               ///< Neutron pulse data
    std::vector<BankInfo*>                  m_banks;                    ///< Container of detector bank information
    std::map<Identifier,MonitorInfo*>       m_monitors;                 ///< Container of monitor information
    std::map<PVKey,PVInfoBase*>             m_pvs_by_key;               ///< Container of process variable information (by key)
    std::map<std::string,PVInfoBase*>       m_pvs_by_name;              ///< Index of process variable information (by name)
    uint32_t                                m_event_buf_write_thresh;   ///< Event buffer write threshold (banks & monitors)
    uint32_t                                m_anc_buf_write_thresh;     ///< Ancillary buffer write threshold (indexes, PVs, etc)
    unsigned short                          m_info_rcvd;                ///< Tracks ADARA informational packets are received
    RunInfo                                 m_run_info;                 ///< Run (and instrument) information
    RunMetrics                              m_run_metrics;              ///< Run metrics
    bool                                    m_strict;                   ///< Controls strict ADARA processing option
    bool                                    m_gen_adara;                ///< Controls generation of ADARA output stream file
    bool                                    m_gather_stats;             ///< Controls gathering of stream statistics
    mutable std::map<uint32_t,PktStats>     m_stats;                    ///< Continer for per-packet-type statistics
    uint64_t                                m_skipped_pkt_count;        ///< Count of ADARA packets that were ignored
};


//---------------------------------------------------------------------------------------------------------------------
// StreamParser Inline / Template Method Implementations
//---------------------------------------------------------------------------------------------------------------------

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

    std::map<PVKey,PVInfoBase*>::iterator ipv = m_pvs_by_key.find(key);
    if ( ipv == m_pvs_by_key.end() )
        THROW_TRACE( ERR_PV_NOT_DEFINED, "pvValueUpdate() failed - PV " << a_device_id << "." << a_pv_id << " not defined." )

    PVInfo<T> *pvinfo = dynamic_cast<PVInfo<T>*>( ipv->second );
    if ( !pvinfo )
        THROW_TRACE( ERR_CAST_FAILED, "pvValueUpdate() failed - PV " << a_device_id << "." << a_pv_id << " not of correct type." )

    uint64_t ts_nano = timespec_to_nsec( a_timestamp );

    // Only process this update if the timestamp is newer than the last time. (m_last_time
    // is initialized to 0, so first real update will succeed.) This will reject PV updates
    // that are at negative time displacements and filter-out duplicate updates caused by
    // SMS file boundary crossings.
    if ( ts_nano > pvinfo->m_last_time )
    {
        double t = 0; // Relative time of update in seconds from first pulse of run

        // Note: if first pulse has not arrived, truncate all PV times to 0
        if ( m_pulse_info.start_time )
        {
            // Truncate negative time offsets to 0
            if ( ts_nano > m_pulse_info.start_time )
            {
                t = (ts_nano - m_pulse_info.start_time)/1000000000.0;
            }
            else if ( pvinfo->m_value_buffer.size() )
            {
                // Because the time value is 0, erase any values recvd before now
                // to avoid duplicate time entries.
                pvinfo->m_value_buffer.clear();
                pvinfo->m_time_buffer.clear();
                pvinfo->m_stats.reset();
            }
        }
        else if ( pvinfo->m_value_buffer.size() )
        {
            // If we recv multiple value updates before first pulse, keep only latest
            pvinfo->m_value_buffer.clear();
            pvinfo->m_time_buffer.clear();
            pvinfo->m_stats.reset();
        }

        pvinfo->m_last_time = ts_nano;
        pvinfo->m_value_buffer.push_back(a_value);
        pvinfo->m_time_buffer.push_back(t);
        pvinfo->m_stats.push(a_value);

        // Check for buffer write
        if ( pvinfo->m_value_buffer.size() >= m_anc_buf_write_thresh )
            pvinfo->flushBuffers();
    }
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
    if ( a_pkt.packet_length() < stats.min_pkt_size || !stats.min_pkt_size )
        stats.min_pkt_size = a_pkt.packet_length();
    if ( a_pkt.packet_length() > stats.max_pkt_size )
        stats.max_pkt_size = a_pkt.packet_length();
    stats.total_size += a_pkt.packet_length();
}


} // End namespace STS

#endif	/* STREAMPARSER_H */

