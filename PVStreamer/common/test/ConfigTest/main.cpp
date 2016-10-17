#include <iostream>
#include <QtCore/QCoreApplication>
#include "ConfigManager.h"
#include "StreamService.h"
#include "EPICS_InputAdapter.h"
#include "ADARA_OutputAdapter.h"
#include "TraceException.h"
//#include <cadef.h>
#include <signal.h>
#include <syslog.h>

using namespace std;
using namespace PVS;


bool
testRedundantConfig( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev1( "dev1", "host-a", 1 );
    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 1, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 1, 0, "m/s" );
    dev1.definePV( "d1p3", "conn_d1p3", PV_UINT, 1, 0, "m/s" );

	bool device_changed = false;
    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1, device_changed );
    DeviceRecordPtr rec2 = cfgmgr.defineDevice( dev1, device_changed );
    //rec1->print( cout );

    bool res = (rec1 == rec2);

    cfgmgr.undefineDevice( rec1 );

    return res;
}


bool
testIDReuse( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev2( "dev2", "host-a", 1 );
    dev2.definePV( "d2p1", "conn_d2p1", PV_INT, 1, 0, "a" );
    dev2.definePV( "d2p2", "conn_d2p2", PV_REAL, 1, 0, "b" );
    dev2.definePV( "d2p3", "conn_d2p3", PV_UINT, 1, 0, "c" );

	bool device_changed = false;
    DeviceRecordPtr rec2 = cfgmgr.defineDevice( dev2, device_changed );

    DeviceDescriptor dev2a( "dev2", "host-a", 1 );
    dev2a.definePV( "d2p1", "conn_d2p1", PV_INT, 1, 0, "a" );
    dev2a.definePV( "d2p3", "conn_d2p3", PV_UINT, 1, 0, "c" );

    DeviceRecordPtr rec2a = cfgmgr.defineDevice( dev2a, device_changed );

    bool res = true;

    if ( rec2a->m_id != rec2->m_id )
        res = false;

    if ( rec2a->m_pvs[0]->m_id != rec2->m_pvs[0]->m_id )
        res = false;

    if ( rec2a->m_pvs[1]->m_id != rec2->m_pvs[2]->m_id )
        res = false;

    cfgmgr.undefineDevice( rec2 );

    return res;
}


bool
testDeviceUndefine( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev1( "dev1", "host-a", 1 );
    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 1, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 1, 0, "m/s" );

	bool device_changed = false;
    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1, device_changed );

    cfgmgr.undefineDevice( rec1 );

    DeviceRecordPtr rec1b = cfgmgr.getDeviceConfig( "dev1", "host-a", 1 );

    return ( rec1b.get() == 0 );
}


bool
testEnums( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev1( "dev1", "host-a", 1 );

    map<int32_t,string> vals;
    vals[1] = "VAL1";
    vals[2] = "VAL2";
    vals[3] = "VAL3";

    EnumDescriptor *e = dev1.defineEnumeration( vals );

    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 1, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 1, 0, "m/s" );
    dev1.definePV( "d1p3", "conn_d1p3", PV_ENUM, 1, e, "" );

	bool device_changed = false;
    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1, device_changed );

    vals[4] = "VAL4";

    e = dev1.defineEnumeration( vals );

    dev1.definePV( "d1p4", "conn_d1p4", PV_ENUM, 1, e, "" );

    rec1 = cfgmgr.defineDevice( dev1, device_changed );

    bool res = true;

    if ( rec1->m_pvs[2]->m_enum->m_values.size() != 3 )
        res = false;

    if ( rec1->m_pvs[3]->m_enum->m_values.size() != 4 )
        res = false;

    cfgmgr.undefineDevice( rec1 );

    return res;
}


#define PROCRES(x,test) if ( !x ) { cout << "Failed: " << test << endl; }


bool g_active = true;

void signalHandler( int a_signal )
{
    g_active = false;
}


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

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

    openlog( "pvsd", 0, LOG_DAEMON );
    syslog( LOG_INFO, "pvsd-qt started." );

#if 1
    cout << "ConfigManager Tests" << endl;

    ConfigManager cfgmgr;

    PROCRES( testRedundantConfig( cfgmgr ), "Redundant Config" );
    PROCRES( testIDReuse( cfgmgr ), "ID Reuse" );
    PROCRES( testDeviceUndefine( cfgmgr ), "Device undefine" );
    PROCRES( testEnums( cfgmgr ), "Enum tests" );
#endif

    try
    {
        uint32_t        port = 31416;
        StreamService   streamer( 100 );
        streamer.attach( new PVS::ADARA::OutputAdapter( streamer, port ));
        streamer.attach( new PVS::EPICS::InputAdapter( streamer, "beamline.xml" ));

        while( g_active )
        {
            sleep(1);
        }
    }
    catch( TraceException &e )
    {
        cout << e.toString() << endl;
    }
    catch( exception &e )
    {
        cout << "Unhandled exception: %s" << e.what() << endl;
    }
    catch( ... )
    {
        cout << "Unknown exception" << endl;
    }

    syslog( LOG_INFO, "pvsd-qt stopping." );
    closelog();


    return 0;
}
