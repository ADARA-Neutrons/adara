#ifndef ADARA_OUTPUTADAPTER_H
#define ADARA_OUTPUTADAPTER_H

#include <set>
#include <map>
#include <list>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

#include "CoreDefs.h"
#include "IOutputAdapter.h"
#include "StreamService.h"
#include "ADARA.h"

#define PVSD_PROTOCOL   0

namespace PVS {
namespace ADARA {

class OutputAdapter : public IOutputAdapter
{
public:
    OutputAdapter( StreamService &a_stream_serv, unsigned short a_port = 31416, unsigned long a_heartbeat = 2000, bool a_no_heartbeat_pv = false );
    ~OutputAdapter();

    std::string     serverAddr();

    uint32_t        numConnected();
    uint32_t        numDevices();
    uint32_t        numPVs();

private:
    // Force visual studio to pack on 4 byte boundaries instead of default 8
    #pragma pack(push,4)

    /// This struct is used to build and transmit ADARA protocol DDP and VVP packets.
    struct OutPacket
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
            struct // Variable Value Packet (VVP) for Numerical Arrays
            {
                uint32_t        var_id;
                uint16_t        severity;
                uint16_t        status;
                uint32_t        elemCount;
            } vvp_array;
            struct // Variable Value Packet (VVP) for Strings
            {
                uint32_t        var_id;
                uint16_t        severity;
                uint16_t        status;
                uint32_t        str_len;
            } vvp_str;
        };
    };

    #pragma pack(pop)

    bool            connected();
    void            streamProcessingThread();
    void            buildDDP( OutPacket &a_adara_pkt,
                        std::vector<uint8_t> &a_payload,
                        DeviceRecordPtr a_device,
                        bool sendDescriptorXML = true );
    void            buildVVP( OutPacket &a_adara_pkt,
                        PVDescriptor *a_pv, PVState &a_state,
                        std::vector<uint8_t> &a_payload );
    bool            translate( StreamPacket &a_pv_pkt,
                        OutPacket &a_adara_pkt,
                        std::vector<uint8_t> &a_payload );
    void            updatePV( PVDescriptor *a_pv, PVState &a_state );
    void            disconnectUndefinedPVs();
    void            defineDevice( DeviceRecordPtr a_device );
    void            redefineDevice( DeviceRecordPtr a_device,
                        DeviceRecordPtr a_old_device );
    void            undefineDevice( DeviceRecordPtr a_device );
    const char *    getPVTypeXML( PVType a_type ) const;

    //----- Sockets-Related Methods -------------------------------------------
    void            initSockets();
    void            makeHeartbeatDevice();
    void            socketListenThread();
    void            sendPacket( OutPacket & a_adara_pkt,
                        std::vector<uint8_t> &a_payload,
                        int a_socket = -1 );
    void            sendSourceInfo( int a_socket );
    void            sendCurrentData( int a_socket );
    bool            send( int a_socket,
                        const char *a_data, uint32_t a_len );

    /// Structure containing ADARA client connection data
    struct ClientInfo
    {
        ClientInfo() : socket(-1) {}
        int         socket;
        std::string addr;
    };

    bool                                m_active;                   ///< Indicates this instances is active or being destroyed
    boost::thread*                      m_socket_listen_thread;     ///< Tcp socket listener thread
    boost::thread*                      m_stream_proc_thread;       ///< Stream processing thread
    std::string                         m_addr;                     ///< Tcp address of ADARA service
    uint16_t                            m_port;                     ///< Tcp port number of ADARA service
    uint32_t                            m_heartbeat;                ///< Heartbeat packet period
    bool                                m_no_heartbeat_pv;          ///< Turn Off PVSD Heartbeat Device/PV
    int                                 m_listen_socket;            ///< WinSock listener socket
    boost::recursive_mutex              m_mutex;                    ///< Mutex to protect internal data
    std::list<ClientInfo>               m_client_info;              ///< Container of active client connections
    std::set<DeviceRecordPtr>           m_devices;                  ///< Currently defined devices
    std::map<PVDescriptor*,PVState>     m_pv_state;                 ///< Currently defined PVs with last-known state
    DeviceRecordPtr                     m_heartbeat_device;         ///< Heartbeat device descriptor
    PVDescriptor                       *m_heartbeat_pv;             ///< Heartbeat PV descriptor
    uint32_t                            m_heartbeat_pv_value;       ///< Heartbeat PV value
};

}}

#endif // ADARA_OUTPUTADAPTER_H

// vim: expandtab

