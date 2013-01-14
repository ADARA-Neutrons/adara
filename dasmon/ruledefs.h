#ifndef RULEDEFS_H
#define RULEDEFS_H

#include <string>
#include <string.h>

namespace RuleEng
{

class Rule;

enum RuleType
{
    EVENT = 0,

    RULE_SIGNAL_NONNUMERIC,
    DEFINED = RULE_SIGNAL_NONNUMERIC,
    NOT_DEFINED,

    RULE_SIGNAL_NUMERIC,
    EQ = RULE_SIGNAL_NUMERIC,
    NEQ,
    LT,
    LTE,
    GT,
    GTE
};

enum RuleCategory
{
    RC_EVENT,
    RC_SIGNAL_NONNUMERIC,
    RC_SIGNAL_NUMERIC
};

enum RuleSeverity
{
    INFO = 0,
    MINOR,
    MAJOR,
    CRITICAL
};


struct RuleSettings
{
    RuleSettings( const std::string &a_item, RuleType a_type, RuleSeverity a_severity, double a_value = 0.0, bool a_pv = false )
        : m_item(a_item), m_type(a_type), m_severity(a_severity), m_value(a_value), m_pv(a_pv)
    {}

    std::string     m_item;
    RuleType        m_type;
    RuleSeverity    m_severity;
    double          m_value;
    bool            m_pv;
};


class IRuleListener
{
public:
    virtual void reportEvent( const Rule &a_rule ) = 0;
    virtual void assertFinding( const std::string &a_id, const Rule &a_rule ) = 0;
    virtual void retractFinding( const std::string &a_id ) = 0;
};





}

#endif // RULEDEFS_H
