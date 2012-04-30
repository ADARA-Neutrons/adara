#include <unistd.h>
#include <errno.h>
#include <sys/sendfile.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "LiveClient.h"
#include "StorageFile.h"

/* We only need to receive the hello packet, which is very small, so don't
 * allocate huge buffers for us.
 */
#define MAX_PKT_SIZE 1024

unsigned int LiveClient::m_max_send_chunk = 2 * 1024 * 1024;
double LiveClient::m_hello_timeout = 30.0;

LiveClient::LiveClient(int fd) : 
	ADARA::Parser(MAX_PKT_SIZE, MAX_PKT_SIZE),
	m_read(NULL), m_write(NULL), m_hello_received(false), m_cur_offset(0),
	m_client_fd(fd), m_file_fd(-1)
{
	m_read = new ReadyAdapter(m_client_fd, fdrRead,
				  boost::bind(&LiveClient::readable, this));

	try {
		m_timer = new TimerAdapter<LiveClient>(this);
		m_timer->start(m_hello_timeout);
	} catch (...) {
		delete m_read;
		throw;
	}
}

LiveClient::~LiveClient()
{
	m_mgrConnection.disconnect();
	m_contConnection.disconnect();
	m_fileConnection.disconnect();
	delete m_read;
	delete m_write;
	close(m_client_fd);
	if (m_file_fd != -1)
		m_files.front()->put_fd();
}

bool LiveClient::timerExpired(void)
{
	/* TODO log no hello from live client */
	delete this;
	return false;
}

void LiveClient::writable(void)
{
	std::list<boost::shared_ptr<StorageFile> >::iterator it;
	StorageFile *f;
	ssize_t len, rc;

	for (it = m_files.begin(); it != m_files.end(); ) {
		f = it->get();
		if (m_file_fd == -1)
			m_file_fd = f->get_fd();

		len = f->size() - m_cur_offset;
		if (len > m_max_send_chunk)
			len = m_max_send_chunk;

		rc = sendfile(m_client_fd, m_file_fd, &m_cur_offset, len);
		if (rc < 0) {
			if (errno == EAGAIN || errno == EINTR)
				goto more;

			if (errno == EPIPE || errno == ECONNRESET) {
				/* Client went away, just clean up */
				delete this;
				return;
			}

			std::string msg("Fatal error during sendfile: ");
			msg += strerror(errno);
			throw std::runtime_error(msg);
		}

		/* If we get rc == 0, then either the file shrunk or
		 * the client went away on us. The file should never
		 * shrink on us, and we'll handle the client going
		 * away on read or via an error return above. We'll just
		 * pretend we got a non-zero rc.
		 */

		/* Did we catch up to the current EOF? */
		if (m_cur_offset != f->size())
			goto more;

		/* At EOF, do we expect to get more? */
		if (f->active())
			goto idle;

		/* We finished this file, and there will be no more data
		 * coming for it; close it out and go to the next one.
		 */
		m_file_fd = -1;
		m_cur_offset = 0;
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
	return;

more:
	/* We have more data to write, so make sure we get notified when
	 * there is room in the socket buffer.
	 */
	if (!m_write)
		m_write = new ReadyAdapter(m_client_fd, fdrWrite,
				boost::bind(&LiveClient::writable, this));
}

void LiveClient::containerChange(StorageManager::ContainerSharedPtr &c,
				 bool starting)
{
	if (starting)
		m_contConnection = c->connect(
				boost::bind(&LiveClient::fileAdded, this, _1));
	else
		m_contConnection.disconnect();
}

void LiveClient::fileAdded(StorageContainer::FileSharedPtr &f)
{
	/* We don't need to try to start sending from this file just yet
	 * (assuming it is the front of our list), as we'll get an update
	 * notification very soon.
	 */
	m_files.push_back(f);
	m_fileConnection = f->connect(boost::bind(&LiveClient::fileUpdated,
						  this, _1));
}

void LiveClient::fileUpdated(const StorageFile &f)
{
	/* The current file just got updated; if we're not already waiting
	 * for buffer space in the socket, try to send the new data
	 */
	if (!m_write)
		writable();

	if (!f.active())
		m_fileConnection.disconnect();
}

void LiveClient::readable(void)
{
	/* TODO protect against invalid packets from clients */
	if (!read(m_client_fd, MAX_PKT_SIZE)) {
		/* EOF or our handlers indicated it was time to stop, so
		 * kill ourselves off. We can't do this from the handlers,
		 * as ADARA::Parser::read() will modify member variables
		 * after calling the handlers.
		 */
		delete this;
	}
}

bool LiveClient::rxPacket(const ADARA::Packet &pkt)
{
	/* We only care about client hello packets; everything else is an
	 * error and we should drop the connection.
	 */
	if (pkt.type() == ADARA::PacketType::CLIENT_HELLO_V0)
		return ADARA::Parser::rxPacket(pkt);

	/* TODO log unexpected packet */
	return true;
}

bool LiveClient::rxOversizePkt(const ADARA::PacketHeader *hdr,
			       const uint8_t *chunk,
			       unsigned int chunk_offset,
			       unsigned int chunk_len)
{
	/* Ok, this is much bigger than we expected, stop processing
	 * this stream and close the connection.
	 */
	/* TODO log oversize packet */
	return true;
}

bool LiveClient::rxPacket(const ADARA::ClientHelloPkt &pkt)
{
	StorageManager::ContainerSharedPtr cur_cont;
	StorageContainer::FileSharedPtr cur_file;

	/* TODO setup replay of historical data */
	m_timer->cancel();
	m_hello_received = true;

	m_mgrConnection = StorageManager::connect(
		boost::bind(&LiveClient::containerChange, this, _1, _2));

	/* TODO send current state of the system (ie, pixel map,
	 * runinfo, environment setup, etc) to the client
	 * before we send event data.
	 */

	cur_cont = StorageManager::container();
	if (cur_cont) {
		m_contConnection = cur_cont->connect(
				boost::bind(&LiveClient::fileAdded, this, _1));
		cur_file = cur_cont->file();
		if (cur_file) {
			m_fileConnection = cur_file->connect(
					boost::bind(&LiveClient::fileUpdated,
						    this, _1));
			m_files.push_back(cur_file);
			m_cur_offset = cur_file->size();
		}
	}

	return false;
}
