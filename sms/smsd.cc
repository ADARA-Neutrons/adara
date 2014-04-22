#include <iostream>
#include <stdexcept>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

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

#define CHILD_INIT_SUCCESS	1
#define CHILD_INIT_FAILED	2

const std::string SMSD_VERSION = "1.0.0";

namespace po = boost::program_options;
namespace ptree = boost::property_tree;

static LoggerPtr logger(Logger::getLogger("SMS"));

static std::string config_file("/SNSlocal/sms/conf/smsd.conf");
static std::string log_conf("/SNSlocal/sms/conf/logging.conf");
static Appender *console_appender;
static bool become_daemon = true;

static int initCompleteFd = -1;

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("foreground,f", "Don't become a daemon")
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
	if (vm.count("foreground"))
		become_daemon = false;
	if (vm.count("conf"))
		config_file = vm["conf"].as<std::string>();
	if (vm.count("logconf"))
		log_conf = vm["logconf"].as<std::string>();
}

static void setcredentials(const char *pname, ptree::ptree &conf)
{
	std::string group = conf.get<std::string>("sms.group", "");
	if (group.length()) {
		struct group *grp = getgrnam(group.c_str());
		gid_t gid = getgid();

		if (!grp) {
			std::cerr << pname << ": unable to lookup group '"
				<< group << "'" << std::endl;
			exit(1);
		}

		if (gid != grp->gr_gid && setgid(grp->gr_gid) < 0) {
			int e = errno;
			std::cerr << pname << ": unable to setgid for '"
				<< group << "':" << strerror(e) << std::endl;
			exit(1);
		}
	}

	std::string user = conf.get<std::string>("sms.user", "");
	if (user.length()) {
		struct passwd *pwd = getpwnam(user.c_str());
		uid_t uid = getuid();

		if (!pwd) {
			std::cerr << pname << ": unable to lookup user '"
				<< user << "'" << std::endl;
			exit(1);
		}

		if (uid != pwd->pw_uid && setuid(pwd->pw_uid) < 0) {
			int e = errno;
			std::cerr << pname << ": unable to setuid for '"
				<< user << "':" << strerror(e) << std::endl;
			exit(1);
		}
	}
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

	setcredentials(pname, conf);

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

		/* Not strictly necessary, but avoid using it in the future;
		 * it was destroyed when we removed it from the root logger.
		 */
		console_appender = NULL;
	}
}

static void release_parent(uint64_t val)
{
	if (initCompleteFd >= 0) {
		if (write(initCompleteFd, &val, sizeof(val)) < 0) {
			int e = errno;
			ERROR("unable to signal parent: " << strerror(e));
			exit(1);
		}

		if (close(initCompleteFd) < 0) {
			int e = errno;
			ERROR("unable to close event fd: " << strerror(e));
			exit(1);
		}

		initCompleteFd = -1;
	}
}

static void daemonize(const char *pname)
{
	pid_t pid;

	initCompleteFd = eventfd(0, EFD_CLOEXEC);

	if (initCompleteFd < 0) {
		int e = errno;
		ERROR("unable to create event fd: " << strerror(e));
		exit(1);
	}

	pid = fork();
	if (pid < 0) {
		int e = errno;
		ERROR("unable to fork: " << strerror(e));
		exit(1);
	}

	if (pid) {
		/* Parent process; wait for the child to signal us that
		 * has finished initialization, successful or not.
		 */
		uint64_t ok;
		if (read(initCompleteFd, &ok, sizeof(ok)) < 0) {
			int e = errno;
			ERROR("unable to receive child signal: "
			      << strerror(e));
			exit(1);
		}

		if (ok != CHILD_INIT_SUCCESS)
			exit(1);

		exit(0);
	}

	/* We're the child process, become a daemon.
	 * Create a new session, then fokr and have the parent exit,
	 * ensuring we are not the leader of the session -- we don't
	 * want a controlling terminal. StorageManager::init() already
	 * took care of our working directory and umask settings.
	 */
	if (setsid() < 0) {
		int e = errno;
		ERROR("unable to setsid: " << strerror(e));
		release_parent(CHILD_INIT_FAILED);
		exit(1);
	}

	pid = fork();
	if (pid < 0) {
		int e = errno;
		ERROR("second fork failed: " << strerror(e));
		release_parent(CHILD_INIT_FAILED);
		exit(1);
	} else if (pid) {
		/* Parent process just exits */
		exit(0);
	}

	/* We're the second child now; we are in our own session, but
	 * are not the leader of it. Let initialization continue.
	 */
	DEBUG("daemonized");
}

static void close_std_files(void)
{
	int fd = open("/dev/null", O_RDWR);

	if (fd < 0) {
		int e = errno;
		std::string msg("Unable to open /dev/null: ");
		msg += strerror(e);
		throw std::runtime_error(msg);
	}

	/* Make /dev/null be our stdin and stderr. If we added a console
	 * appender, then we can also do stdout as it won't be used any
	 * longer. If we didn't add the console appender, we need to leave
	 * stdout alone, as the user requested logging go there.
	 *
	 * Note that we need to be called before remove_temp_logger()
	 */
	dup2(fd, 0);
	dup2(fd, 2);
	if (console_appender)
		dup2(fd, 1);
	if (fd > 2)
		close(fd);
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

	/* Do all of the initialization we can before we become a daemon;
	 * we'll do a post-daemon round of init as well, so that creation
	 * of threads get the right parent, and allow for tasks that need
	 * to happen later.
	 */
	try {
		StorageManager::init();
		SMSControl::init();
		LiveServer::init();
		STSClientMgr::init();

		SMSControl::late_config(conf);
	} catch (std::runtime_error e) {
		ERROR("failed to start: " << e.what());
		exit(1);
	} catch (...) {
		ERROR("failed to start -- unknown exception");
		exit(1);
	}

	if (become_daemon)
		daemonize(argv[0]);

	try {
		StorageManager::lateInit();
		close_std_files();
		remove_temp_logger();
	} catch (std::runtime_error e) {
		ERROR("failed to start: " << e.what());
		release_parent(CHILD_INIT_FAILED);
		exit(1);
	} catch (...) {
		ERROR("failed to start; unknown exception");
		release_parent(CHILD_INIT_FAILED);
		throw;
	}

	try {
		release_parent(CHILD_INIT_SUCCESS);
		for (;;) {
			fileDescriptorManager.process(1000.0);
		}
	} catch (...) {
		ERROR("dying on an unexpected/unhandled exception");
		throw;
	}

	return 0;
}
