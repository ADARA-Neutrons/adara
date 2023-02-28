
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.EventFd"));

#include <stdexcept>
#include <sstream>
#include <memory>

#include <fdManager.h>
#include <sys/eventfd.h>
#include <stdint.h>
#include <errno.h>

#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>

#include "ReadyAdapter.h"
#include "EventFd.h"
#include "SMSControl.h"

EventFd::EventFd( bool nonBlocking )
	: m_ready(NULL), m_nonBlocking(nonBlocking)
{
	if ( nonBlocking )
		init( EFD_NONBLOCK );
	else
		init(0);
}

EventFd::EventFd( callback cb )
	: m_ready(NULL)
{
	SMSControl *ctrl = SMSControl::getInstance();
	m_nonBlocking = true;
	init( EFD_NONBLOCK );
	// File Descriptor Created in init() (or std::runtime_error thrown...)
	try {
		m_ready = new ReadyAdapter( m_fd, fdrRead, cb, ctrl->verbose() );
	} catch (std::exception &e) {
		ERROR("EventFd(): Exception Creating ReadyAdapter: " << e.what());
		m_ready = NULL; // just to be sure... ;-b
		throw;
	} catch (...) {
		ERROR("EventFd(): Unknown Exception Creating ReadyAdapter");
		m_ready = NULL; // just to be sure... ;-b
		throw;
	}
}

EventFd::~EventFd()
{
	SMSControl *ctrl = SMSControl::getInstance();

	if (m_ready) {
		delete m_ready;
		m_ready = NULL;
	}

	if (m_fd >= 0) {
		if (ctrl->verbose() > 0) {
			DEBUG("Close m_fd=" << m_fd);
		}
		close( m_fd );
		m_fd = -1;
	}
}

void EventFd::init( int flags )
{
	SMSControl *ctrl = SMSControl::getInstance();
	m_fd = eventfd( 0, flags );
	if ( m_fd < 0 ) {
		int e = errno;
		std::string msg("Unable to create eventfd: ");
		msg += "m_fd=";
		msg += boost::lexical_cast<std::string>(m_fd);
		msg += " - ";
		msg += strerror(e);
		m_fd = -1;   // just to be sure... ;-b
		throw std::runtime_error(msg);
	}
	if ( ctrl->verbose() > 0 ) {
		DEBUG("New EventFD m_fd=" << m_fd);
	}
}

bool EventFd::read( uint64_t & val )
{
	if ( !m_nonBlocking ) {
		throw std::runtime_error("Calling EventFd::read on a "
				 "blocking object");
	}

	return do_read( val );
}

bool EventFd::block( uint64_t & val )
{
	if ( m_nonBlocking ) {
		throw std::runtime_error("Calling EventFd::block on a "
				 "non-blocking object");
	}

	return do_read( val );
}

bool EventFd::signal( uint64_t val )
{
	return do_write( val );
}

bool EventFd::do_read( uint64_t & val )
{
	SMSControl *ctrl = SMSControl::getInstance();

	char *bufptr = (char *) &val;
	ssize_t len = sizeof(val);
	ssize_t rc;

	std::stringstream ss;

	uint32_t cnt = 0;

	while ( len > 0 && cnt++ < 3 )
	{
		//DEBUG("do_read(): Reading Value from Fd"
			//<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
			//<< " remaining len=" << len);

		// Check File Descriptor...
		if ( m_fd < 0 ) {
			ERROR("do_read(): Invalid File Descriptor (Still Dead)"
				<< " m_fd=" << m_fd);
			return( false );
		}

		// NOTE: This is Standard C Library read()... ;-o
		rc = ::read( m_fd, bufptr, len );

		if ( rc == (ssize_t) -1 ) {
			if (errno != EAGAIN && errno != EINTR) {
				int e = errno;
				ERROR("do_read(): Unable to Read eventfd: "
					<< " m_fd=" << m_fd << " - "
					<< strerror(e));
				// *Don't* Throw Exception Here, Just Limp Along
				// and Wait for Help/Restart... ;-D
				if ( m_fd >= 0 ) {
					if ( ctrl->verbose() > 0 ) {
						DEBUG("Close Dead m_fd=" << m_fd);
					}
					close( m_fd );
					m_fd = -1;
				}
				return( false );
			} else if ( !m_nonBlocking ) {
				DEBUG("do_read(): Read Returned rc=" << rc
					<< " Continuing..." << " (cnt=" << cnt << ")");
			}
		}
		else {
			ss << "Read " << rc << " Bytes "
				<< "out of " << len << " Requested. ";
			bufptr += rc;
			len -= rc;
		}
	}

	if ( len == 0 ) {
		if ( ctrl->verbose() > 2 ) {
			DEBUG("do_read(): " << ss.str() << "Done. "
				<< "Read " << sizeof(val) << " Bytes. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
		}
		return( true );
	}
	else {
		if ( !m_nonBlocking || len != sizeof(val) ) {
			ERROR("do_read(): " << ss.str()
				<< "Value Not Fully Read! "
				<< "Only Read " << ( sizeof(val) - len ) << " Bytes "
				<< "out of " << sizeof(val) << " Requested. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
		}
		// val = ~0;
		return( false );
	}
}

bool EventFd::do_write( uint64_t val )
{
	SMSControl *ctrl = SMSControl::getInstance();

	char *bufptr = (char *) &val;
	ssize_t len = sizeof(val);
	ssize_t rc;

	std::stringstream ss;

	uint32_t cnt = 0;

	while ( len > 0 && cnt++ < 3 )
	{
		//DEBUG("do_write(): Writing Value to Fd"
			//<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
			//<< " remaining len=" << len);

		// Check File Descriptor...
		if ( m_fd < 0 ) {
			ERROR("do_write(): Invalid File Descriptor (Still Dead)"
				<< " m_fd=" << m_fd);
			return( false );
		}

		rc = write( m_fd, bufptr, len );

		if ( rc == (ssize_t) -1 ) {
			if (errno != EAGAIN && errno != EINTR) {
				int e = errno;
				ERROR("do_write(): Unable to Write to eventfd: "
					<< " m_fd=" << m_fd << " - "
					<< strerror(e));
				// *Don't* Throw Exception Here, Just Limp Along
				// and Wait for Help/Restart... ;-D
				if ( m_fd >= 0 ) {
					if ( ctrl->verbose() > 0 ) {
						DEBUG("Close Dead m_fd=" << m_fd);
					}
					close( m_fd );
					m_fd = -1;
				}
				return( false );
			} else if ( ctrl->verbose() > 2 ) {
				DEBUG("do_write(): Write Returned rc=" << rc
					<< " Continuing..." << " (cnt=" << cnt << ")");
			}
		}
		else {
			ss << "Wrote " << rc << " Bytes "
				<< "out of " << len << " Requested. ";
			bufptr += rc;
			len -= rc;
		}
	}

	if ( len == 0 ) {
		if ( ctrl->verbose() > 2 ) {
			DEBUG("do_write(): " << ss.str() << "Done. "
				<< "Wrote " << sizeof(val) << " Bytes. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
		}
		return( true );
	}
	else {
		ERROR("do_write(): " << ss.str()
			<< "Value Not Written! "
			<< "Only Wrote " << ( sizeof(val) - len ) << " Bytes "
			<< "out of " << sizeof(val) << " Requested. "
			<< "Value = " << val << "/0x"
				<< std::hex << val << std::dec);
		return( false );
	}
}

