#ifndef ADARA_OUTPUTADAPTER_H
#define ADARA_OUTPUTADAPTER_H

#include <set>
#include <map>
#include <list>
#include <string>

#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/mutex.hpp>

#include "CoreDefs.h"
#include "IOutputAdapter.h"
#include "StreamService.h"
#include "ADARA.h"

namespace PVS {
namespace ADARA {

class OutputAdapter : public IOutputAdapter
{
public:
    OutputAdapter( StreamService &a_stream_serv, unsigned short a_port = 31416, unsigned long a_heartbeat = 2000 );
    ~OutputAdapter();

private:

    bool            connected();
    void            packetSendThread();
    void            buildDDP( ADARA::Packet &a_adara_pkt, std::string &a_payload, DeviceDescriptor &a_device );
    void            buildVVP( ADARA::Packet &a_adara_pkt, PVDescriptor *a_pv, PVState a_state );
    bool            translate( StreamPacket &a_pv_pkt, ADARA::Packet &a_adara_pkt, std::string &a_payload );
    void            updatePV( PVDescriptor *a_pv, PVState a_state );
    void            defineDevice( DeviceDescriptor &a_device );
    void            redefineDevice( DeviceDescriptor &a_device,  DeviceDescriptor &a_old_device );
    void            undefineDevice( DeviceDescriptor &a_device );
    const char *    getPVTypeXML( PVType a_type ) const;

    //----- Sockets-Related Methods -------------------------------------------
    void            initSockets();
    void            socketListenThread();
    void            sendPacket( ADARA::Packet & a_adara_pkt, std::string *a_payload, int a_socket = -1 );
    void            sendSourceInfo( int a_socket );
    void            sendCurrentData( int a_socket );
    bool            send( int a_socket, const char *a_data, uint32_t a_len );

    /// Structure containing ADARA client connection data
    struct ClientInfo
    {
        ClientInfo() : socket(-1) {}
        int         socket;
        std::string addr;
    };

    bool                                m_active;                   ///< Indicates this instances is active or being destroyed
    boost::thread*                      m_socket_listen_thread;     ///< Tcp socket listener thread
    boost::thread*                      m_pkt_send_thread;          ///< Tcp packet send thread
    std::string                         m_addr;                     ///< Tcp address of ADARA service
    uint16_t                            m_port;                     ///< Tcp port number of ADARA service
    uint32_t                            m_heartbeat;                ///< Heartbeat packet period
    int                                 m_listen_socket;            ///< WinSock listener socket
    boost::recursive_mutex              m_mutex;                    ///< Mutex to protect internal data
    std::list<ClientInfo>               m_client_info;              ///< Container of active client connections

    std::set<DeviceDescriptor*>         m_devices;
    std::map<PVDescriptor*,PVState>     m_pv_state;
};

}}

#endif // ADARA_OUTPUTADAPTER_H
