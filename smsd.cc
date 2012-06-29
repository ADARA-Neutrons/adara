#include <stdio.h>

#include <log4cxx/logger.h>
#include <log4cxx/consoleappender.h>
#include <log4cxx/patternlayout.h>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "LiveServer.h"
#include "STSClientMgr.h"

using namespace log4cxx;

int main(int argc, char **argv)
{
	/* TODO handle this via property file */
	LayoutPtr layout(new PatternLayout("%d %p %c - %m%n"));
	AppenderPtr appender(new ConsoleAppender(layout));
	Logger::getRootLogger()->addAppender(appender);

	StorageManager::init("/adara/data");
	LiveServer liveServer("31415");
	SMSControl control("1", "CNCS", "TestBeam");
	STSClientMgr stsclient("localhost:31417");

	control.addSource("localhost:31416");

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
