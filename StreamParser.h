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
#include "../SMS/ADARAParser.h"
#include "Utils.h"
//#include "NxGen.h"
#include "stsdefs.h"

namespace STS {



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



class StreamParser : public ADARA::Parser, public IStreamAdapter
{
public:
    StreamParser( int a_fd, int a_fd_out, std::string & a_adara_out_file, bool a_strict, bool a_gather_stats = false, uint32_t a_chunk_size = 2048, uint16_t a_buf_chunk_size = 20 );
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
    enum ProcessingState
    {
        WAITING_FOR_RUN_START = 0x0001,
        PROCESSING_RUN_HEADER = 0x0002,
        PROCESSING_EVENTS     = 0x0004,
        DONE_PROCESSING       = 0x0008
    };

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
    
    inline void writePacket( const ADARA::Packet &pkt )
    {
        if ( m_gen_adara )
        {
            m_ofs_adara.write( (char *)pkt.packet(), pkt.packet_length());
        }
    }

    void    processPixelMappingPkt( const ADARA::PixelMappingPkt &pkt );
    void    processPulseInfo( const ADARA::BankedEventPkt &pkt );
    void    processBankEvents( uint32_t bank_id, uint32_t event_count, const uint32_t *rpos );
    void    handleBankPulseGap( BankInfo &a_bi, uint64_t a_count );
    void    processMonitorEvents( uint16_t monitor_id, uint32_t event_count, const uint32_t *rpos );
    void    handleMonitorPulseGap( MonitorInfo &a_mi, uint64_t a_count );
    void    processDeviceDescriptor( Identifier a_device_id, const std::string & a_xml );
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
                        if ( pvinfo->m_value_buffer.size() >= m_chunk_size )
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

    const char*     getPktName( uint32_t a_pkt_id ) const;

    bool                                    m_initialized;
    int                                     m_fd;
    int                                     m_fd_out;
    ProcessingState                         m_processing_state;
    uint32_t                                m_pkt_recvd;
    std::ofstream                           m_ofs_adara;
    uint64_t                                m_pulse_id;                 ///< ID of current pulse
    uint64_t                                m_pulse_count;              ///< Internal pulse counter
    std::vector<BankInfo*>                  m_banks;                    ///< Container of detector bank information
    std::vector<MonitorInfo*>               m_monitors;                 ///< Container of monitor information
    std::map<PVKey,PVInfoBase*>             m_pvs;                      ///< Container of process variable information
    uint32_t                                m_chunk_size;
    uint16_t                                m_event_buf_chunk_count;
    uint16_t                                m_event_buf_write_thresh;
    uint16_t                                m_ancillary_buf_chunk_count;
    uint16_t                                m_ancillary_buf_write_thresh;
    bool                                    m_strict;
    bool                                    m_gen_adara;
    bool                                    m_gather_stats;
    mutable std::map<uint32_t,PktStats>     m_stats;
    uint64_t                                m_skipped_pkt_count;
};

} // End namespace STS

#endif	/* STREAMPARSER_H */

