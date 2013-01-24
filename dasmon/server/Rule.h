#ifndef RULE_H
#define RULE_H

#include "RuleDefs.h"
#include <string>
#include <vector>

namespace RuleEngine
{

class Rule
{
public:
    Rule( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value = 0.0 );
    ~Rule() {}

    inline bool operator==( const Rule &a_rule ) const
    {
        if ( a_rule.m_name == m_name && a_rule.m_level == m_level && a_rule.m_id == m_id )
            return true;
        else
            return false;
    }

    inline const std::string   &getName() const { return m_name; }
    inline const std::string   &getSource() const { return m_source; }
    inline const std::string   &getID() const { return m_id; }
    inline RuleType             getType() const { return m_type; }
    inline ADARA::Level         getLevel() const { return m_level; }
    inline const std::string   &getMessage() const { return m_message; }
    inline bool                 isAsserted() const { return m_asserted; }

    void            event( std::vector<IRuleListener*> &a_listeners );
    void            evaluate( bool a_defined, std::vector<IRuleListener*> &a_listeners );
    void            evaluate( double a_value, std::vector<IRuleListener*> &a_listeners );

private:

    std::string     m_name;
    std::string     m_source;
    std::string     m_id;
    RuleType        m_type;
    ADARA::Level    m_level;
    double          m_value;
    std::string     m_message;
    bool            m_asserted;
};


class RuleGroup
{
public:
    RuleGroup();
    ~RuleGroup();

    bool        defineRule( Rule* a_rule );
    void        resendAsserted( IRuleListener &a_listener );
    void        event( std::vector<IRuleListener*> &a_listeners );
    void        evaluate( bool a_defined, std::vector<IRuleListener*> &a_listeners );
    void        evaluate( double a_value, std::vector<IRuleListener*> &a_listeners );

    std::vector<Rule*>  m_rules;
};

}

#endif // RULE_H
