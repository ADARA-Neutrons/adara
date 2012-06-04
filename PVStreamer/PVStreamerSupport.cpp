#include "stdafx.h"
#include "PVStreamerSupport.h"

using namespace std;

namespace SNS { namespace PVS {

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


Enum::Enum( unsigned long a_id, const map<int,string> &a_values )
: m_id(a_id), m_values(a_values)
{
}

bool
Enum::isValid( int a_value ) const
{
    if ( m_values.find( a_value ) != m_values.end() )
        return true;
    else
        return false;
}

const string &
Enum::getName( int a_value ) const
{
    map<int,string>::const_iterator e = m_values.find( a_value );
    if ( e != m_values.end() )
        return e->second;

    throw -1;
}

int
Enum::getValue( const std::string & a_name ) const
{
    for ( map<int,string>::const_iterator e = m_values.begin(); e != m_values.end(); ++e )
    {
        if ( e->second == a_name )
            return e->first;
    }

    throw -1;
}

const std::map<int,const std::string> &
Enum::getMap() const
{
    return (const std::map<int,const std::string> &) m_values;
}

bool
Enum::operator==(const std::map<int,std::string> &a_values) const
{
    return m_values == a_values;
}


}}
