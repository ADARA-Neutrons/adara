#include <QtGui/QApplication>
#include <iostream>
#include <log4cxx/logger.h>
#include "mainwindow.h"
#include "ruleengine.h"
#include "ComBus.h"
#include "ComBusAppender.h"
//#include "vectorappender.h"

using namespace std;


int main(int argc, char *argv[])
{
    int res = 0;
    QApplication a(argc, argv);

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( "DASMON", 0, "localhost", "", "" );
    ADARA::ComBus::Log4cxxAppender *combus_appender = new ADARA::ComBus::Log4cxxAppender();
    log4cxx::LoggerPtr logger = log4cxx::Logger::getRootLogger();
    logger->addAppender( combus_appender );
    logger->setLevel( log4cxx::Level::getTrace() );

    LOG4CXX_INFO(logger,"DASMON starting");

    try
    {
        MainWindow main_window;
        main_window.show();

        // Create MonitorAdapter with new connection
        StreamMonitor  monitor("localhost");

        // Create rule engine and set main window as listener
        RuleEng::RuleEngine rule_engine(monitor);
        rule_engine.addListener( main_window );

        // Main window needs StreamMonitor instance so it can poll transient metrics
        main_window.setStreamMonitor( &monitor );

        // Set StreamMonitor listeners to receive stream data
        monitor.addListener( main_window );

        // Connect to and process the stream
        monitor.start();

        res = a.exec();

        // Stop processing stream
        monitor.stop();
    }
    catch( exception &e )
    {
        cout << e.what() << endl;
    }

    LOG4CXX_INFO(logger,"DASMON exiting");

    return res;
}
