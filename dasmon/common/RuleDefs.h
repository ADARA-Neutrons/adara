#ifndef RULEDEFS_H
#define RULEDEFS_H

#include <string>
#include <string.h>

#include "ADARADefs.h"

namespace RuleEngine
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


struct RuleSettings
{
    RuleSettings( const std::string &a_name, const std::string &a_source, RuleType a_type, ADARA::Level a_level, double a_value = 0.0, bool a_pv = false )
        : m_name(a_name), m_source(a_source), m_type(a_type), m_level(a_level), m_value(a_value), m_pv(a_pv)
    {}

    std::string     m_name;
    std::string     m_source;
    RuleType        m_type;
    ADARA::Level    m_level;
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
