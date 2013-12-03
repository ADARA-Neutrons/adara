#include <iostream>
#include "ComBus.h"
#include "ComBusRouter.h"
#include "RuleEngine.h"
#include "StreamMonitor.h"
#include <boost/program_options.hpp>
#include <syslog.h>

using namespace std;
using namespace ADARA::DASMON;

#define DASMON_VERSION "1.3"

int main(int argc, char *argv[])
{
    int res = 0;

    string          sms_host;
    unsigned short  sms_port;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    unsigned short  log_level;
    string          config_dir;
    string          domain;
    unsigned long   max_tof;

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
            ("nodiag", "Disable low-level stream diagnostics (test only)")
            ("maxtof", po::value<unsigned long>( &max_tof )->default_value( 33333 ), "set maximum time of flight in usec")
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

    if ( opt_map.count( "help" ))
    {
        cout << options << endl;
        return 0;
    }
    else if ( opt_map.count( "version" ))
    {
        cout << DASMON_VERSION << endl;
        return 0;
    }

    // Initialize SysLog

    openlog( "dasmond", 0, LOG_DAEMON );
    syslog( LOG_INFO, "Dasmon service started." );

    if ( !opt_map.count( "domain" ))
    {
        syslog( LOG_WARNING, "No communication domain specified - probably an error." );
        cout << "No communication domain specified - probably an error."  << endl;
    }

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( domain, "DASMON", 0, broker_uri, broker_user, broker_pass );

#if 0
    ADARA::ComBus::Log4cxxAppender *combus_appender = new ADARA::ComBus::Log4cxxAppender();
    log4cxx::LoggerPtr logger = log4cxx::Logger::getRootLogger();
    logger->addAppender( combus_appender );
    logger->setLevel( log4cxx::Level::getTrace() );
    LOG4CXX_INFO(logger,"DASMON starting");
#endif

    try
    {
        combus->waitForConnect( 10 );
#ifndef NO_DB
        StreamMonitor   monitor( sms_host, sms_port, db_info.name.empty()?0:&db_info, max_tof );
#else
        StreamMonitor   monitor( sms_host, sms_port );
#endif
        if ( opt_map.count( "nodiag" ))
            monitor.enableDiagnostics(false);

        StreamAnalyzer  analyzer( monitor, config_dir );
        ComBusRouter    router( monitor, analyzer );

        // Connect to and process the stream
        monitor.start();

        // Run polling loop on this thread
        router.run();

        // Stop processing stream
        monitor.stop();
    }
    catch( exception &e )
    {
        syslog( LOG_ERR, "Unhandled exception: %s", e.what());
    }

    delete combus;

    syslog( LOG_INFO, "Dasmon service stopping." );
    closelog();

    return res;
}
