#include <iostream>
#include "ComBus.h"
#include "ComBusRouter.h"
#include "RuleEngine.h"
#include "StreamMonitor.h"
#include <boost/program_options.hpp>
#include <syslog.h>

//#include <log4cxx/logger.h>
//#include "ComBusAppender.h"

using namespace std;
using namespace ADARA::DASMON;

#define DASMON_VERSION "0.1.2"

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

#ifdef USE_DB
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
            ("broker_uri", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
#ifdef USE_DB
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

    openlog( "dasmond", 0, LOG_DAEMON );
    syslog( LOG_INFO, "Dasmon service started." );

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( "DASMON", 0, broker_uri, broker_user, broker_pass );

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
#ifdef USE_DB
        StreamMonitor   monitor( sms_host, sms_port, db_info.name.empty()?0:&db_info );
#else
        StreamMonitor   monitor( sms_host, sms_port );
#endif
        StreamAnalyzer  analyzer( monitor, config_dir );
        ComBusRouter    router( monitor, analyzer );

        // TODO This needs to change at some point - config should be from a central data source
        //analyzer.loadConfig();

        // Connect to and process the stream
        monitor.start();

        // Run polling loop on this thread
        router.run();

        // Stop processing stream
        monitor.stop();
    }
    catch( exception &e )
    {
        cout << e.what() << endl;
    }

    //LOG4CXX_INFO(logger,"DASMON exiting");

    delete combus;

    syslog( LOG_INFO, "Dasmon service stopping." );
    closelog();

    return res;
}
