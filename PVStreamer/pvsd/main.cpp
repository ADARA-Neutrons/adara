#include <iostream>
#include "ConfigManager.h"
#include "StreamService.h"
#include "EPICS_InputAdapter.h"
#include "ADARA_OutputAdapter.h"
#include "ComBus.h"
#include <boost/program_options.hpp>
#include <syslog.h>
#include <stdint.h>

using namespace std;
using namespace PVS;

using namespace std;

#define PVSD_VERSION "0.1.0"

int main(int argc, char *argv[])
{
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
        combus->waitForConnect( 10 );

        ConfigManager           cfg_mgr;
        StreamService           streamer( cfg_mgr, 100 );
        PVS::ADARA::OutputAdapter    out_adapt( streamer, port, heartbeat );
        PVS::EPICS::InputAdapter     in_adapter( streamer, cfg_mgr, epics_cfg );

        // The main thread acts as the ComBus health / status output loop
        while(1)
        {
            combus->status( ::ADARA::ComBus::STATUS_OK );
        }
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "Unhandled exception: %s", e.what());
    }

    delete combus;

    syslog( LOG_INFO, "pvvsd stopping." );
    closelog();

    return 0;
}
