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
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace po = boost::program_options;
namespace ptree = boost::property_tree;

static std::string config_file("/SNSlocal/sms/conf/smsd.conf");
static std::string log_conf("/SNSlocal/sms/conf/logging.conf");

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("conf,c", po::value<std::string>(),
				"Path to configuration file")
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

	if (vm.count("conf"))
		config_file = vm["conf"].as<std::string>();
	if (vm.count("logconf"))
		log_conf = vm["logconf"].as<std::string>();
}

void load_config(const char *pname, ptree::ptree &conf)
{
	try {
		ptree::read_ini(config_file, conf);
	} catch (ptree::ini_parser_error e) {
		std::cerr << pname << ": Unable to parse configuration file"
			<< std::endl;
		std::cerr << pname << ": " << e.what() << std::endl;
		exit(1);
	}

	std::string t = conf.get<std::string>("sms.basedir", "");
	if (!t.length())
		conf.put("sms.basedir", "/SNSlocal/sms");

	StorageManager::config(conf);
	SMSControl::config(conf);
	STSClientMgr::config(conf);
	LiveServer::config(conf);
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
	ptree::ptree conf;

	parse_options(argc, argv);

	/* XXX do this later, but setup a simple console appender initially. */
	PropertyConfigurator::configure(log_conf);

	load_config(argv[0], conf);

	block_signals();

	StorageManager::init();
	SMSControl::init();
	LiveServer::init();
	STSClientMgr::init();

	SMSControl::addSources(conf);

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
