
#include <unistd.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <stdint.h>
#include <string>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "LiveServer.h"
#include "LiveClient.h"
#include "StorageManager.h"
#include "StorageFile.h"
#include "Logging.h"
#include "utils.h"

static LoggerPtr logger(Logger::getLogger("SMS.LiveClient"));

RateLimitedLogging::History RLLHistory_LiveClient;

// Rate-Limited Logging IDs...
#define RLL_LIVE_CLIENT_READ_EXCEPTION   0

/* We only need to receive the hello packet, which is very small, so don't
 * allocate huge buffers for us.
 */
#define MAX_PKT_SIZE 1024

unsigned int LiveClient::m_max_send_chunk = 2 * 1024 * 1024;
double LiveClient::m_hello_timeout = 30.0;

void LiveClient::config(const boost::property_tree::ptree &conf)
{
	m_hello_timeout = conf.get<double>("livestream.hello_timeout", 30.0);

	std::string size = conf.get<std::string>("livestream.maxsend", "2M");
	try {
		m_max_send_chunk = parse_size(size);
	} catch (std::runtime_error e) {
		std::string msg("Unable to parse livestream max send size: ");
		msg += e.what();
		ERROR("config(): " << msg);
		throw std::runtime_error(msg);
	}
}

LiveClient::LiveClient(LiveServer *server, int fd) : 
	ADARA::POSIXParser(MAX_PKT_SIZE, MAX_PKT_SIZE),
	m_server(server), m_read(NULL), m_write(NULL), m_hello_received(false),
	m_client_fd(fd), m_file_fd(-1)
{
	char hostname[1024], service[256];
	struct sockaddr_in6 sa;
	socklen_t saLen = sizeof(sa);
	int rc;

	if (getpeername(m_client_fd, (struct sockaddr *) &sa, &saLen) < 0) {
		int e = errno;
		ERROR("Unable to get peer name: " << strerror(e));
		throw std::runtime_error("Unable to create client");
	}

	if (saLen > sizeof(sa)) {
		std::string msg("peer name is too long");
		ERROR(msg);
		throw std::runtime_error(msg);
	}

	rc = getnameinfo((struct sockaddr *) &sa, saLen, hostname,
			 sizeof(hostname), service, sizeof(service),
			 NI_NUMERICHOST | NI_NUMERICSERV);
	if (rc) {
		std::string msg("Unable to name client: ");
		msg += gai_strerror(rc);
		ERROR(msg);
		throw std::runtime_error(msg);
	}

	m_clientName = hostname;
	m_clientName += ":";
	m_clientName += service;

	SMSControl *ctrl = SMSControl::getInstance();

	m_clientId = ctrl->registerLiveClient(m_clientName,
		m_pvName, m_pvRequestedStartTime, m_pvCurrentFilePath, m_pvStatus);

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvName->update(m_clientName, &now);
	m_pvRequestedStartTime->update(-1, &now);
	m_pvCurrentFilePath->update("(none)", &now);
	m_pvStatus->waiting_for_connect_ack();

	m_send_paused_data = m_server->getSendPausedData();

	m_read = new ReadyAdapter(m_client_fd, fdrRead,
				  boost::bind(&LiveClient::readable, this));

	INFO("client " << m_clientName << " ready to connect"
		<< " SendPausedData=" << m_send_paused_data);

	try {
		m_timer = new TimerAdapter<LiveClient>(this);
		m_timer->start(m_hello_timeout);
	}
	catch (std::exception &e) {
		ERROR("Exception in LiveClient() Hello Timer/Timeout"
			<< " client=" << m_clientName << " - " << e.what());
		delete m_read;
		throw;
	}
	catch (...) {
		ERROR("Unknown Exception in LiveClient() Hello Timer/Timeout"
			<< " client=" << m_clientName);
		delete m_read;
		throw;
	}

	INFO("client " << m_clientName << " connected");
}

LiveClient::~LiveClient()
{
	INFO("client " << m_clientName << " disconnected");

	if ( m_clientId >= 0 ) {
		m_pvStatus->disconnected();

		SMSControl *ctrl = SMSControl::getInstance();
		ctrl->unregisterLiveClient(m_clientId);

		m_clientId = -1;
	}

	m_mgrConnection.disconnect();
	m_contConnection.disconnect();
	m_fileConnection.disconnect();
	delete m_read;
	delete m_write;
	delete m_timer;
	close(m_client_fd);
	if (m_file_fd != -1)
		m_files.front().first->put_fd();
}

bool LiveClient::timerExpired(void)
{
	WARN("client " << m_clientName << " did not send hello");

	if ( m_clientId >= 0 ) {
		m_pvStatus->failed();

		SMSControl *ctrl = SMSControl::getInstance();
		ctrl->unregisterLiveClient(m_clientId);

		m_clientId = -1;
	}

	delete this;
	return false;
}

void LiveClient::writable(void)
{
	// DEBUG("writable() entry");

	FileList::iterator it;
	ssize_t len, rc;

	for (it = m_files.begin(); it != m_files.end(); ) {

		StorageFile::SharedPtr &f = it->first;

		// Ignore Paused Run Files (for now...)
		m_send_paused_data = m_server->getSendPausedData();
		if (f->paused() && !m_send_paused_data) {
			it = m_files.erase(it);
			continue;
		}

		if (m_file_fd == -1) {
			try {
				m_file_fd = f->get_fd();
			} catch (std::runtime_error re) {
				std::string cname;
				StorageContainer::SharedPtr c;

				c = f->owner().lock();
				if (c)
					cname = c->name();
				else
					cname = "(unknown)";

				ERROR(m_clientName << ": Unable to open file "
				      << f->fileNumber() << " for container "
				      << cname << ": " << re.what());
				delete this;
				return;
			}
		}

		off_t &cur_offset = it->second;
		len = f->size() - cur_offset;
		if (len > m_max_send_chunk)
			len = m_max_send_chunk;

		if (!cur_offset) {
			DEBUG("writable(): sending new file=" << f->path()
				<< " size=" << f->size() << " to client " << m_clientName);
			if ( m_clientId >= 0 ) {
				struct timespec now;
				clock_gettime(CLOCK_REALTIME, &now);
				m_pvCurrentFilePath->update(f->path(), &now);
			}
		}

		rc = sendfile(m_client_fd, m_file_fd, &cur_offset, len);
		if (rc < 0) {
			if (errno == EAGAIN || errno == EINTR)
				goto more;

			/* Only complain if it's not the client going away */
			int e = errno;
			if (errno != EPIPE && errno != ECONNRESET) {
				ERROR("client " << m_clientName
				      << ": Fatal error during sendfile: "
				      << strerror(e));
			}
			else {
				INFO("client " << m_clientName << " connection broken: "
					<< strerror(e));
			}

			/* Nothing further to do, just clean ourselves up. */
			delete this;
			return;
		}

		/* If we get rc == 0, then either the file shrunk or
		 * the client went away on us. The file should never
		 * shrink on us, and we'll handle the client going
		 * away on read or via an error return above. We'll just
		 * pretend we got a non-zero rc.
		 */

		/* Did we catch up to the current EOF? */
		if (cur_offset != f->size())
			goto more;

		/* At EOF, do we expect to get more? */
		if (f->active())
			goto idle;

		/* We finished this file, and there will be no more data
		 * coming for it; close it out and go to the next one.
		 */
		m_file_fd = -1;
		f->put_fd();
		it = m_files.erase(it);
	}

idle:
	/* We don't need to know when the socket is writable unless we
	 * have data waiting to be sent.
	 */
	if (m_write) {
		delete m_write;
		m_write = NULL;
	}
	// DEBUG("writable() idle exit");
	return;

more:
	/* We have more data to write, so make sure we get notified when
	 * there is room in the socket buffer.
	 */
	if (!m_write)
		m_write = new ReadyAdapter(m_client_fd, fdrWrite,
				boost::bind(&LiveClient::writable, this));
	// DEBUG("writable() more exit");
}

void LiveClient::containerChange(StorageContainer::SharedPtr &c, bool starting)
{
	if (starting)
		m_contConnection = c->connect(
				boost::bind(&LiveClient::fileAdded, this, _1));
	else
		m_contConnection.disconnect();
}

void LiveClient::historicalFile(StorageFile::SharedPtr &f, off_t start)
{
	/* This is an old file, so just put it on the list to be sent.
	 */
	m_files.push_back(std::make_pair(f, start));
}

void LiveClient::fileAdded(StorageFile::SharedPtr &f)
{
	/* We don't need to try to start sending from this file just yet
	 * (assuming it is the front of our list), as we'll get an update
	 * notification very soon.
	 */
	m_files.push_back(std::make_pair(f, 0));
	m_fileConnection = f->connect(boost::bind(&LiveClient::fileUpdated,
						  this, _1));
}

void LiveClient::fileUpdated(const StorageFile &f)
{
	// DEBUG("fileUpdated() entry");

	/* The current file just got updated; if we're not already waiting
	 * for buffer space in the socket, try to send the new data
	 */
	if (!m_write)
		writable();

	if (!f.active())
		m_fileConnection.disconnect();

	// DEBUG("fileUpdated() exit");
}

void LiveClient::readable(void)
{
	// DEBUG("readable() entry");

	std::string log_info;

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		if (!read(m_client_fd, log_info, 4000, MAX_PKT_SIZE)) {
			/* EOF or our handlers indicated it was time to stop,
			 * so kill ourselves off. We can't do this from the
			 * handlers, as ADARA::Parser::read() will modify
			 * member variables after calling the handlers.
			 */
			DEBUG("client " << m_clientName
				<< " error reading stream log_info=(" << log_info << ")");
			delete this;
		}
	} catch (std::runtime_error e) {
		/* Rate-limited logging of LiveClient read exception? */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_LiveClient,
				RLL_LIVE_CLIENT_READ_EXCEPTION, m_clientName,
				2, 10, 100, log_info ) ) {
			ERROR(log_info << "client " << m_clientName
				<< " exception reading stream: " << e.what());
			if ( m_clientId >= 0 ) {
				m_pvStatus->failed();

				SMSControl *ctrl = SMSControl::getInstance();
				ctrl->unregisterLiveClient(m_clientId);

				m_clientId = -1;
			}
		}
		delete this;
	}

	// DEBUG("readable() exit");
}

bool LiveClient::rxPacket(const ADARA::Packet &pkt)
{
	/* We only care about client hello packets; everything else is an
	 * error and we should drop the connection.
	 */
	if (pkt.base_type() == ADARA::PacketType::CLIENT_HELLO_TYPE)
		return ADARA::Parser::rxPacket(pkt);

	WARN("client " << m_clientName
	     << " sent us an unexpected packet type 0x"
	     << std::hex << pkt.type() << std::dec);
	return true;
}

bool LiveClient::rxOversizePkt(const ADARA::PacketHeader *hdr,
					const uint8_t *UNUSED(chunk),
					unsigned int UNUSED(chunk_offset),
					unsigned int chunk_len)
{
	// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
	/* Ok, this is much bigger than we expected, stop processing
	 * this stream and close the connection.
	 */
	if (hdr) {
		ERROR("LiveClient " << m_clientName << " sent us an Oversize Packet"
			<< " at " << hdr->timestamp().tv_sec
			<< "." << hdr->timestamp().tv_nsec
			<< " of type 0x" << std::hex << hdr->type() << std::dec
			<< " payload_length=" << hdr->payload_length()
			<< " max=" << MAX_PKT_SIZE);
	} else {
		ERROR("LiveClient " << m_clientName << " sent us an Oversize Packet"
			<< " chunk_len=" << chunk_len
			<< " max=" << MAX_PKT_SIZE);
	}
	return true;
}

bool LiveClient::rxPacket(const ADARA::ClientHelloPkt &pkt)
{
	StorageContainer::SharedPtr cur_cont;
	StorageFile::SharedPtr cur_file;

	m_timer->cancel();
	m_hello_received = true;

	INFO("LiveClient Hello Received from " << m_clientName
		<< ", Requested Start Time " << pkt.requestedStartTime());

	if ( m_clientId >= 0 ) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvRequestedStartTime->update(pkt.requestedStartTime(), &now);
		m_pvStatus->connected();
	}

	m_mgrConnection = StorageManager::onContainerChange(
		boost::bind(&LiveClient::containerChange, this, _1, _2));

	/* Request the system state at the given timestamp, or just prior.
	 */
	StorageManager::iterateHistory(pkt.requestedStartTime(),
				boost::bind(&LiveClient::historicalFile,
						this, _1, _2));

	/* Register for updates and notification of new files if we
	 * have anything active.
	 */
	cur_cont = StorageManager::container();
	if (cur_cont) {
		m_contConnection = cur_cont->connect(
				boost::bind(&LiveClient::fileAdded, this, _1));
		cur_file = cur_cont->file();
		if (cur_file) {
			/* StorageManager::iterateHistory() will have already
			 * added this file to our list with the appropriate
			 * offset, so just register for new updates.
			 */
			m_fileConnection = cur_file->connect(
					boost::bind(&LiveClient::fileUpdated,
						    this, _1));
		}
	}

	/* And try to send the data we've queued up.
	 */
	m_write = new ReadyAdapter(m_client_fd, fdrWrite,
				boost::bind(&LiveClient::writable, this));

	return false;
}
