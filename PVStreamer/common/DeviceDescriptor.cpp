#include <stdexcept>
#include <iostream>
#include <sstream>
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
        EnumDescriptor *a_enum, const string &a_units,
        bool a_is_active_pv )
    : m_device(a_device), m_id(0),
    m_name(a_name), m_connection(a_connection),
    m_type(a_type), m_elem_count(a_elem_count),
    m_enum(a_enum), m_units(a_units),
    m_is_active_pv(a_is_active_pv), m_is_active(DEVICE_IS_INACTIVE)
{
    // Validate Enum Descriptor for Enum Typed PVs...
    if ( m_type == PV_ENUM && !m_enum )
    {
        std::stringstream ss;
        ss << "EnumDescriptor Cannot Be NULL for PV of Type PV_ENUM:"
            << " name=" << a_name
            << " connection=" << a_connection
            << " elem_count=" << a_elem_count
            << " units=" << a_units;
        throw runtime_error( ss.str() );
    }

    // Always Ignore Any Active Status PVs (Unless Subsuming A Regular PV!)
    m_ignore = ( m_is_active_pv ) ? true : false;
}


PVDescriptor::PVDescriptor( DeviceDescriptor *a_device,
        const PVDescriptor &a_source )
    : m_device(a_device), m_id(a_source.m_id),
    m_name(a_source.m_name), m_connection(a_source.m_connection),
    m_type(a_source.m_type), m_elem_count(a_source.m_elem_count),
    m_enum(0), m_units(a_source.m_units),
    m_is_active_pv(a_source.m_is_active_pv),
    m_is_active(a_source.m_is_active),
    m_ignore(a_source.m_ignore)
{
    // Capture Enum Descriptor for Enum Typed PVs...
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
            && m_is_active_pv == a_desc.m_is_active_pv
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
        const string &a_source, Protocol a_protocol,
        const string &a_active_pv_conn )
    : m_id(0), m_name(a_device_name), m_protocol(a_protocol),
    m_source(a_source), m_active_pv_conn(a_active_pv_conn), m_ready(0)
{
    // Set Up Any "Active Status" PV Descriptor...
    if ( m_active_pv_conn.empty() )
    {
        m_active_pv = (PVDescriptor *) NULL;
        m_active = true;
    }
    else
    {
        m_active_pv = new PVDescriptor( this,
            m_active_pv_conn, m_active_pv_conn,
            PV_INT, 1, 0, "", true );
        m_active = false;
    }
}


DeviceDescriptor::DeviceDescriptor( const DeviceDescriptor &a_source )
    : m_id(a_source.m_id), m_name(a_source.m_name),
    m_protocol(a_source.m_protocol), m_source(a_source.m_source),
    m_active_pv_conn(a_source.m_active_pv_conn),
    m_active(a_source.m_active),
    m_ready(0)
{
    // Copy Enums _Before_ Any PVs (Including "Active Status" PV! ;-D).
    for ( vector<EnumDescriptor*>::const_iterator e =
            a_source.m_enums.begin(); e != a_source.m_enums.end(); ++e )
        m_enums.push_back( new EnumDescriptor( **e ));

    // Set Up Any "Active Status" PV Descriptor...
    if ( a_source.m_active_pv != NULL )
        m_active_pv = new PVDescriptor( this, *(a_source.m_active_pv) );
    else
        m_active_pv = (PVDescriptor *) NULL;
    // Inherit "m_active" value from source instance...

    for( vector<PVDescriptor*>::const_iterator p = a_source.m_pvs.begin();
            p != a_source.m_pvs.end(); ++p )
        m_pvs.push_back( new PVDescriptor( this, **p ) );
}


DeviceDescriptor::~DeviceDescriptor()
{
    delete m_active_pv;

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
    if ( getPvByName( a_name )) {
        std::stringstream ss;
        ss << "Cannot Define PV with Duplicate Name (or Alias): "
            << " name=" << a_name
            << " connection=" << a_connection
            << " type=" << a_type
            << " elem_count=" << a_elem_count
            << " units=" << a_units;
        throw runtime_error( ss.str() );
    }

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
 * @return False if descriptors are different, true if they are exactly the same (_Not_ comparing Device ID, Active/PV or Ready Status...!)
 */
bool
DeviceDescriptor::operator==( const DeviceDescriptor &a_desc ) const
{
    bool res = false;

    // If device name, protocol, and source differ,
    // then devices are not the same
    // (Note: Ignore Temp Device's ID, Active/PV and "Ready" Count Here!)
    if ( m_name == a_desc.m_name
            && m_protocol == a_desc.m_protocol
            && m_source == a_desc.m_source
            && m_pvs.size() == a_desc.m_pvs.size()
            && m_enums.size() == a_desc.m_enums.size() )
    {
        // Dang, Need to Check Enumerated Types for Devices Too...! ;-Q
        for ( vector<EnumDescriptor*>::const_iterator
                    my_e = m_enums.begin(),
                    a_e = a_desc.m_enums.begin() ;
                my_e != m_enums.end() && a_e != a_desc.m_enums.end() ;
                ++my_e, ++a_e )
        {
            if ( **my_e != **a_e )
            {
                return false;
            }
        }

        // Compare Device PVs... (All of Them... ;-D)
        const PVDescriptor *ppv;
        res = true;

        // When an Active Status PV Subsumes a Regular Device PV,
        // it can Ghost the Presence of the PV's Existence...! ;-)
        if ( m_active_pv && !(m_active_pv->m_ignore) )
        {
            ppv = a_desc.getPvByName( m_active_pv->m_name );

            // If found, compare PVs...
            if ( ppv && *ppv != *m_active_pv )
            {
                return false;
            }

            // Else Compare Matching Active Status PVs...
            else if ( ppv == 0 &&
                    ( m_active_pv_conn.compare( a_desc.m_active_pv_conn )
                        || *(a_desc.m_active_pv) != *m_active_pv ) )
            {
                return false;
            }
        }

        // Check Regular Device PVs...
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

        // Check Both Ways!! ;-D

        // When an Active Status PV Subsumes a Regular Device PV,
        // it can Ghost the Presence of the PV's Existence...! ;-)
        if ( a_desc.m_active_pv && !(a_desc.m_active_pv->m_ignore) )
        {
            ppv = getPvByName( a_desc.m_active_pv->m_name );

            // If found, compare PVs...
            if ( ppv && *ppv != *(a_desc.m_active_pv) )
            {
                return false;
            }

            // Else Compare Matching Active Status PVs...
            else if ( ppv == 0 &&
                    ( a_desc.m_active_pv_conn.compare( m_active_pv_conn )
                        || *m_active_pv != *(a_desc.m_active_pv) ) )
            {
                return false;
            }
        }

        // Check Regular Device PVs...
        for ( vector<PVDescriptor*>::const_iterator ipv =
                    a_desc.m_pvs.begin();
                ipv != a_desc.m_pvs.end(); ++ipv )
        {
            ppv = getPvByName( (*ipv)->m_name );

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
    a_out << "id=" << m_id << "," << "name=" << m_name << ","
        << "protocol=" << m_protocol << "," << "source=" << m_source << ","
        << "active_pv_conn=" << "[" << m_active_pv_conn << "]" << ","
        << "active=" << m_active << ","
        << "ready=" << m_ready;
/*
    a_out << "ID:        " << m_id << endl;
    a_out << "Name:      " << m_name << endl;
    a_out << "Prot:      " << m_protocol << endl;
    a_out << "Src:       " << m_source << endl;
    a_out << "Active PV: " << m_active_pv_conn << endl;
    a_out << "Active:    " << m_active << endl;
    a_out << "Ready:     " << m_ready << endl;
*/
    if ( m_active_pv )
    {
        a_out << "," << "active_pv:";
        m_active_pv->print( a_out );
    }

    for ( vector<PVDescriptor*>::const_iterator p = m_pvs.begin();
            p != m_pvs.end(); ++p )
    {
        a_out << "," << "pv:";
        (*p)->print( a_out );
    }

    return a_out;
}


ostream&
PVDescriptor::print( ostream &a_out ) const
{
    a_out << "id=" << m_id << "," << "name=" << m_name << ","
        << "connection=" << m_connection << ","
        << "type=" << m_type << "[" << m_elem_count << "]" << ","
        << "units=" << m_units << ","
        << "is_active_pv=" << m_is_active_pv << ","
        << "is_active=" << m_is_active << ","
        << "ignore=" << m_ignore;
    if ( m_enum )
    {
        a_out << "," << "enum:";
        m_enum->print(a_out);
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

