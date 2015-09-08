#include <set>
#include "RuleEngine.h"
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/tokenizer.hpp>
#include <stdexcept>

#undef assert

using namespace std;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ================================ Value Class =======================================================================


// Value constructors for support native types
RuleEngine::Value::Value() : m_type( VT_VOID ), m_int_value( 1 ) {}
RuleEngine::Value::Value( const RuleEngine::Value &a_src ) : m_type( a_src.m_type ), m_int_value( a_src.m_int_value ) {}
RuleEngine::Value::Value( bool a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( int8_t a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( int16_t a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( int32_t a_value ) : m_type( VT_INT ), m_int_value( a_value ) {}
RuleEngine::Value::Value( int64_t a_value ) : m_type( VT_INT ), m_int_value( a_value ) {}
RuleEngine::Value::Value( uint8_t a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( uint16_t a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( uint32_t a_value ) : m_type( VT_INT ), m_int_value( (int32_t)a_value ) {}
RuleEngine::Value::Value( uint64_t a_value ) : m_type( VT_INT ), m_int_value( (int64_t)a_value ) {}
RuleEngine::Value::Value( float a_value ) : m_type( VT_REAL ), m_real_value( (double)a_value ) {}
RuleEngine::Value::Value( double a_value ) : m_type( VT_REAL ), m_real_value( a_value ) {}


/** \brief Determines if a Value differs in content
  * \param a_value - Value to compare to
  * \return True if content is different; false otherwise
  *
  * This method determines if the provided Value instance has different content
  * from the called instance. Differences are checked between type and value.
  * No epsilon value is used for real values.
  */
bool
RuleEngine::Value::operator!=( const Value &a_value ) const
{
    if ( m_type != a_value.m_type )
        return true;

    if ( m_type == VT_INT && m_int_value != a_value.m_int_value)
        return true;

    if ( m_type == VT_REAL && m_real_value != a_value.m_real_value)
        return true;

    return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ================================ Fact Class ========================================================================

/**
 * @param a_id - ID of new Fact
 * @param a_asserted - Initial assertion state of Fact
 *
 * Creates a new fact object of type VOID
 */
RuleEngine::Fact::Fact( const std::string& a_id, bool a_asserted, bool a_implicit )
    : m_id(a_id), m_asserted(a_asserted), m_implicit(a_implicit), m_rule_output(false)
{}


/**
 * @param a_id - ID of new Fact
 * @param a_asserted - Initial assertion state of Fact
 * @param a_value - Value (and type) of new Fact
 *
 * Creates a new fact object of specified type and value.
 */
RuleEngine::Fact::Fact( const std::string& a_id, bool a_asserted, const Value &a_value )
    : m_id(a_id), m_asserted(a_asserted), m_implicit(false), m_rule_output(false), m_value(a_value)
{}


/**
 * @param a_rule - Rule to add dependency on
 *
 * Adds a forward dependency on the specified Rule to this fact.
 */
void
RuleEngine::Fact::addRuleDependency( Rule *a_rule )
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
RuleEngine::Fact::removeRuleDependency( Rule *a_rule )
{
    vector<Rule*>::iterator r = find( m_rule_deps.begin(), m_rule_deps.end(), a_rule );
    if ( r != m_rule_deps.end())
        m_rule_deps.erase( r );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ================================ Rule Class ========================================================================


/** \param a_engine - Owngin RuleEngine instance
  * \param a_id - Rule ID
  * \param a_expr - Rule expression
  *
  * Constructs a new Rule. Throws if expression has errors.
  */
RuleEngine::Rule::Rule( RuleEngine &a_engine, const std::string a_id, const std::string &a_expr, const std::string &a_desc )
: m_engine(a_engine), m_id(a_id), m_expr(a_expr), m_desc(a_desc), m_rule_fact(0), m_rule_value(0.0), m_valid(false), m_tmp_count(0)
{
    string id = boost::to_upper_copy( a_id );
    boost::algorithm::trim( id );

    if ( m_engine.idInUse( id ))
        throw std::runtime_error( "Rule ID is already in use." );

    m_rule_fact = m_engine.getFact( id );
    m_rule_fact->m_rule_output = true;
    m_rule_fact->m_implicit = false;

    m_parser.DefineNameChars("0123456789_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ:@");
    m_parser.SetVarFactory( parserVarFactory, this );
    m_parser.SetExpr( a_expr );
    m_parser.Eval(); // This call is to force compilation of the new expression and to throw any errors now

    if ( isCircular( this ))
        throw std::runtime_error( "Circular rule definition" );
}


/**
  * Rule destructor.
  */
RuleEngine::Rule::~Rule()
{
    for ( std::map<Fact*,double>::iterator f = m_inputs.begin(); f != m_inputs.end(); ++f )
        f->first->removeRuleDependency( this );
    for ( std::map<Fact*,double>::iterator f = m_asserts.begin(); f != m_asserts.end(); ++f )
        f->first->removeRuleDependency( this );
}

/** \param a_var_name - New variable found by expression parser
  * \parama_data - Rule instance associated with parser
  *
  * This static method is a callback used by muParser to implicitly define variables found
  * in a parsed epression. These variables are automatically mapped to Facts in the rule
  * engine fact space (or new Facts are created and mapped). There are three special cases
  * for variables:
  *
  * 1) A variable prefixed with "@" is mapped to the output value of the rule,
  * 2) Any variable with an underscore prefix is treated as an "assertion" test for a
  *    Fact. Fact "assertion" inputs are assigned a value of 1 if the associated fact is
  *    asserted, and a value of 0 if it is not asserted.
  * 3) Variables names of 1 or 2 characters in length are treated as temporaries and are
  *    not mapped to facts.. A maximum of 10 temporaries per rule are allowed.
  */
double *
RuleEngine::Rule::parserVarFactory( const char *a_var_name, void *a_data )
{
    double *result;
    RuleEngine::Rule *m_inst = (RuleEngine::Rule *)a_data;

    // Note: variable names may vary in case, but facts are always upper case
    // so, force vars to upper case here (can't do this to entire expression
    // because muParser functions are case sensitive)

    // Check for Rule output variable "@"
    if ( a_var_name[0] == '@' )
        return &m_inst->m_rule_value;

    Fact *fact;

    if ( a_var_name[0] == '_' )
    {
        // Variable is an assertion test
        string var = &a_var_name[1];
        boost::to_upper( var );
        fact = m_inst->m_engine.getFact( var );
        double &value = m_inst->m_asserts[fact];
        value = 0;
        result = &value;
    }
    else if ( strlen( a_var_name ) > 2 )
    {
        // Variable is a normal fact input
        string var = a_var_name;
        boost::to_upper( var );
        fact = m_inst->m_engine.getFact( var );
        double &value = m_inst->m_inputs[fact];
        value = 0;
        result = &value;
    }
    else
    {
        if ( m_inst->m_tmp_count < 10 )
            return &m_inst->m_tmp_values[m_inst->m_tmp_count++];
        else
            throw std::runtime_error("Too many temporary variables (10 max)");
    }

    // Add this rule as a dependency on fact
    fact->addRuleDependency( m_inst );

    return result;
}


/** \param a_updated_fact - Pointer to a fact that was changed (optional)
  *
  * This method is called whenever any input fact associated with a rule is changed
  * (asserted, new value, or retracted). If not null, the "a_updated_fact" parameter
  * identifies a input fact that has changed state or value and resulted in the
  * rule being reevaluated. Note that the expression is only evaluated if all value
  * inputs are valid (asserted).
  */
void
RuleEngine::Rule::evaluate( Fact *a_updated_fact )
{
    //cout << "eval " << m_id << "{" << m_expr << "}: ";

    /*if ( a_updated_fact )
    {
        cout << "update fact {" << a_updated_fact->m_id << "} ";

        if ( a_updated_fact->m_asserted )
            cout << "asserted ";
        else
            cout << "retracted ";
    }*/

    if ( a_updated_fact )
    {
        map<Fact*,double>::iterator f = m_asserts.find( a_updated_fact );
        if ( f != m_asserts.end())
        {
            if ( a_updated_fact->m_asserted )
                f->second = 1;
            else
                f->second = 0;
        }

        f = m_inputs.find( a_updated_fact );
        if ( f != m_inputs.end())
        {
            if ( a_updated_fact->m_asserted )
                f->second = (double)f->first->m_value;
            else
                m_valid = false;
        }

        if ( a_updated_fact->m_asserted && !m_valid )
        {
            // See if rule has become valid
            m_valid = true;
            for ( f = m_inputs.begin(); f != m_inputs.end(); ++f )
            {
                if ( !f->first->m_asserted )
                {
                    m_valid = false;
                    break;
                }
            }
        }
    }
    else
    {
        // If evaluate is called without a fact change (absolute vs relative eval), then
        // must explicitly (re)determine validity state. This happens at least once when
        // a new rule is defined.

        m_valid = true;

        for ( map<Fact*,double>::iterator f = m_asserts.begin(); f != m_asserts.end(); ++f )
        {
            if ( f->first->m_asserted )
                f->second = 1;
            else
                f->second = 0;
        }

        for ( map<Fact*,double>::iterator f = m_inputs.begin(); f != m_inputs.end(); ++f )
        {
            if ( f->first->m_asserted )
                f->second = (double)f->first->m_value;
            else
                m_valid = false;
        }
    }

    /*if ( !m_valid )
        cout << " expr is NOT valid." << endl;
    else
        cout << " expr is valid - ";*/

    if ( m_valid )
    {
        // All required inputs are asserted, OK to evaluate expression

        if ( m_parser.Eval() > 0.5 )
        {
            //cout << " TRIG" << endl;

            // Expression evaluated as "fired" (output is 1)
            if ( !m_rule_fact->m_asserted || m_rule_fact->m_value.m_real_value != m_rule_value )
                m_engine.assert( m_rule_fact, m_rule_value );
        }
        else
        {
            //cout << " DEAD" << endl;

            // Expression evaluated as "not fired" (output is 0)
            if ( m_rule_fact->m_asserted )
                m_engine.retract( m_rule_fact );
        }
    }
    else if ( m_rule_fact->m_asserted )
        m_engine.retract( m_rule_fact );
}

/**
 * @param a_rule - Rule to test
 *
 * Tests if the specified Rule is part of a circular definition.
 */
bool
RuleEngine::Rule::isCircular( Rule *a_rule )
{
    return isCircular( a_rule, m_engine.getFact( a_rule->m_id ));
}


/**
 * @param a_target_rule - Rule to test for
 * @param a_rule - Current rule to evaluate
 *
 * Recursive function that traverses the rule graph seeking circular definitions.
 */
bool
RuleEngine::Rule::isCircular( Rule *a_target_rule, Rule *a_rule )
{
    // If we land on the target rule, then there is a circular definition
    if ( a_target_rule == a_rule )
        return true;

    // On a rule node, only follow forward fact dependencies (which is the rule-fact only)
    return isCircular( a_target_rule, m_engine.getFact( a_rule->m_id ));
}

/**
 * @param a_target_rule - Rule to test for
 * @param a_fact - Current fact to evaluate
 *
 * Recursive function that traverses the rule graph seeking circular definitions.
 */
bool
RuleEngine::Rule::isCircular( Rule *a_target_rule, Fact *a_fact )
{

    // On Fact node, follow all rule dependencies
    for ( vector<Rule*>::iterator r = a_fact->m_rule_deps.begin(); r != a_fact->m_rule_deps.end(); ++r )
        if ( isCircular( a_target_rule, *r ))
            return true;

    return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ================================ RuleEngine Class ==================================================================


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


/** \param a_source - RuleEngine class to synchronize from
  *
  * This method is used to transfer internal state from the source RuleEngine insstance
  * to the called instance. This method is needed to support defining a new set of rules
  * on a RuleEngine instance atomically - while still being able to retain the current
  * state if any rule definitions are invalid.
  */
void
RuleEngine::synchronize( const RuleEngine &a_source )
{
    // Prevent callbacks to listeners
    m_batch = true;

    // Transfer listeners
    for ( vector<IFactListener*>::const_iterator l = a_source.m_listeners.begin(); l != a_source.m_listeners.end(); ++l )
        m_listeners.push_back( *l );

    // Transfer asserted, non-rule facts
    for ( map<string,Fact*>::const_iterator f = a_source.m_facts.begin(); f != a_source.m_facts.end(); ++f )
    {
        if ( f->second->m_asserted && !f->second->m_rule_output )
            assert( f->first, f->second->m_value );
    }

    // Restore callbacks
    m_batch = false;
}


/** \param a_listener - Listener to attach
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


/** \param a_listener - Listener to detach
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


/** \param a_listener - Listener to receive signals
  *
  * Sends all currently asserted facts to specified listener.
  */
void
RuleEngine::sendAsserted( IFactListener &a_listener )
{
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted )
            a_listener.onAssert( f->second->m_id );
    }
}


/** \param a_id - ID of rule to define
  * \param a_expression - Expression of new rule
  *
  * Defines a new rule. Throws if ID is in use or expression is invalid.
  */
void
RuleEngine::defineRule( const string &a_id, const string &a_expression, const string a_description )
{
    Rule *rule = 0;

    try
    {
        rule = new Rule( *this, a_id, a_expression, a_description );

        m_rules[rule->getID()] = rule;

        rule->evaluate();
    }
    catch ( mu::Parser::exception_type &e )
    {
        if ( rule )
            delete rule;

        throw std::runtime_error( e.GetMsg() );
    }
    catch( std::exception &e )
    {
        if ( rule )
            delete rule;

        throw;
    }
    catch(...)
    {
        if ( rule )
            delete rule;

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
        retract( r->second->getFact() );
        delete( r->second );
        m_rules.erase( r );
    }
}


/** \brief Undefines (removes) all currently defined rules and retracts all associated facts.
  */
void
RuleEngine::undefineAllRules()
{
    for ( map<string,Rule*>::iterator r = m_rules.begin(); r != m_rules.end(); ++r )
    {
        retract( r->second->getFact() );
        delete( r->second );
    }

    m_rules.clear();
}


/** \brief Retrieves all currently defined rules.
  * \param a_rules - Receives rule definitions
  */
void
RuleEngine::getDefinedRules( vector<RuleInfo> &a_rules ) const
{
    RuleInfo info;
    a_rules.clear();
    for ( map<string,Rule*>::const_iterator r = m_rules.begin(); r != m_rules.end(); ++r )
    {
        info.enabled = true;
        info.fact = r->first;
        info.expr = r->second->getExpr();
        info.desc = r->second->getDesc();
        a_rules.push_back( info );
    }
}


/** \brief Asserts a void type fact by identifier
  * \param a_id - Identifier of fact
  */
void
RuleEngine::assert( const std::string &a_id )
{
    // If fact is not in fact space, then there are no dependencies no it yet
    map<string,Fact*>::iterator f = m_facts.find( a_id );
    if ( f != m_facts.end())
    {
        // Rule facts can not be manipulated via public RuleEngine API
        if ( f->second->m_rule_output )
            return;

        // Assert not asserted, or re-assert if type has changed
        if ( !f->second->m_asserted || f->second->m_value.m_type != VT_VOID )
            assert( f->second );
    }
    else
    {
        Fact *fact = new Fact( a_id, true, false );
        m_facts[a_id] = fact;

        assert( fact, false );
    }
}


/** \brief Asserts a numeric type fact by identifier
  * \param a_id - Identifier of fact
  * \param a_value - Value of fact
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
        // Rule facts can not be manipulated via public RuleEngine API
        if ( f->second->m_rule_output )
            return;

        if ( !f->second->m_asserted || f->second->m_value != value )
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
template void RuleEngine::assert<RuleEngine::Value>( const string &a_id, RuleEngine::Value a_value ); // Used internally only



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
        if ( f->second->m_asserted && !f->second->m_rule_output )
            retract( f->second );
    }
}


/**
 * @param a_prefix - ID prefix of fact(s) to retract
 *
 * This is a public retract method that retracts all facts that begin with the specified ID prefix
 * and updates forward dependencies based on current mode (immediate or batch).
 */
void
RuleEngine::retractPrefix( const std::string &a_prefix )
{
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted && !f->second->m_rule_output &&  boost::istarts_with( f->first, a_prefix ))
            retract( f->second );
    }
}


/** \brief Retracts all user-asserted facts (rule asserted facts may remain asserted)
  */
void
RuleEngine::retractAllFacts()
{
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted && !f->second->m_rule_output )
            retract( f->second );
    }
}


/** \brief Retrieves all asserted facts
  * \param a_asserted_facts - Receives IDs of asserted facts
  */
void
RuleEngine::getAsserted( std::vector<std::string> &a_asserted_facts )
{
    a_asserted_facts.clear();
    for ( map<string,Fact*>::iterator f = m_facts.begin(); f != m_facts.end(); ++f )
    {
        if ( f->second->m_asserted )
            a_asserted_facts.push_back( f->first );
    }
}


/** \brief Gets a handle to a fact
  * \param a_id - Identifier of fact
  * \return Fact handle
  *
  * This method retrieves a handle to a fact given the fact identifier. Handles
  * to rule output facts can not be returned (an exception will be thrown if
  * tried). Using handles for external assertion/retraction is faster than
  * using identifiers.
  */
RuleEngine::HFACT
RuleEngine::getFactHandle( const std::string &a_id )
{
    Fact *fact = getFact( a_id );
    if ( !fact->m_rule_output )
        return (HFACT)fact;

    throw std::runtime_error("Fact associated with a rule.");
}


/** \brief Gets a fact identifier given fact handle
  * \param a_fact - Handle of fact
  * \return Fact identifier
  */
std::string
RuleEngine::getNameHFACT( HFACT a_fact ) const
{
    return ((Fact*)a_fact)->m_id;
}


/** \brief Asserts a fact given a fact handle
  * \param a_fact - Handle of fact to assert
  */
void
RuleEngine::assert( HFACT a_fact )
{
    if ( !((Fact*)a_fact)->m_asserted )
        assert( ((Fact*)a_fact) );
}


/** \brief Asserts a fact with a value given a fact handle
  * \param a_fact - Handle of fact to assert
  * \param a_value - New value of fact
  */
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


/** \brief Retracts a fact given a fact handle
  * \param a_fact - Handle of fact to retract
  */
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
            if ( iold == m_old_facts.end() )
                notify_assert( *inew );
            else
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

/** \brief Tests if fact/rule identifier is in use
  * \param a_id - ID to test
  *
  * Tests to determine if the specified ID is in use as a rule or fact ID. If a fact
  * exists, but was implicitly defined, it is not considered in-use.
  */
bool
RuleEngine::idInUse( const std::string &a_id ) const
{
    if ( m_rules.find( a_id ) != m_rules.end() )
        return true;
    else
    {
        map<std::string,Fact*>::const_iterator f = m_facts.find( a_id );
        if ( f != m_facts.end() )
            return !f->second->m_implicit;
        else
            return false;
    }
}




/** \brief Asserts a fact given Fact instance
  * \param a_fact - Fact to be asserted
  *
  * This is an internal fact assert method. Dependencies are traced and forward rules re-evaluated.
  * Note that this method des not check if the specified fact is already asserted (this is an optimization).
  */
void
RuleEngine::assert( Fact *a_fact )
{
    a_fact->m_asserted = true;
    a_fact->m_implicit = false;
    a_fact->m_value.m_type = VT_VOID;
    a_fact->m_value.m_int_value = 1;

    notify_assert( a_fact );
    update( a_fact );
}


/** \brief Asserts a fact with a value given Fact instance
  * \param a_fact - Fact to be asserted
  * \param a_value - Value to be asserted
  *
  * This is an internal fact assert method. Dependencies are traced and forward rules re-evaluated.
  * Note that this method des not check if the specified fact is already asserted (this is an optimization).
  */
void
RuleEngine::assert( Fact *a_fact, Value &a_value )
{
    a_fact->m_asserted = true;
    a_fact->m_implicit = false;
    a_fact->m_value = a_value;

    notify_assert( a_fact );
    update( a_fact );
}


/** \brief Retracts a fact given Fact instance
  * \param a_fact - Fact to be retracted
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


/** \brief Retrieves a Fact instance given identifier
  * \param a_id - ID of Fact to retieve
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
        fact = new Fact( a_id, false, true );
        m_facts[a_id] = fact;
    }

    return fact;
}


/** \brief Updates rules dependent on given fact
  * \param a_fact - A fact that has been updated
  *
  * This method recursively updates all rules that are dependent on the
  * specified fact. If dependent rule output facts change state, then their
  * dependencies are also updated.
  */
void
RuleEngine::update( Fact *a_fact )
{
    for ( vector<Rule*>::iterator r = a_fact->m_rule_deps.begin(); r != a_fact->m_rule_deps.end(); ++r )
        (*r)->evaluate( a_fact );
}


/** \brief Notify listeners of fact assertion
  * \param a_fact - Asserted Fact
  *
  * This method notifies listeners of a newly asserted fact. If batch mode is
  * active, assertion is buffered for later use.
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
        for ( vector<IFactListener*>::iterator l = m_listeners.begin(); l != m_listeners.end(); ++l )
            (*l)->onAssert( a_fact->m_id );
    }
}


/** \brief Notify listeners of fact retraction
  * \param a_fact - Retracted Fact
  *
  * This method notifies listeners of a newly retracted fact. If batch mode is
  * active, retraction is buffered for later use.
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

