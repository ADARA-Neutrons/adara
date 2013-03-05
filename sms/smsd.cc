#include <iostream>
#include <signal.h>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "LiveServer.h"
#include "STSClientMgr.h"
#include "Logging.h"

#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/consoleappender.h>
#include <log4cxx/patternlayout.h>
#include <log4cxx/logmanager.h>
#include <log4cxx/logger.h>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace po = boost::program_options;
namespace ptree = boost::property_tree;

static std::string config_file("/SNSlocal/sms/conf/smsd.conf");
static std::string log_conf("/SNSlocal/sms/conf/logging.conf");
static Appender *console_appender;

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

static void load_config(const char *pname, ptree::ptree &conf)
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

static void block_signals(void)
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

static void verify_log4cxx_config(void)
{
	LoggerPtr root = Logger::getRootLogger();
	AppenderList appenders = root->getAllAppenders();
	AppenderList::iterator ait, end = appenders.end();
	bool missing_layout = false, console_present = false;
	bool had_appender = false;

	/* Loop through the root appenders and verify that there is a
	 * layout for each one to avoid a segfault when we try to use it.
	 * While we're looking, note if we see a console appender, as
	 * we always want a copy of messages to go to the console during
	 * startup.
	 */
	for (ait = appenders.begin(); ait != end; ++ait) {
		Appender *a = *ait;

		if (a->getLayout() == NULL) {
			std::cerr << "Appender " << a->getName()
				  << " is missing its layout" << std::endl;
			missing_layout = true;
		}

		if (dynamic_cast<ConsoleAppender *>(a))
			console_present = true;

		had_appender = true;
	}

	if (!had_appender) {
		std::cerr << "No log appenders configured, aborting"
			  << std::endl;
		exit(1);
	}

	if (missing_layout)
		exit(1);

	if (console_present)
		return;

	/* No console present, add one temporarily */
	static const LogString pattern(LOG4CXX_STR("%c %p: %m%n"));
	LayoutPtr layout(new PatternLayout(pattern));
	console_appender = new ConsoleAppender(layout);
	root->addAppender(console_appender);
}

static void remove_temp_logger(void)
{
	/* If we added a temporary console logger for startup, remove it
	 */
	if (console_appender) {
		LoggerPtr root = Logger::getRootLogger();
		root->removeAppender(console_appender);
		console_appender = NULL;
	}
}

int main(int argc, char **argv)
{
	ptree::ptree conf;

	parse_options(argc, argv);

	/* Load a configuration for logging, then verify it to ensure we
	 * have a valid configuration (ie, don't segfault in log4cxx). We
	 * also want to spit out error messages to the console before
	 * becoming a daemon.
	 */
	PropertyConfigurator::configure(log_conf);
	verify_log4cxx_config();

	load_config(argv[0], conf);

	block_signals();

	StorageManager::init();
	SMSControl::init();
	LiveServer::init();
	STSClientMgr::init();

	SMSControl::addSources(conf);

	remove_temp_logger();

	for (;;) {
		fileDescriptorManager.process(1000.0);
	}

	return 0;
}
