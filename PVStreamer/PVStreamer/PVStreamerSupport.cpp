/**
 * \file PVStreamerSupport.h
 * \brief Source file for classes defined in PVStreamerSupport.h.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"
#include "PVStreamerSupport.h"

using namespace std;

namespace SNS { namespace PVS {

/**
 * \brief Converts alarm value(s) to human readable text
 * \param a_arams - Alarms to convert
 * \return Text representation of alram state
 */
string
alarmText( unsigned short a_alarms )
{
    string alarm;

    if ( a_alarms & PV_HW_ALARM_HI )
        alarm += " HW_ALM_HI";
    if ( a_alarms & PV_HW_ALARM_LO )
        alarm += " HW_ALM_LO";
    if ( a_alarms & PV_HW_LIMIT_HI )
        alarm += " HW_LIM_HI";
    if ( a_alarms & PV_HW_LIMIT_LO )
        alarm += " HW_LIM_LO";

    return alarm;
}

/**
 * \brief Constructor for Enum class.
 * \param a_id - Identifier of new enum instance
 * \param a_values - Enum key-value pairs
 */
Enum::Enum( unsigned long a_id, const map<int,string> &a_values )
: m_id(a_id), m_values(a_values)
{
}

/**
 * \brief Determines if value is defined in enum.
 * \param a_value - Value to test for validity
 * \return True if value is valid; false otherwise
 */
bool
Enum::isValid( int a_value ) const
{
    if ( m_values.find( a_value ) != m_values.end() )
        return true;
    else
        return false;
}

/**
 * \brief Gets the name associated with a specified enum value (int)
 * \param a_value - Value to look-up
 * \return Name of specified value (throws if not defined)
 */
const string &
Enum::getName( int a_value ) const
{
    map<int,string>::const_iterator e = m_values.find( a_value );
    if ( e != m_values.end() )
        return e->second;

    EXCP( EC_INVALID_PARAM, "Enum value " << a_value << " not defined" );
}

/**
 * \brief Gets the value associated with a specified enum name
 * \param a_name - The enum name to look-up
 * \return Value of specified name (throws if not defined)
 */
int
Enum::getValue( const std::string & a_name ) const
{
    for ( map<int,string>::const_iterator e = m_values.begin(); e != m_values.end(); ++e )
    {
        if ( e->second == a_name )
            return e->first;
    }

    EXCP( EC_INVALID_PARAM, "Enum name " << a_name << " not defined" );
}

/**
 * \brief Gets the key-value pairs of the enum
 * \return All key-value pairs associated with this enum.
 */
const std::map<int,const std::string> &
Enum::getMap() const
{
    return (const std::map<int,const std::string> &) m_values;
}

/**
 * \brief Determines if an enum matches a specified map of key-value pairs
 * \param a_values - A map of key-value pairs to compair
 * \return True if specified key-value pairs match those defined in this enum
 */
bool
Enum::operator==(const std::map<int,std::string> &a_values) const
{
    return m_values == a_values;
}


}}
