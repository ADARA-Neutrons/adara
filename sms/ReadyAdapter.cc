
#include "Logging.h"

LOGGER("SMS.ReadyAdapter")

#include <boost/function.hpp>

#if defined(__GNUC__) && __GNUC_PREREQ(11,0)
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
#include <fdManager.h>
#if defined(__GNUC__) && __GNUC_PREREQ(11,0)
#pragma GCC diagnostic pop
#endif

#include "ReadyAdapter.h"

ReadyAdapter::ReadyAdapter(int fd, fdRegType reg, callback cb,
		uint32_t verbose) :
	fdReg(fd, reg), m_cb(cb), m_fd(fd), m_reg(reg),
	m_verbose(verbose)
{
	LOGGER_INIT();

	if ( m_verbose > 0 )
	{
		DEBUG("ReadyAdapter Created m_fd=" << m_fd
			<< " reg=" << m_reg);
	}
}

ReadyAdapter::~ReadyAdapter()
{
	if ( m_verbose > 0 )
	{
		DEBUG("ReadyAdapter Destroyed m_fd=" << m_fd
			<< " reg=" << m_reg);
	}
}

