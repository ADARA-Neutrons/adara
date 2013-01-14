#ifndef RULE_H
#define RULE_H

#include "ruledefs.h"
#include <string>
#include <vector>

namespace RuleEng
{

class Rule
{
public:
    Rule( const std::string &a_item, const std::string &a_msg_prefix, RuleType a_type, RuleSeverity a_severity, double a_value = 0.0 );
    ~Rule() {}

    inline bool operator==( const Rule &a_rule ) const
    {
        if ( a_rule.m_type == m_type && a_rule.m_severity == m_severity && a_rule.m_id == m_id )
            return true;
        else
            return false;
    }

    inline const std::string   &getItem() const { return m_item; }
    inline const std::string   &getID() const { return m_id; }
    inline RuleType             getType() const { return m_type; }
    inline RuleSeverity         getSeverity() const { return m_severity; }
    inline const std::string   &getMessage() const { return m_message; }
    inline bool                 isAsserted() const { return m_asserted; }

    void            event( std::vector<IRuleListener*> &a_listeners );
    void            evaluate( bool a_defined, std::vector<IRuleListener*> &a_listeners );
    void            evaluate( double a_value, std::vector<IRuleListener*> &a_listeners );

private:

    std::string     m_item;
    std::string     m_id;
    RuleType        m_type;
    RuleSeverity    m_severity;
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
