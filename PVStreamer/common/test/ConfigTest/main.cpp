#include <iostream>
#include <QtCore/QCoreApplication>
#include "ConfigManager.h"
#include "StreamService.h"
#include "EPICS_InputAdapter.h"
#include "ADARA_OutputAdapter.h"
#include <cadef.h>

using namespace std;
using namespace PVS;


bool
testRedundantConfig( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev1( "dev1", "host-a", 1 );
    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 0, "m/s" );
    dev1.definePV( "d1p3", "conn_d1p3", PV_UINT, 0, "m/s" );

    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1 );
    DeviceRecordPtr rec2 = cfgmgr.defineDevice( dev1 );
    //rec1->print( cout );

    bool res = (rec1 == rec2);

    cfgmgr.undefineDevice( "dev1", "host-a", 1 );

    return res;
}


bool
testIDReuse( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev2( "dev2", "host-a", 1 );
    dev2.definePV( "d2p1", "conn_d2p1", PV_INT, 0, "a" );
    dev2.definePV( "d2p2", "conn_d2p2", PV_REAL, 0, "b" );
    dev2.definePV( "d2p3", "conn_d2p3", PV_UINT, 0, "c" );

    DeviceRecordPtr rec2 = cfgmgr.defineDevice( dev2 );

    DeviceDescriptor dev2a( "dev2", "host-a", 1 );
    dev2a.definePV( "d2p1", "conn_d2p1", PV_INT, 0, "a" );
    dev2a.definePV( "d2p3", "conn_d2p3", PV_UINT, 0, "c" );

    DeviceRecordPtr rec2a = cfgmgr.defineDevice( dev2a );

    bool res = true;

    if ( rec2a->m_id != rec2->m_id )
        res = false;

    if ( rec2a->m_pvs[0]->m_id != rec2->m_pvs[0]->m_id )
        res = false;

    if ( rec2a->m_pvs[1]->m_id != rec2->m_pvs[2]->m_id )
        res = false;

    cfgmgr.undefineDevice( "dev1", "host-a", 1 );

    return res;
}


bool
testDeviceUndefine( ConfigManager &cfgmgr )
{
    DeviceDescriptor dev1( "dev1", "host-a", 1 );
    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 0, "m/s" );

    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1 );

    cfgmgr.undefineDevice( "dev1", "host-a", 1 );

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

    dev1.definePV( "d1p1", "conn_d1p1", PV_INT, 0, "m/s" );
    dev1.definePV( "d1p2", "conn_d1p2", PV_REAL, 0, "m/s" );
    dev1.definePV( "d1p3", "conn_d1p3", PV_ENUM, e, "" );

    DeviceRecordPtr rec1 = cfgmgr.defineDevice( dev1 );

    vals[4] = "VAL4";

    e = dev1.defineEnumeration( vals );

    dev1.definePV( "d1p4", "conn_d1p4", PV_ENUM, e, "" );

    rec1 = cfgmgr.defineDevice( dev1 );

    bool res = true;

    if ( rec1->m_pvs[2]->m_enum->m_values.size() != 3 )
        res = false;

    if ( rec1->m_pvs[3]->m_enum->m_values.size() != 4 )
        res = false;

    cfgmgr.undefineDevice( "dev1", "host-a", 1 );

    return res;
}


#define PROCRES(x,test) if ( !x ) { cout << "Failed: " << test << endl; }

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
#if 0
    cout << "ConfigManager Tests" << endl;

    ConfigManager cfgmgr;

    PROCRES( testRedundantConfig( cfgmgr ), "Redundant Config" );
    PROCRES( testIDReuse( cfgmgr ), "ID Reuse" );
    PROCRES( testDeviceUndefine( cfgmgr ), "Device undefine" );
    PROCRES( testEnums( cfgmgr ), "Enum tests" );
#endif

    ConfigManager           cfg_mgr;
    StreamService           streamer( cfg_mgr, 100 );
    ADARA::OutputAdapter    out_adapt( streamer );
    EPICS::InputAdapter     in_adapter( streamer, cfg_mgr, "beamline.xml" );

    while(1)
    {
        //sleep(1);
        ca_pend_event(1.0);
    }

    return 0;
}
