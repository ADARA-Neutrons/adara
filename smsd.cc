#include <stdio.h>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "LiveServer.h"
#include "STSClientMgr.h"

int main(int argc, char **argv)
{
	StorageManager::init("/adara/data");
	LiveServer liveServer("31415");
	SMSControl control("1", "BL0", "TestBeam");
	STSClientMgr stsclient("localhost:31417");

	control.addSource("localhost:31416");

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
