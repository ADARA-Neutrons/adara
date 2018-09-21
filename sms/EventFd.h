#ifndef __EVENT_FD_H
#define __EVENT_FD_H

#include <boost/function.hpp>
#include <fdManager.h>
#include <stdint.h>

#include "ReadyAdapter.h"

class EventFd {
public:
	typedef boost::function<void (fdRegType)> callback;

	EventFd( bool nonBlocking = false );
	EventFd( callback cb );

	~EventFd();

	/* TODO some day, we may need to try to read from a blocking object
	 * without blocking; at that point we can revisit the guards we
	 * have in place.
	 */

	bool read( uint64_t & val );

	bool block( uint64_t & val );

	bool signal( uint64_t val = 1 );

private:
	ReadyAdapter *m_ready;
	int m_fd;
	bool m_nonBlocking;

	void init( int flags );

	bool do_read( uint64_t & val );

	bool do_write( uint64_t val );

};

#endif /* __EVENT_FD_H */
