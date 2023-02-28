#ifndef COREDEFS_H
#define COREDEFS_H

#include <stdint.h>

namespace PVS {

/// Defines the difference between EPICS and Posix timestamp values
#define EPICS_TIME_OFFSET 631152000

typedef uint32_t Identifier;
typedef uint32_t Protocol;

enum PVType
{
    PV_INT,
    PV_UINT,
    PV_REAL,
    PV_ENUM,
    PV_STR,
    PV_INT_ARRAY,
    PV_REAL_ARRAY
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

// vim: expandtab

