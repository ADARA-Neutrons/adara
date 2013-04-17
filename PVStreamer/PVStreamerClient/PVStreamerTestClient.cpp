// PVStreamerClient.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "math.h"
#include "float.h"
#include <iostream>

#include "../PVStreamer/ADARA.h"

#include <winsock2.h>
#include <ws2tcpip.h>

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
        cout << e << endl;

        if ( result )
            freeaddrinfo( result );

        return false;
    }

    return true;
}


int _tmain(int argc, _TCHAR* argv[])
{
    char            *address = "localhost";
    unsigned short  port = 31416;
    unsigned long   pkt_count = 0;
    bool            test = false;

    for ( int i = 1; i < argc; ++i )
    {
        if ( _strnicmp( argv[i], "-port=",6) == 0 )
            port = atoi( &argv[i][6] );
        if ( _strnicmp( argv[i], "-addr=",6) == 0 )
            address = &argv[i][6];
        if ( _stricmp( argv[i], "-test") == 0 )
            test = true;
    }

    cout << "PVStreamer test client " << (test?"[Test Mode]":"[Monitor Mode]" )<< endl;
    cout << "connecting to " << address << ":" << port;

    SOCKET  pvs_socket = INVALID_SOCKET;

    if (!initWinSocket(address,port,pvs_socket))
    {
        cout << " failed." << endl;
        return -1;
    }
    else
        cout << " success." << endl;


    SNS::PVS::ADARA::ADARAPacket hdr;
    SNS::PVS::ADARA::ADARAPacket *pkt;
    char *buf = 0;
    int rc;
    int test_state = 0;
    unsigned long test_pkt_count = 0;
    double next_val;
    unsigned long buf_len = 0;
    unsigned long rcount = 0;

    while (1)
    {
        // Rcv ADARA header ONLY
        rc = recv(pvs_socket, (char*)&hdr, 16,0);
        if ( rc == 16 )
        {
            ++pkt_count;
            // Get payload len from header
            //if ( !test )
            //    cout << "Pkt # " << pkt_count << " [" << hex << hdr.format << dec << "] l=" << hdr.payload_len << " ts=" << hdr.sec << "." << hdr.nsec << endl;

            if ( hdr.payload_len )
            {
                if ( !buf || buf_len < hdr.payload_len )
                {
                    if ( buf )
                        delete[] buf;

                    buf = new char[16+hdr.payload_len + 1];
                    buf_len = hdr.payload_len;
                }

                rcount = 0;
                while ( rcount < hdr.payload_len )
                {
                    rc = recv(pvs_socket, buf+16+rcount, hdr.payload_len - rcount, 0 );
                    if ( rc == 0 )
                    {
                        cout << "  connection closed." << endl;
                        break;
                    }
                    else if ( rc < 0 )
                        cout << "  recv error: " << rc << endl;

                    rcount += rc;
                }

                buf[16+hdr.payload_len] = 0;
            }

            if ( rc <= 0 )
                break;

            pkt = (SNS::PVS::ADARA::ADARAPacket*)buf;

            if ( !test )
            {
                if ( hdr.format == 0x800000 )
                {
                    cout << "DDP: ";
                    cout << " id: " << pkt->dev_id;
                    cout << ", xml len: " << pkt->ddp.xml_len << endl;
                    cout << "  xml: " << &pkt->ddp.xml << endl;
                }
                else if ( hdr.format == 0x800100 )
                {
                    cout << "VVP: ";
                    cout << " id: " << pkt->dev_id << "." << pkt->vvp.var_id;
                    cout << ", value: " << pkt->vvp.uval;
                    cout << ", alarm: " << pkt->vvp.status << " [" << pkt->vvp.severity << "]" << endl;
                }
                else if ( hdr.format == 0x800200 )
                {
                    cout << "VVP: ";
                    cout << " id: " << pkt->dev_id << "." << pkt->vvp.var_id;
                    cout << ", value: " << pkt->vvp.dval;
                    cout << ", alarm: " << pkt->vvp.status << " [" << pkt->vvp.severity << "]" << endl;
                }
                //else if ( hdr.format == 0x400900 )
                    //cout << "  heartbeat." << endl;
                //else if ( hdr.format == 0x200 )
                    //cout << "  source list pkt." << endl;
                //else
                //{
                //    cout << "  unknown pkt type!" << endl;
                //}
            }
            else
            {
                // Verify value update on PV 1.1 (huber)
                if ( hdr.format == 0x800200 && pkt->dev_id == 1 && pkt->vvp.var_id == 1 )
                {
                    switch ( test_state )
                    {
                    case 0:
                        if ( fabs(-180.0 - pkt->vvp.dval) <= DBL_EPSILON )
                        {
                            cout << "Test stream detected." << endl;
                            test_state = 1;
                            next_val = -179.9;
                            test_pkt_count = 1;
                        }
                        break;
                    case 1:
                        if ( fabs(next_val - pkt->vvp.dval) > DBL_EPSILON )
                        {
                            cout << "Bad value at pkt # " << test_pkt_count << endl;
                            cout << "Got: " << pkt->vvp.dval <<", expected: " << next_val << endl;
                            test_state = 0;
                        }
                        else
                        {
                            test_pkt_count++;
                            if ( test_pkt_count == 3601 )
                            {
                                cout << "Test completed successfully!" << endl;
                                test_state = 3;
                            }
                            else
                            {
                                next_val = -180 + test_pkt_count*0.1;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
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

