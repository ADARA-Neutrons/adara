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

using namespace std;
using namespace PVS;

using namespace std;

#define PVSD_VERSION "0.1.0"

bool g_active = true;

void signalHandler( int a_signal )
{
    g_active = false;
}


int main(int argc, char *argv[])
{
    struct sigaction new_action, old_action;

    new_action.sa_handler = signalHandler;
    sigemptyset( &new_action.sa_mask );
    new_action.sa_flags = 0;

    sigaction (SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGINT, &new_action, NULL);

    sigaction (SIGHUP, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGHUP, &new_action, NULL);

    sigaction (SIGTERM, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGTERM, &new_action, NULL);

    sigaction (SIGQUIT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGQUIT, &new_action, NULL);

    uint32_t        port;
    uint32_t        heartbeat;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    string          domain;
    string          epics_cfg;

    namespace po = boost::program_options;
    po::options_description options( "dasmon server options" );
    options.add_options()
            ("help,h", "show help")
            ("version,v", "show version number")
            ("port,p", po::value<uint32_t>( &port )->default_value( 31416 ), "set client port")
            ("hb", po::value<uint32_t>( &heartbeat )->default_value( 2000 ), "set ADARA heartbeat period (msec)")
            ("domain,d", po::value<string>( &domain )->default_value( "" ), "set communication domain prefix (EPICS/ComBus)")
            ("broker_uri,b", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user,u", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass,p", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
            ("epics", po::value<string>( &epics_cfg )->default_value( "beamline.xml" ), "set path to epics configuration file")
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
    syslog( LOG_INFO, "pvsd started." );

    if ( !opt_map.count( "domain" ))
        syslog( LOG_WARNING, "No communication domain specified - probably an error." );

    ::ADARA::ComBus::Connection *combus = new ::ADARA::ComBus::Connection( domain, "PVSD", 0, broker_uri, broker_user, broker_pass );

    try
    {
        //combus->waitForConnect( 10 );

        //ConfigManager               cfg_mgr;
        //StreamService               streamer( cfg_mgr, 100 );
        //PVS::ADARA::OutputAdapter   out_adapter( streamer, port, heartbeat );
        //PVS::EPICS::InputAdapter    in_adapter( streamer, cfg_mgr, epics_cfg );

        StreamService   streamer( 100 );
        streamer.attach( new PVS::ADARA::OutputAdapter( port, heartbeat ));
        streamer.attach( new PVS::EPICS::InputAdapter( epics_cfg ));

        // The main thread acts as the ComBus health / status output loop
        uint32_t count = 0;

        while( g_active )
        {
            if (!(count % 2))
                combus->status( ::ADARA::ComBus::STATUS_OK );

            sleep(1);
        }

        //streamer.stop();
    }
    catch( TraceException &e )
    {
        syslog( LOG_ERR, e.toString().c_str() );
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "Unhandled exception: %s", e.what());
    }
    catch( ... )
    {
        syslog( LOG_ERR, "Unknown exception" );
    }

    delete combus;

    syslog( LOG_INFO, "pvsd stopping." );
    closelog();

    return 0;
}
