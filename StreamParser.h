/*
 * File:   StreamParser.h
 * Author: d3s
 *
 * Created on July 12, 2012, 5:57 PM
 */

#ifndef STREAMPARSER_H
#define	STREAMPARSER_H

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include <boost/lexical_cast.hpp>
//#include "../SMS/ADARA.h"
#include "../SMS/ADARAParser.h"
#include "Utils.h"
#include "sfsdefs.h"

namespace SFS {


/*! \brief Exception class used to communicate translation failures within STS code
 *
 */
class TranslationException : public std::runtime_error
{
public:
    TranslationException( TranslationStatusCode a_status_code, const std::string &a_reason )
        : std::runtime_error( a_reason ), m_status_code(a_status_code)
    {}

    TranslationStatusCode   getStatusCode() { return m_status_code; }

private:
    TranslationStatusCode   m_status_code;
};



/*! \brief Base class that provides ADARA-specific parsing and translation
 *
 * The StreamParser class provides ADARA-specific parsing of an incoming data
 * stream and serves as a base class for format-specific adapter classes that
 * output the extracted data. The StreamParser class communicates with a sub-
 * class via the methods defined on the IStreamAdapter interface. The Stream-
 * Parser class operates on posix file descriptors (for stream input and
 * also output of a filtered adara file).
 */
class StreamParser : public ADARA::Parser, public IStreamAdapter
{
public:
    StreamParser( int a_fd, std::string & a_adara_out_file, bool a_strict, bool a_gather_stats = false, uint32_t a_event_buf_write_thresh = 40960, uint32_t a_ancillary_buf_write_thresh = 4096 );
    virtual ~StreamParser();

    void    processStream();
    void    printStats( std::ostream &a_os ) const;

    const struct timespec & getStartTime() const { return m_start_time; }
    const struct timespec & getEndTime() const { return m_end_time; }
    std::string             getFacilityName() const { return m_facility_name; }
    std::string             getBeamShortName() const { return m_short_name; }
    std::string             getProposalID() const { return m_proposal_id; }
    unsigned long           getRunNumber() const { return m_run_number; }

protected:
    // These attributes are accessible by the adapter subclass

    std::string             m_facility_name;
    std::string             m_short_name;
    std::string             m_proposal_id;
    unsigned long           m_run_number;
    double                  m_total_charge;
    uint64_t                m_events_counted;
    uint64_t                m_events_uncounted;
    struct timespec         m_start_time;
    struct timespec         m_end_time;
    PulseInfo               m_pulse_info;

private:
    /// Defines internal stream processing states of StreamParser class
    enum ProcessingState
    {
        WAITING_FOR_RUN_START = 0x0001,
        PROCESSING_RUN_HEADER = 0x0002,
        PROCESSING_EVENTS     = 0x0004,
        DONE_PROCESSING       = 0x0008
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

    bool    rxPacket( const ADARA::Packet &pkt );
    bool    rxPacket( const ADARA::RunStatusPkt &pkt );
    bool    rxPacket( const ADARA::BankedEventPkt &pkt );
    bool    rxPacket( const ADARA::PixelMappingPkt &pkt );
    bool    rxPacket( const ADARA::BeamMonitorPkt &pkt );
    bool    rxPacket( const ADARA::RunInfoPkt &pkt );
    bool    rxPacket( const ADARA::GeometryPkt &pkt );
    bool    rxPacket( const ADARA::BeamlineInfoPkt &pkt );
    bool    rxPacket( const ADARA::DeviceDescriptorPkt &pkt );
    bool    rxPacket( const ADARA::VariableU32Pkt &pkt );
    bool    rxPacket( const ADARA::VariableDoublePkt &pkt );

    /*! \brief Writes an ADARA packet to an output file
     *  \param pkt - An ADARA packet to write
     *
     * If adara stream output file generation is enabled, this method writes the
     * specified packet to the output file stream object in binary format.
     */
    inline void writePacket( const ADARA::Packet &a_pkt )
    {
        if ( m_gen_adara )
        {
            m_ofs_adara.write( (char *)a_pkt.packet(), a_pkt.packet_length());
        }
    }

    void    processPulseInfo( const ADARA::BankedEventPkt &pkt );
    void    processBankEvents( uint32_t bank_id, uint32_t event_count, const uint32_t *rpos );
    void    handleBankPulseGap( BankInfo &a_bi, uint64_t a_count );
    void    processMonitorEvents( uint16_t monitor_id, uint32_t event_count, const uint32_t *rpos );
    void    handleMonitorPulseGap( MonitorInfo &a_mi, uint64_t a_count );

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
    void    pvValueUpdate( Identifier a_device_id, Identifier a_pv_id, T a_value, const timespec &a_timestamp )
            {
                PVKey   key(a_device_id,a_pv_id);

                std::map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.find(key);
                if ( ipv != m_pvs.end() )
                {
                    PVInfo<T> *pvinfo = dynamic_cast<PVInfo<T>*>( ipv->second );
                    if ( pvinfo )
                    {
                        float t = 0;

                        // Note: if first pulse has not arrived, truncate all PV times to 0
                        if ( m_pulse_info.start_time_nsec )
                        {
                            t = (timespec_to_nsec( a_timestamp ) - m_pulse_info.start_time_nsec)*1.0e-9;
                        }
                        else if ( pvinfo->m_value_buffer.size() )
                        {
                            // If we recv multiple value updates before first pulse, keep only latest
                            pvinfo->m_value_buffer.clear();
                            pvinfo->m_time_buffer.clear();
                            pvinfo->m_stats.reset();
                        }

                        pvinfo->m_value_buffer.push_back(a_value);
                        pvinfo->m_time_buffer.push_back(t);
                        pvinfo->m_stats.push(a_value);

                        // Check for buffer write
                        if ( pvinfo->m_value_buffer.size() >= m_anc_buf_write_thresh )
                            pvinfo->flushBuffers();
                    }
                    else
                    {
                        std::cout << "cast failed! " << a_device_id << ":" << a_pv_id << std::endl;
                    }

                }
                else
                {
                    std::cout << "pvinfo not found! " << a_device_id << "." << a_pv_id << std::endl;
                }
            }

    void    processPulseID( uint64_t a_pulse_id );
    void    finalizeStreamProcessing();
    PVType  toPVType( const char *a_source ) const;

    /*! \brief Gathers statistics from the specified ADARA packet.
     *  \param pkt - An ADARA packet to analyze
     *
     * If stream statistics gathering is enabled, this method collects a number
     * of metrics for the stream and each packet type (total packet count, min/
     * max packet size with payload, total byte count for each packet type.
     */
    inline void gatherStats( const ADARA::Packet &pkt ) const
    {
        PktStats &stats = m_stats[pkt.type()];
        ++stats.count;
        if ( pkt.packet_length() < stats.min_pkt_size || !stats.min_pkt_size )
            stats.min_pkt_size = pkt.packet_length();
        if ( pkt.packet_length() > stats.max_pkt_size )
            stats.max_pkt_size = pkt.packet_length();
        stats.total_size += pkt.packet_length();
    }

    const char*     getPktName( ADARA::PacketType::Enum a_pkt_type ) const;

    bool                                    m_initialized;              ///< Flag indicating if instance has been initialized
    int                                     m_fd;                       ///< Input ADARA stream file descriptor
    ProcessingState                         m_processing_state;         ///< Current (internal) processing state
    uint32_t                                m_pkt_recvd;                ///< Packet received-status bit mask
    std::ofstream                           m_ofs_adara;                ///< ADARA output file stream
    uint64_t                                m_pulse_id;                 ///< ID of current pulse
    uint64_t                                m_pulse_count;              ///< Internal pulse counter
    std::vector<BankInfo*>                  m_banks;                    ///< Container of detector bank information
    std::vector<MonitorInfo*>               m_monitors;                 ///< Container of monitor information
    std::map<PVKey,PVInfoBase*>             m_pvs;                      ///< Container of process variable information
    uint32_t                                m_event_buf_write_thresh;   ///< Event buffer write threshold (banked detectors and monitors)
    uint32_t                                m_anc_buf_write_thresh;     ///< Ancillary buffer write threshold (indexes, PVs, etc)
    bool                                    m_strict;                   ///< Controls strict ADARA processing option
    bool                                    m_gen_adara;                ///< Controls generation of ADARA output stream file
    bool                                    m_gather_stats;             ///< Controls gathering of stream statistics
    mutable std::map<uint32_t,PktStats>     m_stats;                    ///< Continer for per-packet-type statistics
    uint64_t                                m_skipped_pkt_count;        ///< Count of ADARA packets that were ignored
};

} // End namespace STS

#endif	/* STREAMPARSER_H */

