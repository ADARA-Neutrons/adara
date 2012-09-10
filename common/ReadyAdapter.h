#ifndef __READY_ADAPTER_H
#define __READY_ADAPTER_H

#include <boost/function.hpp>
#include <fdManager.h>

class ReadyAdapter : public fdReg {
public:
	typedef boost::function<void (fdRegType)> callback;

	ReadyAdapter(int fd, fdRegType reg, callback cb) :
			fdReg(fd, reg), m_cb(cb) { }

private:
	callback m_cb;

	void callBack(void) { m_cb(getType()); }
};

#endif /* __READY_ADAPTER_H */
