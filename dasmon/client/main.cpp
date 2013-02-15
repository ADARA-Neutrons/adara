#include <QtGui/QApplication>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include "mainwindow.h"
#include "ComBus.h"

using namespace std;

#define DASMON_GUI_VERSION "0.1.0"

int main(int argc, char *argv[])
{
    int res = 0;

    string broker_uri;
    string broker_user;
    string broker_pass;

    namespace po = boost::program_options;
    po::options_description options( "dasmon options" );
    options.add_options()
            ("help,h", "show help")
            ("version", "show version number")
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
        cout << DASMON_GUI_VERSION << endl;
        return 0;
    }

    QApplication a(argc, argv);

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( "DASMON-GUI", getpid(),
                                                                       broker_uri, broker_user, broker_pass );

    try
    {
        MainWindow main_window;
        main_window.show();

        res = a.exec();
    }
    catch( exception &e )
    {
        cout << e.what() << endl;
    }

    delete combus;

    return res;
}
