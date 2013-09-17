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
        m_total_counts          = 0;
        m_total_charge          = 0.0;
        m_pixel_error_count     = 0;
        m_dup_pulse_count       = 0;
        m_pulse_veto_count      = 0;
        m_mapping_error_count   = 0;
        m_missing_rtdl_count    = 0;
    }

    uint32_t        m_total_counts;      ///< Sum of counts over all banks, excluding monitors
    double          m_total_charge;
    uint32_t        m_pixel_error_count;
    uint32_t        m_dup_pulse_count;
    uint32_t        m_pulse_veto_count;
    uint32_t        m_mapping_error_count;
    uint32_t        m_missing_rtdl_count;
};

}}

#endif // DASMONDEFS_H
