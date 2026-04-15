#ifndef __READY_ADAPTER_H
#define __READY_ADAPTER_H

#include "Logging.h"

#include <boost/function.hpp>

#if defined(__GNUC__) && __GNUC_PREREQ(11,0)
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
#include <fdManager.h>
#if defined(__GNUC__) && __GNUC_PREREQ(11,0)
#pragma GCC diagnostic pop
#endif

class ReadyAdapter : public fdReg {
public:
	typedef boost::function<void (fdRegType)> callback;

	ReadyAdapter(int fd, fdRegType reg, callback cb, uint32_t verbose);

	~ReadyAdapter();

private:
	callback m_cb;
	int m_fd;
	fdRegType m_reg;
	uint32_t m_verbose;

	void callBack(void) { m_cb(getType()); }
};

#endif /* __READY_ADAPTER_H */
