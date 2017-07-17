#include <stdexcept>
#include <iostream>
#include "DeviceDescriptor.h"

using namespace std;

namespace PVS {


//==========================================================================
//===== EnumDescriptor Class ===============================================


/**
 * @brief Equality operator to determine if contents of two EnumDescriptors are identical
 * @param a_desc - EnumDescriptor to compare
 * @return False if descriptor differ; true if they are exactly the same
 */
bool
EnumDescriptor::operator==( const EnumDescriptor &a_desc ) const
{
    bool res = false;

    if ( m_values.size() == a_desc.m_values.size() )
    {
        res = true;
        map<int32_t, string>::const_iterator b = a_desc.m_values.begin();
        for ( map<int32_t, string>::const_iterator a = m_values.begin();
                a != m_values.end(); ++a, ++b )
        {
            if ( a->first != b->first || a->second != b->second )
            {
                res = false;
                break;
            }
        }
    }

    return res;
}


bool
EnumDescriptor::operator!=( const EnumDescriptor &a_desc ) const
{
    return !( *this == a_desc );
}


bool
EnumDescriptor::operator==( const map<int32_t, string> &a_enum_vals ) const
{
    bool res = false;

    if ( m_values.size() == a_enum_vals.size() )
    {
        res = true;
        map<int32_t, string>::const_iterator b = a_enum_vals.begin();
        for ( map<int32_t, string>::const_iterator a = m_values.begin();
                a != m_values.end(); ++a, ++b )
        {
            if ( a->first != b->first || a->second != b->second )
            {
                res = false;
                break;
            }
        }
    }

    return res;
}


//==========================================================================
//===== PVDescriptor Class =================================================


PVDescriptor::PVDescriptor( DeviceDescriptor *a_device,
        const string &a_name, const string &a_connection,
        PVType a_type, uint32_t a_elem_count,
        EnumDescriptor *a_enum, const string &a_units )
    : m_device(a_device), m_id(0),
    m_name(a_name), m_connection(a_connection),
    m_type(a_type), m_elem_count(a_elem_count),
    m_enum(a_enum), m_units(a_units), m_ignore(false)
{
    if ( m_type == PV_ENUM && !m_enum )
    {
        throw runtime_error(
            "EnumDescriptor can not be null for PV of type PV_ENUM");
    }
}


PVDescriptor::PVDescriptor( DeviceDescriptor *a_device,
        const PVDescriptor &a_source )
    : m_device(a_device), m_id(a_source.m_id),
    m_name(a_source.m_name), m_connection(a_source.m_connection),
    m_type(a_source.m_type), m_elem_count(a_source.m_elem_count),
    m_enum(0), m_units(a_source.m_units), m_ignore(false)
{
    if ( m_type == PV_ENUM && a_source.m_enum )
        m_enum = a_device->m_enums[a_source.m_enum->m_id - 1];
}


bool
PVDescriptor::operator==( const PVDescriptor &a_desc ) const
{
    bool res = false;

    if ( m_name == a_desc.m_name
            && m_connection == a_desc.m_connection
            && m_type == a_desc.m_type
            && m_elem_count == a_desc.m_elem_count
            && m_units == a_desc.m_units
            && m_ignore == a_desc.m_ignore )
    {
        res = true;

        if ( m_type == PV_ENUM && *m_enum != *a_desc.m_enum )
            res = false;
    }

    return res;
}


bool
PVDescriptor::operator!=( const PVDescriptor &a_desc ) const
{
    return !( *this == a_desc );
}


bool
PVDescriptor::equalMetadata( PVType a_type, uint32_t a_elem_count,
    const string &a_units, const map<int32_t, string> &a_enum_vals ) const
{
    bool res = false;

    if ( a_type == m_type
            && a_elem_count == m_elem_count
            && a_units == m_units )
    {
        if ( m_type == PV_ENUM )
        {
            if ( m_enum && *m_enum == a_enum_vals )
                res = true;
        }
        else
            res = true;
    }

    return res;
}


void
PVDescriptor::setMetadata( PVType a_type, uint32_t a_elem_count,
    const string &a_units, const map<int32_t, string> &a_enum_vals )
{
    m_type = a_type;
    m_elem_count = a_elem_count;
    m_units = a_units;

    if ( m_type == PV_ENUM )
        m_enum = m_device->defineEnumeration( a_enum_vals );
    else
        m_enum = 0;
}


//==========================================================================
//===== DeviceDescriptor Class =============================================


DeviceDescriptor::DeviceDescriptor( const string &a_device_name,
        const string &a_source, Protocol a_protocol )
    : m_id(0), m_name(a_device_name), m_protocol(a_protocol),
    m_source(a_source), m_ready(0)
{
}


DeviceDescriptor::DeviceDescriptor( const DeviceDescriptor &a_source )
    : m_id(a_source.m_id), m_name(a_source.m_name),
    m_protocol(a_source.m_protocol), m_source(a_source.m_source),
    m_ready(0)
{
    for ( vector<EnumDescriptor*>::const_iterator e =
            a_source.m_enums.begin(); e != a_source.m_enums.end(); ++e )
        m_enums.push_back( new EnumDescriptor( **e ));

    for( vector<PVDescriptor*>::const_iterator p = a_source.m_pvs.begin();
            p != a_source.m_pvs.end(); ++p )
        m_pvs.push_back( new PVDescriptor( this, **p ) );
}


DeviceDescriptor::~DeviceDescriptor()
{
    for ( vector<PVDescriptor*>::iterator p = m_pvs.begin();
            p != m_pvs.end(); ++p )
    {
        delete *p;
    }

    for ( vector<EnumDescriptor*>::iterator e = m_enums.begin();
            e != m_enums.end(); ++e )
    {
        delete *e;
    }
}


EnumDescriptor*
DeviceDescriptor::defineEnumeration( const map<int32_t, string> &a_values )
{
    EnumDescriptor *new_enum = new EnumDescriptor( a_values );
    new_enum->m_id = m_enums.size() + 1;

    for ( vector<EnumDescriptor*>::iterator e = m_enums.begin();
            e != m_enums.end(); ++e )
    {
        if ( **e == *new_enum )
        {
            delete new_enum;
            return *e;
        }
    }

    m_enums.push_back( new_enum );
    return new_enum;
}


EnumDescriptor*
DeviceDescriptor::defineEnumeration( const EnumDescriptor &a_enum )
{
    EnumDescriptor *new_enum = new EnumDescriptor( a_enum );
    new_enum->m_id = m_enums.size() + 1;

    for ( vector<EnumDescriptor*>::iterator e = m_enums.begin();
            e != m_enums.end(); ++e )
    {
        if ( **e == *new_enum )
        {
            delete new_enum;
            return *e;
        }
    }

    m_enums.push_back( new_enum );
    return new_enum;
}


void
DeviceDescriptor::definePV(
        const string &a_name, const string &a_connection,
        PVType a_type, uint32_t a_elem_count,
        EnumDescriptor *a_enum, const string &a_units )
{
    if ( getPvByName( a_name ))
        throw runtime_error("Cannot define PV with duplicate name (alias)");

    m_pvs.push_back( new PVDescriptor( this, a_name, a_connection,
        a_type, a_elem_count, a_enum, a_units ) );
}


PVDescriptor*
DeviceDescriptor::getPvByName( const string &a_pv_name ) const
{
    for ( vector<PVDescriptor*>::const_iterator p = m_pvs.begin();
            p != m_pvs.end(); ++p )
    {
        if ( (*p)->m_name == a_pv_name )
            return *p;
    }

    return 0;
}


PVDescriptor*
DeviceDescriptor::getPvByConnection( const string &a_pv_connection ) const
{
    for ( vector<PVDescriptor*>::const_iterator p = m_pvs.begin();
            p != m_pvs.end(); ++p )
    {
        if ( (*p)->m_connection == a_pv_connection )
            return *p;
    }

    return 0;
}


/**
 * @brief Equality operator to determine if two descriptors are exactly the same or not
 * @param a_desc - Descriptor to compare against
 * @return False if descriptors are different, true if they are exactly the same (_Not_ comparing Device ID or Ready Status...!)
 */
bool
DeviceDescriptor::operator==( const DeviceDescriptor &a_desc ) const
{
    bool res = false;

    // If device name, protocol, and source differ,
    // then devices are not the same
    // (Note: Ignore Temp Device's ID and "Ready" Count Here!)
    if ( m_name == a_desc.m_name
            && m_protocol == a_desc.m_protocol
            && m_source == a_desc.m_source
            && m_pvs.size() == a_desc.m_pvs.size() )
    {
        const PVDescriptor *ppv;
        res = true;

        for ( vector<PVDescriptor*>::const_iterator ipv = m_pvs.begin();
                ipv != m_pvs.end(); ++ipv )
        {
            ppv = a_desc.getPvByName( (*ipv)->m_name );

            // If not found, devices differ
            if ( ppv == 0 || *ppv != **ipv )
            {
                res = false;
                break;
            }
        }
    }

    return res;
}


bool
DeviceDescriptor::operator!=( const DeviceDescriptor &a_desc ) const
{
    return !( *this == a_desc );
}


ostream&
DeviceDescriptor::print( ostream &a_out ) const
{
    a_out << m_id << "," << m_name << "," << m_protocol << "," << m_source
        << "," << m_ready << endl;
/*
    a_out << "ID:    " << m_id << endl;
    a_out << "Name:  " << m_name << endl;
    a_out << "Prot:  " << m_protocol << endl;
    a_out << "Src:   " << m_source << endl;
    a_out << "Ready: " << m_ready << endl;
*/
    for ( vector<PVDescriptor*>::const_iterator p = m_pvs.begin();
            p != m_pvs.end(); ++p )
        (*p)->print( a_out );

    return a_out;
}


ostream&
PVDescriptor::print( ostream &a_out ) const
{
    a_out << "  " << m_id << "," << m_name << "," << m_connection << ","
        << m_type << "[" << m_elem_count << "]," << m_units << ","
        << "ignore=" << m_ignore << endl;
    if ( m_enum )
    {
        a_out << "    enum: ";
        m_enum->print(a_out);
        a_out << endl;
    }

/*
    a_out << "  ID:   " << m_id << endl;
    a_out << "  Name: " << m_name << endl;
    a_out << "  Conn: " << m_connection << endl;
    a_out << "  Type: " << m_type << endl;
    a_out << "  ElemCount: " << m_elem_count << endl;
    a_out << "  Enum: ";
    m_enum ? m_enum->print(a_out) : a_out << "n/a";
    a_out << endl;
    a_out << "  Unit: " << m_units << endl;
*/
    return a_out;
}


ostream&
EnumDescriptor::print( ostream &a_out ) const
{
    for ( map<int32_t, string>::const_iterator v = m_values.begin();
            v != m_values.end(); ++v )
    {
        a_out << "[" << v->first << ":" << v->second << "]";
    }

    return a_out;
}


}

// vim: expandtab

