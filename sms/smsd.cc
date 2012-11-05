#include <iostream>
#include <signal.h>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "LiveServer.h"
#include "STSClientMgr.h"
#include "Logging.h"

#include <log4cxx/propertyconfigurator.h>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

static std::string log_conf("/SNSlocal/sms/conf/logging.conf");
static std::string source_port("31416");
static std::string sts_port("31417");

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("logconf,l", po::value<std::string>(),
				"Path to log4cxx property file")
		("source-port", po::value<std::string>(),
				"Socket port for connecting to DAS control source")
		("sts-port", po::value<std::string>(),
				"Socket port for connecting to STS client");

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
	if (vm.count("source-port"))
		source_port = vm["source-port"].as<std::string>();
	if (vm.count("sts-port"))
		sts_port = vm["sts-port"].as<std::string>();
}

void block_signals(void)
{
	/* We don't want any signals to go to a handler; we will
	 * register callbacks with the SignalEvent class in order to
	 * integrate signals with the event loop.
	 */
	/* We want to block most signals from being delivered via an
	 * async signal handler -- we'd much rather they came in via the
	 * SignalEvent class. Block everything but error conditions.
	 *
	 * TODO we also leave open some standard "quit" signals until
	 * we handle them properly.
	 */
	sigset_t all;
	sigfillset(&all);
	sigdelset(&all, SIGCONT);

	/* Don't block error conditions */
	sigdelset(&all, SIGILL);
	sigdelset(&all, SIGABRT);
	sigdelset(&all, SIGFPE);
	sigdelset(&all, SIGILL);

	/* TODO clean shutdown through SignalEvent handlers */
	sigdelset(&all, SIGTERM);
	sigdelset(&all, SIGINT);
	sigdelset(&all, SIGHUP);

	pthread_sigmask(SIG_BLOCK, &all, NULL);
}

int main(int argc, char **argv)
{
	parse_options(argc, argv);

	PropertyConfigurator::configure(log_conf);

	block_signals();

	StorageManager::init("/SNSlocal/sms/data");
	LiveServer liveServer("31415");
	SMSControl control("BL14BS", "HYSA", "HYSPECA");
	// STSClientMgr stsclient("localhost:31417");
	std::string sts_host("localhost");
	STSClientMgr stsclient(sts_host + ":" + sts_port);

	// control.addSource("localhost:31416");
	std::string source_host("localhost");
	control.addSource(source_host + ":" + source_port);

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
