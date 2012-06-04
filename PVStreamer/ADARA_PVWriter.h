#ifndef ADARA_PVWRITER
#define ADARA_PVWRITER

#include <vector>
#include <list>
#include <string>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

#include "PVWriter.h"
#include "ADARA.h"

#include <winsock2.h>

namespace SNS { namespace PVS { namespace ADARA {

#define EPICS_TIME_OFFSET -631152000

class IADARAWriterListener
{
public:
    virtual void    listening( const std::string & a_address, unsigned short a_port ) = 0;
    virtual void    connected() = 0; // const std::string & a_client_address );
    virtual void    disconnected() = 0; // const std::string & a_client_address );
};

class ADARA_PVWriter : public PVWriter
{
public:
    ADARA_PVWriter( PVStreamer &a_streamer, unsigned short a_port = 999, size_t a_adara_buffer_size = 100 );
    ~ADARA_PVWriter();

    void            attachListener( IADARAWriterListener &a_listener );
    void            detachListener( IADARAWriterListener &a_listener );

private:
    bool            connected();
    bool            translate( PVStreamPacket &a_pv_pkt, ADARAPacket &a_adara_pkt );
    void            buildDDP( ADARAPacket &a_adara_pkt, Identifier dev_id, Timestamp a_time );
    void            sendActiveDeviceInfo( SOCKET a_socket = INVALID_SOCKET );
    void            sendPacket( ADARAPacket & a_adara_pkt, SOCKET a_socket = INVALID_SOCKET );

    const char *    getTypeDescriptor( DataType a_type ) const;
    void            socketListenThreadFunc();
    void            packetSendThreadFunc();

    void            notifyConnect();
    void            notifyDisconnect();

    boost::thread*                      m_socket_listen_thread;
    boost::thread*                      m_pkt_send_thread;

    std::vector<ADARAPacket*>           m_adara_packets;
    SyncDeque<ADARAPacket*>             m_free_pkt;
    SyncDeque<ADARAPacket*>             m_ready_pkt;

    std::string                         m_addr;
    unsigned short                      m_port;
    boost::mutex                        m_list_mutex;
    std::vector<IADARAWriterListener*>  m_listeners;

    // ---------- WINDOWS-Specific Sockets ------------------------------------

    bool                            initWinSocket();

    SOCKET                          m_listen_socket;
    boost::mutex                    m_conn_mutex;
    std::list<SOCKET>               m_client_sockets;
};

}}}

#endif
