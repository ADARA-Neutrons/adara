
//
// SNS ADARA SYSTEM - Stream Translation Client (STC)
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
 * \page Overview Introduction to STC capabilities, usage, and design
 * The STC is an integral component of the ADARA system that provides live
 * translation of ADARA streams into Nexus files. The STC is typically installed
 * as an internet service that is launched (on-demand) by a request from an SMS
 * instance running on a beam line. The STC can also be run as a command-line
 * utility to manually translate ADARA files. The STC implements a number of SNS-
 * specific business rules including moving output files to specific locations
 * on the network files system (based on stream metadata) and notification
 * of workflow progress via AMQP messaging. These features can be disabled through
 * the CLI.
 * \subsection Usage
 * The STC can be run in either stream- or file-mode. File mode is activated by
 * specifying an input file with the -f option (see below). Without this option
 * the STC defaults to stream-mode and reads the ADARA stream from stdin. (When
 * configured as an internet service, xinetd maps the socket connection to stdin
 * when launching a new STC instance.)
 * \subsection Design
 * The STC program is a (mostly) single-threaded process that uses the NxGen class
 * to perform stream translation. The 'NxGen' class is a Nexus-adapter class derived
 * from the 'StreamParser' class. The StreamParser class performs ADARA-specific
 * stream parsing/buffering and 'publishes' extracted data through the IStreamAdapter
 * interface and supporting classes (see stcdefs.h). This interface is used internally
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
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
 * @brief renameFile - Attempts to rename a file to the specified path
 * @param a_source - Full path to source file
 * @param a_dest_path - Destination path (no filename)
 * @param a_dest_filename - Destination filename
 *
 * This method attempts to rename the specified file to the specified path
 * and filename. If the operation fails, an exception is thrown. This method
 * can only succeed if the source and destination paths reside on the same
 * physical device (uses a filesystem rename command to avoid copying data).
 */
void
renameFile( const string &a_source,
        const string &a_dest_path, const string &a_dest_filename )
{
    try
    {
        boost::filesystem::create_directories(
            boost::filesystem::path( a_dest_path ) );
        boost::filesystem::remove(
            boost::filesystem::path( a_dest_path + "/" + a_dest_filename )
        );
        boost::filesystem::rename(
            boost::filesystem::path( a_source ),
            boost::filesystem::path( a_dest_path + "/" + a_dest_filename )
        );
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
            "Move of " << a_source << " to "
                << a_dest_path << "/" << a_dest_filename
                << " failed. {" << e.what() << "}" )
    }
}

/**
 * @brief moveFile - Attempts to move a file to the specified path
 * @param a_source - Full path to source file
 * @param a_dest_path - Destination path (no filename)
 * @param a_dest_filename - Destination filename
 *
 * This method attempts to move the specified file to the specified path
 * and filename. If the operation fails, an exception is thrown. This method
 * actually moves the file instead of just renaming it, so it can succeed
 * even if the source and destination paths do not reside on the same
 * physical device.
 */
void
moveFile( const string &a_source,
        const string &a_dest_path, const string &a_dest_filename )
{
    try
    {
        boost::filesystem::create_directories(
            boost::filesystem::path( a_dest_path ) );
        boost::filesystem::remove(
            boost::filesystem::path( a_dest_path + "/" + a_dest_filename )
        );
        boost::filesystem::copy_file(
            boost::filesystem::path( a_source ),
            boost::filesystem::path( a_dest_path + "/" + a_dest_filename )
        );
        boost::filesystem::remove( boost::filesystem::path( a_source ) );
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
            "Move of " << a_source << " to "
                << a_dest_path << "/" << a_dest_filename
                << " failed. {" << e.what() << "}" )
    }
}

/**
 * @brief long_syslog - Split a Long Log Message into Multiple Pieces
 * @param a_label - Text Label for Each Log Message
 * @param a_msg - Full Log Message Text
 *
 * This method Splits a Long Log Message into Multiple Pieces,
 * so that (R)Syslog doesn't just Truncate some Long Output
 * at 8K Octets (2048 characters), by default.
 */
void
long_syslog( const string &a_label, const string &a_msg )
{
    size_t msg_len = a_msg.length();
    size_t msg_index = 0;
    size_t msg_chunk = 1536; // Leave Room for Special Characters... ;-b
    bool done = false;
    bool cont = false;

    while ( !done )
    {
        // Compute (Possibly) "Partial" Length for Next Log Message...
        size_t len = msg_len;
        if ( len > msg_chunk ) {
            len = msg_chunk;
            msg_len -= msg_chunk;
        }
        else {
            done = true;
        }

        syslog( LOG_INFO, "[%i] %s%s%s%s", g_pid,
            a_label.c_str(),
            ( ( cont ) ? " (Cont.) ..." : " " ),
            a_msg.substr( msg_index, len ).c_str(),
            ( ( done ) ? "" : "..." ) );
        give_syslog_a_chance;

        msg_index += len;

        cont = true;
    }
}

/**
 * @brief main - Entry point of STC process
 * @param argc - Number of CLI arguments
 * @param argv - Array of CLI command/parameter strings
 * @return 0 on success, 1 on error
 */
int main( int argc, char** argv )
{
    int                         infd = 0;
    int                         outfd = 1;
    STC::TranslationStatusCode  sms_code = STC::TS_SUCCESS;
    string                      sms_reason;
    bool                        interact;
    string                      work_root;
    string                      work_base;
    string                      work_dir;
    string                      work_path; // Obsolete... (work_root/base)
    string                      base_path;
    string                      config_file;
    unsigned long               chunk_size; // in Dataset Elements! :-O
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

    openlog( "stc", 0, LOG_DAEMON );
    setlogmask( LOG_UPTO( LOG_DEBUG ) );
    syslog( LOG_INFO,
        "[%i] %s. %s Version %s, %s Version %s, %s Version %s, Tag %s",
        g_pid, "Started", "STC", STC_VERSION,
        "ADARA Common", ADARA::VERSION.c_str(),
        "ComBus", ADARA::ComBus::VERSION.c_str(),
        ADARA::TAG_NAME.c_str() );
    give_syslog_a_chance;

    try
    {
        bool strict;
        bool move;
        bool doRename;
        uint32_t verbose_level;
        bool verbose;
        bool gather_stats;
        bool suppress_adara;
        bool suppress_nexus;
        string broker_uri;
        string broker_user;
        string broker_pass;
        string domain;

        namespace po = boost::program_options;
        po::options_description options( "stc program options" );
        options.add_options()
                ("help,h", "show help")
                ("version", "show version number")
                ("interactive,i", po::bool_switch( &interact )->default_value( false ), "interactive mode")
                ("verbose_level", po::value<uint32_t>( &verbose_level )->default_value( 0 ), "verbose output logging level (uint32)")
                ("verbose,v", po::bool_switch( &verbose )->default_value( false ), "verbose output mode (deprecated)")
                ("strict,s", po::bool_switch( &strict )->default_value( false ), "enable strict protocol parsing")
                ("move,m", po::bool_switch( &move )->default_value( false ), "move output nexus file to cataloging location (forces strict parsing)")
                ("report,r", po::bool_switch( &gather_stats )->default_value( false ), "report stream statistics")
                ("no-nexus,n", po::bool_switch( &suppress_nexus )->default_value( false ), "suppress nexus output file generation")
                ("no-adara,a", po::bool_switch( &suppress_adara )->default_value( false ), "suppress adara output stream generation")
                ("keep-temp,k", po::bool_switch( &keep_temp )->default_value( false ), "do not delete temporary output files on translation or move failure")
                ("file,f",po::value<string>(),"read input from file instead of stdin")
                ("work-root",po::value<string>( &work_root ),"set root path to construct working directory")
                ("work-base",po::value<string>( &work_base ),"set base path to construct working directory")
                ("work-path,w",po::value<string>( &work_path ),"set path to working directory")
                ("base-path,b",po::value<string>( &base_path ),"set base cataloging path (none by defualt)")
                ("config,C",po::value<string>( &config_file ),"set STC Config File path (none by defualt)")
                ("compression-level,c", po::value<unsigned short>( &compression_level )->default_value( 0 ), "set nexus compression level (0=off,9=max)")
                ("chunk-size", po::value<unsigned long>( &chunk_size )->default_value( 49152 ),"set hdf5 chunk size (in Dataset Elements!)")
                ("cache-size", po::value<unsigned long>( &cache_size )->default_value( 1024 ),"set hdf5 cache size (in KB)")
                ("event-buf-size", po::value<unsigned short>( &evt_buf_size )->default_value( 200 ),"set event buffers to (in chunks)")
                ("anc-buf-size", po::value<unsigned short>( &anc_buf_size )->default_value( 20 ),"set ancillary buffers (in chunks)")
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
            return STC::TS_TRANSIENT_ERROR;
        }
        else if ( opt_map.count( "version" ))
        {
            cout << STC_VERSION
                 << " (ADARA Common " << ADARA::VERSION
                 << ", ComBus Version " << ADARA::ComBus::VERSION
                 << ", Tag " << ADARA::TAG_NAME << ")" << endl;
            return STC::TS_TRANSIENT_ERROR;
        }

        // Apply "Verbose" Boolean Option (Now Deprecated)
        // to Set New Verbose Logging Level... ;-D
        if ( verbose_level == 0 && verbose )
        {
            // Formerly "--verbose" Triggered "Level 2" Verbosity...
            // ("Effectively", Before there was a "Level 1"... ;-D)
            verbose_level = 2;

            syslog( LOG_INFO, "[%i] %s %s %u.",
                g_pid, "Applying (Deprecated) \"Verbose\" Option",
                "to Set Verbose Logging Level to", verbose_level );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_INFO, "[%i] %s %u.",
                g_pid, "STC Verbose Logging Level Set to", verbose_level );
            give_syslog_a_chance;
        }

        // Log Chunk/Buffer Sizes...
        syslog( LOG_INFO,
            "[%i] %s %s=%lu %s=%u (%lu/0x%lx) %s=%u (%lu/0x%lx).",
            g_pid, "STC Data Buffering",
            "chunk_size", chunk_size,
            "evt_buf_size", evt_buf_size,
            chunk_size * evt_buf_size, chunk_size * evt_buf_size,
            "anc_buf_size", anc_buf_size,
            chunk_size * anc_buf_size, chunk_size * anc_buf_size );
        give_syslog_a_chance;

        // If user has requested cataloging, force sane options
        if ( move )
        {
            strict = true;
            // Don't Force ADARA Stream Generation...
            // - Raw Data Files Now Cached at SMS/DAQ1 Machines for Replay
            // suppress_adara = false;
            suppress_nexus = false;
        }

        // Can't support statistics display when _Not_ in interactive mode
        // (Normally, _Only_ response to SMS is written to stdout...!)
        if ( gather_stats && !interact )
            gather_stats = false;

        //
        // *** New STC Work Directory Specification:
        //    ${WORK_ROOT}/${FACILITY}/${BEAMLINE}/${WORK_BASE}
        // - if either "work_root" and/or "work_base" are specified,
        // these values _Subsume_ any "work_path" value and activate the
        // new "delayed gratification" mode... ;-D
        //    -> where we _Wait_ until we know the FACILITY and BEAMLINE
        //    to actually construct the full Working Directory path.
        //

        if ( work_root.size() || work_base.size() )
        {
            // Clear Out (Now Obsolete) "Work Path",
            // Favor New "Delayed Gratification: Mode. ;-D
            work_path.clear();

            // Make Sure Work Root Ends in "/"...
            // (It might be _Just_ "/"...! ;-D)
            if ( work_root.size() == 0
                    || work_root[ work_root.size() - 1 ] != '/' )
            {
                work_root += "/";
            }

            syslog( LOG_INFO, "[%i] Working Directory Root set to: [%s]",
                g_pid, work_root.c_str() );
            give_syslog_a_chance;

            syslog( LOG_INFO, "[%i] Working Directory Base set to: [%s]",
                g_pid, work_base.c_str() );
            give_syslog_a_chance;
        }

        if ( work_path.size() )
        {
            // Make Sure Work Path Ends in "/"...
            if ( work_path[ work_path.size() - 1 ] != '/' )
                work_path += "/";

            syslog( LOG_INFO, "[%i] Working Directory Path set to: [%s]",
                g_pid, work_path.c_str() );
            give_syslog_a_chance;
        }

        string tempName = genTempName();

        if ( !suppress_adara )
            adara_outfile = work_path + tempName + ".adara";
                // "work_path" could be empty...

        if ( !suppress_nexus )
            nexus_outfile = work_path + tempName + ".nxs";
                // "work_path" could be empty...

        if ( config_file.size() )
        {
            struct stat statbuf;
            int err = stat(config_file.c_str(), &statbuf);
            if ( err )
            {
                syslog( LOG_ERR,
                    "[%i] Stat Error on Config File: %s, %s - Ignoring...",
                    g_pid, config_file.c_str(), strerror(errno) );
                give_syslog_a_chance;
                config_file.clear();
            }
            else if ( statbuf.st_size == 0 )
            {
                syslog( LOG_ERR,
                    "[%i] Empty Config File: %s - Ignoring...",
                    g_pid, config_file.c_str() );
                give_syslog_a_chance;
                config_file.clear();
            }
            else
            {
                syslog( LOG_INFO, "[%i] STC Config File Specified: %s",
                    g_pid, config_file.c_str() );
                give_syslog_a_chance;
            }
        }
        else {
            syslog( LOG_ERR, "[%i] No STC Config File Specified", g_pid );
            give_syslog_a_chance;
        }

        // Only Dump Verbose STC Settings in Interactive Mode...!
        // (else screws up the STC-to-SMS return status...! ;-D)
        if ( interact && verbose_level > 0 )
        {
            cout << "STC Information:" << endl;
            cout << "   STC Version    : "
                 << STC_VERSION << endl;
            cout << "   ADARA Common   : "
                 << ADARA::VERSION << endl;
            cout << "   ComBus Version : "
                 << ADARA::ComBus::VERSION << endl;
            cout << "   ADARA Tag Name : "
                 << ADARA::TAG_NAME << endl;
            cout << "   NeXus File     : "
                 << nexus_outfile << endl;
            cout << "   ADARA File     : "
                 << adara_outfile << endl;
            cout << "   Strict         : "
                 << ( move ? "yes" : "no" ) << endl;
            cout << "   Work Root      : "
                 << work_root << endl;
            cout << "   Work Base      : "
                 << work_base << endl;
            cout << "  (Work Path      : "
                 << work_path << ")" << endl;
            cout << "   Base Path      : "
                 << base_path << endl;
            cout << "   Config File    : "
                 << config_file << endl;
            cout << "   Move NeXus     : "
                 << ( move ? "yes" : "no" ) << endl;
            cout << "   Chunk Size     : "
                 << chunk_size << " (Dataset Elements!)" << endl;
            cout << "   Cache Size     : "
                 << cache_size << " (bytes)" << endl;
            cout << "   Event Buf Size : "
                 << evt_buf_size << " (chunks)" << endl;
            cout << "   Ancil Buf Size : "
                 << anc_buf_size << " (chunks)" << endl;
            cout << "   Compress Level : "
                 << compression_level <<  endl;
            cout << "   Keep Temp      : "
                 << ( keep_temp ? "yes" : "no" ) << endl;
            cout << "   Gather Stats   : "
                 << ( gather_stats ? "yes" : "no" ) << endl;
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
                // In non-interactive mode, must hack around chatty
                // HDF5 library: remap stdout and stderr to /dev/null
                outfd = dup( 1 );
                int nullfd = open( "/dev/null", O_RDWR );
                dup2( nullfd, 1 );
                dup2( nullfd, 2 );
            }

            nxgen = new NxGen( infd,
                work_root, work_base, adara_outfile, nexus_outfile,
                config_file, strict, gather_stats, chunk_size,
                evt_buf_size, anc_buf_size, cache_size, compression_level,
                verbose_level );

            // Start ComBus monitor thread (even in interactive mode!)
            monitor = new ComBusTransMon();
            monitor->start( *nxgen,
                broker_uri, broker_user, broker_pass, domain );

            // Begin ADARA stream processing
            //    - does not return until recording ends
            nxgen->processStream();

            syslog( LOG_INFO, "[%i] Stream processing completed", g_pid );
            give_syslog_a_chance;

            nxgen->dumpProcessingStatistics();

            // If we make it here, translation succeeded
            // Move or Rename the Destination NeXus/ADARA Files

            work_dir = nxgen->getWorkingDirectory();

            doRename = nxgen->getDoRename();

            syslog( LOG_INFO, "[%i] Working Directory: [%s] (doRename=%s)",
                g_pid, work_dir.c_str(), ( doRename ? "true" : "false" ) );
            give_syslog_a_chance;

            string cat_nexus_file;

            if ( move )
            {
                if ( base_path.empty() )
                    base_path = "/";
                else if ( base_path[base_path.length()-1] != '/')
                    base_path += "/";

                string cat_path = base_path + nxgen->getFacilityName()
                    + "/" + nxgen->getBeamShortName()
                    + "/" + nxgen->getProposalID() + "/";

                string cat_name = nxgen->getBeamShortName() + "_"
                    + boost::lexical_cast<string>(nxgen->getRunNumber());

                cat_nexus_file = cat_path + "nexus/" + cat_name + ".nxs.h5";

                // Try to "move" (rename) files
                if ( !adara_outfile.empty() )
                {
                    if ( doRename )
                    {
                        renameFile( work_dir + "/" + adara_outfile,
                            cat_path + "adara", cat_name + ".adara" );

                        syslog( LOG_INFO,
                            "[%i] Successfully %s ADARA file to: %s%s%s%s",
                            g_pid, "Renamed", cat_path.c_str(), "adara/",
                            cat_name.c_str(), ".adara" );
                        give_syslog_a_chance;
                    }
                    else
                    {
                        moveFile( work_dir + "/" + adara_outfile,
                            cat_path + "adara", cat_name + ".adara" );

                        syslog( LOG_INFO,
                            "[%i] Successfully %s ADARA file to: %s%s%s%s",
                            g_pid, "Moved", cat_path.c_str(), "adara/",
                            cat_name.c_str(), ".adara" );
                        give_syslog_a_chance;
                    }
                }
                if ( !nexus_outfile.empty() )
                {
                    if ( doRename )
                    {
                        renameFile( work_dir + "/" + nexus_outfile,
                            cat_path + "nexus", cat_name + ".nxs.h5" );

                        syslog( LOG_INFO,
                            "[%i] Successfully Renamed Nexus file to: %s",
                            g_pid, cat_nexus_file.c_str() );
                        give_syslog_a_chance;
                    }
                    else
                    {
                        moveFile( work_dir + "/" + nexus_outfile,
                            cat_path + "nexus", cat_name + ".nxs.h5" );

                        syslog( LOG_INFO,
                            "[%i] Successfully Moved Nexus file to: %s",
                            g_pid, cat_nexus_file.c_str() );
                        give_syslog_a_chance;
                    }
                }
            }

            // Now Execute Any Pre-Post-Autoreduction Commands
            // As Specified in the STC Config File...! ;-D

            if ( nxgen )
            {
                uint32_t status = nxgen->executePrePostCommands();

                if ( status != 0 )
                {
                    THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
                        "Error Executing Pre-PostProcessing Commands:"
                            << " status=" << status )
                }
            }

            // NOW Send ComBus Final Notify/Trigger Message...
            // (_After_ Any Potential Pre-Post-Autoreduction Commands!)

            if ( move )
            {
                // Send finished messages to ComBus AND workflow manager
                if ( monitor )
                    monitor->success( true, cat_nexus_file );
            }
            else
            {
                // Send finished messages to ComBus only
                if ( monitor )
                {
                    monitor->success( false,
                        work_dir + "/" + nexus_outfile );
                }
            }

            // Disable temp file deletion if translation/rename succeeded
            keep_temp = true;

            // Output stream statistics if enabled
            if ( gather_stats )
                nxgen->printStats( cout );
        }
    }
    catch( TraceException &e )
    {
        // Generally, output errors are transient, others are permanent
        if ( e.getErrorCode() == STC::ERR_OUTPUT_FAILURE )
            sms_code = STC::TS_TRANSIENT_ERROR;
        else
            sms_code = STC::TS_PERM_ERROR;

        if ( nxgen != 0 )
        {
            sms_reason = nxgen->getFacilityName()
                + " " + nxgen->getBeamLongName()
                + " (" + nxgen->getBeamShortName() + ")"
                + " Run "
                    + boost::lexical_cast<string>(nxgen->getRunNumber())
                + " " + nxgen->getProposalID() + " - ";
        }

        sms_reason += e.toString( true, true );

        long_syslog( "STC failed:", sms_reason );
    }
    catch( exception &e )
    {
        // Unexpected exception
        sms_code = STC::TS_PERM_ERROR;

        if ( nxgen != 0 )
        {
            sms_reason = nxgen->getFacilityName()
                + " " + nxgen->getBeamLongName()
                + " (" + nxgen->getBeamShortName() + ")"
                + " Run "
                    + boost::lexical_cast<string>(nxgen->getRunNumber())
                + " " + nxgen->getProposalID() + " - ";
        }

        sms_reason += e.what();

        long_syslog( "STC failed: Exception:", sms_reason );
    }
    catch( ... )
    {
        // Really unexpected exception
        sms_code = STC::TS_PERM_ERROR;

        if ( nxgen != 0 )
        {
            sms_reason = nxgen->getFacilityName()
                + " " + nxgen->getBeamLongName()
                + " (" + nxgen->getBeamShortName() + ")"
                + " Run "
                    + boost::lexical_cast<string>(nxgen->getRunNumber())
                + " " + nxgen->getProposalID() + " - ";
        }

        sms_reason += "Unhandled exception";

        syslog( LOG_INFO, "[%i] STC failed: Unknown exception.", g_pid );
    }

    string run_desc = "";
    if ( nxgen != 0 )
    {
        run_desc = " of " + nxgen->getFacilityName() + " "
            + nxgen->getBeamShortName() + "_"
            + boost::lexical_cast<string>(nxgen->getRunNumber())
            + " (" + nxgen->getProposalID() + ")";
    }

    if ( sms_code != STC::TS_SUCCESS )
    {
        syslog( LOG_INFO, "[%i] STC failed: Translation%s failed. code: %u",
            g_pid, run_desc.c_str(), (unsigned int)sms_code );
        give_syslog_a_chance;

        // Notify ComBus Monitor of Failure...
        if ( monitor )
            monitor->failure( sms_code, sms_reason );
    }
    else
    {
        syslog( LOG_INFO, "[%i] Translation%s succeeded",
            g_pid, run_desc.c_str() );
        give_syslog_a_chance;
    }

    if ( !interact )
    {
        STC::TransCompletePkt ack_pkt( sms_code, sms_reason );
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
                "[%i] STC failed: Translation Complete Message: %s",
                g_pid, log_info.c_str() );
            give_syslog_a_chance;
        }

        send_status |= Utils::sendBytes( outfd,
            (char*)heartbeat_pkt, sizeof(heartbeat_pkt), log_info );

        if ( send_status )
        {
            syslog( LOG_INFO,
                "[%i] Notified SMS of translation status", g_pid );
            give_syslog_a_chance;
        }
        else
        {
            syslog( LOG_INFO,
                "[%i] STC failed: Translation Complete/Heartbeat Msg: %s",
                g_pid, log_info.c_str() );
            give_syslog_a_chance;
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
            give_syslog_a_chance;
        }
    }
    else if ( sms_code != STC::TS_SUCCESS )
    {
        cout << sms_reason << endl;
    }

    // Clean Up, We're Done...! :-D

    syslog( LOG_INFO, "[%i] Cleaning up", g_pid );
    give_syslog_a_chance;

    if ( monitor )
        delete monitor;

    if ( nxgen )
        delete nxgen;

    // Clean-up temp output files if translation or rename failed
    if ( !keep_temp && !nexus_outfile.empty() )
    {
        try {
            boost::filesystem::remove(
                boost::filesystem::path( work_dir + "/" + nexus_outfile ) );
        }
        catch( ... )
        {
            syslog( LOG_INFO,
                "[%i] Error Cleaning Up NeXus File at %s/%s.",
                g_pid, work_dir.c_str(), nexus_outfile.c_str() );
            give_syslog_a_chance;
        }
    }

    if ( !keep_temp && !adara_outfile.empty() )
    {
        try {
            boost::filesystem::remove(
                boost::filesystem::path( work_dir + "/" + adara_outfile ) );
        }
        catch( ... )
        {
            syslog( LOG_INFO,
                "[%i] Error Cleaning Up ADARA File at %s/%s.",
                g_pid, work_dir.c_str(), adara_outfile.c_str() );
            give_syslog_a_chance;
        }
    }

    syslog( LOG_INFO, "[%i] Process exiting", g_pid );
    give_syslog_a_chance;

    closelog();

    return sms_code != STC::TS_SUCCESS;
}

// vim: expandtab

