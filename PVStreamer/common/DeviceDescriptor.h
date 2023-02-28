#ifndef DEVICEDESCRIPTOR_H
#define DEVICEDESCRIPTOR_H

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <stdint.h>
#include "CoreDefs.h"

using namespace std;

namespace PVS {

class DeviceDescriptor; // Forward declare


class EnumDescriptor
{
public:
    bool                operator==( const EnumDescriptor &a_enum ) const;
    bool                operator!=( const EnumDescriptor &a_enum ) const;
    bool                operator==(
                            const map<int32_t, string> &a_enum_vals ) const;

    ostream&       print( ostream &a_out ) const;

    Identifier              m_id;
    map<int32_t, string>    m_values;

private:
    EnumDescriptor( const map<int32_t, string> &a_values )
        : m_id(0), m_values(a_values) {}
    EnumDescriptor( const EnumDescriptor &a_source )
        : m_id(a_source.m_id), m_values(a_source.m_values) {}
    ~EnumDescriptor() {}

    friend class DeviceDescriptor;
};


enum DeviceIsActiveEnum
{
    DEVICE_IS_INACTIVE,   // Device is Inactive
    DEVICE_IS_ACTIVE,     // Device is Active
    DEVICE_IS_UNKNOWN     // Device Active Status *Unknown*,
                          // Active Status PV is Disconnected...
                          // (Leave Device Status Unchanged...!)
};


class PVDescriptor
{
public:
    PVDescriptor( DeviceDescriptor *a_device,
        const string &a_name, const string &a_connection,
        PVType a_type, uint32_t a_elem_count,
        EnumDescriptor *a_enum, const string &a_units,
        bool a_is_active_pv = false );
    PVDescriptor( DeviceDescriptor *a_device,
        const PVDescriptor &a_source );
    ~PVDescriptor() {}

    bool                operator==( const PVDescriptor &a_desc ) const;
    bool                operator!=( const PVDescriptor &a_desc ) const;
    bool                equalMetadata( PVType a_type, uint32_t a_elem_count,
                            const string &a_units,
                            const map<int32_t, string> &a_enum_vals ) const;
    void                setMetadata( PVType a_type, uint32_t a_elem_count,
                            const string &a_units,
                            const map<int32_t, string> &a_enum_vals );

    ostream&            print( ostream &a_out ) const;

    DeviceDescriptor   *m_device;
    Identifier          m_id;
    string              m_name;
    string              m_connection;
    PVType              m_type;
    uint32_t            m_elem_count;
    EnumDescriptor     *m_enum;
    string              m_units;
    bool                m_is_active_pv;
    DeviceIsActiveEnum  m_is_active;
    bool                m_ignore;

    friend class DeviceDescriptor;
};


class DeviceDescriptor
{
public:
    DeviceDescriptor( const string &a_device_name,
            const string &a_source, Protocol a_protocol,
            const string &a_active_pv_conn = "" );
    DeviceDescriptor( const DeviceDescriptor &a_source );
    ~DeviceDescriptor();

    EnumDescriptor     *defineEnumeration(
                            const map<int32_t, string> &a_values );
    EnumDescriptor     *defineEnumeration( const EnumDescriptor &a_enum );
    void                definePV( const string &a_name,
                            const string &a_connection,
                            PVType a_type, uint32_t a_elem_count,
                            EnumDescriptor *a_enum, const string &a_units );
    PVDescriptor       *getPvByName( const string &a_pv_name ) const;
    PVDescriptor       *getPvByConnection(
                            const string &a_pv_connection ) const;
    bool                operator==( const DeviceDescriptor &a_desc ) const;
    bool                operator!=( const DeviceDescriptor &a_desc ) const;

    ostream&            print( ostream &a_out ) const;

    Identifier                      m_id;
    string                          m_name;
    Protocol                        m_protocol;
    string                          m_source;
    string                          m_active_pv_conn;
    PVDescriptor                   *m_active_pv;
    bool                            m_active;
    vector<PVDescriptor*>           m_pvs;
    vector<EnumDescriptor*>         m_enums;
    size_t                          m_ready;
};

}

#endif // DEVICEDESCRIPTOR_H

// vim: expandtab

