#ifndef RULEENGINE_H
#define RULEENGINE_H

#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <stdint.h>

#undef assert

typedef void *  HFACT;

class IFactListener
{
public:
    virtual void onAssert( const std::string &a_fact ) = 0;
    virtual void onAssertInteger( const std::string &a_fact, int64_t a_value )
    { (void)a_value; onAssert( a_fact ); }
    virtual void onAssertDouble( const std::string &a_fact, double a_value )
    { (void)a_value; onAssert( a_fact ); }
    virtual void onRetract( const std::string &a_fact ) = 0;
};

/**
 * @class RuleEngine
 *
 * The RuleEngine class implements a lightweight, single-threaded "business rule engine" or
 * "expert system". Facts representing knowledge, or observations, are asserted and retracted
 * to/from the Rule base as a funcion of some external process. Facts can have no type, or be
 * numeric (integer or real). Rules are defined as expressions of numeric and/or boolean
 * operations acting on Facts (with or without values). As Rules are "fired" as a result of
 * asseted or retracted Facts, clients are notified via a listener interface. The underlying
 * rule engine is dependency driven, thus response times are not a function of the size of
 * the knowledge base or rule base; rather, only the complexity of the rules that are dependent
 * on an asserted or retracted Fact. In addition, optimized interfaced are provided to provide
 * the best-possible performance of the rule-engine for time-critical applications.
 *
 * Note that when assert() or retract() are called, onAssert/onRetract callbacks will be
 * triggered and the callback will use the callers thread. The thread calling assert() or
 * retract() will be blocked until all callbacks are processed. (It may be desirable to de-
 * couple the callbacks in some manner to reduce the amount of time the assert thread is
 * bloacked.)
 */
class RuleEngine
{
public:
    RuleEngine();
    ~RuleEngine();

    void    attach( IFactListener &a_listener );
    void    detach( IFactListener &a_listener );
    void    sendAsserted( IFactListener &a_listener );
    void    defineRule( const std::string &a_expression );
    void    undefineRule( const std::string &a_rule_id );
    void    undefineAllRules();
    void    assert( const std::string &a_id );
    template<class T>
    void    assert( const std::string &a_id, T a_value );
    void    retract( const std::string &a_id );
    void    retractAllFacts();
    HFACT   getFactHandle( const std::string &a_id );
    void    assert( HFACT a_fact );
    template<class T>
    void    assert( HFACT a_fact, T a_value );
    void    retract( HFACT a_fact );
    void    beginBatch();
    void    endBatch();

private:
    class Rule;

    #define OP_UNARY    0x0100
    #define OP_BINARY   0x0200
    #define OP_BOOLEAN  0x1000

    enum Operator
    {
        OP_ASSERTED = OP_UNARY,
        OP_RETRACTED,
        OP_LT       = OP_BINARY,
        OP_LTE,
        OP_EQ,
        OP_NEQ,
        OP_GTE,
        OP_GT,
        OP_OR       = OP_BOOLEAN | OP_BINARY,
        OP_NOR,
        OP_AND,
        OP_NAND,
        OP_XOR
    };

    enum ValueType
    {
        VT_VOID,
        VT_INT,
        VT_REAL
    };

    class Value
    {
    public:
        Value();
        Value( bool a_value );
        Value( uint8_t a_value );
        Value( int8_t a_value );
        Value( uint16_t a_value );
        Value( int16_t a_value );
        Value( uint32_t a_value );
        Value( int32_t a_value );
        Value( uint64_t a_value );
        Value( int64_t a_value );
        Value( float a_value );
        Value( double a_value );

        bool operator==( const Value &a_value ) const;
        bool operator!=( const Value &a_value ) const;
        bool evaluate( Operator a_op, const Value &a_value ) const;
        template<class A, class B>
        bool evaluate( Operator a_op, A a_val1, B a_val2 ) const;

        ValueType           m_type;
        union
        {
            int64_t         m_int_value;
            double          m_real_value;
        };
    };


    class Fact
    {
    public:
        Fact( const std::string& a_id, bool asserted );
        Fact( const std::string& a_id, bool asserted, const Value &a_value );

        void addDep( Rule *a_rule );
        void removeDep( Rule *a_rule );

        std::string         m_id;
        bool                m_asserted;
        bool                m_rule_fact;
        Value               m_value;
        std::vector<Rule*>  m_rule_deps;
    };

    class Condition
    {
    public:
        Condition()
            : m_cond_op(OP_OR), m_left(0), m_op(OP_AND), m_right(0),m_value(0) {}
        Condition( Operator a_cond_op, Fact* a_left, Operator a_op, Fact *a_right )
            : m_cond_op(a_cond_op), m_left(a_left), m_op(a_op), m_right(a_right),m_value(0) {}
        Condition( Operator a_cond_op, Fact* a_left, Operator a_op, const Value &a_value )
            : m_cond_op(a_cond_op), m_left(a_left), m_op(a_op), m_right(0),m_value(a_value) {}

        Operator    m_cond_op;
        Fact*       m_left;
        Operator    m_op;
        Fact*       m_right;
        Value       m_value;
    };

    class Rule
    {
    public:
        Rule() : m_rule_fact(0) {}
        ~Rule()
        {
            for ( std::vector<Fact*>::iterator f = m_fact_deps.begin(); f != m_fact_deps.end(); ++f )
                (*f)->removeDep( this );
        }

        void addCondition( const Condition &a_cond )
        {
            m_conditions.push_back( a_cond );
            addDep( a_cond.m_left );
            if ( a_cond.m_right )
                addDep( a_cond.m_right );
        }

        void addDep( Fact *a_fact )
        {
            if ( std::find( m_fact_deps.begin(), m_fact_deps.end(), a_fact ) == m_fact_deps.end() )
            {
                m_fact_deps.push_back( a_fact );
                a_fact->addDep( this );
            }
        }

        std::string             m_id;
        Fact                   *m_rule_fact;
        std::vector<Condition>  m_conditions;
        std::vector<Fact*>      m_fact_deps;
    };

    bool        idInUse( const std::string &a_id ) const;
    bool        isCircular( Rule *a_rule );
    bool        isCircular( Rule *a_target_rule, Rule *a_rule );
    bool        isCircular( Rule *a_target_rule, Fact *a_fact );
    void        assert( Fact *a_fact );
    void        assert( Fact *a_fact, Value &a_value );
    void        retract( Fact *a_fact );
    void        updateAll();
    void        update( Fact *a_fact );
    void        evaluate( Rule *a_rule );
    void        notify_assert( Fact *a_fact );
    void        notify_retract( Fact *a_fact );
    Fact*       getFact( const std::string &a_id );
    void        evaluateCondition( bool &a_asserted, const Condition &condition );
    Operator    toOperator( const std::string &a_opr );

    bool                            m_batch;
    std::vector<IFactListener*>     m_listeners;
    std::map<std::string,Fact*>     m_facts;
    std::map<std::string,Rule*>     m_rules;
    std::map<Fact*,Value>           m_old_facts;
    std::set<Fact*>                 m_new_facts;

    friend class Rule;
};

#endif // RULEENGINE_H
