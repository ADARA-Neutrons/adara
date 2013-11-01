#ifndef COREDEFS_H
#define COREDEFS_H

#include <stdint.h>
#include <map>
#include <vector>
#include <boost/shared_ptr.hpp>

namespace PVS {

typedef uint32_t Identifier;
typedef uint32_t Protocol;

enum PVType
{
    PV_INT,
    PV_UINT,
    PV_REAL,
    PV_ENUM,
    PV_STR
};

#if 0
class Value
{
public:
    Value( ValueType a_type ) : m_type(a_type) {}

private:
    ValueType           m_type;
    union
    {
        int32_t         m_int_val;    ///< Used for both int and enum type
        uint32_t        m_uint_val;
        double          m_real_val;
    };
    std::string         m_str_val;
};
#endif

enum PVStatus
{
    PV_OK = 0,
    PV_DISCONNECTED,
    PV_LOW_LIMIT,
    PV_HIGH_LIMIT,
    PV_ERR
};


enum
{
    EC_INVALID_OPERATION = 1,
    EC_INVALID_PARAM,
    EC_INVALID_CONFIG_DATA,
    EC_SOCKET_ERROR,
    EC_UNKOWN_ERROR,
    EC_EPICS_API,
    EC_WINDOWS_ERROR = 0x1000
};

}

#endif // COREDEFS_H
