
//
// SNS ADARA SYSTEM - PVSD (PV Streamer Daemon)
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

#include <iostream>
#include <boost/program_options.hpp>
#include <syslog.h>
#include <stdint.h>
#include <signal.h>

#include "ConfigManager.h"
#include "StreamService.h"
#include "EPICS_InputAdapter.h"
#include "ADARA_OutputAdapter.h"
#include "ComBus.h"
#include "TraceException.h"
#include "ADARA.h"
#include "ADARAUtils.h"

using namespace std;
using namespace PVS;

using namespace std;

#define PVSD_VERSION "1.7.8"

bool g_active = true;
bool g_child_signal = false;
int g_child_code = 0;


/// Used to catch shutdown/interrupt signals for clean shutdown
void
signalHandlerExit( int UNUSED(a_signal) )
{
    g_active = false;
}


/// Handles child signal during daemonization
void
signalHandlerChild( int UNUSED(a_signal), siginfo_t *info,
        void *UNUSED(data) )
{
    g_child_signal = true;
    g_child_code = info->si_value.sival_int;
}


/// Sends signal to parent process during daemonization
void
signalParent( int ret_code )
{
    pid_t pid = getppid();
    sigval_t data;

    data.sival_int = ret_code;
    if ( sigqueue( pid, SIGUSR1, data ) < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "PVSD ERROR: Unable to signal parent: %s",
            strerror(e) );
    }
}


/// Function to daemonize the PVStreamer process
void
daemonize()
{
    pid_t pid = fork();
    if ( pid < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "PVSD ERROR: %s: Unable to fork: %s",
            "daemonize()", strerror(e) );
        exit(1);
    }

    if ( pid ) // Grandparent process, wait for parent status
    {
        while ( !g_child_signal )
            sleep(1);

        exit( g_child_code );
    }

    // We're the child process, become a daemon.
    // Create a new session, then fork and have the parent exit,
    // ensuring we are not the leader of the session -- we don't
    // want a controlling terminal.
    if ( setsid() < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "PVSD ERROR: %s: Unable to setsid: %s",
            "daemonize()", strerror(e) );
        exit(1);
    }

    pid = fork();
    if ( pid < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "PVSD ERROR: %s: Second fork failed: %s",
            "daemonize()", strerror(e) );
        exit(1);
    }
    else if ( pid )
    {
        // Parent process, wait for child status
        while ( !g_child_signal )
            sleep(1);

        // Signal grandparent
        signalParent( g_child_code );
        exit( g_child_code );
    }

    // We're the second child now; we are in our own session, but
    // are not the leader of it. Let initialization continue.

    // Close stdin, stdout, sterr
    close( STDIN_FILENO );
    close( STDOUT_FILENO );
    close( STDERR_FILENO );

    // Reopen log
    openlog( "pvsd", 0, LOG_DAEMON );
    syslog( LOG_INFO,
        "PVSD Daemon %s Starting. (ADARA Common %s, ComBus %s, Tag %s)",
        PVSD_VERSION, ::ADARA::VERSION.c_str(),
        ::ADARA::ComBus::VERSION.c_str(), ::ADARA::TAG_NAME.c_str() );

    // Chdir to "/"
    if ( chdir("/") < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "PVSD ERROR: %s: Chdir failed: %s",
            "daemonize()", strerror(e) );
        exit(1);
    }
}


/**
 * @brief Entry point for PVStreamer deaomon
 * @param argc - CLI argument count
 * @param argv - CLI arguments
 * @return Always returns 0
 */
int main(int argc, char *argv[])
{
    int ret_code = 0;

    // Initialize SysLog
    openlog( "pvsd", 0, LOG_DAEMON );
    syslog( LOG_INFO, "PVSD %s Starting (%s %s, %s %s, %s %s)",
        PVSD_VERSION,
        "ADARA Common", ::ADARA::VERSION.c_str(),
        "ComBus", ::ADARA::ComBus::VERSION.c_str(),
        "Tag", ::ADARA::TAG_NAME.c_str() );

    // Setup signal handlers to catch all termination handlers so we can
    // implement orderly shutdown.

    struct sigaction new_action, old_action;

    new_action.sa_handler = signalHandlerExit;
    sigemptyset( &new_action.sa_mask );
    new_action.sa_flags = 0;

    sigaction (SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction( SIGINT, &new_action, NULL );

    sigaction (SIGHUP, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction( SIGHUP, &new_action, NULL );

    sigaction (SIGTERM, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction( SIGTERM, &new_action, NULL );

    sigaction (SIGQUIT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction( SIGQUIT, &new_action, NULL );

    // Attach SIGUSR handler for daemon initialization
    new_action.sa_handler = 0;
    new_action.sa_sigaction = signalHandlerChild;
    new_action.sa_flags = SA_SIGINFO;
    sigaction( SIGUSR1, &new_action, NULL );

    uint32_t        port;
    uint32_t        heartbeat;
    bool            no_heartbeat_pv;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    string          domain;
    string          epics_cfg;
    uint32_t        offset;
    time_t          timeout;
    uint32_t        pid;
    bool            track_logged = false;
    bool            daemon = false;
    ::ADARA::ComBus::Connection *combus = 0;
    PVS::ADARA::OutputAdapter *output = 0;
    PVS::EPICS::InputAdapter *input = 0;

    // Parse program options

    namespace po = boost::program_options;
    po::options_description options( "pvsd server options" );
    options.add_options()
            ("help,h", "show help")
            ("version,v", "show version number")
            ("port,p", po::value<uint32_t>( &port )->default_value( 31416 ), "set client port")
            ("hb", po::value<uint32_t>( &heartbeat )->default_value( 2000 ), "set ADARA heartbeat period (msec)")
            ("no_heartbeat_pv", po::value<bool>( &no_heartbeat_pv )->default_value( false ), "turn off PVSD Heartbeat Device/PV (bool)")
            ("domain,d", po::value<string>( &domain )->default_value( "" ), "set communication domain prefix (EPICS/ComBus)")
            ("pid", po::value<uint32_t>( &pid )->default_value( 0 ), "set combus process identifier")
            ("broker_uri,b", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user,u", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pw,w", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
            ("config,c", po::value<string>( &epics_cfg )->default_value( "beamline.xml" ), "set path to epics configuration file")
            ("offset,o", po::value<uint32_t>( &offset )->default_value( 0 ), "set device ID offset")
            ("timeout,t", po::value<time_t>( &timeout )->default_value( 60 ), "set device Init timeout (seconds)")
            ("track_log", "track logged PVs only (default is all)")
            ("daemon", "Run as background daemon")
            ;

    po::variables_map opt_map;
    po::store( po::parse_command_line(argc,argv,options), opt_map );
    po::notify( opt_map );

    // Process options

    if ( opt_map.count( "track_log" ) )
        track_logged = true;

    if ( opt_map.count( "daemon" ) )
        daemon = true;

    if ( opt_map.count( "help" ) && !daemon )
    {
        cout << options << endl;
        return 0;
    }
    else if ( opt_map.count( "version" ) && !daemon )
    {
        cout << PVSD_VERSION
            << " (ADARA Common " << ::ADARA::VERSION
            << ", ComBus" << ::ADARA::ComBus::VERSION
            << ", Tag" << ::ADARA::TAG_NAME
            << ")" << endl;
        return 0;
    }

    if ( !opt_map.count( "domain" ) )
    {
        syslog( LOG_WARNING,
            "%s %s: No communication domain specified - probably an error.",
            "PVSD ERROR:", "main()" );
    }

    if ( no_heartbeat_pv )
    {
        syslog( LOG_WARNING,
            "%s %s: PVSD Heartbeat Device/PV Deactivated on Command Line!",
            "PVSD ERROR:", "main()" );
    }

    // Parent process will exit in this call
    if ( daemon )
        daemonize();

    try
    {
        // Create ComBus instance
        combus = new ::ADARA::ComBus::Connection(
            domain, "PVSD", pid, broker_uri, broker_user, broker_pass,
            "PVSD ComBus", "PVSD ERROR ComBus" );

        // Create and start protocol streamer
        StreamService   streamer( 100, offset );

        // Attach ADARA output adapter
        output = new PVS::ADARA::OutputAdapter( streamer, port,
            heartbeat, no_heartbeat_pv );

        // Create and attach EPICS input adapter
        input = new PVS::EPICS::InputAdapter( streamer, epics_cfg,
            track_logged, timeout );

        // If we mad it here as a daemon, signal parent that all is well
        if ( daemon )
            signalParent(0);

        // The main thread acts as the ComBus health / status output loop
        uint32_t count = 0;

        while ( g_active )
        {
            if ( !( ++count % 5 ) )
            {
                if ( !(combus->status( ::ADARA::ComBus::STATUS_OK )) )
                {
                    syslog( LOG_ERR, "PVSD ERROR: %s",
                        "Broadcasting PVSD ComBus Status OK Message" );
                }
            }

            if ( !( count % 300 ) )
            {
                uint32_t partialDeviceCount, hungDeviceCount;
                uint32_t inactiveDeviceCount;
                uint32_t readyPVCount, totalPVCount;

                input->getDevicesStatus(
                    partialDeviceCount, hungDeviceCount,
                    inactiveDeviceCount,
                    readyPVCount, totalPVCount );

                std::string logPrefix = "";
                int logType = LOG_INFO;
                if ( partialDeviceCount || hungDeviceCount )
                {
                    logPrefix = "PVSD ERROR: ";
                    logType = LOG_ERR;
                }

                stringstream ss;
                ss << input->numActiveDevices();
                ss << " Active Input Devices";
                ss << " (";
                ss << partialDeviceCount;
                ss << " Partial, ";
                ss << hungDeviceCount;
                ss << " Hung, ";
                ss << inactiveDeviceCount;
                ss << " Inactive), ";
                ss << readyPVCount;
                ss << " Ready PVs, ";
                ss << totalPVCount;
                ss << " Total PVs, ";
                ss << input->numInactiveDevices();
                ss << " Deactivated Input Devices, ";
                ss << output->numConnected();
                ss << " Output Adapters Connected, ";
                ss << output->numDevices();
                ss << " Output Devices Defined, ";
                ss << output->numPVs();
                ss << " Output PVs Defined";

                syslog( logType, "%sPVSD %s %s %s - %s.",
                    logPrefix.c_str(), PVSD_VERSION,
                    "is Alive at", output->serverAddr().c_str(),
                    ss.str().c_str() );
            }

            sleep(1);
        }
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, "PVSD ERROR: %s: %s",
            "main()", e.toString().c_str() );
        ret_code = 1;
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "PVSD ERROR: %s: Unhandled exception: %s",
            "main()", e.what());
        ret_code = 1;
    }
    catch( ... )
    {
        syslog( LOG_ERR, "PVSD ERROR: main(): Unknown exception" );
        ret_code = 1;
    }

    // If we failed due to an exception and we're a daemon, inform parent
    if ( daemon && ret_code )
        signalParent( ret_code );

    // Cleanup...
    if ( combus )
        delete combus;
    if ( output )
        delete output;
    if ( input )
        delete input;

    syslog( LOG_INFO, "PVSD Stopping..." );
    closelog();

    return ret_code;
}

// vim: expandtab

