// PVStreamerClient.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>

#include <winsock2.h>
#include <ws2tcpip.h>
#include "../PVStreamer/ADARA.h"

using namespace std;

bool
initWinSocket( char *a_address, unsigned short a_port, SOCKET &a_socket )
{
    int rc;
    struct addrinfo *result = 0, *ptr = 0, hints;
    WSADATA wsadata;

    rc = WSAStartup( 0x101, &wsadata );
    if ( rc )
        throw -1;


    memset( &hints, 0, sizeof( hints ));
    hints.ai_family     = AF_INET;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;
    hints.ai_flags      = AI_PASSIVE;

    char port_str[20];
    sprintf_s( port_str,20, "%u", a_port );

    try
    {
        // Resolve the local address and port to be used by the server
        rc = getaddrinfo( a_address, port_str, &hints, &result );
        if ( rc )
            throw -2;

        a_socket = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
        if ( a_socket == INVALID_SOCKET )
            throw -3;

        rc = connect(a_socket, result->ai_addr, (int)result->ai_addrlen);
        if ( rc == SOCKET_ERROR )
        {
            closesocket(a_socket);
            throw -4;
        }

        freeaddrinfo( result );
        result = 0;
    }
    catch( int e)
    {
        // TODO Log error
        cout << e << endl;

        if ( result )
            freeaddrinfo( result );

        return false;
    }

    return true;
}


int _tmain(int argc, _TCHAR* argv[])
{
    char *address = "localhost";
    unsigned short port = 31416;

    for ( int i = 1; i < argc; ++i )
    {
        if ( _strnicmp( argv[i], "-port=",6) == 0 )
            port = atoi( &argv[i][6] );
        if ( _strnicmp( argv[i], "-addr=",6) == 0 )
            address = &argv[i][6];
    }

    cout << "PVStreamer test client" << endl;
    cout << "connecting to " << address << ":" << port;

    SOCKET  pvs_socket = INVALID_SOCKET;

    if (!initWinSocket(address,port,pvs_socket))
    {
        cout << " failed." << endl;
        return -1;
    }
    else
        cout << " success." << endl;

    SNS::PVS::ADARA::ADARAPacket pkt;
    int rc;

    while (1)
    {
        rc = recv(pvs_socket, (char*)&pkt, sizeof(SNS::PVS::ADARA::ADARAPacket),0);
        if ( rc > 0 )
        {
            cout << "got packet." << endl;
            cout << "format: " << hex << pkt.format << dec << endl;
            cout << "payload len: " << pkt.payload_len << endl;
            cout << "dev_id: " << pkt.dev_id << endl;
            cout << "time: " << pkt.sec << "." << pkt.nsec << endl;

            if ( pkt.format == 0x800000 )
            {
                cout << "xml len: " << pkt.ddp.xml_len << endl;
                cout << "xml: " << pkt.ddp.xml << endl;
            }
            else if ( pkt.format == 0x800100 )
            {
                cout << "pv id: " << pkt.vvp.var_id << endl;
                cout << "pv value: " << pkt.vvp.uval << endl;
            }
            else if ( pkt.format == 0x800200 )
            {
                cout << "pv id: " << pkt.vvp.var_id << endl;
                cout << "pv value: " << pkt.vvp.dval << endl;
            }
            else
            {
                cout << "unknown pkt type!" << endl;
            }
        }
        else if ( rc == 0 )
        {
            cout << "connection closed." << endl;
            break;
        }
        else if ( rc < 0 )
            cout << "recv failed." << endl;
    }

    WSACleanup();
	return 0;
}

