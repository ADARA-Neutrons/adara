/**
 * @file ADARA.h
 * @brief ADARA protocol definitions
 */

#ifndef ADARA_H
#define ADARA_H

#define ADARA_PROTOCOL 2

namespace SNS { namespace PVS { namespace ADARA {

#define MAX_XML_LEN     32744 // 32K - overhead of DDP packet
#define MAX_STR_LEN     4000

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

enum ADARA_Severity
{
    NoAlarm = 0,
    Minor,
    Major,
    Invalid
};

#pragma pack(push,4)

struct ADARAPacket
{
    unsigned long   payload_len;
    unsigned long   format;
    unsigned long   sec;
    unsigned long   nsec;
    unsigned long   dev_id; // Common to all PV ADARA packets
    union
    {
        struct // Device Descriptor payload
        {
            unsigned long       xml_len;
            char                xml[MAX_XML_LEN];
        } ddp;
        struct // Variable value payload
        {
            unsigned long       var_id;
            unsigned short      status;
            unsigned short      severity;
            union
            {
                unsigned long   uval;   // unsigned long value
                double          dval;   // double value
/*
                struct          sval    // string value & length
                {
                    unsigned long   str_len;
                    char            str[MAX_STR_LEN];
                };
*/
            };
        } vvp;
    };
};

#pragma pack(pop)

}}}

#endif
