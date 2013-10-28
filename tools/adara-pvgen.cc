#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>

#include <iostream>
#include <vector>
#include <list>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#define epicsAssertAuthor "Dave Dillow <dillowda@ornl.gov>"
#define caNetAddrSock

#include "ADARA.h"
#include "ReadyAdapter.h"

#include <fdManager.h>
#include <epicsTimer.h>

#define MAX_PACKET_SIZE 8192

namespace po = boost::program_options;

static uint16_t listen_port = 31416;
static bool verbose = false;
static double update_interval = 1.0;
static uint32_t dev_id = 1;
static uint32_t var_id = 1;

/* Sigh... EPICS timers only think there is 10ms resolution for the timers
 * on Linux (believing the reported clock rate of 100 Hz), and subtracts
 * half the resolution from the user-provided delay. Account for this so
 * that we don't get 85 Hz when we ask for 60 Hz operation. Hardcoded, as
 * there is no public interface to find out what EPICS thinks the resolution
 * is.
 */
static double timer_fudge = 0.005;

/* The PVUpdate class contains the state information needed for a
 * client to generate the packets for a given PV update. Each client will
 * get its own copy of the object, so it can mutate the members to keep
 * track of how much it has sent and how much remains.
 */
class PVUpdate {
public:
	typedef std::list<PVUpdate> List;

	PVUpdate(const struct timespec &t, uint32_t v) :
		ts(t), value(v)
	{
		ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;
	}

	struct timespec ts;
	uint32_t	value;
};


/* The Client class is instantiated for each connected client. As long
 * as it have data to send, it will for the file descriptor to become
 * ready, and then tries to send the packet it has buffered. If it has
 * completed a packet, it will generate the next one for this pulse, or
 * move to the next pulse.
 */
class Client {
public:
	typedef std::list<Client *> List;

	Client(int f) :
		fd(f), pktlen(0), ready(NULL)
	{
		if (verbose)
			std::cout << "Client fd " << fd << " connected\n";
		all_clients.push_back(this);

		buildDevDesc();

		/* Generate a initial update */
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		PVUpdate u(now, 0);
		updates.push_back(u);

		wantWrite();
	}

	~Client() {
		all_clients.remove(this);
		delete ready;
		close(fd);
		if (verbose)
			std::cout << "Client fd " << fd << " disconnected\n";
	}

	void wantWrite(void) {
		if (!ready)
			ready = new ReadyAdapter(fd, fdrWrite,
					boost::bind(&Client::writable, this));
	}

	void idle(void) {
		delete ready;
		ready = NULL;
	}

	void buildDevDesc(void) {
		const char *desc = "<nonsense></nonsense>";
		sent = 0;
		pktlen = 24 + ((strlen(desc) + 3) & ~ 3);

		memset(packet, 0 , MAX_PACKET_SIZE);

		uint32_t *field = (uint32_t *) packet;

		*field++ = 8 + ((strlen(desc) + 3) & ~3);
		*field++ = ADARA::PacketType::DEVICE_DESC_V0;
		*field++ = time(NULL) - ADARA::EPICS_EPOCH_OFFSET;
		*field++ = 0;

		*field++ = dev_id;
		*field++ = strlen(desc);

		strcpy((char *) field, desc);
	}

	void buildUpdate(PVUpdate &u) {
		sent = 0;
		pktlen = 32;

		uint32_t *field = (uint32_t *) packet;

		*field++ = 16;
		*field++ = ADARA::PacketType::VAR_VALUE_U32_V0;
		*field++ = u.ts.tv_sec;
		*field++ = u.ts.tv_nsec;

		*field++ = dev_id;
		*field++ = var_id;
		*field++ = 0;	/* Status/Severity */
		*field = u.value;

		updates.pop_front();
	}

	void writable(void) {
		if (pktlen) {
			ssize_t rc = write(fd, packet + sent, pktlen);
			if (rc < 0) {
				if (errno == EAGAIN || errno == EINTR)
					return;
				if (errno == EPIPE || errno == ECONNRESET) {
					delete this;
					return;
				}

				std::cerr << "Fatal write error " << errno
					<< std::endl;
				exit(1);
			}

			sent += rc;
			pktlen -= rc;

			if (pktlen)
				return;
		}

		/* XXX need to send heartbeat packets */

		if (updates.empty()) {
			idle();
			return;
		}

		PVUpdate &u = updates.front();
		buildUpdate(u);
		writable();
	}

	int 		fd;
	uint32_t	sent;
	uint32_t	pktlen;
	ReadyAdapter *	ready;
	PVUpdate::List	updates;
	uint8_t		packet[MAX_PACKET_SIZE];

	static List	all_clients;
};

Client::List Client::all_clients;


/* The Listener class just listens for new connections, and creates
 * a Client object for each one.
 */
class Listener {
public:
	Listener();
	void newConnection(void);

	ReadyAdapter *	m_fdreg;
	int		m_fd;
};

Listener::Listener()
{
	struct addrinfo hints, *ai;
	std::string service, msg;
	int val, rc, flags;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	/* Overkill, really, for test harness, but copied from LiveServer... */
	service = boost::lexical_cast<std::string>(listen_port);
	rc = getaddrinfo(NULL, service.c_str(), &hints, &ai);
	if (rc) {
		msg = "Unable to convert service '";
		msg += service;
		msg += "' to a port: ";
		msg += gai_strerror(rc);
		throw std::runtime_error(msg);
	}

	m_fd = socket(ai->ai_addr->sa_family, SOCK_STREAM, 0);
	if (m_fd < 0) {
		msg = "Unable to create socket: ";
		msg += strerror(errno);
		goto error;
	}

	flags = fcntl(m_fd, F_GETFL, NULL);
	if (flags < 0 || fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		msg = "Unable to set socket non-blocking: ";
		msg += strerror(errno);
		goto error_fd;
	}

	val = 1;
	if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) < 0) {
		msg = "Unable to SO_REUSEADDR: ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (bind(m_fd, ai->ai_addr, ai->ai_addrlen)) {
		msg = "Unable to bind to port ";
		msg += service;
		msg += ": ";
		msg += strerror(errno);
		goto error_fd;
	}

	if (listen(m_fd, 128)) {
		msg = "Unable to listen: ";
		msg += strerror(errno);
		goto error_fd;
	}

	try {
		m_fdreg = new ReadyAdapter(m_fd, fdrRead,
					boost::bind(&Listener::newConnection,
						    this));
	} catch(...) {
		close(m_fd);
		freeaddrinfo(ai);
		throw;
	}

	return;

error_fd:
	close(m_fd);

error:
	freeaddrinfo(ai);
	throw std::runtime_error(msg);
}

void Listener::newConnection(void)
{
	int rc;

	rc = accept4(m_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (rc < 0) {
		int e = errno;

		if (e == EINTR || e == EAGAIN || e == EWOULDBLOCK ||
							e == ECONNABORTED) {
			/* Not really an error */
			return;
		}

		if (e == ENOBUFS || e == ENOMEM || e == EMFILE || e == ENFILE) {
			/* TODO log no descriptors */
			return;
		}

		std::string msg("Listener::fdReady accept error: ");
		msg += strerror(e);
		throw std::runtime_error(msg);
	}

	/* Client self-manages */
	new Client(rc);
}


/* PVGenerator is called periodically by the EPICS timer system to
 * create the state for the next update. We add a copy of the generated
 * update to each client's pending list.
 */
class PVGenerator : public epicsTimerNotify {
public:
	PVGenerator() :
		timer(fileDescriptorManager.createTimer()), m_value(1)
	{
		timer.start(*this, update_interval + timer_fudge);
	}

	expireStatus expire(const epicsTime &currentTime) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		PVUpdate u(now, m_value++);
		/* Zero is used for the initial value when a client connects */
		if (m_value == 0)
			m_value = 1;

		Client::List::iterator it;
		for (it = Client::all_clients.begin();
					it != Client::all_clients.end(); ++it) {
			if ((*it)->updates.empty())
				(*it)->wantWrite();
			(*it)->updates.push_back(u);
		}

		return expireStatus(restart, update_interval + timer_fudge);
	}

	epicsTimer &	timer;
	uint32_t	m_value;
};

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("hz,r", po::value<double>(), "Pulse generation rate")
		("port,p", po::value<uint16_t>(), "Listening port")
		("devid,d", po::value<uint32_t>(), "Device ID")
		("varid,V", po::value<uint32_t>(), "Variable ID")
		("verbose,v", po::bool_switch(), "Add verbose output");

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch (po::unknown_option e) {
		std::cerr << argv[0] << ": " << e.what() << std::endl
			<< std::endl << desc << std::endl;
		exit(2);
	} catch (po::invalid_option_value e) {
		std::cerr << argv[0] << ": " << e.what() << std::endl
			<< std::endl << desc << std::endl;
		exit(2);
        }

	if (vm.count("help")) {
		std::cerr << desc << std::endl;
		exit(2);
	}

	verbose = !!vm.count("verbose");
	if (vm.count("hz")) {
		double hz = vm["hz"].as<double>();
		if (hz < 0.0000001) {
			std::cerr << argv[0] << ": HZ must be non-zero"
				<< std::endl;
			exit(2);
		}

		update_interval = 1.0 / hz;
	}

	if (vm.count("port"))
		listen_port = vm["port"].as<uint16_t>();
	if (vm.count("devid"))
		dev_id = vm["devid"].as<uint32_t>();
	if (vm.count("varid"))
		var_id = vm["varid"].as<uint32_t>();
}

static void block_signals(void)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
}

int main(int argc, char **argv)
{
	parse_options(argc, argv);

	block_signals();

	Listener	listner;
	PVGenerator	gen;

	for (;;)
		fileDescriptorManager.process(1000.0);
	return 0;
}
