#include <iostream>
#include <boost/program_options.hpp>
#include <syslog.h>
#include "ComBus.h"

#include "PVS_Core.h"
#include "PVS_EPICS_Reader.h"
#include "PVS_ADARA_Writer.h"

using namespace std;
//using namespace ADARA::PVSD;

#define PVSD_VERSION "0.1.0"

int main(int argc, char *argv[])
{
    unsigned short  port;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    string          domain;
    string          log_path;

    namespace po = boost::program_options;
    po::options_description options( "dasmon server options" );
    options.add_options()
            ("help,h", "show help")
            ("version,v", "show version number")
            ("port,p", po::value<unsigned short>( &port )->default_value( 31415 ), "set client port")
            ("domain,d", po::value<string>( &domain )->default_value( "" ), "set communication domain prefix (EPICS/ComBus)")
            ("log,l", po::value<string>( &log_path )->default_value( "" ), "set logging path")
            ("broker_uri,b", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user,u", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass,p", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
            ;

    po::variables_map opt_map;
    po::store( po::parse_command_line(argc,argv,options), opt_map );
    po::notify( opt_map );

    // Process help / version options and exit early

    if ( opt_map.count( "help" ))
    {
        cout << options << endl;
        return 0;
    }
    else if ( opt_map.count( "version" ))
    {
        cout << PVSD_VERSION << endl;
        return 0;
    }

    // Initialize SysLog

    openlog( "pvsd", 0, LOG_DAEMON );
    syslog( LOG_INFO, "pv streamer daemon (pvsd) started." );

    if ( !opt_map.count( "domain" ))
    {
        syslog( LOG_WARNING, "No communication domain specified - probably an error." );
        cout << "No communication domain specified - probably an error."  << endl;
    }

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( domain, "PVSD", 0, broker_uri, broker_user, broker_pass );

    try
    {
        combus->waitForConnect( 10 );

        PVS::CORE::CoreServices     core;
        PVS::CORE::FileLogger       logger( core, log_path );
        PVS::ADARA::ADARA_Reader    reader( core );
        PVS::EPICS::EPICS_Writer    writer( core );

        // CoreServices::run() will use calling thread and will not return
        core.run();
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "Unhandled exception: %s", e.what());
    }

    delete combus;

    syslog( LOG_INFO, "pv streamer daemon (pvsd) stopping." );
    closelog();

    return 0;
}
