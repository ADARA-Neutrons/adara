#include "Rule.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;

namespace RuleEngine
{

Rule::Rule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value )
    : m_name(a_name), m_source(a_source), m_type(a_type), m_level(a_level), m_value(a_value), m_asserted(false)
{
    m_id = boost::lexical_cast<string>(a_level) + "_" + a_name + "_" + boost::lexical_cast<string>(a_type);
    m_message = a_name;

    switch ( a_type )
    {
    case EVENT:
        //m_message += " event.";
        break;
    case DEFINED:
        m_message += " is defined.";
        break;
    case NOT_DEFINED:
        m_message += " is not defined.";
        m_asserted = true; // Initial state is that item has not yet been observed
        break;
    case EQ:
        m_message += " = " + boost::lexical_cast<string>(a_value) + ".";
        break;
    case NEQ:
        m_message += " <> " + boost::lexical_cast<string>(a_value) + ".";
        break;
    case LT:
        m_message += " < " + boost::lexical_cast<string>(a_value) + ".";
        break;
    case LTE:
        m_message += " <= " + boost::lexical_cast<string>(a_value) + ".";
        break;
    case GT:
        m_message += " > " + boost::lexical_cast<string>(a_value) + ".";
        break;
    case GTE:
        m_message += " >= " + boost::lexical_cast<string>(a_value) + ".";
        break;
    }
}


void
Rule::event( std::vector<IRuleListener*> &a_listeners )
{
    if ( m_type == EVENT )
    {
        for ( vector<IRuleListener*>::iterator l = a_listeners.begin(); l != a_listeners.end(); ++l )
            (*l)->reportEvent( *this );
    }
}


void
Rule::evaluate( bool a_defined, vector<IRuleListener*> &a_listeners )
{
    if ((( m_type == DEFINED ) && a_defined ) || (( m_type == NOT_DEFINED ) && !a_defined ))
    {
        if ( !m_asserted )
        {
            m_asserted = true;

            for ( vector<IRuleListener*>::iterator l = a_listeners.begin(); l != a_listeners.end(); ++l )
                (*l)->assertFinding( m_id, *this );
        }
    }
    else if ((( m_type == DEFINED ) && !a_defined ) || (( m_type == NOT_DEFINED ) && a_defined ))
    {
        if ( m_asserted )
        {
            m_asserted = false;

            for ( vector<IRuleListener*>::iterator l = a_listeners.begin(); l != a_listeners.end(); ++l )
                (*l)->retractFinding( m_id );
        }
    }
}


void
Rule::evaluate( double a_value, std::vector<IRuleListener*> &a_listeners )
{
    bool assert = false;

    switch ( m_type )
    {
    case EQ:
        assert = (a_value == m_value);
        break;
    case NEQ:
        assert = (a_value != m_value);
        break;
    case LT:
        assert = (a_value < m_value);
        break;
    case LTE:
        assert = (a_value <= m_value);
        break;
    case GT:
        assert = (a_value > m_value);
        break;
    case GTE:
        assert = (a_value >= m_value);
        break;

    default:
        return;
    }

    if ( assert && ( !m_asserted ))
    {
        m_asserted = true;

        for ( vector<IRuleListener*>::iterator l = a_listeners.begin(); l != a_listeners.end(); ++l )
            (*l)->assertFinding( m_id, *this );
    }
    else if ( (!assert) && m_asserted )
    {
        m_asserted = false;

        for ( vector<IRuleListener*>::iterator l = a_listeners.begin(); l != a_listeners.end(); ++l )
            (*l)->retractFinding( m_id );
    }
}



RuleGroup::RuleGroup()
{
}


RuleGroup::~RuleGroup()
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
        delete *ir;
}


bool
RuleGroup::defineRule( Rule* a_rule )
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
    {
        if ( **ir == *a_rule )
        {
            delete a_rule;
            return false;
        }
    }

    m_rules.push_back( a_rule );
    return true;
}


void
RuleGroup::resendAsserted( IRuleListener &a_listener )
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
    {
        if ( (*ir)->isAsserted() )
        {
            a_listener.assertFinding( (*ir)->getID(), **ir );
        }
    }
}


void
RuleGroup::event( vector<IRuleListener*> &a_listeners )
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
    {
        (*ir)->event( a_listeners );
    }
}


void
RuleGroup::evaluate( bool a_defined, vector<IRuleListener*> &a_listeners )
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
    {
        (*ir)->evaluate( a_defined, a_listeners );
    }
}


void
RuleGroup::evaluate( double a_value, vector<IRuleListener*> &a_listeners )
{
    for ( vector<Rule*>::iterator ir = m_rules.begin(); ir != m_rules.end(); ++ir )
    {
        (*ir)->evaluate( a_value, a_listeners );
    }
}


}

