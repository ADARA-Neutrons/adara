#include <iostream>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "LiveServer.h"
#include "STSClientMgr.h"
#include "Logging.h"

#include <log4cxx/propertyconfigurator.h>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

static std::string log_conf("/adara/conf/logging.conf");

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("logconf,l", po::value<std::string>(),
				"Path to log4cxx property file");

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch (po::unknown_option e) {
		std::cerr << argv[0] << ": " << e.what() << std::endl
			<< std::endl << desc << std::endl;
		exit(2);
	}

	if (vm.count("help")) {
		std::cerr << desc << std::endl;
		exit(2);
	}

	if (vm.count("logconf"))
		log_conf = vm["logconf"].as<std::string>();
}

int main(int argc, char **argv)
{
	parse_options(argc, argv);

	PropertyConfigurator::configure(log_conf);

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
