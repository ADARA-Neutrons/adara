#include <QtGui/QApplication>
#include <iostream>
#include "mainwindow.h"
#include "ComBus.h"

using namespace std;

int main(int argc, char *argv[])
{
    int res = 0;
    QApplication a(argc, argv);

    ADARA::ComBus::Connection *combus = new ADARA::ComBus::Connection( "DASMON-GUI", getpid(), "localhost", "", "" );

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
