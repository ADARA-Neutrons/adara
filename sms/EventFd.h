#ifndef __EVENT_FD_H
#define __EVENT_FD_H

#include <boost/function.hpp>
#include <fdManager.h>
#include <sys/eventfd.h>
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
	void read( void *buffer, ssize_t buffer_length )
	{
		if ( !m_ready.get() ) {
			if ( !m_nonBlocking ) {
				throw std::runtime_error("Calling EventFd::read on a "
						 "blocking object");
			}
		}

		do_read( buffer, buffer_length );
	}

	void block( void *buffer, ssize_t buffer_length )
	{
		if ( m_ready.get() ) {
			if ( m_nonBlocking ) {
				throw std::runtime_error("Calling EventFd::block on a "
						 "non-blocking object");
			}
		}

		do_read( buffer, buffer_length );
	}

	void signal( void *buffer, ssize_t buffer_length )
	{
		DEBUG("EventFd signal():"
			<< " buffer=0x" << std::hex << buffer << std::dec
			<< " buffer_length=" << buffer_length);

		do_write( buffer, buffer_length );
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

	void do_read( void *buffer, ssize_t buffer_length )
	{
		char *bufptr = (char *) buffer;
		ssize_t len = buffer_length;
		ssize_t rc;

		DEBUG("EventFd do_read():"
			<< " buffer=0x" << std::hex << buffer << std::dec
			<< " buffer_length=" << buffer_length);

		while ( len > 0 )
		{
			DEBUG("EventFd do_read(): Reading Value from Fd"
				<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
				<< " remaining len=" << len);

			// NOTE: This is Standard C Library read()... ;-o
			rc = ::read( m_fd, bufptr, len );

			if ( rc == (ssize_t) -1 ) {
				DEBUG("EventFd do_read(): Read Returned rc=" << rc);
				if (errno != EAGAIN && errno != EINTR) {
					int e = errno;
					std::string msg("Unable to read eventfd: ");
					msg += strerror(e);
					throw std::runtime_error(msg);
				} else {
					DEBUG("EventFd do_read(): Continuing...");
					//val = ~0;
				}
			}
			else {
				DEBUG("EventFd do_read(): Read " << rc << " Bytes "
					<< "out of " << len << " Requested.");
				bufptr += rc;
				len -= rc;
			}
		}

		DEBUG("EventFd do_read(): Done. "
			<< "Read " << buffer_length << " Bytes.");
	}

	void do_write( void *buffer, ssize_t buffer_length ) {

		char *bufptr = (char *) buffer;
		ssize_t len = buffer_length;
		ssize_t rc;

		DEBUG("EventFd do_write():"
			<< " buffer=0x" << std::hex << buffer << std::dec
			<< " buffer_length=" << buffer_length);

		while ( len > 0 )
		{
			DEBUG("EventFd do_write(): Writing Value to Fd"
				<< " bufptr=0x" << std::hex << (void *) bufptr << std::dec
				<< " remaining len=" << len);

			rc = write( m_fd, bufptr, len );

			if ( rc == (ssize_t) -1 ) {
				DEBUG("EventFd do_write(): Write Returned rc=" << rc);
				if (errno != EAGAIN && errno != EINTR) {
					int e = errno;
					std::string msg("Unable to write to eventfd: ");
					msg += strerror(e);
					throw std::runtime_error(msg);
				} else {
					DEBUG("EventFd do_write(): Continuing...");
				}
			}
			else {
				DEBUG("EventFd do_write(): Wrote " << rc << " Bytes "
					<< "out of " << len << " Requested.");
				bufptr += rc;
				len -= rc;
			}
		}

		DEBUG("EventFd do_write(): Done. "
			<< "Wrote " << buffer_length << " Bytes.");
	}
};

#endif /* __EVENT_FD_H */
