#include <set>
#include "RuleEngine.h"
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/tokenizer.hpp>
#include <stdexcept>

#undef assert

using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Value Class


RuleEngine::Value::Value()
    : m_type( VT_VOID ), m_int_value( 0 )
{}

RuleEngine::Value::Value( bool a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( int8_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( int16_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( int32_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( int64_t a_value )
    : m_type( VT_INT ), m_int_value( a_value )
{}

RuleEngine::Value::Value( uint8_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( uint16_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( uint32_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}

RuleEngine::Value::Value( uint64_t a_value )
    : m_type( VT_INT ), m_int_value( (int64_t)a_value )
{}


RuleEngine::Value::Value( float a_value )
    : m_type( VT_REAL ), m_real_value( (double)a_value )
{}


RuleEngine::Value::Value( double a_value )
    : m_type( VT_REAL ), m_real_value( a_value )
{}

bool
RuleEngine::Value::operator==( const Value &a_value ) const
{
    return (m_type == a_value.m_type) && (m_int_value == a_value.m_int_value);
}

bool
RuleEngine::Value::operator!=( const Value &a_value ) const
{
    return !(*this == a_value);
}


bool
RuleEngine::Value::evaluate( Operator a_op, const Value &a_value ) const
{
    switch ( m_type )
    {
    case VT_INT:
        switch( a_value.m_type )
        {
        case VT_INT:
            return evaluate<int64_t,int64_t>( a_op, m_int_value, a_value.m_int_value );
        case VT_REAL:
            return evaluate<int64_t,double>( a_op, m_int_value, a_value.m_real_value );
        default:
            break;
        }
        break;
    case VT_REAL:
        switch( a_value.m_type )
        {
        case VT_INT:
            return evaluate<double,int64_t>( a_op, m_real_value, a_value.m_int_value );
        case VT_REAL:
            return evaluate<double,double>( a_op, m_real_value, a_value.m_real_value );
        default:
            break;
        }
        break;
    default:
        break;
    }

    return false;
}


template<class A, class B>
bool
RuleEngine::Value::evaluate( Operator a_op, A a_val1, B a_val2 ) const
{
    switch ( a_op )
    {
    case OP_LT:  return a_val1 < a_val2;
    case OP_LTE: return a_val1 <= a_val2;
    case OP_EQ:  return a_val1 == a_val2;
    case OP_NEQ: return a_val1 != a_val2;
    case OP_GTE: return a_val1 >= a_val2;
    case OP_GT:  return a_val1 > a_val2;
    case OP_OR:  return (a_val1 != 0) || (a_val2 != 0);
    case OP_NOR: return !((a_val1 != 0) || (a_val2 != 0));
    case OP_AND: return (a_val1 != 0) && (a_val2 != 0);
    case OP_NAND:return !((a_val1 != 0) && (a_val2 != 0));
    case OP_XOR: return ((a_val1 != 0) ^ (a_val2 != 0));
    default:
        break;
    }

    return false;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fact Class

/**
 * @param a_id - ID of new Fact
 * @param a_asserted - Initial assertion state of Fact
 *
 * Creates a new fact object of type VOID
 */
RuleEngine::Fact::Fact( const std::string& a_id, bool a_asserted )
    : m_id(a_id), m_asserted(a_asserted), m_rule_fact(false)
{}


/**
 * @param a_id - ID of new Fact
 * @param a_asserted - Initial assertion state of Fact
 * @param a_value - Value (and type) of new Fact
 *
 * Creates a new fact object of specified type and value.
 */
RuleEngine::Fact::Fact( const std::string& a_id, bool a_asserted, const Value &a_value )
    : m_id(a_id), m_asserted(a_asserted), m_rule_fact(false), m_value(a_value)
{}


/**
 * @param a_rule - Rule to add dependency on
 *
 * Adds a forward dependency on the specified Rule to this fact.
 */
void
RuleEngine::Fact::addDep( Rule *a_rule )
{
    if ( find( m_rule_deps.begin(), m_rule_deps.end(), a_rule ) == m_rule_deps.end())
        m_rule_deps.push_back( a_rule );
}

/**
 * @param a_rule - Rule to remove dependency from
 *
 * Removes a forward dependency on the specified Rule from this fact.
 */
void
RuleEngine::Fact::removeDep( Rule *a_rule )
{
    vector<Rule*>::iterator r = find( m_rule_deps.begin(), m_rule_deps.end(), a_rule );
    if ( r != m_rule_deps.end())
        m_rule_deps.erase( r );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RuleEngine Class

// ================================ Public Methods ====================================================================

/**
 * RuleEngine construtcor.
 */
RuleEngine::RuleEngine()
    : m_batch(false)
{}


/**
 * RuleEngine destrutcor.
 */
RuleEngine::~RuleEngine()
{
    // Must delete rules first (they sever fact dependencies)
    for ( map<string,Rule*>::iterator r = m_rules.begin(); r != m_rules.end(); ++r )
        delete r->second;

    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
        delete f->second;
}


/**
 * @param a_listener - Listener to attach
 *
 * Attaches an IFactListener instance to this RuleEngine. All currently asserted facts are
 * sent to the new listener.
 */
void
RuleEngine::attach( IFactListener &a_listener )
{
    if ( find( m_listeners.begin(), m_listeners.end(), &a_listener ) == m_listeners.end() )
    {
        m_listeners.push_back( &a_listener );
        sendAsserted( a_listener );
    }
}


/**
 * @param a_listener - Listener to detach
 *
 * Detaches an IFactListener instance from this RuleEngine.
 */
void
RuleEngine::detach( IFactListener &a_listener )
{
    vector<IFactListener*>::iterator l = find( m_listeners.begin(), m_listeners.end(), &a_listener );
    if ( l != m_listeners.end() )
        m_listeners.erase( l );
}


void
RuleEngine::sendAsserted( IFactListener &a_listener )
{
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted )
        {
            switch ( f->second->m_value.m_type )
            {
            case VT_VOID:
                a_listener.onAssert( f->second->m_id );
                break;

            case VT_INT:
                a_listener.onAssert( f->second->m_id, f->second->m_value.m_int_value );
                break;

            case VT_REAL:
                a_listener.onAssert( f->second->m_id, f->second->m_value.m_real_value );
                break;
            }
        }
    }
}


/**
 * @param a_expression - String that defines the rule name and conditions (see description)
 *
 * This method defines a new rule using the following syntax:
 *
 *   RULE_ID COND_EXPR [COND_OP COND_EXP] [COND_OP COND_EXP] [...]
 *
 * where COND_EXPR is defined as either a unary or binary condition as follows:
 *
 *   LVAL OPR [RVAL]
 *
 * where LVAL must be a FACT_ID, opr can be one of [<,<=,=,!=,>=,>,&,!&,|,!|,^,DEF,UNDEF],
 * and RVAL can be a numeric value (integer or float) or a FACT_ID (note that DEF and UNDEF
 * operators are unary). If a FACT_ID of type VOID is used as an rval, then the condition
 * expression will always evaluate as false.
 *
 * For subsequent conditions, a boolean condition operator (COND_OP) must be specified (i.e.
 * one of [&,!&,|,!|,^]). It is not possible to define subgroupings within an expression
 * (parenthesis are not supported), however child-rules may be defined to construct arbitrarily
 * complex rules.
 *
 * When a rule is defined, an associated fact of the same name and type VOID is also defined.
 * This fact is asserted when the rule fires, and is retracted when the rule ceases to fire.
 * The RuleEngine class ensures that rule and fact IDs do not collide, and also ensure that
 * circular rule definitions are not created (exceptions will be thrown).
 */
void
RuleEngine::defineRule( const std::string &a_expression )
{
    boost::char_separator<char> sep(" ");
    boost::tokenizer<boost::char_separator<char> > tokens(a_expression, sep);
    boost::tokenizer<boost::char_separator<char> >::iterator tok = tokens.begin();
    Condition cond;
    Rule *rule = 0;

    if ( tok == tokens.end())
        throw std::runtime_error( "Syntax error (empty expression)." );

    try
    {
        if ( idInUse( *tok ))
            throw std::runtime_error( "Rule ID is already in use." );

        rule = new Rule();
        rule->m_id = *tok;
        rule->m_rule_fact = getFact( *tok );
        rule->m_rule_fact->m_rule_fact = true;

        // First condition is always AND
        cond.m_cond_op = OP_AND;

        while ( tok != tokens.end() )
        {
            if ( ++tok == tokens.end())
                throw std::runtime_error( "Syntax error (missing lval)." );

            cond.m_left = getFact( *tok );

            if ( ++tok == tokens.end())
                throw std::runtime_error( "Syntax error (missing operator)." );

            cond.m_op = toOperator( *tok );

            cond.m_right = 0;
            cond.m_value = Value();

            if (( cond.m_op & OP_UNARY ) == 0 )
            {
                if ( ++tok == tokens.end())
                    throw std::runtime_error( "Syntax error (missing rval)." );

                // Is tok2 an int, double, or fact?
                try
                {
                    cond.m_value = Value( boost::lexical_cast<int64_t>(*tok) );
                }
                catch ( boost::bad_lexical_cast &e )
                {
                    try
                    {
                        cond.m_value = Value( boost::lexical_cast<double>(*tok) );
                    }
                    catch ( boost::bad_lexical_cast &e )
                    {
                        cond.m_right = getFact( *tok );
                    }
                }
            }

            rule->addCondition( cond );

            if ( ++tok != tokens.end())
            {
                cond.m_cond_op = toOperator( *tok );
                if (( cond.m_cond_op & OP_BOOLEAN ) == 0 )
                    throw std::runtime_error( "Syntax error (non-boolean condition operator)." );
            }
        }

        if ( isCircular( rule ))
            throw std::runtime_error( "Circular rule definition" );

        m_rules[rule->m_id] = rule;

        evaluate( rule );
    }
    catch( std::exception &e )
    {
        if ( rule )
            delete rule;

        //cout << "Failed parsing rule [" << a_expression << "]: " << e.what() << endl;
        throw;
    }
}


/**
 * @param a_rule_id - ID of rule to undefine
 *
 * Undefines (removes) rule from rule engine and retracts associated fact.
 */
void
RuleEngine::undefineRule( const std::string &a_rule_id )
{
    map<string,Rule*>::iterator r = m_rules.find( a_rule_id );
    if ( r != m_rules.end())
    {
        retract( r->second->m_rule_fact );
        delete( r->second );
        m_rules.erase( r );
    }
}


/**
 * Undefines (removes) all currently defined rules and retracts all associated facts.
 */
void
RuleEngine::undefineAllRules()
{
    for ( map<string,Rule*>::iterator r = m_rules.begin(); r != m_rules.end(); ++r )
    {
        retract( r->second->m_rule_fact );
        delete( r->second );
    }

    m_rules.clear();
}

/**
 * @param
 *
 *
 */
void
RuleEngine::assert( const std::string &a_id )
{
    // If fact is not in fact space, then there are no dependencies no it yet
    map<string,Fact*>::iterator f = m_facts.find( a_id );
    if ( f != m_facts.end())
    {
        if ( !f->second->m_asserted && !f->second->m_rule_fact )
            assert( f->second );
    }
    else
    {
        Fact *fact = new Fact( a_id, true );
        m_facts[a_id] = fact;

        assert( fact );
    }
}


/**
 * @param
 *
 *
 */
template<class T>
void
RuleEngine::assert( const std::string &a_id, T a_value )
{
    Value value(a_value);

    // If fact is not in fact space, then there are no dependencies no it yet
    map<string,Fact*>::iterator f = m_facts.find( a_id );
    if ( f != m_facts.end())
    {
        if (( !f->second->m_asserted || f->second->m_value != value ) && !f->second->m_rule_fact )
            assert( f->second, value );
    }
    else
    {
        Fact *fact = new Fact( a_id, true, value );
        m_facts[a_id] = fact;

        assert( fact, value );
    }
}

template void RuleEngine::assert<bool>( const string &a_id, bool a_value );
template void RuleEngine::assert<char>( const string &a_id, char a_value );
template void RuleEngine::assert<int8_t>( const string &a_id, int8_t a_value );
template void RuleEngine::assert<uint8_t>( const string &a_id, uint8_t a_value );
template void RuleEngine::assert<int16_t>( const string &a_id, int16_t a_value );
template void RuleEngine::assert<uint16_t>( const string &a_id, uint16_t a_value );
template void RuleEngine::assert<int32_t>( const string &a_id, int32_t a_value );
template void RuleEngine::assert<uint32_t>( const string &a_id, uint32_t a_value );
template void RuleEngine::assert<int64_t>( const string &a_id, int64_t a_value );
template void RuleEngine::assert<uint64_t>( const string &a_id, uint64_t a_value );
template void RuleEngine::assert<float>( const string &a_id, float a_value );
template void RuleEngine::assert<double>( const string &a_id, double a_value );


/**
 * @param a_id - ID of fact to retract
 *
 * This is a public retract method that updates forward dependencies based on current mode (immediate or batch).
 */
void
RuleEngine::retract( const std::string &a_id )
{
    map<string,Fact*>::iterator f = m_facts.find( a_id );
    if ( f != m_facts.end())
    {
        if ( f->second->m_asserted && !f->second->m_rule_fact )
            retract( f->second );
    }
}

/**
 * This method retracts all user-asserted facts (rule asserted facts may remain asserted)
 */
void
RuleEngine::retractAllFacts()
{
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted && !f->second->m_rule_fact )
            retract( f->second );
    }
}

HFACT
RuleEngine::getFactHandle( const std::string &a_id )
{
    Fact *fact = getFact( a_id );
    if ( !fact->m_rule_fact )
        return (HFACT)fact;

    throw std::runtime_error("Fact associated with a rule.");
}

void
RuleEngine::assert( HFACT a_fact )
{
    if ( !((Fact*)a_fact)->m_asserted )
        assert( ((Fact*)a_fact) );
}

template<class T>
void
RuleEngine::assert( HFACT a_fact, T a_value )
{
    Value value(a_value);

    if (( !((Fact*)a_fact)->m_asserted || ((Fact*)a_fact)->m_value != value ))
        assert( ((Fact*)a_fact), value );
}

template void RuleEngine::assert<bool>( HFACT a_fact, bool a_value );
template void RuleEngine::assert<char>( HFACT a_fact, char a_value );
template void RuleEngine::assert<int8_t>( HFACT a_fact, int8_t a_value );
template void RuleEngine::assert<uint8_t>( HFACT a_fact, uint8_t a_value );
template void RuleEngine::assert<int16_t>( HFACT a_fact, int16_t a_value );
template void RuleEngine::assert<uint16_t>( HFACT a_fact, uint16_t a_value );
template void RuleEngine::assert<int32_t>( HFACT a_fact, int32_t a_value );
template void RuleEngine::assert<uint32_t>( HFACT a_fact, uint32_t a_value );
template void RuleEngine::assert<int64_t>( HFACT a_fact, int64_t a_value );
template void RuleEngine::assert<uint64_t>( HFACT a_fact, uint64_t a_value );
template void RuleEngine::assert<float>( HFACT a_fact, float a_value );
template void RuleEngine::assert<double>( HFACT a_fact, double a_value );

void
RuleEngine::retract( HFACT a_fact )
{
    if ( ((Fact*)a_fact)->m_asserted )
        retract( ((Fact*)a_fact) );
}

/**
 * This method initiates batch-mode. During batch mode, facts may be asserted or retracted, and rules
 * may fire or cease firing, but no notifications are sent to listeners. This prevents "thrash" due to
 * multiple fact assertes/retarcts from being propogated to listeners.
 */
void
RuleEngine::beginBatch()
{
    if ( !m_batch )
    {
        m_batch = true;

        for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
        {
            if ( f->second->m_asserted )
            {
                m_old_facts[f->second] = f->second->m_value;
                m_new_facts.insert(f->second);
            }
        }
    }
}

/**
 * This method ends batch mode and results in notification of any new asserts or retracts since
 * batch mode was started.
 */
void
RuleEngine::endBatch()
{
    if ( m_batch )
    {
        m_batch = false;

    #if 0
        cout << "Previously asserted:";
        for ( s = m_old_facts.begin(); s != m_old_facts.end(); ++s )
            cout << " " << (*s)->m_id;
        cout << endl;

        cout << "New asserted:";
        for ( s = m_new_facts.begin(); s != m_new_facts.end(); ++s )
            cout << " " << (*s)->m_id;
        cout << endl;
    #endif

        map<Fact*,Value>::iterator iold = m_old_facts.begin();
        for ( ; iold != m_old_facts.end(); ++iold )
        {
            if ( !m_new_facts.count( iold->first ))
                notify_retract( iold->first );
        }

        for ( set<Fact*>::iterator inew = m_new_facts.begin(); inew != m_new_facts.end(); ++inew )
        {
            iold = m_old_facts.find( *inew );
            if ( iold != m_old_facts.end() )
            {
                if ( iold->second != (*inew)->m_value )
                    notify_assert( *inew );
            }
        }

        m_new_facts.clear();
        m_old_facts.clear();
    }
}


// ================================ Private Methods ===================================================================

/**
 * @param a_id - ID to test
 *
 * Tests to determine if the specified ID is in use as a rule or fact ID.
 */
bool
RuleEngine::idInUse( const std::string &a_id ) const
{
    if ( m_rules.find( a_id ) != m_rules.end() || m_facts.find( a_id ) != m_facts.end() )
        return true;
    else
        return false;
}


/**
 * @param a_rule - Rule to test
 *
 * Tests if the specified Rule is part of a circular definition.
 */
bool
RuleEngine::isCircular( Rule *a_rule )
{
    return isCircular( a_rule, getFact( a_rule->m_id ));
}


/**
 * @param a_target_rule - Rule to test for
 * @param a_rule - Current rule to evaluate
 *
 * Recursive function that traverses the rule graph seeking circular definitions.
 */
bool
RuleEngine::isCircular( Rule *a_target_rule, Rule *a_rule )
{
    // If we land on the target rule, then there is a circular definition
    if ( a_target_rule == a_rule )
        return true;

    // On a rule node, only follow forward fact dependencies (which is the rule-fact only)
    return isCircular( a_target_rule, getFact( a_rule->m_id ));
}

/**
 * @param a_target_rule - Rule to test for
 * @param a_fact - Current fact to evaluate
 *
 * Recursive function that traverses the rule graph seeking circular definitions.
 */
bool
RuleEngine::isCircular( Rule *a_target_rule, Fact *a_fact )
{
    // On Fact node, follow all rule dependencies
    for ( vector<Rule*>::iterator r = a_fact->m_rule_deps.begin(); r != a_fact->m_rule_deps.end(); ++r )
        if ( isCircular( a_target_rule, *r ))
            return true;

    return false;
}


/**
 * @param a_fact - Fact to be asserted
 *
 * This is an internal fact assert method. Dependencies are traced and forward rules re-evaluated.
 * Note that this method des not check if the specified fact is already asserted (this is an optimization).
 */
void
RuleEngine::assert( Fact *a_fact )
{
    a_fact->m_asserted = true;
    a_fact->m_value.m_type = VT_VOID;

    notify_assert( a_fact );
    update( a_fact );
}


/**
 * @param a_fact - Fact to be asserted
 * @param a_value - Value to be asserted
 *
 * This is an internal fact assert method. Dependencies are traced and forward rules re-evaluated.
 * Note that this method des not check if the specified fact is already asserted (this is an optimization).
 */
void
RuleEngine::assert( Fact *a_fact, Value &a_value )
{
    a_fact->m_asserted = true;
    a_fact->m_value = a_value;

    notify_assert( a_fact );
    update( a_fact );
}


/**
 * @param a_fact - Fact to be retracted
 *
 * This is an internal fact retract method. Dependencies are traced and forward rules re-evaluated.
 * Note that this method des not check if the specified fact is already retracted (this is an optimization).
 */
void
RuleEngine::retract( Fact *a_fact )
{
    a_fact->m_asserted = false;

    notify_retract( a_fact );
    update( a_fact );
}


/**
 * @param a_id - ID of Fact to retieve
 *
 * This method finds, or creates if not found, the specified fact.
 */
RuleEngine::Fact*
RuleEngine::getFact( const std::string &a_id )
{
    Fact *fact;

    map<string,Fact*>::iterator f = m_facts.find( a_id );
    if ( f != m_facts.end())
    {
        fact = f->second;
    }
    else
    {
        fact = new Fact( a_id, false );
        m_facts[a_id] = fact;
    }

    return fact;
}


/**
 * @param a_fact - The ancestor fact that has been updated
 *
 * This is a dependency-driven rule-update method.
 */
void
RuleEngine::update( Fact *a_fact )
{
    for ( vector<Rule*>::iterator r = a_fact->m_rule_deps.begin(); r != a_fact->m_rule_deps.end(); ++r )
        evaluate( *r );
}


/**
 * @param a_rule - The rule that has been updated/changed
 *
 * This is a dependency-driven rule-update method.
 */
void
RuleEngine::evaluate( Rule *a_rule )
{
    bool asserted = true;

    for ( vector<Condition>::iterator c = a_rule->m_conditions.begin(); c != a_rule->m_conditions.end(); ++c )
        evaluateCondition( asserted, *c );

    if ( asserted && !a_rule->m_rule_fact->m_asserted )
        assert( a_rule->m_rule_fact );
    else if ( !asserted && a_rule->m_rule_fact->m_asserted )
        retract( a_rule->m_rule_fact );
}


/**
 * @param a_asserted - Boolean value to accumulate result
 * @param a_condition - Condition to evaluate
 *
 * This method evalutes (computes) the specified condition from the associated LVAL, operators,
 * and optional RVAL. The result is accumulated in the passed-in a_asserted parameter.
 */
void
RuleEngine::evaluateCondition( bool &a_asserted, const Condition &a_condition )
{
    bool cond = false;

    switch ( a_condition.m_op )
    {
    case OP_ASSERTED:
        cond = a_condition.m_left->m_asserted;
        break;
    case OP_RETRACTED:
        cond = !a_condition.m_left->m_asserted;
        break;
    default:
        if ( a_condition.m_left->m_asserted )
        {
            if ( a_condition.m_right && a_condition.m_right->m_asserted )
                cond = a_condition.m_left->m_value.evaluate( a_condition.m_op, a_condition.m_right->m_value );
            else
                cond = a_condition.m_left->m_value.evaluate( a_condition.m_op, a_condition.m_value );
        }
        break;
    }

    switch ( a_condition.m_cond_op )
    {
    case OP_AND:
        a_asserted &= cond;
        break;
    case OP_OR:
        a_asserted |= cond;
        break;
    case OP_NAND:
        a_asserted = !(a_asserted & cond);
        break;
    case OP_NOR:
        a_asserted = !(a_asserted | cond);
        break;
    case OP_XOR:
        a_asserted ^= cond;
        break;
    default:
        break;
    }
}


/**
 * @param a_opr - String representation of an operator
 *
 * This method converts a string representation of an operator to the associated enum value. If no
 * conversion is possible, an exception is thrown.
 */
RuleEngine::Operator
RuleEngine::toOperator( const std::string &a_opr )
{
    if ( boost::iequals( a_opr, "DEF" ))
        return OP_ASSERTED;
    else if ( boost::iequals( a_opr, "UNDEF" ))
        return OP_RETRACTED;
    else if ( a_opr == "<" )
        return OP_LT;
    else if ( a_opr == "<=" )
        return OP_LTE;
    else if ( a_opr == "=" )
        return OP_EQ;
    else if ( a_opr == "!=" )
        return OP_NEQ;
    else if ( a_opr == ">=" )
        return OP_GTE;
    else if ( a_opr == ">" )
        return OP_GT;
    else if ( a_opr == "|" )
        return OP_OR;
    else if ( a_opr == "!|" )
        return OP_NOR;
    else if ( a_opr == "&" )
        return OP_AND;
    else if ( a_opr == "!&" )
        return OP_NAND;
    else if ( a_opr == "^" )
        return OP_XOR;

    throw std::runtime_error( string( "Bad operator: " ) + a_opr );
}


/**
 * @param a_fact - Asserted Fact to notify listeners about
 *
 * This method notifies listeners of a newly asserted fact.
 */
void
RuleEngine::notify_assert( Fact *a_fact )
{
    if ( m_batch )
    {
        m_new_facts.insert( a_fact );
    }
    else
    {
        vector<IFactListener*>::iterator l = m_listeners.begin();

        switch ( a_fact->m_value.m_type )
        {
        case VT_VOID:
            for ( ; l != m_listeners.end(); ++l )
                (*l)->onAssert( a_fact->m_id );
            break;

        case VT_INT:
            for ( ; l != m_listeners.end(); ++l )
                (*l)->onAssert( a_fact->m_id, a_fact->m_value.m_int_value );
            break;

        case VT_REAL:
            for ( ; l != m_listeners.end(); ++l )
                (*l)->onAssert( a_fact->m_id, a_fact->m_value.m_real_value );
            break;
        }
    }
}


/**
 * @param a_fact - Retracted Fact to notify listeners about
 *
 * This method notifies listeners of a newly retracted fact.
 */
void
RuleEngine::notify_retract( Fact *a_fact )
{
    if ( m_batch )
    {
        m_new_facts.erase( a_fact );
    }
    else
    {
        for ( vector<IFactListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
        {
            (*l)->onRetract( a_fact->m_id );
        }
    }
}

