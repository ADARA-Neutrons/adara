
//
// SNS ADARA SYSTEM - DasMon Server
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
#include "ComBus.h"
#include "ComBusRouter.h"
#include "RuleEngine.h"
#include "StreamMonitor.h"
#include <boost/program_options.hpp>
#include <syslog.h>
#include <unistd.h>
#include "ADARAUtils.h"

using namespace std;
using namespace ADARA::DASMON;

#define DASMON_VERSION "1.6.9"


bool g_child_signal = false;
int g_child_code = 0;


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
        syslog( LOG_ERR, "Unable to signal parent: %s", strerror(e));
        usleep(30000); // give syslog a chance...
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
        syslog( LOG_ERR, "Unable to fork: %s", strerror(e));
        usleep(30000); // give syslog a chance...
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
        syslog( LOG_ERR, "Unable to setsid: %s", strerror(e));
        usleep(30000); // give syslog a chance...
        exit(1);
    }

    pid = fork();
    if ( pid < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "Second fork failed: %s", strerror(e));
        usleep(30000); // give syslog a chance...
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
    openlog( "dasmond", 0, LOG_DAEMON );
    syslog( LOG_INFO, "dasmond daemon starting" );
    usleep(30000); // give syslog a chance...

    // Chdir to "/"
    if ( chdir("/") < 0 )
    {
        int e = errno;
        syslog( LOG_ERR, "Chdir failed: %s", strerror(e));
        usleep(30000); // give syslog a chance...
        exit(1);
    }
}


int main(int argc, char *argv[])
{
    int res = 0;

    // Attach SIGUSR handler for daemon initialization
    struct sigaction new_action;

    new_action.sa_handler = 0;
    new_action.sa_sigaction = signalHandlerChild;
    sigemptyset( &new_action.sa_mask );
    new_action.sa_flags = SA_SIGINFO;
    sigaction( SIGUSR1, &new_action, NULL );

    string          sms_host;
    unsigned short  sms_port;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    unsigned short  log_level;
    string          config_dir;
    string          domain;
    unsigned long   max_tof;
    bool            daemon = false;
    uint16_t        metrics_period = 4;

#ifndef NO_DB
    DBConnectInfo   db_info;
#endif

    namespace po = boost::program_options;
    po::options_description options( "dasmon server options" );
    options.add_options()
            ("help,h", "show help")
            ("version", "show version number")
            ("verbosity,v", po::value<unsigned short>( &log_level )->default_value( 3 ), "verbosity level (0=trace,3=warn,5=fatal)")
            ("cfg_dir,c", po::value<string>( &config_dir )->default_value( "" ), "App configuration directory")
            ("sms_host", po::value<string>( &sms_host )->default_value( "localhost" ), "set sms hostname/ip")
            ("sms_port", po::value<unsigned short>( &sms_port )->default_value( 31415 ), "set sms port")
            ("domain", po::value<string>( &domain )->default_value( "" ), "set communication domain prefix (EPICS/ComBus)")
            ("broker_uri", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
            ("metrics_period", po::value<unsigned short>( &metrics_period )->default_value( 4 ), "Metrics AMQP broadcast period")
            ("nodiag", "Disable low-level stream diagnostics (test only)")
            ("maxtof", po::value<unsigned long>( &max_tof )->default_value( 33333 ), "set maximum time of flight in usec")
            ("daemon", "Run as background daemon")
#ifndef NO_DB
            ("db_host", po::value<string>( &db_info.host )->default_value( "" ), "set database hostname")
            ("db_port", po::value<unsigned short>( &db_info.port )->default_value( 0 ), "set database port")
            ("db_name", po::value<string>( &db_info.name )->default_value( "" ), "set database name")
            ("db_user", po::value<string>( &db_info.user )->default_value( "" ), "set database user name")
            ("db_pass", po::value<string>( &db_info.pass )->default_value( "" ), "set database password")
            ("db_period", po::value<unsigned short>( &db_info.period )->default_value( 5 ), "set database update period in seconds")
#endif
            ;

    po::variables_map opt_map;
    po::store( po::parse_command_line(argc,argv,options), opt_map );
    po::notify( opt_map );

    // Process help / version options and exit early
    if ( opt_map.count( "daemon" ))
        daemon = true;

    if ( opt_map.count( "help" ) && !daemon )
    {
        cout << options << endl;
        return 0;
    }
    else if ( opt_map.count( "version" ) && !daemon )
    {
        cout << DASMON_VERSION
            << " (ADARA Common " << ADARA::VERSION
            << ", ComBus " << ADARA::ComBus::VERSION << ")" << endl;
        return 0;
    }

    // Initialize SysLog

    openlog( "dasmond", 0, LOG_DAEMON );
    syslog( LOG_INFO,
        "Dasmon Daemon %s Started. (%s %s, %s %s)", DASMON_VERSION,
        "ADARA Common", ADARA::VERSION.c_str(),
        "ComBus", ADARA::ComBus::VERSION.c_str() );
    usleep(30000); // give syslog a chance...

    if ( !opt_map.count( "domain" ))
    {
        syslog( LOG_WARNING, "No communication domain specified - probably an error." );
        usleep(30000); // give syslog a chance...
        cout << "No communication domain specified - probably an error."  << endl;
    }

    // Parent process will exit in this call
    if ( daemon )
        daemonize();

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection(
        domain, "DASMON", 0, broker_uri, broker_user, broker_pass,
        "Dasmon Daemon ComBus", "Dasmon Daemon Error ComBus" );

    try
    {
        if ( !combus->waitForConnect( 5 ) )
        {
            syslog( LOG_ERR, "ComBus: Failed to Connect to AMQP!" );
            usleep(30000); // give syslog a chance...
        }
        else
        {
            syslog( LOG_NOTICE, "ComBus: Connected to AMQP." );
            usleep(30000); // give syslog a chance...
        }

#ifndef NO_DB
        StreamMonitor   monitor( sms_host, sms_port, db_info.name.empty()?0:&db_info, max_tof );
#else
        StreamMonitor   monitor( sms_host, sms_port );
#endif
        if ( opt_map.count( "nodiag" ))
            monitor.enableDiagnostics(false);

        StreamAnalyzer  analyzer( monitor, config_dir );
        ComBusRouter    router( monitor, analyzer, metrics_period );

        // Connect to and process the stream
        monitor.start();

        // If we made it here as a daemon, signal parent that all is well
        if ( daemon )
            signalParent(0);

        // Run polling loop on this thread
        router.run();

        // Stop processing stream
        monitor.stop();
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "Unhandled exception: %s", e.what());
        usleep(30000); // give syslog a chance...
    }

    delete combus;

    syslog( LOG_INFO, "Dasmon service stopping." );
    usleep(30000); // give syslog a chance...

    closelog();

    return res;
}
