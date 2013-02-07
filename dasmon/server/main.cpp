#include <iostream>
#include "ComBus.h"
#include "ComBusRouter.h"
#include "RuleEngine.h"
#include "StreamMonitor.h"
#include <boost/program_options.hpp>

//#include <log4cxx/logger.h>
//#include "ComBusAppender.h"

using namespace std;
using namespace ADARA::DASMON;

#define DASMON_VERSION "0.1.0"

int main(int argc, char *argv[])
{
    int res = 0;

    string          sms_host;
    unsigned short  sms_port;
    string          broker_uri;
    string          broker_user;
    string          broker_pass;
    unsigned short  log_level;
    string          config_file;

    namespace po = boost::program_options;
    po::options_description options( "dasmon server options" );
    options.add_options()
            ("help,h", "show help")
            ("version", "show version number")
            ("verbosity,v", po::value<unsigned short>( &log_level )->default_value( 3 ), "verbosity level (0=trace,3=warn,5=fatal)")
            ("config,c", po::value<string>( &config_file )->default_value( "signal.cfg" ), "Rule/signal configuration file")
            ("sms_host", po::value<string>( &sms_host )->default_value( "localhost" ), "set sms hostname/ip")
            ("sms_port", po::value<unsigned short>( &sms_port )->default_value( 31415 ), "set sms port")
            ("broker_uri", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
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

        StreamMonitor   monitor( sms_host, sms_port );
        StreamAnalyzer  analyzer( monitor );
        ComBusRouter    router( monitor, analyzer );

        if ( !config_file.empty())
            analyzer.loadConfig( config_file );
        else
            cout << "Starting without signal configuration." << endl;

        combus->setControlListener( router );

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

    return res;
}
