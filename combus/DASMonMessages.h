#ifndef DASMONMESSAGES_H
#define DASMONMESSAGES_H

#include <iostream>
#include <set>
#include <boost/foreach.hpp>
#include "ADARAUtils.h"
#include "ComBusDefs.h"
#include "RuleEngine.h"
#include "DASMonDefs.h"

namespace ADARA {
namespace ComBus {
namespace DASMON {


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DASMon Command Messages - These messages related to command and control of dasmond

/// GetRuleDefinitions message requests rule and signal config data from dasmond
DEF_SIMPLE_MSG(GetRuleDefinitions,MSG_DASMON_GET_RULES)

/// RestoreDefaultRuleDefinitions message requests that dasmond restore default rules and signals
DEF_SIMPLE_MSG(RestoreDefaultRuleDefinitions,MSG_DASMON_RESTORE_DEFAULT_RULES)

/// GetProcessVariables message requests dasmond to emitt all currently defined PVs
DEF_SIMPLE_MSG(GetProcessVariables,MSG_DASMON_GET_PVS)

/// GetInputFacts message requests available and/or asserted facts from rule engine
DEF_SIMPLE_MSG(GetInputFacts,MSG_DASMON_GET_INPUT_FACTS)


/** \brief The RulePayload class provides rule and signal configuration access.
  *
  * The RulePayload class is used internally by the RuleDefinitions and SetRuleDefinitions
  * message classes to provide access to and serialization of rule and signal configuration
  * data.
  */
class RulePayload
{
public:
    RulePayload() {}

    std::vector<RuleEngine::RuleInfo>       m_rules;
    std::vector<ADARA::DASMON::SignalInfo>  m_signals;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        m_rules.clear();
        RuleEngine::RuleInfo rule;
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("rules"))
            {
                rule.enabled = v.second.get( "enabled", false );
                rule.fact = v.second.get( "fact", "" );
                rule.expr = v.second.get( "expr", "" );
                rule.desc = v.second.get( "desc", "" );

                m_rules.push_back( rule );
            }
        } catch(...) {}

        m_signals.clear();
        ADARA::DASMON::SignalInfo signal;
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("signals"))
            {
                signal.enabled = v.second.get( "enabled", false );
                signal.name = v.second.get( "name", "" );
                signal.fact = v.second.get( "fact", "" );
                signal.source = v.second.get( "source", "" );
                signal.level = (Level)v.second.get( "level", 0 );
                signal.msg = v.second.get( "message", "" );
                signal.desc = v.second.get( "desc", "" );

                m_signals.push_back( signal );
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        for ( std::vector<RuleEngine::RuleInfo>::iterator r = m_rules.begin(); r != m_rules.end(); ++r )
        {
            boost::property_tree::ptree pt;
            pt.put( "enabled", r->enabled );
            pt.put( "fact", r->fact );
            pt.put( "expr", r->expr );
            pt.put( "desc", r->desc );
            a_prop_tree.add_child( "rules.rule", pt );
        }

        for ( std::vector<ADARA::DASMON::SignalInfo>::iterator s = m_signals.begin(); s != m_signals.end(); ++s )
        {
            boost::property_tree::ptree pt;
            pt.put( "enabled", s->enabled );
            pt.put( "name", s->name );
            pt.put( "fact", s->fact );
            pt.put( "source", s->source );
            pt.put( "level", (unsigned short)s->level );
            pt.put( "message", s->msg );
            pt.put( "desc", s->desc );
            a_prop_tree.add_child( "signals.signal", pt );
        }
    }
};


/** \brief Message that describes current rules and signals configured in dasmond
  *
  * The RuleDefinitions message class is emitted by dasmond to describe the
  * current configuration of rules and signals. This message can be sent in
  * response to a GetRuleDefinitions message or as an Ack/Nack in response to a
  * SetRuleDefinitions message.
  */
class RuleDefinitions :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_RULE_DEFINITIONS,RuleDefinitions>,
        public RulePayload
{
public:
    RuleDefinitions()
    {}

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );
        RulePayload::read( a_prop_tree );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );
        RulePayload::write( a_prop_tree );
    }
};

/** \brief Message that sets rule and signal configuration for dasmond
  *
  * The SetRuleDefinitions message is sent to dasmond to configure both rules
  * and signals. Dasmond will respond by emitting a RuleDefinitions message
  * describing the rules and signals that are set. Note that if any errors
  * are present in the rule or signal definitions, none of the changes will
  * be applied by dasmond. It is the responsibility of the sender to assess
  * the differences between what was sent/requested and what was received.
  */
class SetRuleDefinitions :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_SET_RULES,SetRuleDefinitions>,
        public RulePayload
{
public:
    SetRuleDefinitions()
    {}

    bool m_set_default;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );
        RulePayload::read( a_prop_tree );

        m_set_default = a_prop_tree.get( "set_default", false );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );
        RulePayload::write( a_prop_tree );

        a_prop_tree.put( "set_default", m_set_default );
    }
};


class RuleErrors :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_RULE_ERRORS,RuleErrors>
{
public:
    RuleErrors()
    {}

    std::map<std::string,std::string> m_errors;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        try {
            m_errors.clear();

            // Throws if erros doesn't exist
            boost::property_tree::ptree pt = a_prop_tree.get_child("errors");
            for ( boost::property_tree::ptree::const_iterator i = pt.begin(); i != pt.end(); i++ )
            {
                m_errors[i->first] = i->second.data();
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        boost::property_tree::ptree sub;
        std::map<std::string,std::string>::const_iterator ie = m_errors.begin();
        for ( ; ie != m_errors.end(); ++ie )
            sub.put( ie->first, ie->second );

        a_prop_tree.put_child( "errors", sub );
    }
};



/** \brief Message sent from dasmond to describe available and asserted facts
  *
  * The InputFacts message is emitted by dasmond in reponse to a GetInputFacts
  * message and contains available facts that can be used as inputs to rules.
  * Facts that are derived from configured rules are not included, but facts
  * that are asserted due to process variable or process status issues are
  * included.
  */
class InputFacts : public ADARA::ComBus::TemplMessageBase<MSG_DASMON_INPUT_FACTS,InputFacts>
{
public:
    InputFacts()
    {}

    /// Contains facts name mapped to fact description
    std::set<std::string> m_facts;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_facts.clear();
        try {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v, a_prop_tree.get_child("facts"))
            {
                m_facts.insert(v.second.data());
            }
        } catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );
        boost::property_tree::ptree pt;

        for ( std::set<std::string>::iterator fact = m_facts.begin(); fact != m_facts.end(); ++fact )
        {
            pt.push_back( std::make_pair( "", *fact ));
        }

        a_prop_tree.add_child( "facts", pt );
    }
};

// Note: The ProcessVariables message will be removed when direct output to db is available

/// Process variable types
enum PVDataType
{
    PVDT_UINT,
    PVDT_DOUBLE,
    PVDT_STRING,
    PVDT_UINT_ARRAY,
    PVDT_DOUBLE_ARRAY
};

class ProcessVariables :
    public ADARA::ComBus::TemplMessageBase<MSG_DASMON_PVS, ProcessVariables>
{
public:
    ProcessVariables()
    {}

    struct PVData
    {
        PVData()
            : pv_type(PVDT_DOUBLE), is_str(false),
            uint_val(0), dbl_val(0.0),
            status(0), timestamp(0), timestamp_nanosec(0)
        {}

        PVData( uint32_t a_value, int a_status,
                uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
            : pv_type(PVDT_UINT), is_str(false),
            uint_val(a_value), dbl_val(0.0),
            status(a_status),
            timestamp(a_timestamp), timestamp_nanosec(a_timestamp_nanosec)
        {}

        PVData( double a_value, int a_status,
                uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
            : pv_type(PVDT_DOUBLE), is_str(false),
            uint_val(0), dbl_val(a_value),
            status(a_status),
            timestamp(a_timestamp), timestamp_nanosec(a_timestamp_nanosec)
        {}

        PVData( const std::string &a_value, int a_status,
                uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
            : pv_type(PVDT_STRING), is_str(true),
            uint_val(0), dbl_val(0.0), str_val(a_value),
            status(a_status),
            timestamp(a_timestamp), timestamp_nanosec(a_timestamp_nanosec)
        {}

        PVData( std::vector<uint32_t> a_value, int a_status,
                uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
            : pv_type(PVDT_UINT_ARRAY), is_str(false),
            uint_val(0), dbl_val(0.0), uint_array(a_value),
            status(a_status),
            timestamp(a_timestamp), timestamp_nanosec(a_timestamp_nanosec)
        {}

        PVData( std::vector<double> a_value, int a_status,
                uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
            : pv_type(PVDT_DOUBLE_ARRAY), is_str(false),
            uint_val(0), dbl_val(0.0), dbl_array(a_value),
            status(a_status),
            timestamp(a_timestamp), timestamp_nanosec(a_timestamp_nanosec)
        {}

        PVDataType              pv_type;
        bool                    is_str;
        uint32_t                uint_val;
        double                  dbl_val;
        std::string             str_val;
        std::vector<uint32_t>   uint_array;
        std::vector<double>     dbl_array;
        int                     status;
        uint32_t                timestamp;
        uint32_t                timestamp_nanosec;
    };

    std::map<std::string, PVData> m_pvs;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        PVData data;
        m_pvs.clear();
        try
        {
            BOOST_FOREACH( const boost::property_tree::ptree::value_type &v,
                    a_prop_tree.get_child("pvs") )
            {
                int32_t type_ck = v.second.get( "pv_type", -1 );

                // Old Style, No "pv_type", Only "is_str" (Assumes Double)
                if ( type_ck == -1 )
                {
                    data.is_str = v.second.get( "is_str", false );
                    data.pv_type = ( data.is_str )
                        ? PVDT_STRING : PVDT_DOUBLE;
                }

                // New Style, Use "pv_type", But Still Set "is_str"... ;-D
                else
                {
                    data.pv_type = (PVDataType) type_ck;
                    data.is_str = ( data.pv_type == PVDT_STRING );
                }

                data.status = v.second.get( "status", 0 );

                std::string array_str;

                switch ( data.pv_type )
                {
                    case PVDT_UINT:
                        data.uint_val = v.second.get( "uint_val", 0 );
                        break;
                    case PVDT_DOUBLE:
                        data.dbl_val = v.second.get( "dbl_val", 0.0 );
                        break;
                    case PVDT_STRING:
                        data.str_val = v.second.get( "str_val", "" );
                        break;
                    case PVDT_UINT_ARRAY:
                        array_str = v.second.get( "uint_array", "[]" );
                        Utils::parseArrayString(
                            array_str, data.uint_array );
                        break;
                    case PVDT_DOUBLE_ARRAY:
                        array_str = v.second.get( "dbl_array", "[]" );
                        Utils::parseArrayString(
                            array_str, data.dbl_array );
                        break;
                    // Unknown Data Type, Dang... (No Way to Log from Here!)
                    // - Just Default to a Double(0.0)... ;-b
                    default:
                        data.pv_type = PVDT_DOUBLE;
                        data.dbl_val = 0.0;
                        break;
                }

                data.timestamp = v.second.get( "timestamp", 0UL );

                double timestamp_micro_ck =
                    v.second.get( "timestamp_micro", -1.0 );

                // Old Style, No Double Floating Point Timestamp w/Microsecs
                if ( timestamp_micro_ck == -1.0 )
                    data.timestamp_nanosec = 0;

                // Extract Nanosecs (Approx) from Double Timestamp...
                // Converting to Double _Only_ Retains Microsecond Precision
                else
                {
                    data.timestamp_nanosec = (uint32_t)
                        ( ( timestamp_micro_ck - ((double) data.timestamp) )
                            * 1e9 );
                }

                m_pvs[v.second.get( "name", "" )] = data;
            }
        }
        catch(...) {}
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        boost::property_tree::ptree ppt;

        for ( std::map<std::string,PVData>::iterator ipv = m_pvs.begin();
                ipv != m_pvs.end(); ++ipv )
        {
            boost::property_tree::ptree pt;
            pt.put( "name", ipv->first );
            pt.put( "status", ipv->second.status );

            std::string array_str;

            switch ( ipv->second.pv_type )
            {
                case PVDT_UINT:
                    pt.put( "uint_val", ipv->second.uint_val );
                    // For Backwards Compatibility, For Now... ;-D
                    pt.put( "dbl_val", (double)ipv->second.uint_val );
                    break;
                case PVDT_DOUBLE:
                    pt.put( "dbl_val", ipv->second.dbl_val );
                    break;
                case PVDT_STRING:
                    pt.put( "str_val", ipv->second.str_val );
                    break;
                case PVDT_UINT_ARRAY:
                    Utils::printArrayString( ipv->second.uint_array,
                        array_str );
                    pt.put( "uint_array", array_str );
                    // For Backwards Compatibility, For Now... ;-D
                    if ( ipv->second.uint_array.size() > 0 )
                    {
                        pt.put( "dbl_val",
                            (double)ipv->second.uint_array[0] );
                    }
                    else
                        pt.put( "dbl_val", 0.0 );
                    break;
                case PVDT_DOUBLE_ARRAY:
                    Utils::printArrayString( ipv->second.dbl_array,
                        array_str );
                    pt.put( "dbl_array", array_str );
                    // For Backwards Compatibility, For Now... ;-D
                    if ( ipv->second.dbl_array.size() > 0 )
                        pt.put( "dbl_val", ipv->second.dbl_array[0] );
                    else
                        pt.put( "dbl_val", 0.0 );
                    break;
                // Unknown Data Type, Dang... (No Way to Log from Here!)
                // - Just Default to a Double(0.0)... ;-b
                default:
                    ipv->second.pv_type = PVDT_DOUBLE;
                    ipv->second.is_str = false;
                    pt.put( "dbl_val", 0.0 );
                    break;
            }

            pt.put( "pv_type", ipv->second.pv_type );
            pt.put( "is_str", ipv->second.is_str );

            pt.put( "timestamp", ipv->second.timestamp );

            // Assemble Double Timestamp with Microsecond Precision
            // (Converting to Double _Only_ Retains Microsecond Precision
            // from Nanosecond Value...)
            double timestamp_micro = ((double) ipv->second.timestamp)
                + ( ((double) ipv->second.timestamp_nanosec) / 1.0e9 );
            pt.put( "timestamp_micro", timestamp_micro );

            ppt.push_back( std::make_pair( "", pt ));
        }
        a_prop_tree.add_child( "pvs", ppt );
    }
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DASMon Application Messages - These messages related to dasmond broadcast messages

/// Indicates the current connection status and host information
class ConnectionStatusMessage :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_SMS_CONN_STATUS,ConnectionStatusMessage>
{
public:
    ConnectionStatusMessage()
    {}

    ConnectionStatusMessage( bool m_connected, const std::string &a_host, unsigned short a_port )
        : m_connected(m_connected), m_host(a_host), m_port(a_port) {}


    bool                m_connected;
    std::string         m_host;
    unsigned short      m_port;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_connected = a_prop_tree.get( "connected", false );
        m_host = a_prop_tree.get( "host", "" );
        m_port = a_prop_tree.get( "port", 0U );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "connected", m_connected );
        a_prop_tree.put( "host", m_host );
        a_prop_tree.put( "port", m_port );
    }
};

/// Indicates current run status, number, and start time
class RunStatusMessage :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_RUN_STATUS,RunStatusMessage>
{
public:
    RunStatusMessage()
    {}

    RunStatusMessage( bool a_recording, uint32_t a_run_number,
            uint32_t a_timestamp, uint32_t a_timestamp_nanosec )
        : m_recording(a_recording), m_run_number(a_run_number),
        m_timestamp(a_timestamp), m_timestamp_nanosec(a_timestamp_nanosec)
    {}

    bool        m_recording;
    uint32_t    m_run_number;
    uint32_t    m_timestamp;
    uint32_t    m_timestamp_nanosec;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_recording = a_prop_tree.get( "recording", false );
        m_run_number = a_prop_tree.get( "run_number", 0UL );
        m_timestamp = a_prop_tree.get( "timestamp", 0UL );

        double timestamp_micro_ck =
            a_prop_tree.get( "timestamp_micro", -1.0 );

        // Old Style, No Double Floating Point Timestamp w/Microsecs
        if ( timestamp_micro_ck == -1.0 )
            m_timestamp_nanosec = 0;

        // Extract Nanosecs (Approx) from Double Timestamp...
        // Converting to Double _Only_ Retains Microsecond Precision
        else {
            m_timestamp_nanosec = (uint32_t)
                ( ( timestamp_micro_ck - ((double) m_timestamp) ) * 1.0e9 );
        }
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "recording", m_recording );
        a_prop_tree.put( "run_number", m_run_number );
        a_prop_tree.put( "timestamp", m_timestamp );

        // Assemble Double Timestamp with Microsecond Precision
        // (Converting to Double _Only_ Retains Microsecond Precision
        // from Nanosecond Value...)
        double timestamp_micro = ((double) m_timestamp)
            + ( ((double) m_timestamp_nanosec) / 1.0e9 );
        a_prop_tree.put( "timestamp_micro", timestamp_micro );
    }
};


/// Indicates current pause state
class PauseStatusMessage :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_PAUSE_STATUS,PauseStatusMessage>
{
public:
    PauseStatusMessage()
    {}

    PauseStatusMessage( bool a_paused )
        : m_paused(a_paused) {}

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


/// Indicates current scan state and scan index value
class ScanStatusMessage :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_SCAN_STATUS,ScanStatusMessage>
{
public:
    ScanStatusMessage()
    {}

    ScanStatusMessage( bool a_scaning, uint32_t a_scan_index )
        : m_scaning(a_scaning), m_scan_index(a_scan_index) {}

    bool        m_scaning;
    uint32_t    m_scan_index;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_scaning = a_prop_tree.get( "scanning", false );
        m_scan_index = a_prop_tree.get( "scan_index", 0UL );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "scanning", m_scaning );
        a_prop_tree.put( "scan_index", m_scan_index );
    }
};


/// Conveys a payload of beam line information
class BeamInfoMessage :
        public ADARA::ComBus::TemplMessageBase<MSG_DASMON_BEAM_INFO,BeamInfoMessage>,
        public ADARA::DASMON::BeamInfo
{
public:
    BeamInfoMessage()
    {}

    BeamInfoMessage( const ADARA::DASMON::BeamInfo &a_info )
      : ADARA::DASMON::BeamInfo( a_info )
    {}

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_facility = a_prop_tree.get( "facility", "" );

        m_target_station_number = a_prop_tree.get(
            "target_station_number", 1 );

        m_beam_id = a_prop_tree.get( "beam_id", "" );
        m_beam_sname = a_prop_tree.get( "beam_sname", "" );
        m_beam_lname = a_prop_tree.get( "beam_lname", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "facility", m_facility );

        a_prop_tree.put( "target_station_number", m_target_station_number );

        a_prop_tree.put( "beam_id", m_beam_id );
        a_prop_tree.put( "beam_sname", m_beam_sname );
        a_prop_tree.put( "beam_lname", m_beam_lname );
    }
};


/// Carries a payload of run information
class RunInfoMessage :
        public TemplMessageBase<MSG_DASMON_RUN_INFO,RunInfoMessage>,
        public ADARA::DASMON::RunInfo
{
public:
    RunInfoMessage()
    {}

    RunInfoMessage( const ADARA::DASMON::RunInfo &a_info )
      : ADARA::DASMON::RunInfo( a_info )
    {}


protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_proposal_id = a_prop_tree.get( "proposal_id", "" );
        m_run_title = a_prop_tree.get( "run_title", "" );
        m_run_num = a_prop_tree.get( "run_num", 0UL );
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


/// Carries a payload of beam metrics data
class BeamMetricsMessage :
        public TemplMessageBase<MSG_DASMON_BEAM_METRICS,BeamMetricsMessage>,
        public ADARA::DASMON::BeamMetrics
{
public:
    BeamMetricsMessage()
    {}

    BeamMetricsMessage( const ADARA::DASMON::BeamMetrics &a_metrics )
      : ADARA::DASMON::BeamMetrics( a_metrics )
    {}

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_count_rate = a_prop_tree.get( "count_rate", 0.0 );
        m_pulse_charge = a_prop_tree.get( "pulse_charge", 0.0 );
        m_pulse_freq = a_prop_tree.get( "pulse_freq", 0.0 );
        m_pixel_error_rate = a_prop_tree.get( "pixel_error_rate", 0.0 );
        m_stream_bps = a_prop_tree.get( "stream_bps", 0UL );

        m_monitor_count_rate.clear();
        uint32_t id;
        double counts;
        try {
            // Throws id monitors doesn't exist
            boost::property_tree::ptree pt = a_prop_tree.get_child("monitors");
            for ( boost::property_tree::ptree::const_iterator i = pt.begin(); i != pt.end(); i++ )
            {
                id = boost::lexical_cast<uint32_t>( i->first );
                counts = boost::lexical_cast<double>( i->second.data() );
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

        boost::property_tree::ptree sub;
        std::map<uint32_t,double>::const_iterator im = m_monitor_count_rate.begin();
        for ( ; im != m_monitor_count_rate.end(); ++im )
            sub.put( boost::lexical_cast<std::string>(im->first), im->second );

        a_prop_tree.put_child( "monitors", sub );
    }
};


/// Carries a payload of run metrics data
class RunMetricsMessage :
        public TemplMessageBase<MSG_DASMON_RUN_METRICS,RunMetricsMessage>,
        public ADARA::DASMON::RunMetrics
{
public:
    RunMetricsMessage()
    {}

    RunMetricsMessage( const ADARA::DASMON::RunMetrics &a_metrics )
      : ADARA::DASMON::RunMetrics( a_metrics )
    {}

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_time                  = a_prop_tree.get( "total_time", 0.0 );
        m_total_counts          = a_prop_tree.get( "total_counts", 0UL );
        m_total_charge          = a_prop_tree.get( "total_charge", 0.0 );
        m_pixel_error_count     = a_prop_tree.get( "pixel_error_count", 0UL );
        m_dup_pulse_count       = a_prop_tree.get( "dup_pulse_count", 0UL );
        m_pulse_veto_count      = a_prop_tree.get( "pulse_veto_count", 0UL );
        m_mapping_error_count   = a_prop_tree.get( "mapping_error_count", 0UL );
        m_missing_rtdl_count    = a_prop_tree.get( "missing_rtdl_count", 0UL );
        m_pulse_pcharge_uncorrected
                                = a_prop_tree.get(
                                      "pulse_pcharge_uncorrected", 0UL );
        m_got_metadata_count    = a_prop_tree.get( "got_metadata_count", 0UL );
        m_got_neutrons_count    = a_prop_tree.get( "got_neutrons_count", 0UL );
        m_has_states_count      = a_prop_tree.get( "has_states_count", 0UL );
        m_total_pulses_count    = a_prop_tree.get( "total_pulses_count", 0UL );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "total_time", m_time );
        a_prop_tree.put( "total_counts", m_total_counts );
        a_prop_tree.put( "total_charge", m_total_charge );
        a_prop_tree.put( "pixel_error_count", m_pixel_error_count );
        a_prop_tree.put( "dup_pulse_count", m_dup_pulse_count );
        a_prop_tree.put( "pulse_veto_count", m_pulse_veto_count );
        a_prop_tree.put( "mapping_error_count", m_mapping_error_count );
        a_prop_tree.put( "missing_rtdl_count", m_missing_rtdl_count );
        a_prop_tree.put( "pulse_pcharge_uncorrected",
            m_pulse_pcharge_uncorrected );
        a_prop_tree.put( "got_metadata_count", m_got_metadata_count );
        a_prop_tree.put( "got_neutrons_count", m_got_neutrons_count );
        a_prop_tree.put( "has_states_count", m_has_states_count );
        a_prop_tree.put( "total_pulses_count", m_total_pulses_count );
    }
};


/// Carries a payload of run metrics data
class StreamMetricsMessage :
        public TemplMessageBase<MSG_DASMON_STREAM_METRICS,StreamMetricsMessage>,
        public ADARA::DASMON::StreamMetrics
{
public:
    StreamMetricsMessage()
    {}

    StreamMetricsMessage( const ADARA::DASMON::StreamMetrics &a_metrics )
      : ADARA::DASMON::StreamMetrics( a_metrics )
    {}

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_invalid_pkt_type      = a_prop_tree.get( "pkt_type", 0UL );
        m_invalid_pkt           = a_prop_tree.get( "inv_pkt", 0UL );
        m_invalid_pkt_time      = a_prop_tree.get( "pkt_time", 0UL );
        m_duplicate_packet      = a_prop_tree.get( "dup_pkt", 0UL );
        m_pulse_freq_tol        = a_prop_tree.get( "pulse_freq", 0UL );
        m_cycle_err             = a_prop_tree.get( "cycle", 0UL );
        m_invalid_bank_id       = a_prop_tree.get( "inv_bank", 0UL );
        m_bank_source_mismatch  = a_prop_tree.get( "bank_src", 0UL );
        m_duplicate_source      = a_prop_tree.get( "dup_src", 0UL );
        m_duplicate_bank        = a_prop_tree.get( "dup_bank", 0UL );
        m_pixel_map_err         = a_prop_tree.get( "pix_map", 0UL );
        m_pixel_bank_mismatch   = a_prop_tree.get( "pix_bank", 0UL );
        m_pixel_invalid_tof     = a_prop_tree.get( "pix_tof", 0UL );
        m_pixel_unknown_id      = a_prop_tree.get( "pix_id", 0UL );
        m_pixel_errors          = a_prop_tree.get( "pix_err", 0UL );
        m_bad_ddp_xml           = a_prop_tree.get( "ddp_xml", 0UL );
        m_bad_runinfo_xml       = a_prop_tree.get( "runinfo_xml", 0UL );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "pkt_type", m_invalid_pkt_type );
        a_prop_tree.put( "inv_pkt", m_invalid_pkt );
        a_prop_tree.put( "pkt_time", m_invalid_pkt_time );
        a_prop_tree.put( "dup_pkt", m_duplicate_packet );
        a_prop_tree.put( "pulse_freq", m_pulse_freq_tol );
        a_prop_tree.put( "cycle", m_cycle_err );
        a_prop_tree.put( "inv_bank", m_invalid_bank_id );
        a_prop_tree.put( "bank_src", m_bank_source_mismatch );
        a_prop_tree.put( "dup_src", m_duplicate_source );
        a_prop_tree.put( "dup_bank", m_duplicate_bank );
        a_prop_tree.put( "pix_map", m_pixel_map_err );
        a_prop_tree.put( "pix_bank", m_pixel_bank_mismatch );
        a_prop_tree.put( "pix_tof", m_pixel_invalid_tof );
        a_prop_tree.put( "pix_id", m_pixel_unknown_id );
        a_prop_tree.put( "pix_err", m_pixel_errors );
        a_prop_tree.put( "ddp_xml", m_bad_ddp_xml );
        a_prop_tree.put( "runinfo_xml", m_bad_runinfo_xml );
    }
};

}}}


#endif // DASMONMESSAGES_H

// vim: expandtab

