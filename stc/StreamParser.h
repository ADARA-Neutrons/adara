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
#include "stcdefs.h"
#include "TraceException.h"

namespace STC {


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

    /// Used to Identify "Special" Detector Bank Ids (Error & Unmapped)
    enum SpecialBankIds
    {
        UNMAPPED_BANK   = 0xffffffff,
        ERROR_BANK      = 0xfffffffe
    };

    /// Used to Identify "Special" Detector Bank Indices (Error & Unmapped)
    enum SpecialBankIndices
    {
        UNMAPPED_BANK_INDEX = 0,
        ERROR_BANK_INDEX    = 1,
        NUM_SPECIAL_BANKS   = 2
    };

    typedef BankInfo *BankInfoPtr;

    StreamParser( int a_fd,
        const std::string & a_work_root,
        const std::string & a_work_base,
        const std::string & a_adara_out_file,
        const std::string & a_config_file,
        bool a_strict, bool a_gather_stats = false,
        uint32_t a_event_buf_write_thresh = 40960, // number of elems
        uint32_t a_ancillary_buf_write_thresh = 4096, // number of elems
        uint32_t a_verbose_level = 0 );

    virtual ~StreamParser();

    void    processStream();
    void    printStats( std::ostream &a_os ) const;
    void    getXmlNodeValue( xmlNode *a_node, std::string & a_value ) const;

    std::string getBeamShortName() const
        { return m_beamline_info.instr_shortname; }
    std::string getBeamLongName() const
        { return m_beamline_info.instr_longname; }
    std::string getFacilityName() const
        { return m_run_info.facility_name; }
    std::string getProposalID() const
        { return m_run_info.proposal_id; }
    uint32_t getRunNumber() const
        { return m_run_info.run_number; }
    bool getNoSampleInfo() const
        { return m_run_info.no_sample_info; }
    bool getSavePixelMap() const
        { return m_run_info.save_pixel_map; }
    bool getRunNotesUpdatesEnabled() const
        { return m_run_info.run_notes_updates_enabled; }

    bool infoReady() const
        { return (m_info_rcvd & INFO_SENT); }

    bool isWorkingDirectoryReady(void);

    bool constructWorkingDirectory( bool a_force_init = false,
        std::string caller = "" );

    void flushAdaraStreamBuffer(void);

    std::string getWorkingDirectory(void) { return m_work_dir; }

    bool getDoRename(void) { return m_do_rename; }

    // Needed by NxGen::runComment()... ;-D
    void markerComment( double a_time, uint64_t a_ts_nano,
        const std::string &a_comment );

    uint32_t verbose(void) { return m_verbose_level; }

    uint64_t m_total_bytes_count;   ///< Total Input Stream Byte Count of ADARA packets

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
    bool        rxPacket( const ADARA::MultVariableU32Pkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableDoublePkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableStringPkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableU32ArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::MultVariableDoubleArrayPkt &a_pkt );
    bool        rxPacket( const ADARA::AnnotationPkt &a_pkt );

    bool        rxOversizePkt( const ADARA::PacketHeader *hdr,
                    const uint8_t *chunk, unsigned int chunk_offset,
                    unsigned int chunk_len );

    using ADARA::POSIXParser::rxPacket; // Shunt remaining rxPacket flavors
                                        // to base class implementations

    void        reallocateBanksArray( uint32_t a_bank_id,
                    uint32_t a_state );

    void        processPulseInfo( const ADARA::BankedEventPkt &a_pkt );
    void        processPulseInfo( const ADARA::BankedEventStatePkt &a_pkt );
    void        processBankEvents( uint32_t a_bank_id, uint32_t a_state,
                    uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleBankPulseGap( BankInfo &a_bi, uint64_t a_count );
    void        processMonitorEvents( Identifier a_monitor_id,
                    uint32_t a_event_count, const uint32_t *a_rpos );
    void        handleMonitorPulseGap( MonitorInfo &a_mi,
                    uint64_t a_count );
    void        getBeamMonitorConfig( Identifier a_monitor_id,
                    STC::BeamMonitorConfig &a_config );
    std::vector<STC::DetectorBankSet *>
                getDetectorBankSets( Identifier a_bank_id );
    void        associateDetectorBankSet(
                    STC::DetectorBankSet *a_bank_set );
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

    void        markerPause( double a_time, uint64_t a_ts_nano,
                    const std::string &a_comment  );
    void        markerResume( double a_time, uint64_t a_ts_nano,
                    const std::string &a_comment  );
    void        markerScanStart( double a_time, uint64_t a_ts_nano,
                    uint32_t a_scan_index, const std::string &a_comment );
    void        markerScanStop( double a_time, uint64_t a_ts_nano,
                    uint32_t a_scan_index, const std::string &a_comment  );

    int                                     m_fd;                       ///< Input ADARA stream file descriptor
    ProcessingState                         m_processing_state;         ///< Current (internal) processing state
    uint32_t                                m_pkt_recvd;                ///< Packet received-status bit mask
    std::ofstream                           m_ofs_adara;                ///< ADARA output file stream
    std::vector<ADARA::Packet *>            m_adara_queue;              ///< ADARA output packet buffer
    uint64_t                                m_pulse_id;                 ///< ID of current pulse
    uint64_t                                m_pulse_count;              ///< Internal pulse counter
    PulseInfo                               m_pulse_info;               ///< Neutron pulse data
    std::vector<STC::DetectorBankSet *>     m_bank_sets;                ///< Vector of Detector Bank Sets info
    BankInfoPtr                            *m_banks_arr;                ///< Container of Detector Bank Information
    uint32_t                                m_banks_arr_size;           ///< Size of Detector Bank Array (for Realloc)
    uint32_t                                m_maxBank;                  ///< Maximum Detector Bank ID Encountered
    uint32_t                                m_numStates;                ///< Total Number of States Encountered (Includes State 0)
    std::vector<STC::BeamMonitorConfig>     m_monitor_config;           ///< Vector of Beam Monitor (Histo) Config info
    std::map<Identifier,MonitorInfo*>       m_monitors;                 ///< Container of monitor information
    uint32_t                                m_event_buf_write_thresh;   ///< Event buffer write threshold (banks & monitors; number of elements)
    uint32_t                                m_anc_buf_write_thresh;     ///< Ancillary buffer write threshold (indexes, PVs, etc; number of elements)
    unsigned short                          m_info_rcvd;                ///< Tracks ADARA informational packets are received
    BeamlineInfo                            m_beamline_info;            ///< Beamline (instrument) information
    RunInfo                                 m_run_info;                 ///< Run information
    RunMetrics                              m_run_metrics;              ///< Run metrics
    std::string                             m_work_root;                ///< Working Directory Root
    std::string                             m_work_base;                ///< Working Directory Base
    std::string                             m_work_dir;                 ///< Working Directory
    bool                                    m_do_rename;                ///< Can We Do a NeXus File Rename?
    std::string                             m_adara_out_file;           ///< Filename of output ADARA stream file
    std::string                             m_config_file;              ///< Path to STC Config file
    bool                                    m_strict;                   ///< Controls strict ADARA processing option
    bool                                    m_gen_adara;                ///< Controls generation of ADARA output stream file
    bool                                    m_gather_stats;             ///< Controls gathering of stream statistics
    mutable std::map<uint32_t,PktStats>     m_stats;                    ///< Continer for per-packet-type statistics
    uint64_t                                m_skipped_pkt_count;        ///< Count of ADARA packets that were ignored

    uint16_t                                m_pulse_flag;

    struct timespec                         m_default_run_start_time;   ///< Default Run Start Time (No Neutron Pulses)...

    uint32_t                                m_verbose_level;            ///< STC Verbosity Level

protected:
    std::multimap<uint64_t, std::pair<double, uint16_t> >
                                            m_pause_multimap;           /// Pause/Resume annotation nsec-to-timestamp/state (on/off) map
    std::multimap<uint64_t, std::pair<double, uint16_t> >::iterator
                                            m_last_pause_multimap_it;   /// Last Inserted Pause/Resume annotation
    std::string                             m_last_pause_comment;       /// Comment for Last Pause/Resume annotation
    bool                                    m_pause_has_non_normalized; /// Pause/Resume Marker received before 1st pulse...

    std::multimap<uint64_t, std::pair<double, uint32_t> >
                                            m_scan_multimap;            /// Scan annotation nsec-to-timestamp/state (on/off) map
    std::multimap<uint64_t, std::pair<double, uint32_t> >::iterator
                                            m_last_scan_multimap_it;    /// Last Inserted Scan annotation
    std::string                             m_last_scan_comment;        /// Comment for Last Scan annotation
    bool                                    m_scan_has_non_normalized;  /// Scan Marker received before 1st pulse...

    std::multimap<uint64_t, std::pair<double, std::string> >
                                            m_comment_multimap;         /// Comment annotation nsec-to-timestamp/string map
    std::multimap<uint64_t, std::pair<double, std::string> >::iterator
                                            m_last_comment_multimap_it; /// Last Inserted Comment annotation
    bool                                    m_comment_has_non_normalized; /// Comment annotation received before 1st pulse...

    std::vector<PVInfoBase*>                m_pvs_list;                 ///< Collection of all process variable information
    std::map<PVKey,PVInfoBase*>             m_pvs_by_key;               ///< Container of process variable information (by key)
    std::map<std::string,PVKey>             m_pvs_by_name_xref;         ///< Index of process variable information (by name)
    std::map<Identifier,std::vector<PVEnumeratedType> >
                                            m_enums_by_dev;             ///< Container of Enumerated Types (by device)
};


} // End namespace STC

#endif /* STREAMPARSER_H */

// vim: expandtab

