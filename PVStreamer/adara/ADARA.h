#ifndef ADARA_H
#define ADARA_H

#include <stdint.h>

#define ADARA_PROTOCOL 2

/// Defines the difference between EPICS and Posix timestamp values
#define EPICS_TIME_OFFSET -631152000

namespace PVS { namespace ADARA {

/// ADARA process variable status codes (alarms/errors)
enum Status
{
    None    = 0,
    Read,
    Write,
    HiHi,
    High,
    LoLo,
    Low,
    State,
    Cos,
    Comm,
    Timeout,
    HwLimit,
    Calc,
    Scan,
    Link,
    Soft,
    BadSub,
    UDF,
    Disable,
    Simm,
    ReadAccess,
    WriteAccess
};

/// ADARA process variable alarm severity codes
enum Severity
{
    NoAlarm = 0,
    Minor,
    Major,
    Invalid
};

// Force visual studio to pack on 4 byte boundaries instead of default 8
#pragma pack(push,4)

/// This struct is used to build and transmit ADARA protocol DDP and VVP packets.
struct Packet
{
    uint32_t    payload_len;
    uint32_t    format;
    uint32_t    sec;
    uint32_t    nsec;
    uint32_t    dev_id; // Common to both DDP and VVP ADARA packets
    union
    {
        struct // Device Descriptor Packet (DDP)
        {
            uint32_t        xml_len;
            char            xml; // Placeholder for start of xml payload
        } ddp;
        struct // Variable Value Packet (VVP)
        {
            uint32_t        var_id;
            uint16_t        severity;
            uint16_t        status;
            union
            {
                uint32_t    uval;   // unsigned long value
                double      dval;   // double value
            };
        } vvp;
    };
};

#pragma pack(pop)

}}

#endif
