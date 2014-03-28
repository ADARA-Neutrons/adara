#include <sys/types.h>
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
static unsigned int events_per_pulse = 100;
static unsigned int num_monitors = 1;
static unsigned int monitor_events = 1;
static unsigned int num_choppers = 1;
static unsigned int chopper_events = 1;
static double pulse_interval = 1.0 / 60;
static std::vector<uint32_t> source_ids;
static double min_tof = 0.010;
static double max_tof = 0.015;
static uint32_t tof_base, tof_range;
static uint32_t num_pixels = 4096;
static uint32_t events_per_source;

/* Sigh... EPICS timers only think there is 10ms resolution for the timers
 * on Linux (believing the reported clock rate of 100 Hz), and subtracts
 * half the resolution from the user-provided delay. Account for this so
 * that we don't get 85 Hz when we ask for 60 Hz operation. Hardcoded, as
 * there is no public interface to find out what EPICS thinks the resolution
 * is.
 */
static double timer_fudge = 0.005;

static uint32_t genTOF(void)
{
	uint32_t tof = random() % tof_range;
	return tof + tof_base;
}

/* The Pulse class contains the state information needed for a
 * client to generate the packets for a given pulse. Each client will
 * get its own copy of the object, so it can mutate the members to keep
 * track of how much it has sent and how much remains.
 */
class Pulse {
public:
	typedef std::list<Pulse> List;

	Pulse(const struct timespec &t, uint32_t intra, uint32_t c) :
		ts(t), intrapulse(intra), nevents(events_per_pulse),
		mevents(monitor_events), cevents(chopper_events), mon_index(0),
		chopper_index(0), src_index(0), src_events(events_per_source),
		cycle(c), rtdl_pending(true), pkt_seq(0)
	{
		ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;
	}

	struct timespec ts;
	uint32_t	intrapulse;
	uint32_t	nevents;
	uint32_t	mevents;
	uint32_t	cevents;
	uint32_t	mon_index;
	uint32_t	chopper_index;
	uint32_t	src_index;
	uint32_t	src_events;
	uint32_t	cycle;
	bool		rtdl_pending;
	uint16_t	pkt_seq;
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
		fd(f), pktlen(0), ready(NULL), dsp_seq(source_ids.size(), 0)
	{
		if (verbose)
			std::cout << "Client fd " << fd << " connected\n";
		all_clients.push_back(this);
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

	uint32_t *buildCommon(uint32_t *pkt, Pulse &p) {
		/* pulse charge and beam flavor first */
		if (p.cycle) {
			*pkt = (uint32_t) ADARA::PulseFlavor::NORMAL << 24;

		} else {
			/* cycle 0 means no beam */
			*pkt = (uint32_t) ADARA::PulseFlavor::NO_BEAM << 24;
		}

		if (p.cycle != 1) {
			/* 18.47e-6 C => 18.47 uC => 18470000 pC
			 * charge is in units of 10 pC
			 *
			 * Charge lags one cycle, so cycle 1 will have
			 * no charge because cycle 0 had no beam.
			 */
			*pkt |= 1847000;
		}
		pkt++;

		/* next fields:
		 * timing info + cycle (TODO actual timing info)
		 * intrapulse time, 100ns units
		 * tof offset (bit 31 indicates corrected)
		 * ring period (hardcoded for now)
		 */
		*pkt++ = p.cycle;
		*pkt++ = p.intrapulse / 100;
		*pkt++ = 0x80000000;

		return pkt;
	}

	void buildRTDL(Pulse &p) {
		sent = 0;
		pktlen = 136;

		uint32_t *field = (uint32_t *) packet;
		*field++ = 120;
		*field++ = ADARA::PacketType::RTDL_V0;
		*field++ = p.ts.tv_sec;
		*field++ = p.ts.tv_nsec;
		memset(field, 0, 120);
		field = buildCommon(field, p);
		*field++ = (4 << 24) | 964728;
		p.rtdl_pending = false;
		dsp_seq[0]++;
	}

	void buildRaw(Pulse &p) {
		sent = 0;
		pktlen = 40;

		uint32_t *field = (uint32_t *) packet;
		uint32_t *payload_len = field;

		*field++ = 24;
		*field++ = ADARA::PacketType::RAW_EVENT_V0;
		*field++ = p.ts.tv_sec;
		*field++ = p.ts.tv_nsec;

		*field++ = source_ids[p.src_index];
		uint32_t *eop_field = field;
		*field++ = (p.pkt_seq++ << 16) | dsp_seq[p.src_index]++;
		field = buildCommon(field, p);

		/* All choppers and monitors go into the first source, then
		 * we'll start filling with real (fake) data.
		 */
		while (p.chopper_index < num_choppers) {
			while (p.cevents) {
				p.cevents--;
				*field++ = 1 + (chopper_events - p.cevents);
				*field++ = (p.chopper_index << 16) |
					(7 << 28) | 1;
				pktlen += 8;
				*payload_len += 8;

				if (pktlen >= (MAX_PACKET_SIZE - 8))
					return;
			}

			/* Finished this chopper, start the next one. */
			p.cevents = chopper_events;
			p.chopper_index++;
		}

		while (p.mon_index < num_monitors) {
			while (p.mevents) {
				/* Fake TOF at a quarter of the detectors. */
				p.mevents--;
				*field++ = genTOF() / 4;
				*field++ = (p.mon_index << 16) | (4 << 28) | 1;
				pktlen += 8;
				*payload_len += 8;

				if (pktlen >= (MAX_PACKET_SIZE - 8))
					return;
			}

			/* Finished this monitor, start the next one. */
			p.mevents = monitor_events;
			p.mon_index++;
		}

		/* Push events into this packt until we run out of
		 * events, have enough for this source, or the
		 * packet gets too big.
		 */
		while (p.nevents && p.src_events) {
			p.nevents--;
			p.src_events--;
			*field++ = genTOF();
			*field++ = random() % num_pixels;
			pktlen += 8;
			*payload_len += 8;

			if (pktlen >= (MAX_PACKET_SIZE - 8))
				break;
		}

		/* Ok, lets see if we need to mark this pulse as
		 * completed for this source. We must have events_per_source
		 * calculated such that p.nevents will run out before
		 * p.src_events.
		 */
		if (!p.src_events || !p.nevents) {
			/* This packet marks the end-of-pulse for this source */
			*eop_field |= 0x80000000;
			p.src_index++;
			p.src_events = events_per_source;
			p.pkt_seq = 0;
		}

		if (!p.nevents && p.src_index == source_ids.size())
			pulses.pop_front();
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

		if (pulses.empty()) {
			idle();
			return;
		}

		Pulse &p = pulses.front();
		if (p.rtdl_pending) {
			buildRTDL(p);
			writable();
			return;
		}

		buildRaw(p);
		writable();
	}

	int 		fd;
	uint32_t	sent;
	uint32_t	pktlen;
	ReadyAdapter *	ready;
	Pulse::List	pulses;
	uint8_t		packet[MAX_PACKET_SIZE];

	std::vector<uint16_t>	dsp_seq;
	static List		all_clients;
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


/* PulseGenerator is called periodically by the EPICS timer system to
 * create the state for the next pulse. We add a copy of the generated
 * pulse to each client's pending list.
 */
class PulseGenerator : public epicsTimerNotify {
public:
	PulseGenerator() :
		timer(fileDescriptorManager.createTimer()), cycle(0)
	{
		clock_gettime(CLOCK_REALTIME, &last_pulse);
		timer.start(*this, pulse_interval + timer_fudge);
	}

	expireStatus expire(const epicsTime &currentTime) {
		struct timespec now, diff;
		uint32_t intrapulse;

		clock_gettime(CLOCK_REALTIME, &now);
		diff.tv_sec = now.tv_sec - last_pulse.tv_sec;
		diff.tv_nsec = now.tv_nsec - last_pulse.tv_nsec;
		if (diff.tv_nsec < 0) {
			diff.tv_sec -= 1;
			diff.tv_nsec += 1000 * 1000 * 1000;
		}

		intrapulse = diff.tv_sec * 1000 * 1000 * 1000;
		intrapulse += diff.tv_nsec;

		last_pulse = now;

		Pulse p(now, intrapulse, cycle++);
		cycle %= 600;

		Client::List::iterator it;
		for (it = Client::all_clients.begin();
					it != Client::all_clients.end(); ++it) {
			if ((*it)->pulses.empty())
				(*it)->wantWrite();
			(*it)->pulses.push_back(p);
		}

		return expireStatus(restart, pulse_interval + timer_fudge);
	}

	epicsTimer &	timer;
	struct timespec last_pulse;
	uint32_t	cycle;
};

static void parse_options(int argc, char **argv)
{
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "Show usage information")
		("hz,r", po::value<double>(), "Pulse generation rate")
		("events,e", po::value<unsigned int>(&events_per_pulse),
				"Events per pulse")
		("source,s", po::value< std::vector<uint32_t> >(&source_ids),
				"Source ID(s) to use")
		("monitors,m", po::value<unsigned int>(&num_monitors),
				"Number of beam monitors")
		("mevents,M", po::value<unsigned int>(&monitor_events),
				"Number of beam monitor events per pulse")
		("choppers,c", po::value<unsigned int>(&num_choppers),
				"Number of choppers")
		("cevents,C", po::value<unsigned int>(&chopper_events),
				"Number of chopper events per pulse")
		("port,p", po::value<uint16_t>(&listen_port), "Listening port")
		("mintof,t", po::value<double>(&min_tof),
				"Minumum time-of-flight")
		("maxtof,T", po::value<double>(&max_tof),
				"Maximum time-of-flight")
		("pixels,P", po::value<uint32_t>(&num_pixels),
				"Number of pixels to simulate")
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

		pulse_interval = 1.0 / hz;
	}

	if (min_tof >= max_tof) {
		std::cerr << argv[0] << ": mintof must be less than maxtof"
				<< std::endl;
		exit(2);
	}

	/* Convert TOF range into 100ns based integers, suitable for
	 * random() % range.
	 */
	tof_base = (uint32_t) (min_tof * 1e9 / 100);
	tof_range = (uint32_t) ((max_tof - min_tof) * 1e9 / 100);

	if (source_ids.empty())
		source_ids.push_back(listen_port);

	events_per_source = (events_per_pulse + source_ids.size() - 1);
	events_per_source /= source_ids.size();
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
	PulseGenerator	gen;

	for (;;)
		fileDescriptorManager.process(1000.0);
	return 0;
}
