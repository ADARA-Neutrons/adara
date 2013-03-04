#ifndef DASMONDEFS_H
#define DASMONDEFS_H

#include <string>
#include <vector>
#include "ADARADefs.h"


/*
  DASMon common data structures and definitions
*/
namespace ADARA {
namespace DASMON {

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
    unsigned long           m_run_num;
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
        m_num_monitors          = 0;
        m_pulse_charge          = 0.0;
        m_pulse_freq            = 0.0;
        m_pixel_error_rate      = 0.0;
        m_stream_bps            = 0;
    }

    double                  m_count_rate;
    unsigned short          m_num_monitors;
    double                  m_monitor_count_rate[8];
    double                  m_pulse_charge;
    double                  m_pulse_freq;
    double                  m_pixel_error_rate;
    unsigned long           m_stream_bps;
};


class RunMetrics
{
public:
    virtual ~RunMetrics() {}

    void clear()
    {
        m_pulse_count           = 0;
        m_pulse_charge          = 0.0;
        m_pixel_error_count     = 0;
        m_dup_pulse_count       = 0;
        m_cycle_error_count     = 0;
    }

    unsigned long           m_pulse_count;
    double                  m_pulse_charge;
    unsigned long           m_pixel_error_count;
    unsigned long           m_dup_pulse_count;
    unsigned long           m_cycle_error_count;
};

}}

#endif // DASMONDEFS_H
