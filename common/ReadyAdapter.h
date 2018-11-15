#ifndef __READY_ADAPTER_H
#define __READY_ADAPTER_H

// Note: Assumes #including File has #included sms/Logging.h for Logging...

#include <boost/function.hpp>
#include <fdManager.h>

class ReadyAdapter : public fdReg {
public:
	typedef boost::function<void (fdRegType)> callback;

	ReadyAdapter(int fd, fdRegType reg, callback cb) :
			fdReg(fd, reg), m_cb(cb), m_fd(fd), m_reg(reg)
	{
		DEBUG("ReadyAdapter Created m_fd=" << m_fd << " reg=" << m_reg);
	}

	~ReadyAdapter()
	{
		DEBUG("ReadyAdapter Destroyed m_fd=" << m_fd << " reg=" << m_reg);
	}

private:
	callback m_cb;
int m_fd;
fdRegType m_reg;

	void callBack(void) { m_cb(getType()); }
};

#endif /* __READY_ADAPTER_H */
