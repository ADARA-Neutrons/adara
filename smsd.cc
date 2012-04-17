#include <stdio.h>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "DataSource.h"
#include "LiveServer.h"
#include "STSClientMgr.h"

int main(int argc, char **argv)
{
	StorageManager::init("/data/dad/adara");
	LiveServer liveServer("31415");
	DataSource src1("localhost:31416");
	SMSControl control("BL0");
	STSClientMgr stsclient("localhost:31417");

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
