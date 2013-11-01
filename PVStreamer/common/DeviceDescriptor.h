#ifndef DEVICEDESCRIPTOR_H
#define DEVICEDESCRIPTOR_H

#include <string>
#include <vector>
#include <iostream>
#include <stdint.h>
#include "CoreDefs.h"

namespace PVS {

class DeviceDescriptor; // Forward declare


class EnumDescriptor
{
public:
    bool                operator==( const EnumDescriptor &a_enum ) const;
    bool                operator!=( const EnumDescriptor &a_enum ) const;

    std::ostream&       print( std::ostream &a_out ) const;

    Identifier                      m_id;
    std::map<int32_t,std::string>   m_values;

private:
    EnumDescriptor( const std::map<int32_t,std::string> &a_values ) : m_id(0), m_values(a_values) {}
    EnumDescriptor( const EnumDescriptor &a_source ) : m_id(a_source.m_id), m_values(a_source.m_values) {}
    ~EnumDescriptor() {}

    friend class DeviceDescriptor;
};


class PVDescriptor
{
public:
    bool                operator==( const PVDescriptor &a_desc ) const;
    bool                operator!=( const PVDescriptor &a_desc ) const;

    std::ostream&       print( std::ostream &a_out ) const;

    DeviceDescriptor   *m_device;
    Identifier          m_id;
    std::string         m_name;
    std::string         m_connection;
    PVType              m_type;
    EnumDescriptor     *m_enum;
    std::string         m_units;

private:
    PVDescriptor( DeviceDescriptor *a_device, const std::string &a_name, const std::string &a_connection, PVType a_type, EnumDescriptor *a_enum, const std::string &a_units );
    PVDescriptor( DeviceDescriptor *a_device, const PVDescriptor &a_source );
    ~PVDescriptor() {}

    friend class DeviceDescriptor;
};


class DeviceDescriptor
{
public:
    DeviceDescriptor( const std::string &a_device_name, const std::string &a_source, Protocol a_protocol );
    DeviceDescriptor( const DeviceDescriptor &a_source );
    ~DeviceDescriptor();

    EnumDescriptor     *defineEnumeration( const std::map<int32_t,std::string> &a_values );
    EnumDescriptor     *defineEnumeration( const EnumDescriptor &a_enum );
    void                definePV( const std::string &a_name, const std::string &a_connection, PVType a_type, EnumDescriptor *a_enum, const std::string &a_units );
    PVDescriptor       *getPV( const std::string &a_pv_name ) const;
    bool                operator==( const DeviceDescriptor &a_desc ) const;
    bool                operator!=( const DeviceDescriptor &a_desc ) const;

    std::ostream&       print( std::ostream &a_out ) const;

    Identifier                      m_id;
    std::string                     m_name;
    Protocol                        m_protocol;
    std::string                     m_source;
    std::vector<PVDescriptor*>      m_pvs;
    std::vector<EnumDescriptor*>    m_enums;
};

}

#endif // DEVICEDESCRIPTOR_H
