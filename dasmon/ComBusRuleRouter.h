#ifndef AMQPRULEROUTER_H
#define AMQPRULEROUTER_H

#include <string>
#include "ruleengine.h"


class ComBusRuleRouter : public RuleEng::IRuleListener
{
public:
    ComBusRuleRouter();

    void reportEvent( const RuleEng::Rule &a_rule );
    void assertFinding( const std::string &a_id, const RuleEng::Rule &a_rule );
    void retractFinding( const std::string &a_id );
};

#endif // AMQPRULEROUTER_H
