#include <stdio.h>

#include <fdManager.h>
#include "StorageManager.h"
#include "DataSource.h"
#include "LiveServer.h"

int main(int argc, char **argv)
{
	StorageManager::init("/data/dad/adara");

	LiveServer liveServer("31415");

	DataSource src1("localhost:31416");

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
