#ifndef DASMONDEFS_H
#define DASMONDEFS_H

#include <string>
#include <vector>
#include <map>
#include <stdint.h>
#include "ADARADefs.h"


/*
  DASMon common data structures and definitions
*/
namespace ADARA {
namespace DASMON {

enum PVState
{
    PV_OK = 0,
    PV_LIMIT,
    PV_ERROR
};

struct SignalInfo
{
    bool            enabled;
    std::string     name;
    std::string     fact;
    std::string     source;
    ADARA::Level    level;
    std::string     msg;
    std::string     desc;
};


class BeamInfo
{
public:
    virtual ~BeamInfo() {}

    void clear()
    {
        m_facility.clear();

        m_target_station_number = 1;

        m_beam_id.clear();
        m_beam_sname.clear();
        m_beam_lname.clear();
    }

    std::string             m_facility;

    uint32_t                m_target_station_number;

    std::string             m_beam_id;
    std::string             m_beam_sname;
    std::string             m_beam_lname;
};


struct UserInfo
{
    std::string             m_name;
    std::string             m_id;
    std::string             m_role;
};


class RunInfo
{
public:
    virtual ~RunInfo() {}

    void clear()
    {
        m_run_num = 0;
        m_run_title.clear();
        m_proposal_id.clear();
        m_sample_id.clear();
        m_sample_name.clear();
        m_sample_nature.clear();
        m_sample_formula.clear();
        m_sample_environ.clear();
        m_user_info.clear();
    }

    std::string             m_proposal_id;
    std::string             m_run_title;
    uint32_t                m_run_num;
    std::string             m_sample_id;
    std::string             m_sample_name;
    std::string             m_sample_environ;
    std::string             m_sample_formula;
    std::string             m_sample_nature;
    std::vector<UserInfo>   m_user_info;
};


class BeamMetrics
{
public:
    virtual ~BeamMetrics() {}

    void clear()
    {
        m_count_rate            = 0.0;
        m_monitor_count_rate.clear();
        m_pulse_charge          = 0.0;
        m_pulse_freq            = 0.0;
        m_pixel_error_rate      = 0.0;
        m_stream_bps            = 0;
    }

    void print( std::ostream &a_out )
    {
        a_out << "BeamMetrics:";
        a_out << " Count Rate: " << m_count_rate;
        a_out << ", Monitor Count Rate: [";
        std::map<uint32_t,double>::iterator it;
        for ( it = m_monitor_count_rate.begin() ;
                it != m_monitor_count_rate.end() ; ++it )
        {
            if ( it != m_monitor_count_rate.begin() )
                a_out << ",";
            a_out << " ID " << it->first << " = " << it->second;
        }
        a_out << "]";
        a_out << ", Pulse Charge: " << m_pulse_charge;
        a_out << ", Pulse Freq: " << m_pulse_freq;
        a_out << ", Pixel Error Rate: " << m_pixel_error_rate;
        a_out << ", Stream BPS: " << m_stream_bps;
    }

    double                      m_count_rate;
    std::map<uint32_t,double>   m_monitor_count_rate;
    double                      m_pulse_charge;
    double                      m_pulse_freq;
    double                      m_pixel_error_rate;
    uint32_t                    m_stream_bps;
};


class RunMetrics
{
public:
    virtual ~RunMetrics() {}

    void clear()
    {
        m_time                      = 0.0;
        m_total_counts              = 0;
        m_total_charge              = 0.0;
        m_pixel_error_count         = 0;
        m_dup_pulse_count           = 0;
        m_pulse_veto_count          = 0;
        m_mapping_error_count       = 0;
        m_missing_rtdl_count        = 0;
        m_pulse_pcharge_uncorrected = 0;
        m_got_metadata_count        = 0;
        m_got_neutrons_count        = 0;
        m_has_states_count          = 0;
        m_total_pulses_count        = 0;
        m_total_bytes_count         = 0;
    }

    void print( std::ostream &a_out )
    {
        a_out << "RunMetrics:";
        a_out << " Run Time: " << m_time;
        a_out << ", Total Bank Counts: " << m_total_counts;
        a_out << ", Total Charge: " << m_total_charge;
        a_out << ", Pixel Errors: " << m_pixel_error_count;
        a_out << ", Dup Pulses: " << m_dup_pulse_count;
        a_out << ", Pulse Vetoes: " << m_pulse_veto_count;
        a_out << ", Mapping Errors: " << m_mapping_error_count;
        a_out << ", Missing RTDLs: " << m_missing_rtdl_count;
        a_out << ", Pulse PCharge Uncorrected: "
            << m_pulse_pcharge_uncorrected;
        a_out << ", Got MetaData: " << m_got_metadata_count;
        a_out << ", Got Neutrons: " << m_got_neutrons_count;
        a_out << ", Has States: " << m_has_states_count;
        a_out << ", Total Pulses: " << m_total_pulses_count;
        a_out << ", Total Bytes: " << m_total_bytes_count;
    }

    double          m_time;                 ///< Run time (seconds)
    uint64_t        m_total_counts;         ///< Sum of counts over all banks, excluding monitors
    double          m_total_charge;         ///< Accumulated charge
    uint32_t        m_pixel_error_count;
    uint32_t        m_dup_pulse_count;
    uint32_t        m_pulse_veto_count;
    uint32_t        m_mapping_error_count;
    uint32_t        m_missing_rtdl_count;
    uint32_t        m_pulse_pcharge_uncorrected;
    uint32_t        m_got_metadata_count;
    uint32_t        m_got_neutrons_count;
    uint32_t        m_has_states_count;
    uint32_t        m_total_pulses_count;
    uint32_t        m_total_bytes_count;
};


class StreamMetrics
{
public:
    virtual ~StreamMetrics() {}

    void clear()
    {
        m_invalid_pkt_type      = 0;
        m_invalid_pkt           = 0;
        m_invalid_pkt_time      = 0;
        m_duplicate_packet      = 0;
        m_pulse_freq_tol        = 0;
        m_cycle_err             = 0;
        m_invalid_bank_id       = 0;
        m_bank_source_mismatch  = 0;
        m_duplicate_source      = 0;
        m_duplicate_bank        = 0;
        m_pixel_map_err         = 0;
        m_pixel_bank_mismatch   = 0;
        m_pixel_errors          = 0;
        m_pixel_invalid_tof     = 0;
        m_pixel_unknown_id      = 0;
        m_pixel_errors          = 0;
        m_bad_ddp_xml           = 0;
        m_bad_runinfo_xml       = 0;
    }

    void print( std::ostream &a_out )
    {
        a_out << "StreamMetrics:";
        a_out << " Bad Pkt Type: " << m_invalid_pkt_type;
        a_out << ", Bad Pkt: " << m_invalid_pkt;
        a_out << ", Bad Pkt Time: " << m_invalid_pkt_time;
        a_out << ", Dup Pkt: " << m_duplicate_packet;
        a_out << ", Bad Intra Pulse: " << m_pulse_freq_tol;
        a_out << ", Cycle Error: " << m_cycle_err;
        a_out << ", Bad Bank ID: " << m_invalid_bank_id;
        a_out << ", Bank Src Mismatch: " << m_bank_source_mismatch;
        a_out << ", Dup Src: " << m_duplicate_source;
        a_out << ", Dup Bank: " << m_duplicate_bank;
        a_out << ", Pixel Map Error: " << m_pixel_map_err;
        a_out << ", Pixel Bank Mismatch: " << m_pixel_bank_mismatch;
        a_out << ", Bad Pixel TOF: " << m_pixel_invalid_tof;
        a_out << ", Unknown Pixel ID: " << m_pixel_unknown_id;
        a_out << ", Pixel Error: " << m_pixel_errors;
        a_out << ", Bad DDP XML: " << m_bad_ddp_xml;
        a_out << ", Bad RunInfo XML: " << m_bad_runinfo_xml;
    }

    uint32_t        m_invalid_pkt_type;     ///< Not a defined ADARA Packet type
    uint32_t        m_invalid_pkt;          ///< A malformed packet (not type/len)
    uint32_t        m_invalid_pkt_time;     ///< Invalid (reversed) timestamp on event data packets
    uint32_t        m_duplicate_packet;     ///< More than one event/monitor packet per pulse
    uint32_t        m_pulse_freq_tol;       ///< Invalid (inaccurate) intra-pulse time
    uint32_t        m_cycle_err;            ///< Pulse cycle number discontinuity
    uint32_t        m_invalid_bank_id;      ///< Received an invalid bank id in a packet
    uint32_t        m_bank_source_mismatch; ///< A detector bank exists in more than one source
    uint32_t        m_duplicate_source;     ///< A source is reported more than once in a packet
    uint32_t        m_duplicate_bank;       ///< A bank is reported more than once in a packet
    uint32_t        m_pixel_map_err;        ///< Pixels reported as having a bank mapping error
    uint32_t        m_pixel_bank_mismatch;  ///< A pixel is mapped to wrong bank
    uint32_t        m_pixel_invalid_tof;    ///< Invalid TOF (< 0 or > X), X is cmd line param
    uint32_t        m_pixel_unknown_id;     ///< Pixels with an unkown/invalid ID
    uint32_t        m_pixel_errors;         ///< Pixels reported as having error bit set
    uint32_t        m_bad_ddp_xml;          ///< Malformed DDP xml payload
    uint32_t        m_bad_runinfo_xml;      ///< Malformed RunInfo xml payload
};

}}

// vim: expandtab

#endif // DASMONDEFS_H
