#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <math.h>
//#include "float.h"
#include <iostream>

#include "../adara/ADARA.h"


using namespace std;

bool
initConnection( const char *a_address, unsigned short a_port, int &a_socket )
{
    int rc;
    struct addrinfo *result = 0, *ptr = 0, hints;

    memset( &hints, 0, sizeof( hints ));
    hints.ai_family     = AF_INET;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;
    hints.ai_flags      = AI_PASSIVE;

    char port_str[20];
    sprintf( port_str, "%u", a_port );

    try
    {
        // Resolve the local address and port to be used by the server
        rc = getaddrinfo( a_address, port_str, &hints, &result );
        if ( rc )
            throw -2;

        a_socket = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
        if ( a_socket < 0 )
            throw -3;

        rc = connect(a_socket, result->ai_addr, (int)result->ai_addrlen);
        if ( rc < 0 )
        {
            close(a_socket);
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


int main( int argc, char* argv[])
{
    const char     *address = "localhost";
    unsigned short  port = 31416;
    unsigned long   pkt_count = 0;

    for ( int i = 1; i < argc; ++i )
    {
        if ( strncmp( argv[i], "-port=",6) == 0 )
            port = atoi( &argv[i][6] );
        if ( strncmp( argv[i], "-addr=",6) == 0 )
            address = &argv[i][6];
    }

    cout << "PVStreamer test client" << endl;
    cout << "connecting to " << address << ":" << port;

    int  pvs_socket = -1;

    if (!initConnection( address, port, pvs_socket ))
    {
        cout << " failed." << endl;
        return -1;
    }
    else
        cout << " success." << endl;


    PVS::ADARA::Packet hdr;
    PVS::ADARA::Packet *pkt;
    char *buf = 0;
    int rc;
    unsigned long buf_len = 0;
    unsigned long rcount = 0;

    while (1)
    {
        // Rcv ADARA header ONLY
        rc = read( pvs_socket, (char*)&hdr, 16 );
        if ( rc == 16 )
        {
            ++pkt_count;
            // Get payload len from header
            //if ( !test )
            //cout << "Pkt # " << pkt_count << " [" << hex << hdr.format << dec << "] l=" << hdr.payload_len << " ts=" << hdr.sec << "." << hdr.nsec << endl;

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
                    rc = read( pvs_socket, buf+16+rcount, hdr.payload_len - rcount );
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

            pkt = (PVS::ADARA::Packet*)buf;

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
        else if ( rc == 0 )
        {
            cout << "connection closed." << endl;
            break;
        }
        else if ( rc < 0 )
            cout << "recv failed." << endl;
    }

    return 0;
}

