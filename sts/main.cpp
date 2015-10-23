
//
// SNS ADARA SYSTEM - Stream Translation Service (STS)
// 
// This repository contains the software for the next-generation Data
// Acquisition System (DAS) at the Spallation Neutron Source (SNS) at
// Oak Ridge National Laboratory (ORNL) -- "ADARA".
// 
// Copyright (c) 2015, UT-Battelle LLC
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

/**
 * \page Overview Introduction to STS capabilities, usage, and design
 * The STS is an integral component of the ADARA system that provides live
 * translation of ADARA streams into Nexus files. The STS is typically installed
 * as an internet service that is launched (on-demand) by a request from an SMS
 * instance running on a beam line. The STS can also be run as a command-line
 * utility to manually translate ADARA files. The STS implements a number of SNS-
 * specific business rules including moving output files to specific locations
 * on the network files system (based on stream metadata) and notification
 * of workflow progress via AMQP messaging. These features can be disabled through
 * the CLI.
 * \subsection Usage
 * The STS can be run in either stream- or file-mode. File mode is activated by
 * specifying an input file with the -f option (see below). Without this option
 * the STS defaults to stream-mode and reads the ADARA stream from stdin. (When
 * configured as an internet service, xinetd maps the socket connection to stdin
 * when launching a new STS instance.)
 * \subsection Design
 * The STS program is a (mostly) single-threaded process that uses the NxGen class
 * to perform stream translation. The 'NxGen' class is a Nexus-adapter class derived
 * from the 'StreamParser' class. The StreamParser class performs ADARA-specific
 * stream parsing/buffering and 'publishes' extracted data through the IStreamAdapter
 * interface and supporting classes (see stsdefs.h). This interface is used internally
 * by the StreamParser class to push data to derived implementation through virtual
 * methods and abstract data classes. This architecture allows the ouptut adapter to be
 * changed to support different formats without requiring changes to the ADARA input
 * implementation. The ComBusTransMon class provides a simple interface to the AMQP
 * messaging system used by the SNS for monitoring and workflow notifications. The
 * main function (entry point in main.cpp) provides several capabilities including
 * the command-line interface, file-input mode, message interface management, output
 * file relocation, and SMS acknowledgement.
 */

#include <cstdlib>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "ADARA.h"
#include "ADARAUtils.h"
#include "TransCompletePkt.h"
#include "TraceException.h"
#include "NxGen.h"
#include "ComBusTransMon.h"

using namespace std;

// Global pid
pid_t g_pid = 0;

/**
 * @brief moveFile - Attempts to move a file to the specified path
 * @param a_source - Full path to source file
 * @param a_dest_path - Destination path (no filename)
 * @param a_dest_filename - Destination filename
 *
 * This method attempts to move the specified file to the specified path and
 * filename. If the operation fails, an exception is thrown. This method can
 * only succeed if the source and destination paths reside on the same phyiscal
 * device (uses a filesystem move command to avoid copying data).
 */
void
moveFile( const string &a_source, const string &a_dest_path, const string &a_dest_filename )
{
    try
    {
        boost::filesystem::create_directories( boost::filesystem::path( a_dest_path ));
        boost::filesystem::remove( boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
        boost::filesystem::rename( boost::filesystem::path( a_source ), boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "Move of " << a_source << " to " << a_dest_path << "/" << a_dest_filename << " failed. {" << e.what() << "}" )
    }
}


/**
 * @brief main - Entry point of STS process
 * @param argc - Number of CLI arguments
 * @param argv - Array of CLI command/parameter strings
 * @return 0 on success, 1 on error
 */
int main( int argc, char** argv )
{
    int                         infd = 0;
    int                         outfd = 1;
    STS::TranslationStatusCode  sms_code = STS::TS_SUCCESS;
    string                      sms_reason;
    bool                        interact;
    string                      work_path;
    string                      base_path;
    unsigned long               chunk_size;
    unsigned short              evt_buf_size;
    unsigned short              anc_buf_size;
    unsigned long               cache_size;
    unsigned short              compression_level;
    NxGen                      *nxgen = 0;
    ComBusTransMon             *monitor = 0;
    string                      nexus_outfile;
    string                      adara_outfile;
    bool                        keep_temp = false;

    // Setup global syslog info
    g_pid = getpid();

    openlog( "sts", 0, LOG_DAEMON );
    syslog( LOG_INFO, "[%i] Started. STS ver: %s, common ver: %s, tag: %s", g_pid, STS_VERSION, ADARA::VERSION.c_str(), ADARA::TAG_NAME.c_str() );

    try
    {
        bool strict;
        bool move;
        bool verbose;
        bool gather_stats;
        bool suppress_adara;
        bool suppress_nexus;
        string broker_uri;
        string broker_user;
        string broker_pass;
        string domain;

        namespace po = boost::program_options;
        po::options_description options( "sts program options" );
        options.add_options()
                ("help,h", "show help")
                ("version", "show version number")
                ("interactive,i", po::bool_switch( &interact )->default_value( false ), "interactive mode")
                ("verbose,v", po::bool_switch( &verbose )->default_value( false ), "verbose output (interactive mode only)")
                ("strict,s", po::bool_switch( &strict )->default_value( false ), "enable strict protocol parsing")
                ("move,m", po::bool_switch( &move )->default_value( false ), "move output nexus file to cataloging location (forces strict parsing)")
                ("report,r", po::bool_switch( &gather_stats )->default_value( false ), "report stream statistics")
                ("no-nexus,n", po::bool_switch( &suppress_nexus )->default_value( false ), "suppress nexus output file generation")
                ("no-adara,a", po::bool_switch( &suppress_adara )->default_value( false ), "suppress adara output stream generation")
                ("keep-temp,k", po::bool_switch( &keep_temp )->default_value( false ), "do not delete temporary output files on translation or move failure")
                ("file,f",po::value<string>(),"read input from file instead of stdin")
                ("work-path,w",po::value<string>( &work_path ),"set path to working directory")
                ("base-path,b",po::value<string>( &base_path ),"set base cataloging path (none by defualt)")
                ("compression-level,c", po::value<unsigned short>( &compression_level )->default_value( 0 ), "set nexus compression level (0=off,9=max)")
                ("chunk-size", po::value<unsigned long>( &chunk_size )->default_value( 2048 ),"set hdf5 chunk size (in bytes)")
                ("cache-size", po::value<unsigned long>( &cache_size )->default_value( 1024 ),"set hdf5 cache size (in KB)")
                ("event-buf-size", po::value<unsigned short>( &evt_buf_size )->default_value( 20 ),"set event buffers to (in chunks)")
                ("anc-buf-size", po::value<unsigned short>( &anc_buf_size )->default_value( 2 ),"set ancillary buffers (in chunks)")
                ("broker_uri", po::value<string>( &broker_uri )->default_value( "" ), "set AMQP broker URI/IP address")
                ("broker_user", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
                ("broker_pass", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
                ("domain", po::value<string>( &domain )->default_value( "" ), "Override ComBus domain prefix (TEST ONLY)")
                ;


        po::variables_map opt_map;
        po::store( po::parse_command_line(argc,argv,options), opt_map );
        po::notify( opt_map );

        if ( opt_map.count( "help" ))
        {
            cout << options << endl;
            return STS::TS_TRANSIENT_ERROR;
        }
        else if ( opt_map.count( "version" ))
        {
            cout << STS_VERSION
                 << " (ADARA Common " << ADARA::VERSION
                 << ", Tag Name " << ADARA::TAG_NAME << ")" << endl;
            return STS::TS_TRANSIENT_ERROR;
        }

        // If user has requested cataloging, force sane options
        if ( move )
        {
            strict = true;
            suppress_adara = false;
            suppress_nexus = false;
        }

        // Can't support statistics display in interactive mode
        if ( gather_stats && !interact )
            gather_stats = false;

        if ( work_path.size() )
        {
            if ( work_path[work_path.size()-1] != '/' )
                work_path += "/";
        }

        string tempName = genTempName();

        if ( !suppress_adara )
            adara_outfile = work_path + tempName + ".adara";

        if ( !suppress_nexus )
            nexus_outfile = work_path + tempName + ".nxs";

        if ( interact && verbose )
        {
            cout << "sts information" << endl;
            cout << "  version       : " << STS_VERSION << endl;
            cout << "  ADARA version : " << ADARA::VERSION << endl;
            cout << "  tag name      : " << ADARA::TAG_NAME << endl;
            cout << "  nexus file    : " << nexus_outfile << endl;
            cout << "  adara file    : " << adara_outfile << endl;
            cout << "  strict        : " << ( move ? "yes" : "no" ) << endl;
            cout << "  work path     : " << work_path << endl;
            cout << "  base path     : " << base_path << endl;
            cout << "  move nexus    : " << ( move ? "yes" : "no" ) << endl;
            cout << "  chunk size    : " << chunk_size << " (bytes)" << endl;
            cout << "  cache size    : " << cache_size << " (bytes)" << endl;
            cout << "  evt buf size  : " << evt_buf_size << " (chunks)" << endl;
            cout << "  anc buf size  : " << anc_buf_size << " (chunks)" << endl;
            cout << "  comp lev      : " << compression_level <<  endl;
            cout << "  keep temp     : " << ( keep_temp ? "yes" : "no" ) << endl;
            cout << "  gather stats  : " << ( gather_stats ? "yes" : "no" ) << endl;
        }

        if ( opt_map.count( "file" ))
        {
            infd = open( opt_map["file"].as<string>().c_str(), O_RDONLY );
            if ( infd < 0 )
                throw std::runtime_error("Failed to open input file");
        }

        if ( infd >= 0 )
        {
            if ( !interact )
            {
                // In non-interactive mode, must hack around chatty HDF5 library: remap stdout and stderr to /dev/null
                outfd = dup( 1 );
                int nullfd = open( "/dev/null", O_RDWR );
                dup2( nullfd, 1 );
                dup2( nullfd, 2 );
            }

            nxgen = new NxGen( infd, adara_outfile, nexus_outfile, strict, gather_stats, chunk_size, evt_buf_size,
                            anc_buf_size, cache_size, compression_level );

            // Start ComBus monitor thread if not in interactive mode
            if ( !interact )
            {
                monitor = new ComBusTransMon();
                monitor->start( *nxgen,
                    broker_uri, broker_user, broker_pass, domain );
            }

            // Begin ADARA stream processing - does not return until recording ends
            nxgen->processStream();

            syslog( LOG_INFO, "[%i] Stream processing completed", g_pid );

            nxgen->dumpProcessingStatistics();

            // If we make it here, translation succeeded

            if ( move )
            {
                if ( base_path.empty() )
                    base_path = "/";
                else if ( base_path[base_path.length()-1] != '/')
                    base_path += "/";

                string cat_path = base_path + nxgen->getFacilityName() + "/" + nxgen->getBeamShortName() + "/" + nxgen->getProposalID() + "/";
                string cat_name = nxgen->getBeamShortName() + "_" + boost::lexical_cast<string>(nxgen->getRunNumber());

                // Try to move files
                moveFile( adara_outfile, cat_path + "adara", cat_name + ".adara" );
                moveFile( nexus_outfile, cat_path + "nexus", cat_name + ".nxs.h5" );

                string cat_nexus_file = cat_path + "nexus/" + cat_name + ".nxs.h5";

                syslog( LOG_INFO, "[%i] Successfully moved Nexus file to: %s", g_pid, cat_nexus_file.c_str() );

                // Send finished messages to ComBus AND workflow manager
                if ( monitor )
                    monitor->success( true, cat_nexus_file );
            }
            else
            {
                // Send finished messages to ComBus only
                if ( monitor )
                    monitor->success( false, nexus_outfile );
            }


            // Disable temp file deletion if translation / move succeeded
            keep_temp = true;

            // Output stream statistics if enabled
            if ( gather_stats )
                nxgen->printStats( cout );
        }
    }
    catch( TraceException &e )
    {
        // Generally, output errors are transient, others are permanent
        if ( e.getErrorCode() == STS::ERR_OUTPUT_FAILURE )
            sms_code = STS::TS_TRANSIENT_ERROR;
        else
            sms_code = STS::TS_PERM_ERROR;

        sms_reason = e.toString( true, true );

        syslog( LOG_INFO,
            "[%i] STS failed: %s", g_pid, sms_reason.c_str() );
    }
    catch( exception &e )
    {
        // Unexpected exception
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = e.what();

        syslog( LOG_INFO,
            "[%i] STS failed: Exception: %s", g_pid, sms_reason.c_str() );
    }
    catch( ... )
    {
        // Really unexpected exception
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = "Unhandled exception";

        syslog( LOG_INFO, "[%i] STS failed: Unknown exception.", g_pid );
    }

    if ( sms_code != STS::TS_SUCCESS )
        syslog( LOG_INFO, "[%i] STS failed: Translation failed. code: %u",
            g_pid, (unsigned int)sms_code );
    else
        syslog( LOG_INFO, "[%i] Translation succeeded", g_pid );

    if ( !interact )
    {
        if ( sms_code != STS::TS_SUCCESS && monitor )
        {
            monitor->failure( sms_code, sms_reason );
        }

        STS::TransCompletePkt ack_pkt( sms_code, sms_reason );
        uint32_t heartbeat_pkt[4] = {0,0x00400900,0,0};

        // Send ACK/NACK packet to SMS - go to extra effort to ensure the message is sent and
        // any errors are detected. The second write of 1 byte after the message is sent is
        // required due to limitations of tcp in detecting dropped connections. If the connection
        // has been lost, the first write may or may not detect an error, but the second write
        // (of the heartbeat packet) will fail.
        // Also, the connection must be shutdown properly with shutdown(), and we must wait for
        // the SMS to drop the connection when read() returns 0. This prevents the connection from
        // being reset before the data is actually sent.

        // Ignore SIGPIPE signals so we can get error codes from write()
        signal( SIGPIPE, SIG_IGN );

        bool send_status = false;

        std::string log_info;

        send_status |= Utils::sendBytes( outfd,
            ack_pkt.getMessageBuffer(), ack_pkt.getMessageLength(),
            log_info );

        if ( !send_status )
        {
            syslog( LOG_INFO,
                "[%i] STS failed: Translation Complete Message: %s",
                g_pid, log_info.c_str() );
        }

        send_status |= Utils::sendBytes( outfd,
            (char*)heartbeat_pkt, sizeof(heartbeat_pkt), log_info );

        if ( send_status )
        {
            syslog( LOG_INFO,
                "[%i] Notified SMS of translation status", g_pid );
        }
        else
        {
            syslog( LOG_INFO,
                "[%i] STS failed: Translation Complete/Heartbeat Msg: %s",
                g_pid, log_info.c_str() );
        }

        // Request shutdown of write socket - should initiate buffer flush
        shutdown( outfd, SHUT_WR );

        // Read-spin on infd until connection is closed by SMS
        char buf[1];
        ssize_t ec;
        long cnt = 0;
        while ( 1 )
        {
            // NOTE: This is Standard C Library read()... ;-o
            ec = ::read( infd, buf, 1 );
            if ( ec > 0 )
            {
                cnt += ec;
                continue;
            }
            else if ( ec == 0 )
                break;
            else
            {
                switch ( ec )
                {
                    case EINTR:
                    case EAGAIN:
                        continue;
                    default:
                        break;
                }
            }
        }

        // Log any extra data read from socket (probably DataDonePkt... :)
        if ( cnt > 0 )
        {
            syslog( LOG_INFO,
                "[%i] Warning: Extra Data Read from SMS socket cnt=%ld",
                g_pid, cnt );
        }
    }
    else if ( sms_code != STS::TS_SUCCESS )
    {
        cout << sms_reason << endl;
    }

    syslog( LOG_INFO, "[%i] Cleaning up", g_pid );

    if ( monitor )
        delete monitor;

    if ( nxgen )
        delete nxgen;

    // Clean-up temp output files if translation or move failed
    if ( !keep_temp && !nexus_outfile.empty() )
    {
        try {
            boost::filesystem::remove(
                boost::filesystem::path( nexus_outfile ));
        }
        catch( ... )
        {
            syslog( LOG_INFO, "[%i] Error Cleaning Up NeXus File at %s.",
                g_pid, nexus_outfile.c_str() );
        }
    }

    if ( !keep_temp && !adara_outfile.empty() )
    {
        try {
            boost::filesystem::remove(
                boost::filesystem::path( adara_outfile ));
        }
        catch( ... )
        {
            syslog( LOG_INFO, "[%i] Error Cleaning Up ADARA File at %s.",
                g_pid, adara_outfile.c_str() );
        }
    }

    syslog( LOG_INFO, "[%i] Process exiting", g_pid );

    return sms_code != STS::TS_SUCCESS;
}

// vim: expandtab

