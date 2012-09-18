/**
 * \file ADARA_PVWriter.h
 * \brief Header file for ADARA_PVWriter class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef ADARA_PVWRITER
#define ADARA_PVWRITER

#include <vector>
#include <list>
#include <string>

#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/mutex.hpp>

#include "PVWriter.h"
#include "ADARA.h"

#include <winsock2.h>

namespace SNS { namespace PVS { namespace ADARA {

/// Defines the difference between EPICS and Posix timestamp values
#define EPICS_TIME_OFFSET -631152000

/// Interface for listeners that want to receive writer state notifications.
class IADARAWriterListener
{
public:
    virtual void    listening( const std::string & a_address, unsigned short a_port ) = 0;
    virtual void    connected( std::string &a_address ) = 0;
    virtual void    disconnected( std::string &a_address ) = 0;
};

/**
 * \class ADARA_PVWriter
 * \brief Process variable stream translator / server for output to ADARA protocol on tcp.
 *
 * The ADARA_PVWriter class provides process variable translation and streaming
 * over tcp for ADARA pv stream clients. Up to five concurrent clients are supported;
 * however, only the SMS process is expected to connect to this service currently
 * (stream debuggers/loggers are potential additional clients). The port number can be
 * specified in the constructor (31416 is the default). A simple connection status API
 * is provided through the IADARAWriterListener interface.
 */
class ADARA_PVWriter : public PVWriter
{
public:
    ADARA_PVWriter( PVStreamer &a_streamer, unsigned short a_port = 31416, unsigned long a_heartbeat = 2000 );
    ~ADARA_PVWriter();

    void                    attachListener( IADARAWriterListener &a_listener );
    void                    detachListener( IADARAWriterListener &a_listener );

private:

    bool                    connected();
    bool                    translate( PVStreamPacket &a_pv_pkt, ADARAPacket &a_adara_pkt, std::string &a_payload );
    void                    buildDDP( ADARAPacket &a_adara_pkt, std::string &a_payload, Identifier dev_id );
    void                    buildVVP( ADARAPacket &a_adara_pkt, const PVInfo &a_pv_info, PVStreamPacket *a_pv_pkt );
    void                    sendSourceInfo( SOCKET a_socket );
    bool                    sendActiveDeviceInfo( SOCKET a_socket = INVALID_SOCKET );
    void                    sendPacket( ADARAPacket & a_adara_pkt, std::string *a_payload = 0, SOCKET a_socket = INVALID_SOCKET );
    const char *            getTypeDescriptor( DataType a_type ) const;
    void                    socketListenThreadFunc();
    void                    packetSendThreadFunc();
    void                    notifyConnect( std::string &a_address );
    void                    notifyDisconnect( std::string &a_address );
    void                    initWinSocket();

    /// Structure containing ADARA client connection data
    struct ClientInfo
    {
        ClientInfo() : sock(INVALID_SOCKET),ddp(false) {}
        SOCKET      sock;
        std::string addr;
        bool        ddp;
    };

    bool                                m_active;                   ///< Indicates this instances is active or being destroyed
    boost::thread*                      m_socket_listen_thread;     ///< Tcp socket listener thread
    boost::thread*                      m_pkt_send_thread;          ///< Tcp packet send thread
    std::string                         m_addr;                     ///< Tcp address of ADARA service
    unsigned short                      m_port;                     ///< Tcp port number of ADARA service
    unsigned long                       m_heartbeat;                ///< Heartbeat packet period
    boost::mutex                        m_list_mutex;               ///< Mutex to protect listener container
    std::vector<IADARAWriterListener*>  m_listeners;                ///< Container of ADARA writer listeners
    SOCKET                              m_listen_socket;            ///< WinSock listener socket
    boost::recursive_mutex              m_conn_mutex;               ///< Mutex to protect client connection container
    std::list<ClientInfo>               m_client_info;              ///< Container of active client connections
};

}}}

#endif
