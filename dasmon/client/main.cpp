#include <QtGui/QApplication>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include "MainWindow.h"
#include "ComBus.h"

using namespace std;


int main(int argc, char *argv[])
{
    int res = 0;

    string  broker_uri;
    string  broker_user;
    string  broker_pass;
    bool    kiosk = false;
    bool    master = false;

    namespace po = boost::program_options;
    po::options_description options( "dasmon options" );
    options.add_options()
            ("help,h", "show help")
            ("version", "show version number")
            ("broker_uri,b", po::value<string>( &broker_uri )->default_value( "localhost" ), "set AMQP broker URI/IP address")
            ("broker_user,u", po::value<string>( &broker_user )->default_value( "" ), "set AMQP broker user name")
            ("broker_pass,p", po::value<string>( &broker_pass )->default_value( "" ), "set AMQP broker password")
            ("kiosk,k", "run in kiosk mode" )
            ("master,m", "run as master display instance (proc id = 0)")
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

    if ( opt_map.count( "kiosk" ))
        kiosk = true;

    if ( opt_map.count( "master" ))
        master = true;

    QApplication a(argc, argv);

    try
    {
        MainWindow main_window( broker_uri, broker_user, broker_pass, kiosk, master );
        main_window.show();

        res = a.exec();
    }
    catch( exception &e )
    {
        cout << e.what() << endl;
    }

    //delete combus;

    return res;
}
