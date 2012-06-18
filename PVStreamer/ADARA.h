/**
 * \file ADARA.h
 * \brief Header file for ADARA definitions.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef ADARA_H
#define ADARA_H

#define ADARA_PROTOCOL 2

namespace SNS { namespace PVS { namespace ADARA {

#define MAX_XML_LEN     32744 // 32K minus overhead of DDP packet
#define MAX_STR_LEN     4000


/// ADARA process variable status codes (alarms/errors)
enum ADARA_Status
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
enum ADARA_Severity
{
    NoAlarm = 0,
    Minor,
    Major,
    Invalid
};

// Force visual studio to pack on 4 byte boundaries instead of default 8
#pragma pack(push,4)

/// This struct is used to build and transmit ADARA protocol DDP and VVP packets.
struct ADARAPacket
{
    unsigned long   payload_len;
    unsigned long   format;
    unsigned long   sec;
    unsigned long   nsec;
    unsigned long   dev_id; // Common to both DDP and VVP ADARA packets
    union
    {
        struct // Device Descriptor Packet (DDP)
        {
            unsigned long       xml_len;
            char                xml[MAX_XML_LEN];
        } ddp;
        struct // Variable Value Packet (VVP)
        {
            unsigned long       var_id;
            unsigned short      status;
            unsigned short      severity;
            union
            {
                unsigned long   uval;   // unsigned long value
                double          dval;   // double value
            };
        } vvp;
    };
};

#pragma pack(pop)

}}}

#endif
