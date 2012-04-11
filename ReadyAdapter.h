#ifndef __READY_ADAPTER_H
#define __READY_ADAPTER_H

#include <fdManager.h>

template<class T> class ReadyAdapter : public fdReg {
public:
	explicit ReadyAdapter(int fd, fdRegType reg, T *obj) :
			fdReg(fd, reg), m_obj(obj) { }

private:
	void callBack(void) { m_obj->fdReady(getType()); }

	T *m_obj;
};

#endif /* __READY_ADAPTER_H */
