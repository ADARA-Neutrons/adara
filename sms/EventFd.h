#ifndef __EVENT_FD_H
#define __EVENT_FD_H

#include <boost/function.hpp>
#include <fdManager.h>
#include <sys/eventfd.h>
#include <sstream>
#include <stdint.h>
#include <errno.h>

#include <memory>
#include <stdexcept>

#include "ReadyAdapter.h"
#include "Logging.h"

class EventFd {
public:
	typedef boost::function<void (fdRegType)> callback;

	EventFd( bool nonBlocking = false )
		: m_nonBlocking(nonBlocking)
	{
		if ( nonBlocking )
			init( EFD_NONBLOCK );
		else
			init(0);
	}

	EventFd( callback cb )
	{
		m_nonBlocking = true;
		init( EFD_NONBLOCK );
		m_ready.reset(new ReadyAdapter( m_fd, fdrRead, cb ));
	}

	/* TODO some day, we may need to try to read from a blocking object
	 * without blocking; at that point we can revisit the guards we
	 * have in place.
	 */
	bool read( uint64_t & val )
	{
		if ( !m_nonBlocking ) {
			throw std::runtime_error("Calling EventFd::read on a "
					 "blocking object");
		}

		return do_read( val );
	}

	bool block( uint64_t & val )
	{
		if ( m_nonBlocking ) {
			throw std::runtime_error("Calling EventFd::block on a "
					 "non-blocking object");
		}

		return do_read( val );
	}

	bool signal( uint64_t val = 1 )
	{
		return do_write( val );
	}

	~EventFd() { close( m_fd ); }

private:
	std::auto_ptr<ReadyAdapter> m_ready;
	int m_fd;
	bool m_nonBlocking;

	void init( int flags )
	{
		m_fd = eventfd( 0, flags );
		if ( m_fd < 0 ) {
			int e = errno;
			std::string msg("Unable to create eventfd: ");
			msg += strerror(e);
			throw std::runtime_error(msg);
		}
	}

	bool do_read( uint64_t & val )
	{
		char *bufptr = (char *) &val;
		ssize_t len = sizeof(val);
		ssize_t rc;

		std::stringstream ss;

		uint32_t cnt = 0;

		while ( len > 0 && cnt++ < 3 )
		{
			//DEBUG("EventFd do_read(): Reading Value from Fd"
				//<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
				//<< " remaining len=" << len);

			// NOTE: This is Standard C Library read()... ;-o
			rc = ::read( m_fd, bufptr, len );

			if ( rc == (ssize_t) -1 ) {
				if (errno != EAGAIN && errno != EINTR) {
					int e = errno;
					std::string msg("Unable to read eventfd: ");
					msg += strerror(e);
					throw std::runtime_error(msg);
				} else if ( !m_nonBlocking ) {
					DEBUG("EventFd do_read(): Read Returned rc=" << rc
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
			DEBUG("EventFd do_read(): " << ss.str() << "Done. "
				<< "Read " << sizeof(val) << " Bytes. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
			return( true );
		}
		else {
			if ( !m_nonBlocking || len != sizeof(val) ) {
				ERROR("EventFd do_read(): " << ss.str()
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

	bool do_write( uint64_t val ) {

		char *bufptr = (char *) &val;
		ssize_t len = sizeof(val);
		ssize_t rc;

		std::stringstream ss;

		uint32_t cnt = 0;

		while ( len > 0 && cnt++ < 3 )
		{
			//DEBUG("EventFd do_write(): Writing Value to Fd"
				//<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
				//<< " remaining len=" << len);

			rc = write( m_fd, bufptr, len );

			if ( rc == (ssize_t) -1 ) {
				if (errno != EAGAIN && errno != EINTR) {
					int e = errno;
					std::string msg("Unable to write to eventfd: ");
					msg += strerror(e);
					throw std::runtime_error(msg);
				} else {
					DEBUG("EventFd do_write(): Write Returned rc=" << rc
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
			DEBUG("EventFd do_write(): " << ss.str() << "Done. "
				<< "Wrote " << sizeof(val) << " Bytes. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
			return( true );
		}
		else {
			ERROR("EventFd do_write(): " << ss.str()
				<< "Value Not Written! "
				<< "Only Wrote " << ( sizeof(val) - len ) << " Bytes "
				<< "out of " << sizeof(val) << " Requested. "
				<< "Value = " << val << "/0x"
					<< std::hex << val << std::dec);
			return( false );
		}
	}

};

#endif /* __EVENT_FD_H */
