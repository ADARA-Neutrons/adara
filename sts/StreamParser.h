#ifndef STREAMPARSER_H
#define STREAMPARSER_H

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <libxml/tree.h>
#include "POSIXParser.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
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

    /// Used to Identify "Special" Detector Bank Indices (Error & Unmapped)
    enum SpecialBank
    {
        UNMAPPED_BANK   = 0xffffffff,
        ERROR_BANK      = 0xfffffffe
    };

    StreamParser( int a_fd, const std::string & a_adara_out_file,
        bool a_strict, bool a_gather_stats = false,
        uint32_t a_event_buf_write_thresh = 40960, // number of elems
        uint32_t a_ancillary_buf_write_thresh = 4096, // number of elems
        bool a_verbose = false );

    virtual ~StreamParser();

    void    processStream();
    void    printStats( std::ostream &a_os ) const;
    void    getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const;

    std::string getBeamShortName() const
        { return m_beamline_info.instr_shortname; }
    std::string getFacilityName() const
        { return m_run_info.facility_name; }
    std::string getProposalID() const
        { return m_run_info.proposal_id; }
    uint32_t getRunNumber() const
        { return m_run_info.run_number; }
    bool getNoSampleInfo() const
        { return m_run_info.no_sample_info; }

    bool infoReady() const
        { return (m_info_rcvd & INFO_SENT); }

private:

    typedef std::pair<uint32_t, uint32_t> BankIndex;

    typedef std::map< BankIndex, BankInfo * > BankInfoMap;

    /// Defines internal stream processing states of StreamParser class
    enum ProcessingState
    {
        PROCESSING_NOT_STARTED  = 0x0000,
        WAITING_FOR_RUN_START   = 0x0001,
        PROCESSING_RUN_HEADER   = 0x0002,
        PROCESSING_EVENTS       = 0x0004,
        DONE_PROCESSING         = 0x0008
    };

    /// Convert Processing State Enum to String Value (for Logging)
    std::string getProcessingStateString()
    {
        std::stringstream ss;
        switch ( m_processing_state )
        {
            case PROCESSING_NOT_STARTED:
                ss << "PROCESSING_NOT_STARTED"; break;
            case WAITING_FOR_RUN_START:
                ss << "WAITING_FOR_RUN_START"; break;
            case PROCESSING_RUN_HEADER:
                ss << "PROCESSING_RUN_HEADER"; break;
            case PROCESSING_EVENTS:
                ss << "PROCESSING_EVENTS"; break;
            case DONE_PROCESSING:
                ss << "DONE_PROCESSING"; break;
            default:
                ss << "<UnknownProcessingState>"; break;
        }
        ss << " (0x" << std::hex << m_processing_state << std::dec << ")";
        return( ss.str() );
    }

    /// Defines packet statistics that can be gathers and displayed
    struct PktStats
    {
        PktStats() :
            count(0), min_pkt_size(0), max_pkt_size(0), total_size(0) {}

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
    bool        rxPacket( const ADARA::BankedEventStatePkt &a_pkt );
    bool        rxPacket( const ADARA::PixelMappingPkt &a_pkt );
    bool        rxPacket( const ADARA::PixelMappingAltPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamMonitorPkt &a_pkt );
    bool        rxPacket( const ADARA::RunInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::GeometryPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamlineInfoPkt &a_pkt );
    bool        rxPacket( const ADARA::BeamMonitorConfigPkt &a_pkt );
    bool        rxPacket( const ADARA::DetectorBankSetsPkt &a_pkt );
    bool        rxPacket( const ADARA::DataDonePkt &a_pkt );
    bool        rxPacket( const ADARA::DeviceDescriptorPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::VariableStringPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableU32ArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::VariableDoubleArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::AnnotationPkt &a_pkt );

    bool        rxOversizePkt( const ADARA::PacketHeader *hdr,
                    const uint8_t *chunk, unsigned int chunk_offset,
                    unsigned int chunk_len );

    using ADARA::POSIXParser::rxPacket; // Shunt remaining rxPacket flavors
                                        // to base class implementations

    void        processPulseInfo( const ADARA::BankedEventPkt &a_pkt );
    void        processPulseInfo( const ADARA::BankedEventStatePkt &a_pkt );
    void        processBankEvents( uint32_t a_bank_id, uint32_t a_state,
                    uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleBankPulseGap( BankInfo &a_bi, uint64_t a_count );
    void        processMonitorEvents( Identifier a_monitor_id,
                    uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleMonitorPulseGap( MonitorInfo &a_mi,
                    uint64_t a_count );
    STS::BeamMonitorConfig *
                getBeamMonitorConfig( Identifier a_monitor_id,
                    bool & known_monitor );
    std::vector<STS::DetectorBankSet *>
                getDetectorBankSets( Identifier a_bank_id );
    void        associateDetectorBankSet(
                    STS::DetectorBankSet *a_bank_set );
    template<class T>
    void        pvValueUpdate( Identifier a_device_id, Identifier a_pv_id,
                    T a_value, const timespec &a_timestamp );
    template<class T>
    void        resetInUseVector( std::vector<T> a_buffer,
                    uint64_t a_buffer_size );
    //void        processPulseID( uint64_t a_pulse_id );
    void        receivedInfo( InfoBit a_bit );
    void        finalizeStreamProcessing();
    void        collapseDuplicatePVs();
    PVType      toPVType( const char *a_source ) const;
    inline void gatherStats( const ADARA::Packet &a_pkt ) const;
    const char* getPktName( uint32_t a_pkt_type ) const;
    void        updateRunInfo( const RunInfo &a_run_info );

    int                                     m_fd;                       ///< Input ADARA stream file descriptor
    ProcessingState                         m_processing_state;         ///< Current (internal) processing state
    uint32_t                                m_pkt_recvd;                ///< Packet received-status bit mask
    std::ofstream                           m_ofs_adara;                ///< ADARA output file stream
    uint64_t                                m_pulse_id;                 ///< ID of current pulse
    uint64_t                                m_pulse_count;              ///< Internal pulse counter
    PulseInfo                               m_pulse_info;               ///< Neutron pulse data
    std::vector<STS::DetectorBankSet *>     m_bank_sets;                ///< Vector of Detector Bank Sets info
    BankInfoMap                             m_banks;                    ///< Container of detector bank information
    std::vector<STS::BeamMonitorConfig>     m_monitor_config;           ///< Vector of Beam Monitor (Histo) Config info
    std::map<Identifier,MonitorInfo*>       m_monitors;                 ///< Container of monitor information
    uint32_t                                m_event_buf_write_thresh;   ///< Event buffer write threshold (banks & monitors; number of elements)
    uint32_t                                m_anc_buf_write_thresh;     ///< Ancillary buffer write threshold (indexes, PVs, etc; number of elements)
    unsigned short                          m_info_rcvd;                ///< Tracks ADARA informational packets are received
    BeamlineInfo                            m_beamline_info;            ///< Beamline (instrument) information
    RunInfo                                 m_run_info;                 ///< Run information
    RunMetrics                              m_run_metrics;              ///< Run metrics
    bool                                    m_strict;                   ///< Controls strict ADARA processing option
    bool                                    m_gen_adara;                ///< Controls generation of ADARA output stream file
    bool                                    m_gather_stats;             ///< Controls gathering of stream statistics
    mutable std::map<uint32_t,PktStats>     m_stats;                    ///< Continer for per-packet-type statistics
    uint64_t                                m_skipped_pkt_count;        ///< Count of ADARA packets that were ignored

    uint16_t                                m_pulse_flag;

    struct timespec                         m_default_run_start_time;   ///< Default Run Start Time (No Neutron Pulses)...
    bool                                    m_verbose;                  ///< STS Verbosity

protected:
    std::vector<PVInfoBase*>                m_pvs_list;                 ///< Collection of all process variable information
    std::map<PVKey,PVInfoBase*>             m_pvs_by_key;               ///< Container of process variable information (by key)
    std::map<std::string,PVKey>             m_pvs_by_name_xref;         ///< Index of process variable information (by name)
    std::map<Identifier,std::vector<PVEnumeratedType> >
                                            m_enums_by_dev;             ///< Container of Enumerated Types (by device)
};


} // End namespace STS

#endif /* STREAMPARSER_H */

// vim: expandtab

