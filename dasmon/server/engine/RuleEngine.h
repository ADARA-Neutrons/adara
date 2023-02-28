#ifndef RULEENGINE_H
#define RULEENGINE_H

#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <stdint.h>
#include "muParser.h"

#undef assert


/** \class RuleEngine
  *
  * The RuleEngine class implements a lightweight, single-threaded "business rule engine" or
  * "expert system". Facts representing knowledge, or observations, are asserted and retracted
  * to/from the Rule base as a funcion of some external process. Facts can have no type, or be
  * numeric (integer or real). Rules are defined as expressions of numeric and/or boolean
  * operations acting on Facts (with or without values). As Rules are "fired" as a result of
  * asseted or retracted Facts, clients are notified via a listener interface. The underlying
  * rule engine is dependency driven, thus response times are not a function of the size of
  * the knowledge base or rule base; rather, only the complexity of the rules that are dependent
  * on an asserted or retracted Fact. In addition, optimized interfaces are available to provide
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
    typedef void *  HFACT;

    struct RuleInfo
    {
        bool            enabled;
        std::string     fact;
        std::string     expr;
        std::string     desc;
    };

    class IFactListener
    {
    public:
        virtual void onAssert( const std::string &a_fact ) = 0;
        virtual void onRetract( const std::string &a_fact ) = 0;
    };

    RuleEngine();
    ~RuleEngine();

    void        synchronize( const RuleEngine &a_source );
    void        attach( IFactListener &a_listener );
    void        detach( IFactListener &a_listener );
    void        sendAsserted( IFactListener &a_listener );
    void        defineRule( const std::string &a_id, const std::string &a_expression, const std::string a_description );
    void        undefineRule( const std::string &a_rule_id );
    void        undefineAllRules();
    void        getDefinedRules( std::vector<RuleInfo> &a_rules ) const;
    void        assert( const std::string &a_id );
    template<class T>
    void        assert( const std::string &a_id, T a_value );
    void        assert( HFACT a_fact );
    template<class T>
    void        assert( HFACT a_fact, T a_value );
    void        retract( const std::string &a_id );
    void        retract( HFACT a_fact );
    void        retractPrefix( const std::string &a_id );
    void        retractAllFacts();
    void        getAsserted( std::vector<std::string> &a_asserted_facts );
    HFACT       getFactHandle( const std::string &a_id );
    std::string getNameHFACT( HFACT a_fact ) const;
    void        beginBatch();
    void        endBatch();

private:
    class Rule;

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
        Value( const Value &a_src );
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

        bool operator!=( const Value &a_value ) const;
        operator double()
        {
            if ( m_type == VT_REAL )
                return m_real_value;
            else
                return m_int_value; // Void types will return 1 since int val is inited thus
        }

        ValueType           m_type;
        union
        {
            int64_t         m_int_value;
            double          m_real_value;
        };
    };

    /** \class Fact
      * \brief Class that defines a fact in the rule engine fact space
      *
      * The Fact class defines an atomic and unique fact within the fact space of the
      * rule engine. Fact instances can have an associated value and can be asserted
      * or not. A fact value is only meaningful if/when a fact is asserted. If the
      * fact is an input into one or more rules, then the fact maintains a list of
      * rule dependencies such that rules can be efficiently evaluated when a Fact
      * is changed (asserted, retracted, value changed). A Fact that is associated
      * with th ouptut of a rule is a special case (m_rule_fact == true), and these
      * facts can not be altered via the rule engine public API.
      */
    class Fact
    {
    public:
                Fact( const std::string& a_id, bool a_asserted, bool a_implicit );
                Fact( const std::string& a_id, bool a_asserted, const Value &a_value );
        void    addRuleDependency( Rule *a_rule );
        void    removeRuleDependency( Rule *a_rule );

        std::string         m_id;           ///< Unique ID of fact
        bool                m_asserted;     ///< Assertion state of fact
        bool                m_implicit;     ///< Fact has been implicitly defined (rule input)
        bool                m_rule_output;  ///< Flag indicating fact is the output of a rule
        Value               m_value;        ///< Value of fact
        std::vector<Rule*>  m_rule_deps;    ///< Rules that use this fact as an input
    };


    class Rule
    {
    public:
                                    Rule( RuleEngine &a_engine, const std::string a_id, const std::string &a_expr, const std::string &a_desc );
                                   ~Rule();
        void                        evaluate( Fact *a_updated_fact = 0 );
        inline const std::string&   getID() { return m_id; }
        inline const std::string&   getExpr() { return m_expr; }
        inline Fact*                getFact() { return m_rule_fact; }
        inline const std::string&   getDesc() { return m_desc; }

    private:
        static double  *parserVarFactory( const char *a_var_name, void *a_data );
        bool            isCircular( Rule *a_rule );
        bool            isCircular( Rule *a_target_rule, Rule *a_rule );
        bool            isCircular( Rule *a_target_rule, Fact *a_fact );

        /*
        struct FactInfo
        {
            FactInfo()
            : m_value(0.0), m_assert_type(false)
            {}

            FactInfo( double a_value, bool a_assert_type )
            : m_value(a_value), m_assert_type(a_assert_type)
            {}

            double  m_value;        ///< Variable (addr) given to muParser
            bool    m_assert_type;  ///< Flag indicates a "assertion" input to expression
        };*/

        RuleEngine                 &m_engine;       ///< Owning RuleEngine instance
        std::string                 m_id;           ///< Unique ID of rule (and ID of output fact)
        std::string                 m_expr;         ///< Original rule expression
        std::string                 m_desc;         ///< Rule description
        Fact                       *m_rule_fact;    ///< Output fact
        double                      m_rule_value;   ///< Output value of rule
        bool                        m_valid;        ///< Flag indicates if all value inputs are valid (asserted)
        std::map<Fact*,double>      m_inputs;       ///< Maps input facts to muParser value variables
        std::map<Fact*,double>      m_asserts;      ///< Maps input facts to muParser value variables
        mu::Parser                  m_parser;       ///< Expression parser and evaluator
        double                      m_tmp_values[10];
        unsigned short              m_tmp_count;
    };

    bool        idInUse( const std::string &a_id ) const;
    void        assert( Fact *a_fact );
    void        assert( Fact *a_fact, Value &a_value );
    void        retract( Fact *a_fact );
    void        updateAll();
    void        update( Fact *a_fact );
    void        notify_assert( Fact *a_fact );
    void        notify_retract( Fact *a_fact );
    Fact*       getFact( const std::string &a_id );

    bool                            m_batch;        ///< Flag indicates batch mode is active
    std::vector<IFactListener*>     m_listeners;    ///< Objects to receive fact assertion/retraction notifications
    std::map<std::string,Fact*>     m_facts;        ///< Fact Space (Fact ID to Fact instance)
    std::map<std::string,Rule*>     m_rules;        ///< Rule Space (Rule ID to Rule instance)
    std::map<Fact*,Value>           m_old_facts;    ///< Supports batch mode
    std::set<Fact*>                 m_new_facts;    ///< Supports batch mode
};

#endif // RULEENGINE_H
