
//
// SNS ADARA SYSTEM - Stream Management Service (SMS)
//
// This repository contains the software for the next-generation Data
// Acquisition System (DAS) at the Spallation Neutron Source (SNS) at
// Oak Ridge National Laboratory (ORNL) -- "ADARA".
//
// Copyright (c) 2015, UT-Battelle LLC
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS"));

#include <iostream>
#include <stdexcept>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <stdint.h>
#include <grp.h>
#include <pwd.h>

#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/consoleappender.h>
#include <log4cxx/patternlayout.h>
#include <log4cxx/logmanager.h>
#include <log4cxx/logger.h>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "EPICS.h"
#include "SMSControl.h"
#include "StorageManager.h"
#include "ComBusSMSMon.h"
#include "LiveServer.h"
#include "STCClientMgr.h"

#define CHILD_INIT_SUCCESS	1
#define CHILD_INIT_FAILED	2

std::string SMSD_VERSION = "1.8.1";

namespace po = boost::program_options;
namespace ptree = boost::property_tree;

static std::string config_file("/SNSlocal/sms/conf/smsd.conf");

static std::string log_conf("/SNSlocal/sms/conf/logging.conf");

static bool become_daemon_sysv = false;
static bool become_daemon_systemd = true;

static Appender *console_appender;
static bool create_temp_logger = false;

static int initCompleteFd = -1;

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("version", "Show software version(s) information")
		("foreground,f", "Don't become a daemon")
		("sysv", "Become a SysV daemon")
		("systemd", "Become a SystemD daemon (Default)")
		("conf,c", po::value<std::string>(),
				"Path to configuration file")
		("logconf,l", po::value<std::string>(),
				"Path to log4cxx property file")
		("logtemp", "Create temporary console logger");

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
	if (vm.count("version")) {
		std::cerr << "SMS Daemon Version " << SMSD_VERSION
			<< " (ADARA Common Version " << ADARA::VERSION
			<< ", ComBus Version " << ADARA::ComBus::VERSION
			<< ", Tag " << ADARA::TAG_NAME << ")"
			<< std::endl;
		exit(2);
	}
	if (vm.count("foreground")) {
		become_daemon_sysv = false;
		become_daemon_systemd = false;
	}
	if (vm.count("sysv")) {
		become_daemon_sysv = true;
		become_daemon_systemd = false;
	}
	if (vm.count("systemd")) {
		become_daemon_sysv = false;
		become_daemon_systemd = true;
	}
	if (vm.count("conf"))
		config_file = vm["conf"].as<std::string>();
	if (vm.count("logconf"))
		log_conf = vm["logconf"].as<std::string>();
	if (vm.count("logtemp"))
		create_temp_logger = true;
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

static void load_config(const char *pname, ptree::ptree &conf,
		std::string version_str)
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

	conf.put("sms.version", version_str);

	setcredentials(pname, conf);

	StorageManager::config(conf);
	ComBusSMSMon::config(conf);
	SMSControl::config(conf);
	STCClientMgr::config(conf);
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
	sigfillset(&all); // includes SIGPIPE, for write()/sendfile()... Whew!
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
	if (create_temp_logger) {
		static const LogString pattern(LOG4CXX_STR("%c %p: %m%n"));
		LayoutPtr layout(new PatternLayout(pattern));
		console_appender = new ConsoleAppender(layout);
		root->addAppender(console_appender);
	}
}

static void remove_temp_logger(void)
{
	/* If we added a temporary console logger for startup, remove it
	 */
	if (console_appender) {

		DEBUG("remove_temp_logger()");

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

static void daemonize_sysv(const char *pname)
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
		// NOTE: This is Standard C Library read()... ;-o
		if (read(initCompleteFd, &ok, sizeof(ok)) < 0) {
			int e = errno;
			ERROR("unable to receive child signal: "
				<< strerror(e));
			_exit(1);
		}

		if (ok != CHILD_INIT_SUCCESS)
			_exit(1);

		_exit(0);
	}

	/* We're the child process, become a daemon.
	 * Create a new session, then fork and have the parent exit,
	 * ensuring we are not the leader of the session -- we don't
	 * want a controlling terminal. StorageManager::init() already
	 * took care of our working directory and umask settings.
	 */
	if (setsid() < 0) {
		int e = errno;
		ERROR("unable to setsid: " << strerror(e));
		release_parent(CHILD_INIT_FAILED);
		_exit(1);
	}

	pid = fork();
	if (pid < 0) {
		int e = errno;
		ERROR("second fork failed: " << strerror(e));
		release_parent(CHILD_INIT_FAILED);
		_exit(1);
	} else if (pid) {
		/* Parent process just exits */
		_exit(0);
	}

	/* We're the second child now; we are in our own session, but
	 * are not the leader of it. Let initialization continue.
	 */
	DEBUG("SysV daemonized (" << pname << ")");
}

static void daemonize_systemd(const char *pname)
{
	DEBUG("SystemD daemonized pid=" << getpid() << " ppid=" << getppid()
		<< " (" << pname << ")");
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

	std::string version_str =
		" SMSD Version " + SMSD_VERSION
		+ " (ADARA Common Version " + ADARA::VERSION
		+ ", ComBus Version " + ADARA::ComBus::VERSION
		+ ", Tag " + ADARA::TAG_NAME + ")";

	INFO("SMS Daemon Started, " << version_str);

	block_signals();

	if (become_daemon_sysv) {
		daemonize_sysv(argv[0]);
		close_std_files();
	}
	else if (become_daemon_systemd) {
		daemonize_systemd(argv[0]);
	}
	else {
		DEBUG("Running SMS Daemon in Foreground");
	}

	// Seems like a good idea to check the EPICS Environment... ;-D
	INFO("EPICS Environment Settings:");
	system("printenv | grep -i EPICS");

	/* Try to Configure the SMS Daemon... (Catch Exceptions Dagnabbit.) */
	try {
		load_config(argv[0], conf, version_str);
	} catch (std::runtime_error e) {
		ERROR("Failed to Start (Load Config): " << e.what());
		exit(1);
	} catch (...) {
		ERROR("Failed to Start (Load Config) -- Unknown Exception");
		exit(1);
	}

	/* Do all of the initialization we can AFTER we become a daemon;
	 * we'll do a post-daemon round of init as well, so that creation
	 * of threads get the right parent, and allow for tasks that need
	 * to happen later.
	 */
	try {
		StorageManager::init();
		SMSControl::init();
		LiveServer::init();
		STCClientMgr::init();

		SMSControl::late_config(conf);
	} catch (std::runtime_error e) {
		ERROR("Failed to Start (Init): " << e.what());
		exit(1);
	} catch (...) {
		ERROR("Failed to Start (Init) -- Unknown Exception");
		exit(1);
	}

	try {
		StorageManager::lateInit();
		remove_temp_logger();
	} catch (std::runtime_error e) {
		ERROR("Failed to Start (Late Init): " << e.what());
		release_parent(CHILD_INIT_FAILED);
		exit(1);
	} catch (...) {
		ERROR("Failed to Start (Late Init) -- Unknown Exception");
		release_parent(CHILD_INIT_FAILED);
		throw;
	}

	try {
		release_parent(CHILD_INIT_SUCCESS);
		for (;;) {
			// Call File Descriptor Manager for Shared EPICS/Socket Select
			// Reset Errno for this iteration...
			errno = 0;
			// Handle Callbacks on Open File Descriptors (Sockets, Files...)
			fileDescriptorManager.process(1000.0);
			// Check for Diabolical or Evil Errno Results...
			// For now, Just Log 'Em... ;-D
			if (errno && errno != EINPROGRESS && errno != EAGAIN) {
				int e = errno;
				ERROR("main(): fileDescriptorManager.process()"
					<< " returned with errno=" << e << ": " << strerror(e));
				errno = 0;
			}
		}
	} catch (std::runtime_error e) {
		ERROR("Dying on an Unexpected/Unhandled Exception: " << e.what());
		exit(1);
	} catch (...) {
		ERROR("Dying on an Unexpected/Unhandled/Unknown Exception");
		throw;
	}

	return 0;
}

