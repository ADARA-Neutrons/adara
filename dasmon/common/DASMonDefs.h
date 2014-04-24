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
    std::string     name;
    std::string     fact;
    std::string     source;
    ADARA::Level    level;
    std::string     msg;
};


class BeamInfo
{
public:
    virtual ~BeamInfo() {}

    void clear()
    {
        m_facility.clear();
        m_beam_id.clear();
        m_beam_sname.clear();
        m_beam_lname.clear();
    }

    std::string             m_facility;
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
        m_time                  = 0.0;
        m_total_counts          = 0;
        m_total_charge          = 0.0;
        m_pixel_error_count     = 0;
        m_dup_pulse_count       = 0;
        m_pulse_veto_count      = 0;
        m_mapping_error_count   = 0;
        m_missing_rtdl_count    = 0;
    }

    double          m_time;                 ///< Run time (seconds)
    uint64_t        m_total_counts;         ///< Sum of counts over all banks, excluding monitors
    double          m_total_charge;         ///< Accumulated charge
    uint32_t        m_pixel_error_count;
    uint32_t        m_dup_pulse_count;
    uint32_t        m_pulse_veto_count;
    uint32_t        m_mapping_error_count;
    uint32_t        m_missing_rtdl_count;
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
        a_out << "StreamMetrics:" << std::endl;
        a_out << "  UPT: " << m_invalid_pkt_type << std::endl;
        a_out << "  INP: " << m_invalid_pkt << std::endl;
        a_out << "  IPT: " << m_invalid_pkt_time << std::endl;
        a_out << "  DUP: " << m_duplicate_packet << std::endl;
        a_out << "  PFT: " << m_pulse_freq_tol << std::endl;
        a_out << "  CYE: " << m_cycle_err << std::endl;
        a_out << "  IBI: " << m_invalid_bank_id << std::endl;
        a_out << "  BSM: " << m_bank_source_mismatch << std::endl;
        a_out << "  DUS: " << m_duplicate_source << std::endl;
        a_out << "  DUB: " << m_duplicate_bank << std::endl;
        a_out << "  PME: " << m_pixel_map_err << std::endl;
        a_out << "  PBM: " << m_pixel_bank_mismatch << std::endl;
        a_out << "  PTF: " << m_pixel_invalid_tof << std::endl;
        a_out << "  PUI: " << m_pixel_unknown_id << std::endl;
        a_out << "  PER: " << m_pixel_errors << std::endl;
        a_out << "  DDX: " << m_bad_ddp_xml << std::endl;
        a_out << "  RIX: " << m_bad_runinfo_xml << std::endl;
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

#endif // DASMONDEFS_H
