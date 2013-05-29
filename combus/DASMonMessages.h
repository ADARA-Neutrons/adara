#ifndef DASMONMESSAGES_H
#define DASMONMESSAGES_H

#include <string>
#include <boost/lexical_cast.hpp>
#include "ComBusMessages.h"
#include "RuleEngine.h"
#include "DASMonDefs.h"

namespace ADARA {
namespace ComBus {
namespace DASMON {

//////////////////////////////////////////////////////////////////////////////
// DASMon Commands

DEF_SIMPLE_CMD(GetRuleDefinitions,MSG_DASMON_GET_RULES)
DEF_SIMPLE_CMD(RestoreDefaultRuleDefinitions,MSG_DASMON_RESTORE_DEFAULT_RULES)
DEF_SIMPLE_CMD(GetProcessVariables,MSG_DASMON_GET_PVS)

/// Message containing current rule & signal definitions
class RuleDefinitions : public ControlMessage
{
public:
    RuleDefinitions()
    {}

    inline MessageType getMessageType() const
    { return MSG_DASMON_RULE_DEFINITIONS; }

    std::vector<RuleEngine::RuleInfo>       m_rules;
    std::vector<ADARA::DASMON::SignalInfo>  m_signals;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::read( a_prop_tree );

        m_rules.clear();
        RuleEngine::RuleInfo rule;
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("rules"))
            {
                rule.fact = v.second.get( "fact", "" );
                rule.expr = v.second.get( "expr", "" );

                m_rules.push_back( rule );
            }
        } catch(...) {}

        m_signals.clear();
        ADARA::DASMON::SignalInfo signal;
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("signals"))
            {
                signal.name = v.second.get( "name", "" );
                signal.fact = v.second.get( "fact", "" );
                signal.source = v.second.get( "source", "" );
                signal.level = (Level)v.second.get( "level", 0 );
                signal.msg = v.second.get( "message", "" );

                m_signals.push_back( signal );
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::write( a_prop_tree );

        for ( std::vector<RuleEngine::RuleInfo>::iterator r = m_rules.begin(); r != m_rules.end(); ++r )
        {
            boost::property_tree::ptree pt;
            pt.put( "fact", r->fact );
            pt.put( "expr", r->expr );
            a_prop_tree.add_child( "rules.rule", pt );
        }

        for ( std::vector<ADARA::DASMON::SignalInfo>::iterator s = m_signals.begin(); s != m_signals.end(); ++s )
        {
            boost::property_tree::ptree pt;
            pt.put( "name", s->name );
            pt.put( "fact", s->fact );
            pt.put( "source", s->source );
            pt.put( "level", (unsigned short)s->level );
            pt.put( "message", s->msg );
            a_prop_tree.add_child( "signals.signal", pt );
        }
    }
};


class SetRuleDefinitions : public RuleDefinitions
{
public:
    SetRuleDefinitions()
    {}

    inline MessageType getMessageType() const
    { return MSG_DASMON_SET_RULES; }

    bool m_set_default;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        RuleDefinitions::read( a_prop_tree );

        m_set_default = a_prop_tree.get( "set_default", false );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        RuleDefinitions::write( a_prop_tree );

        a_prop_tree.put( "set_default", m_set_default );
    }
};


/// Simple request message to retrieve currently defined rule (and signal) definitions
DEF_SIMPLE_CMD(GetInputFacts,MSG_DASMON_GET_INPUT_FACTS)

/// Message containing current built-in facts
class InputFacts : public ControlMessage
{
public:
    InputFacts()
    {}

    inline MessageType getMessageType() const
    { return MSG_DASMON_INPUT_FACTS; }

    std::map<std::string,std::string> m_facts;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::read( a_prop_tree );

        m_facts.clear();
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("facts"))
            {
                m_facts[v.second.data()] = "";
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::write( a_prop_tree );
        boost::property_tree::ptree pt;

        for ( std::map<std::string,std::string>::iterator fact = m_facts.begin(); fact != m_facts.end(); ++fact )
        {
            pt.push_back( std::make_pair( "", fact->first ));
        }

        a_prop_tree.add_child( "facts", pt );
    }
};


/// Message containing current built-in facts
class ProcessVariables : public ControlMessage
{
public:
    ProcessVariables()
    {}

    inline MessageType getMessageType() const
    { return MSG_DASMON_PVS; }

    struct PVData
    {
        PVData()
            : value(0.0), status(0), timestamp(0)
        {}

        PVData( double a_value, int a_status, unsigned long a_timestamp )
            : value(a_value), status(a_status), timestamp(a_timestamp)
        {}

        double          value;
        int             status;
        unsigned long   timestamp;
    };

    std::map<std::string,PVData> m_pvs;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::read( a_prop_tree );
        PVData data;
        m_pvs.clear();
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("pvs"))
            {
                data.status = v.second.get( "status", 0 );
                data.value = v.second.get( "value", 0.0 );
                data.timestamp = v.second.get( "timestamp", 0 );
                m_pvs[v.first] = data;
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::write( a_prop_tree );

        for ( std::map<std::string,PVData>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
        {
            boost::property_tree::ptree pt;
            pt.put( "status", ipv->second.status );
            pt.put( "value", ipv->second.value );
            pt.put( "timestamp", ipv->second.timestamp );

            a_prop_tree.add_child( std::string("pvs.") + ipv->first, pt );
        }
    }
};


#if 0
class SubscribeProcessVariableCommand : public Command
{
public:
SubscribeProcessVariableCommand( std::string a_pv_name, bool a_subscribe, uint32_t a_update_interval )
    : m_pv_name(a_pv_name), m_subscribe(a_subscribe), m_update_interval(a_update_interval)
{}
SubscribeProcessVariableCommand( const cms::Message &a_msg )
    : Command( a_msg )
{  translateFrom( a_msg ); }

inline MessageType getMessageType() const
{ return MSG_CMD_CONFIG_LOGGING; }

private:
virtual void translateTo( cms::Message &a_msg )
{
    Command::translateTo( a_msg );

    a_msg.setBooleanProperty( "log_enabled", m_enabled );
    a_msg.setShortProperty( "log_level", (short)m_level );
}

void translateFrom( const cms::Message &a_msg )
{
    m_enabled = a_msg.getBooleanProperty( "log_enabled" );
    m_level = (Level)a_msg.getShortProperty( "log_level" );
}

std::string     m_pv_name;
bool            m_subscribe;
uint32_t        m_update_interval;
};
#endif

//////////////////////////////////////////////////////////////////////////////
// DASMon Application Messages

class ConnectionStatusMessage : public MessageBase
{
public:
    ConnectionStatusMessage()
    {}

    ConnectionStatusMessage( bool m_connected, const std::string &a_host, unsigned short a_port )
        : m_connected(m_connected), m_host(a_host), m_port(a_port) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_SMS_CONN_STATUS; }

    bool                m_connected;
    std::string         m_host;
    unsigned short      m_port;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_connected = a_prop_tree.get( "connected", false );
        m_host = a_prop_tree.get( "host", "" );
        m_port = a_prop_tree.get( "port", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "connected", m_connected );
        a_prop_tree.put( "host", m_host );
        a_prop_tree.put( "port", m_port );
    }
};


class RunStatusMessage : public MessageBase
{
public:
    RunStatusMessage()
    {}

    RunStatusMessage( bool a_recording, unsigned long a_run_number, unsigned long a_timestamp )
        : m_recording(a_recording), m_run_number(a_run_number), m_timestamp(a_timestamp) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_STATUS; }

    bool                m_recording;
    unsigned long       m_run_number;
    unsigned long       m_timestamp;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_recording = a_prop_tree.get( "recording", false );
        m_run_number = a_prop_tree.get( "run_number", 0 );
        m_timestamp = a_prop_tree.get( "timestamp", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "recording", m_recording );
        a_prop_tree.put( "run_number", m_run_number );
        a_prop_tree.put( "timestamp", m_timestamp );
    }
};


class PauseStatusMessage : public MessageBase
{
public:
    PauseStatusMessage()
    {}

    PauseStatusMessage( bool a_paused )
        : m_paused(a_paused) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_PAUSE_STATUS; }
    bool                m_paused;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_paused = a_prop_tree.get( "paused", false );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "paused", m_paused );
    }
};


class ScanStatusMessage : public MessageBase
{
public:
    ScanStatusMessage()
    {}

    ScanStatusMessage( bool a_scaning, unsigned long a_scan_index )
        : m_scaning(a_scaning), m_scan_index(a_scan_index) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_SCAN_STATUS; }

    bool                m_scaning;
    unsigned long       m_scan_index;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_scaning = a_prop_tree.get( "scanning", false );
        m_scan_index = a_prop_tree.get( "scan_index", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "scanning", m_scaning );
        a_prop_tree.put( "scan_index", m_scan_index );
    }
};


class BeamInfoMessage : public MessageBase, public ADARA::DASMON::BeamInfo
{
public:
    BeamInfoMessage()
    {}

    BeamInfoMessage( const ADARA::DASMON::BeamInfo &a_info )
      : ADARA::DASMON::BeamInfo( a_info )
    {}

    inline MessageType  getMessageType() const { return MSG_DASMON_BEAM_INFO; }

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_facility = a_prop_tree.get( "facility", "" );
        m_beam_id = a_prop_tree.get( "beam_id", "" );
        m_beam_sname = a_prop_tree.get( "beam_sname", "" );
        m_beam_lname = a_prop_tree.get( "beam_lname", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "facility", m_facility );
        a_prop_tree.put( "beam_id", m_beam_id );
        a_prop_tree.put( "beam_sname", m_beam_sname );
        a_prop_tree.put( "beam_lname", m_beam_lname );
    }
};


class RunInfoMessage : public MessageBase, public ADARA::DASMON::RunInfo
{
public:
    RunInfoMessage()
    {}

    RunInfoMessage( const ADARA::DASMON::RunInfo &a_info )
      : ADARA::DASMON::RunInfo( a_info )
    {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_INFO; }

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_proposal_id = a_prop_tree.get( "proposal_id", "" );
        m_run_title = a_prop_tree.get( "run_title", "" );
        m_run_num = a_prop_tree.get( "run_num", 0 );
        m_sample_id = a_prop_tree.get( "sample_id", "" );
        m_sample_name = a_prop_tree.get( "sample_name", "" );
        m_sample_environ = a_prop_tree.get( "sample_environment", "" );
        m_sample_formula = a_prop_tree.get( "sample_formula", "" );
        m_sample_nature = a_prop_tree.get( "sample_nature", "" );

        m_user_info.clear();
        ADARA::DASMON::UserInfo info;

        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("users"))
            {
                info.m_id = v.second.get( "id", "" );
                info.m_name = v.second.get( "name", "" );
                info.m_role = v.second.get( "role", "" );
                m_user_info.push_back( info );
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "proposal_id", m_proposal_id );
        a_prop_tree.put( "run_title", m_run_title );
        a_prop_tree.put( "run_num", m_run_num );
        a_prop_tree.put( "sample_id", m_sample_id );
        a_prop_tree.put( "sample_name", m_sample_name );
        a_prop_tree.put( "sample_environment", m_sample_environ );
        a_prop_tree.put( "sample_formula", m_sample_formula );
        a_prop_tree.put( "sample_nature", m_sample_nature );

        for ( std::vector<ADARA::DASMON::UserInfo>::iterator u = m_user_info.begin(); u != m_user_info.end(); ++u )
        {
            boost::property_tree::ptree ut;
            ut.put( "id", u->m_id );
            ut.put( "name", u->m_name );
            ut.put( "role", u->m_role );
            a_prop_tree.add_child( "users.user", ut );
        }
    }
};



class BeamMetricsMessage : public MessageBase, public ADARA::DASMON::BeamMetrics
{
public:
    BeamMetricsMessage()
    {}

    BeamMetricsMessage( const ADARA::DASMON::BeamMetrics &a_metrics )
      : ADARA::DASMON::BeamMetrics( a_metrics )
    {}

    inline MessageType  getMessageType() const { return MSG_DASMON_BEAM_METRICS; }

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_count_rate = a_prop_tree.get( "count_rate", 0.0 );
        m_pulse_charge = a_prop_tree.get( "pulse_charge", 0.0 );
        m_pulse_freq = a_prop_tree.get( "pulse_freq", 0.0 );
        m_pixel_error_rate = a_prop_tree.get( "pixel_error_rate", 0.0 );
        m_stream_bps = a_prop_tree.get( "stream_bps", 0 );

        m_monitor_count_rate.clear();
        uint32_t id;
        double counts;

        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("monitors"))
            {
                id = v.second.get( "id", 0 );
                counts = v.second.get( "counts", 0.0 );
                m_monitor_count_rate[id] = counts;
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "count_rate", m_count_rate );
        a_prop_tree.put( "pulse_charge", m_pulse_charge );
        a_prop_tree.put( "pulse_freq", m_pulse_freq );
        a_prop_tree.put( "pixel_error_rate", m_pixel_error_rate );
        a_prop_tree.put( "stream_bps", m_stream_bps );

        std::map<uint32_t,double>::const_iterator im = m_monitor_count_rate.begin();
        for ( ; im != m_monitor_count_rate.end(); ++im )
        {
            boost::property_tree::ptree sub;
            sub.put( "id", im->first );
            sub.put( "counts", im->second );
            a_prop_tree.add_child( "monitors.monitor", sub );
        }
    }
};


class RunMetricsMessage : public MessageBase, public ADARA::DASMON::RunMetrics
{
public:
    RunMetricsMessage()
    {}

    RunMetricsMessage( const ADARA::DASMON::RunMetrics &a_metrics )
      : ADARA::DASMON::RunMetrics( a_metrics )
    {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_METRICS; }

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_pulse_count           = a_prop_tree.get( "pulse_count", 0 );
        m_pulse_charge          = a_prop_tree.get( "pulse_charge", 0.0 );
        m_pixel_error_count     = a_prop_tree.get( "pixel_error_count", 0 );
        m_dup_pulse_count       = a_prop_tree.get( "dup_pulse_count", 0 );
        m_pulse_veto_count      = a_prop_tree.get( "pulse_veto_count", 0 );
        m_mapping_error_count   = a_prop_tree.get( "mapping_error_count", 0 );
        m_missing_rtdl_count    = a_prop_tree.get( "missing_rtdl_count", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "pulse_count", m_pulse_count );
        a_prop_tree.put( "pulse_charge", m_pulse_charge );
        a_prop_tree.put( "pixel_error_count", m_pixel_error_count );
        a_prop_tree.put( "dup_pulse_count", m_dup_pulse_count );
        a_prop_tree.put( "pulse_veto_count", m_pulse_veto_count );
        a_prop_tree.put( "mapping_error_count", m_mapping_error_count );
        a_prop_tree.put( "missing_rtdl_count", m_missing_rtdl_count );
    }
};

}}}


#endif // DASMONMESSAGES_H
