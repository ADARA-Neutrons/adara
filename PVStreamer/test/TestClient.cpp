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

#include "../../common/ADARA.h"
//#include "../../common/ADARAPackets.h"


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
    unsigned short  port = 50011;
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


    ADARA::Header   hdr;
    //PVS::ADARA::Packet hdr;
    //PVS::ADARA::Packet *pkt;
    char *buf = 0;
    uint32_t *fields;
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

            uint32_t base_type = ADARA_BASE_PKT_TYPE( hdr.pkt_format );

            // Get payload len from header
            if ( base_type != ADARA::PacketType::HEARTBEAT_TYPE )
            {
                cout << "[" << hex << hdr.pkt_format << dec
                     << "] l=" << hdr.payload_len
                     << " ts=" << hdr.ts_sec << "." << hdr.ts_nsec << endl;
            }

            if ( hdr.payload_len )
            {
                if ( !buf || buf_len < hdr.payload_len )
                {
                    if ( buf )
                        delete[] buf;

                    buf = new char[ hdr.payload_len + 1 ];
                    buf_len = hdr.payload_len;
                }

                rcount = 0;
                while ( rcount < hdr.payload_len )
                {
                    rc = read( pvs_socket,
                        buf + rcount, hdr.payload_len - rcount );
                    if ( rc == 0 )
                    {
                        cout << "  connection closed." << endl;
                        break;
                    }
                    else if ( rc < 0 )
                        cout << "  recv error: " << rc << endl;

                    rcount += rc;
                }

                buf[ hdr.payload_len ] = 0;
            }

            if ( rc <= 0 )
                break;

            //pkt = (PVS::ADARA::Packet*)buf;
            fields = (uint32_t*)buf;

            if ( base_type == ADARA::PacketType::DEVICE_DESC_TYPE )
            {
                cout << "DDP: ";
                cout << " id: " << fields[0];
                cout << ", xml len: " << fields[1] << endl;
                cout << "  xml: " << &buf[8] << endl;
            }
            else if ( base_type == ADARA::PacketType::VAR_VALUE_U32_TYPE )
            {
                cout << "VVP: ";
                cout << " id: " << fields[0] << "." << fields[1];
                cout << ", value: " << fields[3];
                cout << ", alarm: " << ( fields[2] >> 16 )
                     << " [" << ( fields[2] & 0xFFFF ) << "]" << endl;
            }
            else if ( base_type
                    == ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE )
            {
                cout << "VVP: ";
                cout << " id: " << fields[0] << "." << fields[1];
                cout << ", value: " << *((double*)&fields[3]);
                cout << ", alarm: " << ( fields[2] >> 16 )
                     << " [" << ( fields[2] & 0xFFFF ) << "]" << endl;
            }
            else if ( base_type
                    == ADARA::PacketType::VAR_VALUE_STRING_TYPE )
            {
                // Null terminate string val
                buf[16 + fields[3]] = 0;

                cout << "VVP: ";
                cout << " id: " << fields[0] << "." << fields[1];
                cout << ", len: " << fields[3];
                cout << ", value: " << &buf[16];
                cout << ", alarm: " << ( fields[2] >> 16 )
                     << " [" << ( fields[2] & 0xFFFF ) << "]" << endl;
            }
            else if ( base_type
                    == ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE )
            {
                cout << "VVP: ";
                cout << " id: " << fields[0] << "." << fields[1];
                cout << ", count: " << fields[3];
                cout << ", value: ";
                for ( int i = 1; i < fields[3]; ++i )
                {
                    if ( i ) cout << ", ";
                    cout << ((uint32_t*)&fields[4])[i];
                }
                cout << ", alarm: " << ( fields[2] >> 16 )
                     << " [" << ( fields[2] & 0xFFFF ) << "]" << endl;
            }
            else if ( base_type
                    == ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE )
            {
                cout << "VVP: ";
                cout << " id: " << fields[0] << "." << fields[1];
                cout << ", count: " << fields[3];
                cout << ", value: ";
                for ( int i = 1; i < fields[3]; ++i )
                {
                    if ( i ) cout << ", ";
                    cout << ((double*)&fields[4])[i];
                }
                cout << ", alarm: " << ( fields[2] >> 16 )
                     << " [" << ( fields[2] & 0xFFFF ) << "]" << endl;
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

