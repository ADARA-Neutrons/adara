
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.STCClient"));

#include <string>
#include <sstream>

#include <unistd.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <stdint.h>

#include <boost/bind.hpp>

#include "EPICS.h"
#include "STCClient.h"
#include "STCClientMgr.h"
#include "ReadyAdapter.h"
#include "SMSControl.h"
#include "ADARAUtils.h"
#include "ADARAPackets.h"
#include "utils.h"

#define INITIAL_BUFFER_SIZE	4096
#define MAX_PACKET_SIZE		(128 * 1024)

double STCClient::m_heartbeat_interval = 5.0;
unsigned int STCClient::m_max_send_chunk = 2 * 1024 * 1024;

void STCClient::config(const boost::property_tree::ptree &conf)
{
	m_heartbeat_interval = conf.get<double>("stcclient.heartbeat", 5.0);
	std::string chunk = conf.get<std::string>("stcclient.maxsend", "2M");
	try {
		m_max_send_chunk = parse_size(chunk);
	} catch (std::runtime_error e) {
		std::string msg("Unable to parse STC max send: ");
		msg += e.what();
		throw std::runtime_error(msg);
	}
}

STCClient::STCClient( int fd, StorageContainer::SharedPtr &run,
		STCClientMgr &mgr ) :
	ADARA::POSIXParser( INITIAL_BUFFER_SIZE, MAX_PACKET_SIZE ),
	m_mgr(mgr), m_stc_fd(fd), m_file_fd(-1), m_cur_offset(0), m_run(run),
	m_send_paused_data(m_mgr.m_send_paused_data),
	m_read(NULL), m_write(NULL), m_timer(NULL),
	m_disp(STCClientMgr::CONNECTION_LOSS), m_reason("")
{
	INFO("Initiating Translation of " << m_run->runNumber()
		<< " SendPausedData=" << m_send_paused_data);

	std::list<StorageFile::SharedPtr>::iterator it;

	SMSControl *ctrl = SMSControl::getInstance();

	m_timer = new TimerAdapter<STCClient>( this,
		&STCClient::sendHeartbeat );

	std::stringstream ss;
	try
	{
		m_read = new ReadyAdapter( m_stc_fd, fdrRead,
			boost::bind( &STCClient::readable, this ),
			ctrl->verbose() );
	}
	catch ( std::exception &e )
	{
		ss << "Exception Creating ReadyAdapter Read"
			<< " for Run " << m_run->runNumber() << " - " << e.what();
		ERROR( ss.str() );
		m_read = NULL; // just to be sure... ;-b
		goto exception;
	}
	catch (...)
	{
		ss << "Unknown Exception Creating ReadyAdapter Read"
			<< " for Run " << m_run->runNumber();
		ERROR( ss.str() );
		m_read = NULL; // just to be sure... ;-b
		goto exception;
	}

	try
	{
		m_write = new ReadyAdapter( m_stc_fd, fdrWrite,
			boost::bind( &STCClient::writable, this ),
			ctrl->verbose() );
	}
	catch ( std::exception &e )
	{
		ss << "Exception Creating ReadyAdapter Write"
			<< " for Run " << m_run->runNumber() << " - " << e.what();
		ERROR( ss.str() );
		m_write = NULL; // just to be sure... ;-b
		goto exception;
	}
	catch (...)
	{
		ss << "Unknown Exception Creating ReadyAdapter Write"
			<< " for Run " << m_run->runNumber();
		ERROR( ss.str() );
		m_write = NULL; // just to be sure... ;-b
		goto exception;
	}

	// Capture Any Existing Run Container Files...
	run->getFiles( m_files );

	DEBUG("STCClient(): Starting m_files.size() = " << m_files.size()
		<< " for Run Container " << run->name()
		<< " for Run " << m_run->runNumber());
	for ( it = m_files.begin() ; it != m_files.end() ; ++it )
	{
		DEBUG("STCClient(): m_files[]: " << (*it)->path()
			<< " for Run " << m_run->runNumber());
	}

	// Is This An Active Run Container That We Need to Connect
	// for File Added Notify...?
	if ( run->active() )
	{
		if ( ctrl->verbose() )
		{
			DEBUG("STCClient():"
				<< " Connecting File Added Notify " << run->name()
				<< " for Active Run " << m_run->runNumber());
		}

		m_contConnection = run->connect(
			boost::bind( &STCClient::fileAdded, this, _1 ) );

		if ( m_files.size() > 0 && m_files.front()->active() )
		{
			if ( ctrl->verbose() )
			{
				DEBUG("STCClient(): Connecting First File Updated Notify"
					<< " file=" << m_files.front()->path()
					<< " size=" << m_files.front()->size()
					<< " active=" << m_files.front()->active()
					<< " for Run " << m_run->runNumber());
			}

			m_fileConnection = m_files.front()->connect(
				boost::bind( &STCClient::fileUpdated, this, _1 ) );
		}
	}

	return;

exception:

	// Something Bad Happened Above in Constructor,
	// Clean Up All the Callback Hooks and Throw...!
	if ( m_read )
	{
		delete m_read;
		m_read = NULL;
	}
	if ( m_write )
	{
		delete m_write;
		m_write = NULL;
	}
	if ( m_timer )
	{
		m_timer->cancel();
		delete m_timer;
		m_timer = NULL;
	}
	throw std::runtime_error( ss.str() );
}

STCClient::~STCClient()
{
	SMSControl *ctrl = SMSControl::getInstance();

	m_contConnection.disconnect();
	m_fileConnection.disconnect();

	if (m_read) {
		delete m_read;
		m_read = NULL;
	}

	if (m_write) {
		delete m_write;
		m_write = NULL;
	}

	if (m_timer) {
		m_timer->cancel();
		delete m_timer;
		m_timer = NULL;
	}

	if (m_stc_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_stc_fd=" << m_stc_fd);
		}
		close(m_stc_fd);
		m_stc_fd = -1;
	}

	if (m_file_fd >= 0) {
		m_files.front()->put_fd();
		m_file_fd = -1;
	}

	/* Inform the manager of our final status */
	m_mgr.clientComplete(m_run, m_disp, m_reason);
}

bool STCClient::sendHeartbeat(void)
{
	/* TODO send hearbeat packet */
	return true;
}

void STCClient::writable(void)
{
	// DEBUG("writable() entry");

	SMSControl *ctrl = SMSControl::getInstance();

	std::list<StorageFile::SharedPtr>::iterator it;
	ssize_t len, rc;

	/* We're trying to send data, so cancel the heartbeat timer. We'll
	 * re-enable it if we go idle.
	 */
	m_timer->cancel();

	for ( it = m_files.begin(); it != m_files.end(); )
	{
		StorageFile::SharedPtr &f = *it;

		// Ignore Paused Run Files, as Optionally Configured... :-D
		if ( f->paused()
				&& !m_cur_offset // don't trash a file midstream!
				&& !m_send_paused_data ) // tho shouldn't change during run
		{
			DEBUG("writable(): Skipping Paused File"
				<< " file=" << f->path()
				<< " size=" << f->size()
				<< " for Run " << m_run->runNumber());

			// We're Skipping This Paused Mode File Now,
			// So Disconnect Any File Updated Notifications...
			if ( m_fileConnection.connected() )
			{
				if ( ctrl->verbose() )
				{
					DEBUG("writable(): Disconnecting Paused"
							<< " File Updated Notify"
						<< " file=" << f->path()
						<< " size=" << f->size()
						<< " for Run " << m_run->runNumber());
				}

				m_fileConnection.disconnect();
			}

			// Remove File from List...
			it = m_files.erase(it);

			// Is There is Another File on the List?
			if ( it != m_files.end() )
			{
				// IF No File is Already Connected to File Updated,
				// And If This Next File Is an Active File,
				// Then Connect Up to File Updated Notifications... ;-D
				if ( !(m_fileConnection.connected())
						&& (*it)->active() )
				{
					if ( ctrl->verbose() )
					{
						DEBUG("writable():"
							<< " Connecting Next File Updated Notify"
							<< " file=" << (*it)->path()
							<< " size=" << (*it)->size()
							<< " for Run " << m_run->runNumber());
					}

					m_fileConnection = (*it)->connect(
						boost::bind( &STCClient::fileUpdated, this, _1 ) );
				}
			}

			continue;
		}

		if ( m_file_fd < 0 )
		{
			try
			{
				m_file_fd = f->get_fd();
			}
			catch ( std::runtime_error re )
			{
				// Decode Any Mode Index from the File Index
				// (SMS After 1.7.0)
				uint32_t fileNum = f->fileNumber();
				uint32_t modeNum = 0;

				// Embedded Mode Number...?
				if ( fileNum > 0xfff )
				{
					modeNum = ( fileNum >> 12 ) & 0xfff;
					fileNum &= 0xfff;
				}

				std::stringstream ss;
				ss << "Unable to Open File Number " << f->fileNumber()
					<< " (Mode Index #" << modeNum
					<< ", File Index #" << fileNum << ")"
					<< ", Pause File Number " << f->pauseFileNumber()
					<< ", Addendum File Number " << f->addendumFileNumber()
					<< " for Run " << m_run->runNumber()
					<< ": " << re.what();
				ERROR( ss.str() );

				m_disp = STCClientMgr::PERMAMENT_FAIL;
				m_reason = ss.str();
				delete this;
				return;
			}
			if ( ctrl->verbose() > 0 ) {
				DEBUG("Using Data File Descriptor m_file_fd=" << m_file_fd);
			}
		}

		len = f->size() - m_cur_offset;
		if ( len > m_max_send_chunk )
			len = m_max_send_chunk;

		if ( !m_cur_offset )
		{
			DEBUG("writable(): Sending New file=" << f->path()
				<< " size=" << f->size()
				<< " for Run " << m_run->runNumber());
		}

		// Check Client File Descriptor...
		if ( m_stc_fd < 0 )
		{
			std::stringstream ss;
			ss << "Invalid Client File Descriptor in writable()"
				<< " for Run " << m_run->runNumber()
				<< " (m_stc_fd=" << m_stc_fd << ")";
			ERROR( ss.str() );
			m_disp = STCClientMgr::TRANSIENT_FAIL;
			m_reason = ss.str();
			delete this;
			return;
		}

		// Check Data File Descriptor...
		if ( m_file_fd < 0 )
		{
			std::stringstream ss;
			ss << "Invalid Data File Descriptor in writable()"
				<< " for Run " << m_run->runNumber()
				<< " (m_file_fd=" << m_file_fd << ")";
			ERROR( ss.str() );
			m_disp = STCClientMgr::TRANSIENT_FAIL;
			m_reason = ss.str();
			delete this;
			return;
		}

		rc = sendfile( m_stc_fd, m_file_fd, &m_cur_offset, len );
		if ( rc < 0 )
		{
			if ( errno == EAGAIN || errno == EINTR )
				goto more;

			std::stringstream ss;

			if ( errno == EPIPE || errno == ECONNRESET )
			{
				ss << "Lost Connection to STC for Run "
					<< m_run->runNumber()
					<< " in writable():"
					<< " [m_stc_fd=" << m_stc_fd
					<< " m_file_fd=" << m_file_fd
					<< " m_cur_offset=" << m_cur_offset
					<< " len=" << len << "]";
				ERROR( ss.str() );
			}
			else
			{
				int e = errno;
				ss << "Run " << m_run->runNumber()
					<< " had fatal sendfile error in writable():"
					<< " [m_stc_fd=" << m_stc_fd
					<< " m_file_fd=" << m_file_fd
					<< " m_cur_offset=" << m_cur_offset
					<< " len=" << len << "] - "
					<< strerror(e);
				ERROR( ss.str() );
			}

			m_disp = STCClientMgr::TRANSIENT_FAIL;
			m_reason = ss.str();
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
		if ( m_cur_offset != f->size() )
			goto more;

		/* At EOF, do we expect to get more? */
		if ( f->active() )
			goto idle;

		/* We finished this file, and there will be no more data
		 * coming for it; close it out and go to the next one.
		 */

		DEBUG("writable(): Done with file=" << f->path()
			<< " wrote " << m_cur_offset << " of size=" << f->size()
			<< " for Run " << m_run->runNumber());

		if ( m_file_fd >= 0 )
		{
			f->put_fd();
			m_file_fd = -1;
		}

		m_cur_offset = 0;

		// So Disconnect Any File Updated Notifications...
		if ( m_fileConnection.connected() )
		{
			if ( ctrl->verbose() )
			{
				DEBUG("writable(): Disconnecting File Updated Notify"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " for Run " << m_run->runNumber());
			}

			m_fileConnection.disconnect();
		}

		// Remove File from List...
		it = m_files.erase(it);

		// Is There is Another File on the List?
		if ( it != m_files.end() )
		{
			// IF No File is Already Connected to File Updated,
			// And If This Next File Is an Active File,
			// Then Connect Up to File Updated Notifications... ;-D
			if ( !(m_fileConnection.connected())
					&& (*it)->active() )
			{
				if ( ctrl->verbose() )
				{
					DEBUG("writable(): Connecting Next File Updated Notify"
						<< " file=" << (*it)->path()
						<< " size=" << (*it)->size()
						<< " for Run " << m_run->runNumber());
				}

				m_fileConnection = (*it)->connect(
					boost::bind( &STCClient::fileUpdated, this, _1 ) );
			}
		}
	}

	if ( !m_run->active() )
	{
		/* We've sent everything from this file, so shutdown the
		 * write side of our socket. This will signal an EOF to
		 * STC, so we'll notice if we're trying to resend a
		 * corrupted file without a proper ending RunStatus packet.
		 */
#if 0
		if ( shutdown( m_stc_fd, SHUT_WR ) )
		{
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
	if ( m_write )
	{
		delete m_write;
		m_write = NULL;
	}
	m_timer->start( m_heartbeat_interval );
	// DEBUG("writable() idle exit");
	return;

more:
	/* We have more data to write, so make sure we get notified when
	 * there is room in the socket buffer. We also do not need to send
	 * any heartbeat packets, as we have a full pipe.
	 */
	if ( !m_write )
	{
		try
		{
			m_write = new ReadyAdapter( m_stc_fd, fdrWrite,
				boost::bind( &STCClient::writable, this ),
				ctrl->verbose() );
		}
		catch ( std::exception &e )
		{
			std::stringstream ss;
			ss << "Exception Creating ReadyAdapter in writable()"
				<< " for Run " << m_run->runNumber()
			 	<< " - " << e.what();
			ERROR( ss.str() );
			m_write = NULL; // just to be sure... ;-b
			m_disp = STCClientMgr::TRANSIENT_FAIL;
			m_reason = ss.str();
			delete this;
			return;
		}
		catch (...)
		{
			std::stringstream ss;
			ss << "Exception Creating ReadyAdapter in writable()"
				<< " for Run " << m_run->runNumber();
			ERROR( ss.str() );
			m_write = NULL; // just to be sure... ;-b
			m_disp = STCClientMgr::TRANSIENT_FAIL;
			m_reason = ss.str();
			delete this;
			return;
		}
	}
	// DEBUG("writable() more exit");
}

void STCClient::sendDataDone(void)
{
	uint32_t data_done_pkt[4] =
		{ 0, ADARA_PKT_TYPE( ADARA::PacketType::DATA_DONE_TYPE,
			ADARA::PacketType::DATA_DONE_VERSION ), 0, 0 };

	std::string log_info;

	DEBUG("Sending Data Done to STC for Run " << m_run->runNumber());

	bool send_status = false;

	// Check Client File Descriptor...
	if ( m_stc_fd >= 0 ) {
		send_status = Utils::sendBytes( m_stc_fd,
			(char *) data_done_pkt, sizeof( data_done_pkt ), log_info );
	}
	else {
		ERROR("Invalid Client File Descriptor in sendDataDone()"
			<< " - Skipping..." << " (m_stc_fd=" << m_stc_fd << ")");
		return;
	}

	// Dang, it didn't work... ;-b
	if ( !send_status ) {

		ERROR("sendDataDone() failed! log_info=(" << log_info << ")");

		// Resort to the dreaded shutdown() system call,
		// which doesn't appear to work through our network setup... ;-b
		if (shutdown(m_stc_fd, SHUT_WR)) {
			int e = errno;
			ERROR("shutdown() failed: "
				 << "(m_stc_fd=" << m_stc_fd << ") - "
				 << strerror(e));
		}
	}
}

void STCClient::fileAdded( StorageFile::SharedPtr &f )
{
	SMSControl *ctrl = SMSControl::getInstance();

	DEBUG("fileAdded(): Add File " << f->path()
		<< " f->active()=" << f->active()
		<< " for Run " << m_run->runNumber());

	m_files.push_back( f );

	// Note: We Can't Immediately Subscribe to fileUpdated() for This File,
	// We Need to Wait Until We're Done with Any Previous File...
	// (Formerly, We would _Only_ Have One Open File/Container...)

	// Is This the First/Only File on the List?
	if ( m_files.size() == 1 )
	{
		// IF No File is Already Connected to File Updated,
		// And If This Is an Active File,
		// Then Connect Up to File Updated Notifications... ;-D
		if ( !(m_fileConnection.connected())
				&& f->active() )
		{
			if ( ctrl->verbose() )
			{
				DEBUG("fileAdded(): Connecting File Updated Notify"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " for Run " << m_run->runNumber());
			}

			m_fileConnection = f->connect(
				boost::bind( &STCClient::fileUpdated, this, _1 ) );
		}

		// If we're not already waiting for buffer space in the socket,
		// try to send the new data...

		if ( !m_write )
			writable();
	}
}

void STCClient::fileUpdated( const StorageFile &f )
{
	// DEBUG("fileUpdated() entry");

	SMSControl *ctrl = SMSControl::getInstance();

	// DEBUG("fileUpdated(): Update File " << f.path()
		// << " f.active()=" << f.active()
		// << " for Run " << m_run->runNumber());

	// The current file just got updated...

	// If the File is No Longer Active, Cancel the File Updated Notifies...

	if ( !f.active() )
	{
		if ( ctrl->verbose() )
		{
			DEBUG("fileUpdated():"
				<< " Disconnecting Inactive File Updated Notify"
				<< " file=" << f.path()
				<< " size=" << f.size()
				<< " for Run " << m_run->runNumber());
		}

		m_fileConnection.disconnect();
	}

	// If we're not already waiting for buffer space in the socket,
	// try to send the new data...

	if ( !m_write )
		writable();

	// DEBUG("fileUpdated() exit");
}

void STCClient::readable(void)
{
	DEBUG("readable() entry");

	std::string log_info;

	bool ok = false;

	// Check Client File Descriptor...
	if (m_stc_fd < 0) {
		std::stringstream ss;
		ss << "Invalid Client File Descriptor in readable()"
			<< " for Run " << m_run->runNumber();
		ERROR( ss.str() );
		m_disp = STCClientMgr::TRANSIENT_FAIL;
		m_reason = ss.str();
		delete this;
		return;
	}

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		ok = read(m_stc_fd, log_info, 4000, MAX_PACKET_SIZE);
		if (!ok && m_disp == STCClientMgr::CONNECTION_LOSS) {
			/* We log the reason for closing the connection
			 * elsewhere, except for the default case of an
			 * unexpected connection loss.
			 * Take care of that case here.
			 */
			ERROR("Lost Connection to STC for Run " << m_run->runNumber()
				 << " (m_stc_fd=" << m_stc_fd << ") "
				 << " log_info=(" << log_info << ")");
		}
	}
	catch (ADARA::invalid_packet e) {
		std::stringstream ss;
		ss << "Got invalid packet from STC: " << e.what();
		ERROR( ss.str() );
		m_disp = STCClientMgr::INVALID_PROTOCOL;
		m_reason = ss.str();
		ok = false;
	}
	catch (std::runtime_error &r) {
		std::stringstream ss;
		ss << "Exception reading from STC for Run " << m_run->runNumber()
			 << " log_info=(" << log_info << ") - " << r.what();
		ERROR( ss.str() );
		m_disp = STCClientMgr::TRANSIENT_FAIL;
		m_reason = ss.str();
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

bool STCClient::rxPacket(const ADARA::Packet &pkt)
{
	/* We only care about translation complete packets; everything else
	 * is an error and we should drop the connection.
	 */
	if (pkt.base_type() == ADARA::PacketType::TRANS_COMPLETE_TYPE)
		return ADARA::Parser::rxPacket(pkt);

	std::stringstream ss;
	ss << "Received unexpected packet type 0x"
		<< std::hex << pkt.type() << std::dec;
	ERROR( ss.str() );
	m_disp = STCClientMgr::TRANSIENT_FAIL;
	m_reason = ss.str();
	return true;
}

bool STCClient::rxOversizePkt(const ADARA::PacketHeader *hdr,
			       const uint8_t *UNUSED(chunk),
			       unsigned int UNUSED(chunk_offset),
			       unsigned int chunk_len)
{
	// NOTE: ADARA::PacketHeader *hdr can be NULL...! ;-o
	/* Ok, this is much bigger than we expected, stop processing
	 * this stream and close the connection.
	 */
	std::stringstream ss;
	if (hdr) {
		ss << "Received Unexpected Oversize Packet"
			<< " at " << hdr->timestamp().tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< hdr->timestamp().tv_nsec << std::setw(0)
			<< " of type 0x" << std::hex << hdr->type() << std::dec
			<< " payload_length=" << hdr->payload_length()
			<< " max=" << MAX_PACKET_SIZE;
	} else {
		ss << "Received Unexpected Oversize Packet"
			<< " chunk_len=" << chunk_len
			<< " max=" << MAX_PACKET_SIZE;
	}
	ERROR( ss.str() );
	m_disp = STCClientMgr::TRANSIENT_FAIL;
	m_reason = ss.str();
	return true;
}

bool STCClient::rxPacket(const ADARA::TransCompletePkt &pkt)
{
	std::stringstream ss;
	if ( !pkt.status() ) {
		if ( pkt.reason().length() ) {
			ss << "Run " << m_run->runNumber() << " successfully "
				"translated with status message \'"
				<< pkt.reason() << "'";
		} else {
			ss << "Run " << m_run->runNumber() << " successfully "
				"translated";
		}
		INFO( ss.str() );
		m_disp = STCClientMgr::SUCCESS;
		m_reason = ss.str();
	} else if ( pkt.status() < 0x8000 ) {
		/* TODO remove magic numbers */
		if (pkt.reason().length()) {
			ss << "Run " << m_run->runNumber() << " had a transient "
				"failure, status 0x" << std::hex
				<< pkt.status() << std::dec << ", message \'"
				<< pkt.reason() << "'";
		} else {
			ss << "Run " << m_run->runNumber() << " had a transient "
				"failure, status 0x" << std::hex
				<< pkt.status() << std::dec;
		}
		ERROR( ss.str() );
		m_disp = STCClientMgr::TRANSIENT_FAIL;
		m_reason = ss.str();
	} else {
		if ( pkt.reason().length() ) {
			ss << "Run " << m_run->runNumber() << " had a "
				"permanent failure, status 0x" << std::hex
				<< pkt.status() << std::dec << ", message \'"
				<< pkt.reason() << "'";
		} else {
			ss << "Run " << m_run->runNumber() << " had a "
				"permanent failure, status 0x" << std::hex
				<< pkt.status() << std::dec;
		}
		ERROR( ss.str() );
		m_disp = STCClientMgr::PERMAMENT_FAIL;
		m_reason = ss.str();
	}
	return true;
}

