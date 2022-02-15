
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.LiveClient"));

#include <sstream>
#include <string>

#include <unistd.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <stdint.h>

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
#include "utils.h"

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
	m_server(server), m_starting_new_file(true), m_bytes_written(0),
	m_read(NULL), m_write(NULL), m_hello_received(false),
	m_client_fd(fd), m_file_fd(-1), m_client_flags(0)
{
	char hostname[1024], service[256];
	struct sockaddr_in6 sa;
	socklen_t saLen = sizeof(sa);
	int rc;

	// Check Client File Descriptor...
	if ( m_client_fd < 0 ) {
		ERROR("Invalid Client File Descriptor in LiveClient()"
			<< " (m_client_fd=" << m_client_fd << ")");
		throw std::runtime_error("Invalid Client File Descriptor Passed");
	}

	if (getpeername(m_client_fd, (struct sockaddr *) &sa, &saLen) < 0) {
		int e = errno;
		ERROR("Unable to get peer name: "
			<< "(m_client_fd=" << m_client_fd << ") - "
			<< strerror(e));
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

	// File Descriptor Already Checked Above...
	try {
		m_read = new ReadyAdapter(m_client_fd, fdrRead,
			boost::bind(&LiveClient::readable, this),
			ctrl->verbose());
	}
	catch (std::exception &e) {
		ERROR("Exception in LiveClient() Creating ReadyAdapter Read"
			<< " client=" << m_clientName << " - " << e.what());
		m_read = NULL; // just to be sure... ;-b
		throw;
	}
	catch (...) {
		ERROR("Unknown Exception in LiveClient()"
			<< " Creating ReadyAdapter Read"
			<< " client=" << m_clientName);
		m_read = NULL; // just to be sure... ;-b
		throw;
	}

	ERROR("client " << m_clientName << " ready to connect"
		<< " SendPausedData=" << m_send_paused_data);

	try {
		m_timer = new TimerAdapter<LiveClient>(this);
		m_timer->start(m_hello_timeout);
	}
	catch (std::exception &e) {
		ERROR("Exception in LiveClient() Hello Timer/Timeout"
			<< " client=" << m_clientName << " - " << e.what());
		delete m_read;
		m_read = NULL;
		throw;
	}
	catch (...) {
		ERROR("Unknown Exception in LiveClient() Hello Timer/Timeout"
			<< " client=" << m_clientName);
		delete m_read;
		m_read = NULL;
		throw;
	}

	ERROR("client " << m_clientName << " connected");
}

LiveClient::~LiveClient()
{
	SMSControl *ctrl = SMSControl::getInstance();

	ERROR("client " << m_clientName << " disconnected");

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
	m_read = NULL;

	delete m_write;
	m_write = NULL;

	delete m_timer;

	if (m_client_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_client_fd=" << m_client_fd);
		}
		close(m_client_fd);
		m_client_fd = -1;
	}

	if (m_file_fd >= 0) {
		m_files.front().first->put_fd();
		m_file_fd = -1;
	}
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

	static uint32_t cnt = 0;

	SMSControl *ctrl = SMSControl::getInstance();

	// [LESS FREQUENTLY] Update Send Paused Data PV,
	// Once Every 100 Calls...
	uint32_t freq = 100;

	FileList::iterator it;
	ssize_t len, rc;

	for ( it = m_files.begin(); it != m_files.end(); )
	{
		StorageFile::SharedPtr &f = it->first;

		off_t &cur_offset = it->second;

		// Allow Client Override to Force Inclusion of Paused Data...
		if ( !(m_client_flags & ADARA::ClientHelloPkt::SEND_PAUSE_DATA) )
		{
			if ( !(++cnt % freq) )
			{
				m_send_paused_data = m_server->getSendPausedData();
			}

			// Ignore Paused Run Files as Configured (by SMS or Client)...
			if ( f->paused()
					&& !cur_offset // don't trash a file midstream!
					&& ( !m_send_paused_data
						|| m_client_flags
							& ADARA::ClientHelloPkt::NO_PAUSE_DATA ) )
			{
				DEBUG("writable(): Skipping Paused File"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " m_files.size()=" << m_files.size()
					<< " for client " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");

				// We're Skipping This Paused Mode File Now,
				// So Disconnect Any File Updated Notifications...
				if ( m_fileConnection.connected() )
				{
					if ( ctrl->verbose() > 0 )
					{
						DEBUG("writable(): Disconnecting Paused"
								<< " File Updated Notify"
							<< " file=" << f->path()
							<< " size=" << f->size()
							<< " m_files.size()=" << m_files.size()
							<< " for client " << m_clientName
							<< " (m_client_fd=" << m_client_fd << ")");
					}

					m_fileConnection.disconnect();
				}

				// Remove File from List...
				it = m_files.erase(it);

				// Is There is Another File on the List?
				if ( it != m_files.end() )
				{
					// Starting a New File...
					m_starting_new_file = true;
					m_bytes_written = 0;

					// IF No File is Already Connected to File Updated,
					// And If This Next File Is an Active File,
					// Then Connect Up to File Updated Notifications... ;-D
					if ( !(m_fileConnection.connected())
							&& it->first->active() )
					{
						if ( ctrl->verbose() > 0 )
						{
							DEBUG("writable():"
								<< " Connecting Next File Updated Notify"
								<< " file=" << it->first->path()
								<< " size=" << it->first->size()
								<< " m_files.size()=" << m_files.size()
								<< " for client " << m_clientName
								<< " (m_client_fd=" << m_client_fd << ")");
						}

						m_fileConnection = it->first->connect(
							boost::bind( &LiveClient::fileUpdated,
								this, _1 ) );
					}
				}

				continue;
			}
		}

		if ( m_file_fd < 0 )
		{
			try
			{
				m_file_fd = f->get_fd();
			}
			catch ( std::runtime_error re )
			{
				std::string cname;
				StorageContainer::SharedPtr c;

				c = f->owner().lock();
				if ( c )
					cname = c->name();
				else
					cname = "(unknown)";

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

				ERROR(m_clientName << ": Unable to Open File Number "
				      << f->fileNumber()
					  << " (Mode Index #" << modeNum
					  << ", File Index #" << fileNum << ")"
					  << ", Pause File Number " << f->pauseFileNumber()
					  << ", Addendum File Number "
					  	<< f->addendumFileNumber()
					  << " for Container "
				      << cname << ": " << re.what());
				delete this;
				return;
			}
		}

		len = f->size() - cur_offset;
		if ( len > m_max_send_chunk )
			len = m_max_send_chunk;

		if ( m_starting_new_file )
		{
			DEBUG("writable(): Sending New file=" << f->path()
				<< " cur_offset=" << cur_offset
				<< " size=" << f->size() << " to client " << m_clientName
				<< " (m_client_fd=" << m_client_fd << ")");
			if ( m_clientId >= 0 ) {
				struct timespec now;
				clock_gettime(CLOCK_REALTIME, &now);
				m_pvCurrentFilePath->update(f->path(), &now);
			}
			m_starting_new_file = false;
		}

		// Check Client File Descriptor...
		if ( m_client_fd < 0 )
		{
			ERROR("Invalid Client File Descriptor in writable() for "
				<< m_clientName << " before sendfile()"
				<< " (m_client_fd=" << m_client_fd << ")");
			delete this;
			return;
		}

		// Check Data File Descriptor...
		if ( m_file_fd < 0 )
		{
			ERROR("Invalid Data File Descriptor in writable() for "
				<< m_clientName << " before sendfile()"
				<< " (m_file_fd=" << m_file_fd << ")");
			delete this;
			return;
		}

		rc = sendfile(m_client_fd, m_file_fd, &cur_offset, len);
		if ( rc < 0 )
		{
			if ( errno == EAGAIN || errno == EINTR )
				goto more;

			/* Only complain if it's not the client going away */
			int e = errno;
			if ( errno != EPIPE && errno != ECONNRESET )
			{
				ERROR("client " << m_clientName
					<< ": Fatal error during sendfile: "
					<< "(m_client_fd=" << m_client_fd << ") - "
					<< strerror(e));
			}
			else
			{
				ERROR("client " << m_clientName << " connection broken: "
					<< "(m_client_fd=" << m_client_fd << ") - "
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
		m_bytes_written += rc;

		/* Did we catch up to the current EOF? */
		if ( cur_offset != f->size() )
			goto more;

		/* At EOF, do we expect to get more? */
		if ( f->active() )
			goto idle;

		/* We finished this file, and there will be no more data
		 * coming for it; close it out and go to the next one.
		 */

		DEBUG("writable(): Done with file=" << f->path()
			<< " wrote " << m_bytes_written << " of size=" << f->size()
			<< " for client " << m_clientName
			<< " (m_client_fd=" << m_client_fd << ")");

		if ( m_file_fd >= 0 )
		{
			f->put_fd();
			m_file_fd = -1;
		}

		// So Disconnect Any File Updated Notifications...
		if ( m_fileConnection.connected() )
		{
			if ( ctrl->verbose() > 0 )
			{
				DEBUG("writable(): Disconnecting File Updated Notify"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " m_files.size()=" << m_files.size()
					<< " for client " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");
			}

			m_fileConnection.disconnect();
		}

		// Remove File from List...
		it = m_files.erase(it);

		// Is There Another File on the List?
		if ( it != m_files.end() )
		{
			// Starting a New File...
			m_starting_new_file = true;
			m_bytes_written = 0;

			// IF No File is Already Connected to File Updated,
			// And If This Next File Is an Active File,
			// Then Connect Up to File Updated Notifications... ;-D
			if ( !(m_fileConnection.connected())
					&& it->first->active() )
			{
				if ( ctrl->verbose() > 0 )
				{
					DEBUG("writable(): Connecting Next File Updated Notify"
						<< " file=" << it->first->path()
						<< " size=" << it->first->size()
						<< " m_files.size()=" << m_files.size()
						<< " for client " << m_clientName
						<< " (m_client_fd=" << m_client_fd << ")");
				}

				m_fileConnection = it->first->connect(
					boost::bind( &LiveClient::fileUpdated, this, _1 ) );
			}
		}
	}

idle:
	/* We don't need to know when the socket is writable unless we
	 * have data waiting to be sent.
	 */
	if ( m_write )
	{
		delete m_write;
		m_write = NULL;
	}
	// DEBUG("writable() idle exit");
	return;

more:
	/* We have more data to write, so make sure we get notified when
	 * there is room in the socket buffer.
	 */
	if ( !m_write )
	{
		// Check Client File Descriptor...
		if ( m_client_fd < 0 )
		{
			ERROR("Invalid Client File Descriptor in writable() for "
				<< m_clientName << " (m_client_fd=" << m_client_fd << ")");
			delete this;
			return;
		}

		try
		{
			m_write = new ReadyAdapter( m_client_fd, fdrWrite,
				boost::bind( &LiveClient::writable, this ),
				ctrl->verbose() );
		}
		catch ( std::exception &e )
		{
			ERROR("Exception in writable()"
				<< " Creating ReadyAdapter Write for "
				<< m_clientName << ": " << e.what());
			m_write = NULL; // just to be sure... ;-b
			delete this;
			return;
		}
		catch (...)
		{
			ERROR("Unknown Exception in writable()"
				<< " Creating ReadyAdapter Write for "
				<< m_clientName);
			m_write = NULL; // just to be sure... ;-b
			delete this;
			return;
		}
	}
	// DEBUG("writable() more exit");
}

void LiveClient::containerChange( StorageContainer::SharedPtr &c,
		bool starting )
{
	SMSControl *ctrl = SMSControl::getInstance();

	// New Container Starting Up...
	if ( starting )
	{
		// _Always_ Track Each Container on List...
		m_conts.push_back( std::make_pair( c, starting ) );

		// Connect to Container File Added Notify...
		if ( !(m_contConnection.connected()) )
		{
			if ( ctrl->verbose() > 0 )
			{
				DEBUG("containerChange():"
					<< " Connecting New Container File Added Notify "
						<< c->name()
					<< " starting=" << starting
					<< " m_conts.size()=" << m_conts.size()
					<< " for " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");
			}

			m_contConnection = c->connect(
				boost::bind( &LiveClient::fileAdded, this, _1 ) );
		}
		else
		{
			DEBUG("containerChange():"
				<< " New Container Starting Up "
					<< c->name()
				<< " Added to Queue"
				<< " starting=" << starting
				<< " m_conts.size()=" << m_conts.size()
				<< " for " << m_clientName
				<< " (m_client_fd=" << m_client_fd << ")");
		}
	}

	// Existing Container Closing Down...
	else
	{
		// Find Container on List, Set "Starting" Flag to False...
		// - If Container is at Front of List, Then Actually Disconnect(),
		// Remove That ContEntry From List, And Then Connect the Next Cont

		ContList::iterator it;

		bool found = false;

		for ( it = m_conts.begin(); it != m_conts.end(); ++it )
		{
			StorageContainer::SharedPtr &list_c = it->first;
			bool &list_starting = it->second;

			// Is This the Closing Container...?
			if ( !(list_c->name().compare( c->name() )) )
			{
				// Found It
				found = true;

				// Mark Container as Closed...
				if ( list_starting )
				{
					DEBUG("containerChange():"
						<< " Marking"
						<< ( ( it == m_conts.begin() ) ? " Current" : "" )
						<< " Container Closed " << list_c->name()
						<< " list_starting=" << list_starting
						<< " m_conts.size()=" << m_conts.size()
						<< " for " << m_clientName
						<< " (m_client_fd=" << m_client_fd << ")");

					list_starting = starting;
				}
				else
				{
					ERROR("containerChange():"
						<< ( ( it == m_conts.begin() ) ? " Current" : "" )
						<< " Container Already Marked Closed? "
							<< list_c->name()
						<< " list_starting=" << list_starting
						<< " m_conts.size()=" << m_conts.size()
						<< " for " << m_clientName
						<< " (m_client_fd=" << m_client_fd << ")");
				}

				// Check if This is the Current/Connected Container,
				// If So Then Actually Close It, and Remove It From List...
				if ( it == m_conts.begin() )
				{
					if ( m_contConnection.connected() )
					{
						if ( ctrl->verbose() > 0 )
						{
							DEBUG("containerChange():"
								<< " Disconnecting Current Container "
									<< list_c->name()
								<< " list_starting=" << list_starting
								<< " m_conts.size()=" << m_conts.size()
								<< " for " << m_clientName
								<< " (m_client_fd=" << m_client_fd << ")");
						}

						m_contConnection.disconnect();
					}
					else
					{
						ERROR("containerChange():"
							<< " Current Container Already Disconnected? "
								<< list_c->name()
							<< " list_starting=" << list_starting
							<< " m_conts.size()=" << m_conts.size()
							<< " for " << m_clientName
							<< " (m_client_fd=" << m_client_fd << ")");
					}

					// Remove Container from List...
					// (Iterator Left Pointing to _Next_ Container in List)
					it = m_conts.erase( it );

					// Is There Another Container on the List...?
					if ( it != m_conts.end() )
					{
						bool next_starting; // not a ref!

						do
						{
							// Get Next Container on List...
							StorageContainer::SharedPtr &next_c =
								it->first;
							next_starting = it->second;

							DEBUG("containerChange():"
								<< " Choosing Next Container "
									<< next_c->name()
								<< " next_starting=" << next_starting
								<< " m_conts.size()=" << m_conts.size()
								<< " for " << m_clientName
								<< " (m_client_fd=" << m_client_fd << ")");

							// Capture Any Existing Container Files...
							std::list<StorageFile::SharedPtr> file_list;
							next_c->getFiles( file_list );

							// Append Next Container File List
							// to LiveClient List...
							std::list<StorageFile::SharedPtr>
								::iterator fit;
							for ( fit = file_list.begin();
									fit != file_list.end(); ++fit )
							{
								m_files.push_back(
									std::make_pair( *fit, 0 ) );

								DEBUG("containerChange():"
									<< " Added File for Next Container "
										<< next_c->name()
									<< " next_starting=" << next_starting
									<< " m_conts.size()=" << m_conts.size()
									<< " file=" << (*fit)->path()
									<< " size=" << (*fit)->size()
									<< " active=" << (*fit)->active()
									<< " m_files.size()=" << m_files.size()
									<< " for " << m_clientName
									<< " (m_client_fd="
										<< m_client_fd << ")");

								// Is This the First/Only File on the List?
								if ( m_files.size() == 1 )
								{
									// Starting a New File...
									m_starting_new_file = true;
									m_bytes_written = 0;

									// IF No File is Already Connected to
									//    File Updated,
									// And If This Is an Active File,
									// Then Connect Up to
									//    File Updated Notifications... ;-D
									if ( !(m_fileConnection.connected())
											&& (*fit)->active() )
									{
										if ( ctrl->verbose() > 0 )
										{
											DEBUG("containerChange():"
												<< " Connecting"
												<< " Next Container"
												<< " File Updated Notify "
													<< next_c->name()
												<< " next_starting="
													<< next_starting
												<< " m_conts.size()="
													<< m_conts.size()
												<< " file="
													<< (*fit)->path()
												<< " size="
													<< (*fit)->size()
												<< " active="
													<< (*fit)->active()
												<< " m_files.size()="
													<< m_files.size()
												<< " for " << m_clientName
												<< " (m_client_fd="
													<< m_client_fd << ")");
										}

										m_fileConnection = (*fit)->connect(
											boost::bind(
												&LiveClient::fileUpdated,
						  							this, _1 ) );
									}
								}
							}

							// Connect _Next_ Active Container
							//    to File Added Notify
							if ( next_starting )
							{
								if ( ctrl->verbose() > 0 )
								{
									DEBUG("containerChange():"
										<< " Connecting Next Container"
											<< " File Added Notify "
											<< next_c->name()
										<< " next_starting="
											<< next_starting
										<< " m_conts.size()="
											<< m_conts.size()
										<< " for " << m_clientName
										<< " (m_client_fd="
											<< m_client_fd << ")");
								}

								m_contConnection = next_c->connect(
									boost::bind( &LiveClient::fileAdded,
										this, _1 ) );
							}
							// This Container is Already Closed,
							// So We're Done With It - Go On to Any Next...
							else
							{
								DEBUG("containerChange():"
									<< " This Container is Already Closed "
										<< next_c->name()
									<< " Add Any Next Container Files Too"
									<< " next_starting=" << next_starting
									<< " m_conts.size()=" << m_conts.size()
										<< " (pre-delete)"
									<< " for " << m_clientName
									<< " (m_client_fd="
										<< m_client_fd << ")");

								// Remove Container from List...
								// (Iterator Left Pointing
								//    to _Next_ Container in List)
								it = m_conts.erase( it );
							}
						}
						while ( !next_starting && it != m_conts.end() );

						// If we're not already waiting for buffer space
						// in the socket, try to send the new data...
						if ( !m_write )
							writable();
					}
					else
					{
						DEBUG("containerChange():"
							<< " No More Containers on List After "
								<< c->name()
							<< " m_conts.size()=" << m_conts.size()
							<< " for " << m_clientName
							<< " (m_client_fd=" << m_client_fd << ")");
					}
				}

				break;
			}
		}

		// Log If Container Not Found...!
		if ( !found )
		{
			ERROR("containerChange():"
				<< " Inactive Container Not Found on List! " << c->name()
				<< " m_conts.size()=" << m_conts.size()
				<< " for " << m_clientName
				<< " (m_client_fd=" << m_client_fd << ")");
		}
	}
}

void LiveClient::historicalFile( StorageFile::SharedPtr &f, off_t start )
{
	// This is an old file, so just put it on the list to be sent.

	SMSControl *ctrl = SMSControl::getInstance();

	m_files.push_back( std::make_pair( f, start ) );

	DEBUG("historicalFile(): Added File " << f->path()
		<< " start=" << start
		<< " f->active()=" << f->active()
		<< " m_files.size()=" << m_files.size()
		<< " for client " << m_clientName
		<< " (m_client_fd=" << m_client_fd << ")");

	// Note: We Can't Immediately Subscribe to fileUpdated() for This File,
	// We Need to Wait Until We're Done with Any Previous File...
	// (Formerly, We would _Only_ Have One Open File/Container...)

	// Is This the First/Only File on the List?
	if ( m_files.size() == 1 )
	{
		// Starting a New File...
		m_starting_new_file = true;
		m_bytes_written = 0;

		// IF No File is Already Connected to File Updated,
		// And If This Is an Active File,
		// Then Connect Up to File Updated Notifications... ;-D
		if ( !(m_fileConnection.connected())
				&& f->active() )
		{
			if ( ctrl->verbose() > 0 )
			{
				DEBUG("historicalFile(): Connecting File Updated Notify"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " m_files.size()=" << m_files.size()
					<< " for client " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");
			}

			m_fileConnection = f->connect(
				boost::bind( &LiveClient::fileUpdated, this, _1 ) );
		}
	}

	// If we're not already waiting for buffer space
	// in the socket, try to send the new data...
	if ( !m_write )
		writable();
}

void LiveClient::fileAdded( StorageFile::SharedPtr &f )
{
	SMSControl *ctrl = SMSControl::getInstance();

	m_files.push_back( std::make_pair( f, 0 ) );

	DEBUG("fileAdded(): Added File " << f->path()
		<< " start=0"
		<< " f->active()=" << f->active()
		<< " m_files.size()=" << m_files.size()
		<< " for client " << m_clientName
		<< " (m_client_fd=" << m_client_fd << ")");

	// Note: We Can't Immediately Subscribe to fileUpdated() for This File,
	// We Need to Wait Until We're Done with Any Previous File...
	// (Formerly, We would _Only_ Have One Open File/Container...)

	// Is This the First/Only File on the List?
	if ( m_files.size() == 1 )
	{
		// Starting a New File...
		m_starting_new_file = true;
		m_bytes_written = 0;

		// IF No File is Already Connected to File Updated,
		// And If This Is an Active File,
		// Then Connect Up to File Updated Notifications... ;-D
		if ( !(m_fileConnection.connected())
				&& f->active() )
		{
			if ( ctrl->verbose() > 0 )
			{
				DEBUG("fileAdded(): Connecting File Updated Notify"
					<< " file=" << f->path()
					<< " size=" << f->size()
					<< " m_files.size()=" << m_files.size()
					<< " for client " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");
			}

			m_fileConnection = f->connect(
				boost::bind( &LiveClient::fileUpdated, this, _1 ) );
		}
	}

	// If we're not already waiting for buffer space
	// in the socket, try to send the new data...
	if ( !m_write )
		writable();
}

void LiveClient::fileUpdated( const StorageFile &f )
{
	// DEBUG("fileUpdated() entry");

	// The current file just got updated...

	SMSControl *ctrl = SMSControl::getInstance();

	// If the File is No Longer Active, Cancel the File Updated Notifies...
	if ( !f.active() )
	{
		if ( ctrl->verbose() > 0 )
		{
			DEBUG("fileUpdated():"
				<< " Disconnecting Inactive File Updated Notify"
				<< " file=" << f.path()
				<< " size=" << f.size()
				<< " m_files.size()=" << m_files.size()
				<< " for client " << m_clientName
				<< " (m_client_fd=" << m_client_fd << ")");
		}

		m_fileConnection.disconnect();
	}

	// If we're not already waiting for buffer space
	// in the socket, try to send the new data...
	if ( !m_write )
		writable();

	// DEBUG("fileUpdated() exit");
}

void LiveClient::readable(void)
{
	// DEBUG("readable() entry");

	std::string log_info;

	// Check Client File Descriptor...
	if ( m_client_fd < 0 ) {
		ERROR("Invalid Client File Descriptor in readable() for "
			<< m_clientName << " (m_client_fd=" << m_client_fd << ")");
		delete this;
		return;
	}

	try {
		// NOTE: This is POSIXParser::read()... ;-o
		if (!read(m_client_fd, log_info, 4000, MAX_PKT_SIZE)) {
			/* EOF or our handlers indicated it was time to stop,
			 * so kill ourselves off. We can't do this from the
			 * handlers, as ADARA::Parser::read() will modify
			 * member variables after calling the handlers.
			 */
			ERROR("client " << m_clientName
				<< "(m_client_fd=" << m_client_fd << ") "
				<< " error reading stream log_info=(" << log_info << ")");
			delete this;
			return;
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
		return;
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
		ERROR("LiveClient "
			<< m_clientName << " sent us an Oversize Packet at "
			<< hdr->timestamp().tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< hdr->timestamp().tv_nsec << std::setw(0)
			<< " of type 0x" << std::hex << hdr->type() << std::dec
			<< " payload_length=" << hdr->payload_length()
			<< " max=" << MAX_PKT_SIZE);
	} else {
		ERROR("LiveClient "
			<< m_clientName << " sent us an Oversize Packet"
			<< " chunk_len=" << chunk_len
			<< " max=" << MAX_PKT_SIZE);
	}
	return true;
}

bool LiveClient::rxPacket( const ADARA::ClientHelloPkt &pkt )
{
	SMSControl *ctrl = SMSControl::getInstance();

	StorageContainer::SharedPtr cur_cont;

	m_timer->cancel();

	m_hello_received = true;

	m_client_flags = pkt.clientFlags();  // Available in Version 1, else 0.

	std::stringstream ss;
	ss << "[";
	if ( m_client_flags & ADARA::ClientHelloPkt::SEND_PAUSE_DATA )
		ss << "SEND_PAUSE_DATA";
	else if ( m_client_flags & ADARA::ClientHelloPkt::NO_PAUSE_DATA )
		ss << "NO_PAUSE_DATA";
	else
		ss << "PAUSE_AGNOSTIC";
	ss << "]";

	ERROR("LiveClient Hello V" << pkt.version()
		<< " Received from " << m_clientName
		<< ", Requested Start Time = " << pkt.requestedStartTime()
		<< ", Client Flags = 0x" << std::hex << m_client_flags << std::dec
		<< " " << ss.str() );

	if ( m_clientId >= 0 ) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		m_pvRequestedStartTime->update( pkt.requestedStartTime(), &now );
		m_pvStatus->connected();
	}

	m_mgrConnection = StorageManager::onContainerChange(
		boost::bind( &LiveClient::containerChange, this, _1, _2 ) );

	// Request the system state at the given timestamp, or just prior.
	StorageManager::iterateHistory( pkt.requestedStartTime(),
				boost::bind( &LiveClient::historicalFile,
						this, _1, _2 ) );

	// Register for updates and notification of new files if we
	// have anything active.

	cur_cont = StorageManager::container();

	if ( cur_cont )
	{
		// _Always_ Track Each Container on List...
		m_conts.push_back(
			std::make_pair( cur_cont, true /* starting */ ) );

		// Connect to Container File Added Notify...
		if ( !(m_contConnection.connected()) )
		{
			if ( ctrl->verbose() > 0 )
			{
				DEBUG("rxPacket(ClientHelloPkt):"
					<< " Connecting New Container File Added Notify "
						<< cur_cont->name()
					<< " starting=true"
					<< " m_conts.size()=" << m_conts.size()
					<< " for " << m_clientName
					<< " (m_client_fd=" << m_client_fd << ")");
			}

			m_contConnection = cur_cont->connect(
					boost::bind( &LiveClient::fileAdded, this, _1 ) );
		}
		else
		{
			ERROR("rxPacket(ClientHelloPkt):"
				<< " Hmmm... Another Container Already Connected"
					<< " for File Added Notify? "
					<< cur_cont->name()
				<< " starting=true"
				<< " m_conts.size()=" << m_conts.size()
				<< " for " << m_clientName
				<< " (m_client_fd=" << m_client_fd << ")");
		}

		// Note: No Need to Redundantly Connect the Current/Front File
		// to the File Updated Notifications, This has Already Been Done
		// in Either LiveClient::historicalFile() or when the
		// Next Active File is Added via the File Added Notification.
	}

	// And try to send the data we've queued up.

	// Note: If Anything Goes Wrong Here, Just Let it Go...
	// The LiveClient connection will Time Out and Clean Itself Up. ;-D

	// Check Client File Descriptor...
	if ( m_client_fd < 0 ) {
		ERROR("Invalid Client File Descriptor"
			<< " in rxPacket(ClientHelloPkt) for " << m_clientName
			<< " (m_client_fd=" << m_client_fd << ")");
		return false;
	}

	if ( !m_write )
	{
		try
		{
			m_write = new ReadyAdapter( m_client_fd, fdrWrite,
				boost::bind( &LiveClient::writable, this ),
				ctrl->verbose() );
		}
		catch ( std::exception &e )
		{
			ERROR("Exception in rxPacket(ClientHelloPkt)"
				<< " Creating ReadyAdapter Write for "
				<< m_clientName << ": " << e.what());
			m_write = NULL; // just to be sure... ;-b
			// Close Our Client Socket to Allow Graceful Cleanup...
			if ( m_client_fd >= 0 ) {
				if ( ctrl->verbose() > 0 ) {
					DEBUG("Close m_client_fd=" << m_client_fd);
				}
				close(m_client_fd);
				m_client_fd = -1;
			}
			return false;
		}
		catch (...)
		{
			ERROR("Unknown Exception in rxPacket(ClientHelloPkt)"
				<< " Creating ReadyAdapter Write for "
				<< m_clientName);
			m_write = NULL; // just to be sure... ;-b
			// Close Our Client Socket to Allow Graceful Cleanup...
			if ( m_client_fd >= 0 ) {
				if ( ctrl->verbose() > 0 ) {
					DEBUG("Close m_client_fd=" << m_client_fd);
				}
				close(m_client_fd);
				m_client_fd = -1;
			}
			return false;
		}
	}

	return false;
}

