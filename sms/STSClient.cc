#include <unistd.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <stdint.h>

#include <boost/bind.hpp>
#include <string>

#include "EPICS.h"
#include "STSClient.h"
#include "STSClientMgr.h"
#include "ReadyAdapter.h"
#include "Logging.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "utils.h"

static LoggerPtr logger(Logger::getLogger("SMS.STSClient"));

#define INITIAL_BUFFER_SIZE	4096
#define MAX_PACKET_SIZE		(128 * 1024)

double STSClient::m_heartbeat_interval = 5.0;
unsigned int STSClient::m_max_send_chunk = 2 * 1024 * 1024;

void STSClient::config(const boost::property_tree::ptree &conf)
{
	m_heartbeat_interval = conf.get<double>("stsclient.heartbeat", 5.0);
	std::string chunk = conf.get<std::string>("stsclient.maxsend", "2M");
	try {
		m_max_send_chunk = parse_size(chunk);
	} catch (std::runtime_error e) {
		std::string msg("Unable to parse STS max send: ");
		msg += e.what();
		throw std::runtime_error(msg);
	}
}

STSClient::STSClient(int fd, StorageContainer::SharedPtr &run,
		     STSClientMgr &mgr) :
	ADARA::POSIXParser(INITIAL_BUFFER_SIZE, MAX_PACKET_SIZE),
	m_mgr(mgr), m_sts_fd(fd), m_file_fd(-1), m_cur_offset(0), m_run(run),
	m_send_paused_data(m_mgr.m_send_paused_data),
	m_read(new ReadyAdapter(fd, fdrRead,
				boost::bind(&STSClient::readable, this))),
	m_write(new ReadyAdapter(fd, fdrWrite,
				 boost::bind(&STSClient::writable, this))),
	m_timer(new TimerAdapter<STSClient>(this, &STSClient::sendHeartbeat)),
	m_disp(STSClientMgr::CONNECTION_LOSS)
{
	INFO("Initiating Translation of " << m_run->runNumber()
		<< " SendPausedData=" << m_send_paused_data);
	run->getFiles(m_files);
	if (run->active()) {
		m_contConnection = run->connect(
			boost::bind(&STSClient::fileAdded, this, _1));
		if (run->file()) {
			m_fileConnection = run->file()->connect(
				boost::bind(&STSClient::fileUpdated, this, _1));
		}
	}
}

STSClient::~STSClient()
{
	m_contConnection.disconnect();
	m_fileConnection.disconnect();
	m_timer->cancel();
	close(m_sts_fd);
	if (m_file_fd != -1)
		m_files.front()->put_fd();

	/* Inform the manager of our final status */
	m_mgr.clientComplete(m_run, m_disp);
}

bool STSClient::sendHeartbeat(void)
{
	/* TODO send hearbeat packet */
	return true;
}

void STSClient::writable(void)
{
	// DEBUG("writable() entry");

	std::list<StorageFile::SharedPtr>::iterator it;
	ssize_t len, rc;

	/* We're trying to send data, so cancel the hearbeat timer. We'll
	 * re-enable it if we go idle.
	 */
	m_timer->cancel();

	for (it = m_files.begin(); it != m_files.end(); ) {

		StorageFile::SharedPtr &f = *it;

		// Ignore Paused Run Files, as Optionally Configured... :-D
		if (f->paused() && !m_send_paused_data) {
			it = m_files.erase(it);
			continue;
		}

		if (m_file_fd == -1) {
			try {
				m_file_fd = f->get_fd();
			} catch (std::runtime_error re) {
				ERROR("Unable to open file number " << f->fileNumber()
					<< " (pause file number " << f->pauseFileNumber() << ")"
					<< " for run " << m_run->runNumber()
					<< ": " << re.what());
				m_disp = STSClientMgr::PERMAMENT_FAIL;
				delete this;
				return;
			}
		}

		len = f->size() - m_cur_offset;
		if (len > m_max_send_chunk)
			len = m_max_send_chunk;

		if (!m_cur_offset) {
			DEBUG("writable(): sending new file=" << f->path()
				<< " size=" << f->size());
		}

		rc = sendfile(m_sts_fd, m_file_fd, &m_cur_offset, len);
		if (rc < 0) {
			if (errno == EAGAIN || errno == EINTR)
				goto more;

			if (errno == EPIPE || errno == ECONNRESET) {
				WARN("Lost connection to STS for run "
				     << m_run->runNumber());
			} else {
				int e = errno;
				ERROR("Run " << m_run->runNumber()
					<< " had fatal sendfile error error: "
					<< strerror(e));
			}

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

	if (!m_run->active()) {
		/* We've sent everything from this file, so shutdown the
		 * write side of our socket. This will signal an EOF to
		 * STS, so we'll notice if we're trying to resend a
		 * corrupted file without a proper ending RunStatus packet.
		 */
#if 0
		if (shutdown(m_sts_fd, SHUT_WR)) {
			int e = errno;
			WARN("shutdown() failed: " << strerror(e));
		}
#else
		sendDataDone();
#endif
	}

idle:
	/* We don't need to know when the socket is writable unless we
	 * have data waiting to be sent. Go ahead and start the heartbeat
	 * as well, as we have no guarantees when we'll see more data.
	 */
	m_write.reset();
	m_timer->start(m_heartbeat_interval);
	// DEBUG("writable() idle exit");
	return;

more:
	/* We have more data to write, so make sure we get notified when
	 * there is room in the socket buffer. We also do not need to send
	 * any heartbeat packets, as we have a full pipe.
	 */
	if (!m_write.get())
		m_write.reset(new ReadyAdapter(m_sts_fd, fdrWrite,
				boost::bind(&STSClient::writable, this)));
	// DEBUG("writable() more exit");
}

void STSClient::sendDataDone(void)
{
	uint32_t data_done_pkt[4] = { 0, 0x00400C00, 0, 0 };

	std::string log_info;

	DEBUG("Sending Data Done to STS for run " << m_run->runNumber());

	bool send_status = Utils::sendBytes( m_sts_fd,
		(char *) data_done_pkt, sizeof( data_done_pkt ), log_info );

	// Dang, it didn't work... ;-b
	if ( !send_status ) {

		ERROR("sendDataDone() failed! log_info=(" << log_info << ")");

		// Resort to the dreaded shutdown() system call,
		// which doesn't appear to work through our network setup... ;-b
		if (shutdown(m_sts_fd, SHUT_WR)) {
			int e = errno;
			ERROR("shutdown() failed: " << strerror(e));
		}
	}
}

void STSClient::fileAdded(StorageFile::SharedPtr &f)
{
        /* We don't need to try to start sending from this file just yet
	 * (assuming it is the front of our list), as we'll get an update
	 * notification very soon.
	 */
	m_files.push_back(f);
	m_fileConnection = f->connect(boost::bind(&STSClient::fileUpdated,
						  this, _1));
}

void STSClient::fileUpdated(const StorageFile &f)
{
	// DEBUG("fileUpdated() entry");

	/* The current file just got updated; if we're not already waiting
	 * for buffer space in the socket, try to send the new data
	 */
	if (!m_write.get())
		writable();

	if (!f.active())
		m_fileConnection.disconnect();

	// DEBUG("fileUpdated() exit");
}

void STSClient::readable(void)
{
	DEBUG("readable() entry");

	std::string log_info;

	bool ok = false;

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		ok = read(m_sts_fd, log_info, 4000, MAX_PACKET_SIZE);
		if (!ok && m_disp == STSClientMgr::CONNECTION_LOSS) {
			/* We log the reason for closing the connection
			 * elsewhere, except for the default case of an
			 * unexpected connection loss.
			 * Take care of that case here.
			 */
			WARN("Lost connection to STS for run " << m_run->runNumber()
				 << " log_info=(" << log_info << ")");
		}
	} catch (ADARA::invalid_packet e) {
		WARN("Got invalid packet from STS: " << e.what());
		m_disp = STSClientMgr::INVALID_PROTOCOL;
		ok = false;
	}

	/* Hit an EOF, or our handlers indicated it was time to stop, so
	 * kill ourselves off. We cannot do this from the handlers, as
	 * ADARA::Parser::read() will modify member variables after
	 * calling them.
	 */
	if (!ok)
		delete this;

	DEBUG("readable() exit");
}

bool STSClient::rxPacket(const ADARA::Packet &pkt)
{
	/* We only care about translation complete packets; everything else
	 * is an error and we should drop the connection.
	 */
	if (pkt.base_type() == ADARA::PacketType::TRANS_COMPLETE_TYPE)
		return ADARA::Parser::rxPacket(pkt);

	m_disp = STSClientMgr::TRANSIENT_FAIL;
	WARN("Received unexpected packet type 0x"
		<< std::hex << pkt.type() << std::dec);
	return true;
}

bool STSClient::rxOversizePkt(const ADARA::PacketHeader *hdr,
			       const uint8_t *UNUSED(chunk),
			       unsigned int UNUSED(chunk_offset),
			       unsigned int chunk_len)
{
	// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
	/* Ok, this is much bigger than we expected, stop processing
	 * this stream and close the connection.
	 */
	m_disp = STSClientMgr::TRANSIENT_FAIL;
	if (hdr) {
		ERROR("Received Unexpected Oversize Packet"
			<< " at " << hdr->timestamp().tv_sec
			<< "." << hdr->timestamp().tv_nsec
			<< " of type 0x" << std::hex << hdr->type() << std::dec
			<< " payload_length=" << hdr->payload_length()
			<< " max=" << MAX_PACKET_SIZE);
	} else {
		ERROR("Received Unexpected Oversize Packet"
			<< " chunk_len=" << chunk_len
			<< " max=" << MAX_PACKET_SIZE);
	}
	return true;
}

bool STSClient::rxPacket(const ADARA::TransCompletePkt &pkt)
{
	if (!pkt.status()) {
		m_disp = STSClientMgr::SUCCESS;
		if (pkt.reason().length()) {
			INFO("Run " << m_run->runNumber() << " successfully "
				"translated with status message \'"
				<< pkt.reason() << "'");
		} else {
			INFO("Run " << m_run->runNumber() << " successfully "
				"translated");
		}
	} else if (pkt.status() < 0x8000) {
		/* TODO remove magic numbers */
		m_disp = STSClientMgr::TRANSIENT_FAIL;
		if (pkt.reason().length()) {
			WARN("Run " << m_run->runNumber() << " had a transient "
				"failure, status 0x" << std::hex
				<< pkt.status() << std::dec << ", message \'"
				<< pkt.reason() << "'");
		} else {
			WARN("Run " << m_run->runNumber() << " had a transient "
				"failure, status 0x" << std::hex
				<< pkt.status() << std::dec);
		}
	} else {
		m_disp = STSClientMgr::PERMAMENT_FAIL;
		if (pkt.reason().length()) {
			ERROR("Run " << m_run->runNumber() << " had a "
				"permament failure, status 0x" << std::hex
				<< pkt.status() << std::dec << ", message \'"
				<< pkt.reason() << "'");
		} else {
			ERROR("Run " << m_run->runNumber() << " had a "
				"permament failure, status 0x" << std::hex
				<< pkt.status() << std::dec);
		}
	}
	return true;
}
