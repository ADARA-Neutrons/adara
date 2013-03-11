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
        BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("rules"))
        {
            rule.fact = v.second.get( "fact", "" );
            rule.expr = v.second.get( "expr", "" );

            m_rules.push_back( rule );
        }

        m_signals.clear();
        ADARA::DASMON::SignalInfo signal;
        BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("signals"))
        {
            signal.name = v.second.get( "name", "" );
            signal.fact = v.second.get( "fact", "" );
            signal.source = v.second.get( "source", "" );
            signal.level = (Level)v.second.get( "level", 0 );
            signal.msg = v.second.get( "message", "" );

            m_signals.push_back( signal );
        }
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

        BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("facts"))
            m_facts[v.first] = v.second.get( "desc", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        ControlMessage::write( a_prop_tree );

        for ( std::map<std::string,std::string>::iterator fact = m_facts.begin(); fact != m_facts.end(); ++fact )
        {
            boost::property_tree::ptree pt;
            pt.put( "desc", fact->second );
            a_prop_tree.add_child( std::string("facts.") + fact->first, pt );
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

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "connected", m_connected );
                            a_msg.setStringProperty( "host", m_host );
                            a_msg.setShortProperty( "port", (short)m_port );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_connected = a_msg.getBooleanProperty( "connected" );
                            m_host = a_msg.getStringProperty( "host" );
                            m_port = (unsigned short)a_msg.getShortProperty( "port" );
                        }
    */
};


class RunStatusMessage : public MessageBase
{
public:
    RunStatusMessage()
    {}

    RunStatusMessage( bool a_recording, unsigned long a_run_number )
        : m_recording(a_recording), m_run_number(a_run_number) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_STATUS; }

    bool                m_recording;
    unsigned long       m_run_number;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_recording = a_prop_tree.get( "recording", false );
        m_run_number = a_prop_tree.get( "run_number", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "recording", m_recording );
        a_prop_tree.put( "run_number", m_run_number );
    }

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "recording", m_recording );
                            a_msg.setIntProperty( "run_number", (long)m_run_number );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_recording = a_msg.getBooleanProperty( "recording" );
                            m_run_number = (unsigned long)a_msg.getIntProperty( "run_number" );
                        }
    */
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
/*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "paused", m_paused );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_paused = a_msg.getBooleanProperty( "paused" );
                        }
*/
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

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "scanning", m_scaning );
                            a_msg.setIntProperty( "scan_index", (long)m_scan_index );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_scaning = a_msg.getBooleanProperty( "scanning" );
                            m_scan_index = (unsigned long)a_msg.getIntProperty( "scan_index" );
                        }
    */
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

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setStringProperty( "facility", m_facility );
                            a_msg.setStringProperty( "beam_id", m_beam_id );
                            a_msg.setStringProperty( "beam_sname", m_beam_sname );
                            a_msg.setStringProperty( "beam_lname", m_beam_lname );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_facility = a_msg.getStringProperty( "facility" );
                            m_beam_id = a_msg.getStringProperty( "beam_id" );
                            m_beam_sname = a_msg.getStringProperty( "beam_sname" );
                            m_beam_lname = a_msg.getStringProperty( "beam_lname" );
                        }
    */
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

        BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("users"))
        {
            info.m_id = v.second.get( "id", "" );
            info.m_name = v.second.get( "name", "" );
            info.m_role = v.second.get( "role", "" );
            m_user_info.push_back( info );
        }

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

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setStringProperty( "proposal_id", m_proposal_id );
                            a_msg.setStringProperty( "run_title", m_run_title );
                            a_msg.setIntProperty( "run_num", (long)m_run_num );
                            a_msg.setStringProperty( "sample_id", m_sample_id );
                            a_msg.setStringProperty( "sample_name", m_sample_name );
                            a_msg.setStringProperty( "sample_environment", m_sample_environ );
                            a_msg.setStringProperty( "sample_formula", m_sample_formula );
                            a_msg.setStringProperty( "sample_nature", m_sample_nature );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_proposal_id = a_msg.getStringProperty( "proposal_id" );
                            m_run_title = a_msg.getStringProperty( "run_title" );
                            m_run_num = (unsigned long)a_msg.getIntProperty( "run_num" );
                            m_sample_id = a_msg.getStringProperty( "sample_id" );
                            m_sample_name = a_msg.getStringProperty( "sample_name" );
                            m_sample_environ = a_msg.getStringProperty( "sample_environment" );
                            m_sample_formula = a_msg.getStringProperty( "sample_formula" );
                            m_sample_nature = a_msg.getStringProperty( "sample_nature" );
                        }
    */
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
        m_num_monitors = a_prop_tree.get( "num_monitors", 0 );

        for ( unsigned short m = 0; m < m_num_monitors; ++m )
            m_monitor_count_rate[m] = a_prop_tree.get( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m), 0.0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "count_rate", m_count_rate );
        a_prop_tree.put( "pulse_charge", m_pulse_charge );
        a_prop_tree.put( "pulse_freq", m_pulse_freq );
        a_prop_tree.put( "pixel_error_rate", m_pixel_error_rate );
        a_prop_tree.put( "stream_bps", m_stream_bps );
        a_prop_tree.put( "num_monitors", m_num_monitors );

        for ( unsigned short m = 0; m < m_num_monitors; ++m )
            a_prop_tree.put( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m), m_monitor_count_rate[m] );
    }

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setDoubleProperty( "count_rate", m_count_rate );
                            a_msg.setDoubleProperty( "pulse_charge", m_pulse_charge );
                            a_msg.setDoubleProperty( "pulse_freq", m_pulse_freq );
                            a_msg.setDoubleProperty( "pixel_error_rate", m_pixel_error_rate );
                            a_msg.setIntProperty( "stream_bps", (long)m_stream_bps );
                            a_msg.setShortProperty( "num_monitors", m_num_monitors );
                            for ( unsigned short m = 0; m < m_num_monitors; ++m )
                                a_msg.setDoubleProperty( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m), m_monitor_count_rate[m] );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_count_rate = a_msg.getDoubleProperty( "count_rate" );
                            m_pulse_charge = a_msg.getDoubleProperty( "pulse_charge" );
                            m_pulse_freq = a_msg.getDoubleProperty( "pulse_freq" );
                            m_pixel_error_rate = a_msg.getDoubleProperty( "pixel_error_rate" );
                            m_stream_bps = (unsigned long)a_msg.getIntProperty( "stream_bps" );
                            m_num_monitors = (unsigned short)a_msg.getShortProperty( "num_monitors" );
                            for ( unsigned short m = 0; m < m_num_monitors; ++m )
                                m_monitor_count_rate[m] = a_msg.getDoubleProperty( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m) );
                        }
    */
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

        m_pulse_count = a_prop_tree.get( "pulse_count", 0 );
        m_pulse_charge = a_prop_tree.get( "pulse_charge", 0.0 );
        m_pixel_error_count = a_prop_tree.get( "pixel_error_count", 0 );
        m_dup_pulse_count = a_prop_tree.get( "dup_pulse_count", 0 );
        m_cycle_error_count = a_prop_tree.get( "cycle_error_count", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "pulse_count", m_pulse_count );
        a_prop_tree.put( "pulse_charge", m_pulse_charge );
        a_prop_tree.put( "pixel_error_count", m_pixel_error_count );
        a_prop_tree.put( "dup_pulse_count", m_dup_pulse_count );
        a_prop_tree.put( "cycle_error_count", m_cycle_error_count );
    }

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setIntProperty( "pulse_count", (long)m_pulse_count );
                            a_msg.setDoubleProperty( "pulse_charge", m_pulse_charge );
                            a_msg.setIntProperty( "pixel_error_count", (long)m_pixel_error_count );
                            a_msg.setIntProperty( "dup_pulse_count", (long)m_dup_pulse_count );
                            a_msg.setIntProperty( "cycle_error_count", (long)m_cycle_error_count );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_pulse_count = (unsigned long) a_msg.getIntProperty( "pulse_count" );
                            m_pulse_charge = a_msg.getDoubleProperty( "pulse_charge" );
                            m_pixel_error_count = (unsigned long) a_msg.getIntProperty( "pixel_error_count" );
                            m_dup_pulse_count = (unsigned long) a_msg.getIntProperty( "dup_pulse_count" );
                            m_cycle_error_count = (unsigned long) a_msg.getIntProperty( "cycle_error_count" );
                        }
    */
};

}}}


#endif // DASMONMESSAGES_H
