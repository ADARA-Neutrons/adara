#ifndef __SMS_CONTROL_PV_H
#define __SMS_CONTROL_PV_H

#include <time.h>
#include <string>

#include "EPICS.h"

#include <gddAppFuncTable.h>
#include <smartGDDPointer.h>

class SMSControl;

class smsPV : public casPV {
public:
	smsPV();
	~smsPV();

	const char *getName(void) const;
	void show(unsigned level) const;

	caStatus interestRegister(void);
	void interestDelete(void);
	virtual aitEnum bestExternalType(void) const;

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual casChannel *createChannel(const casCtx &ctx,
					  const char * const user,
					  const char * const host);

	void destroy(void);

protected:
	smartGDDPointer m_value;
	std::string m_pv_name;

	void notify(void);

private:
	bool m_interested;
};

class smsReadOnlyChannel : public casChannel {
public:
	smsReadOnlyChannel(const casCtx &cas);
	bool writeAccess() const;
};

class smsRunNumberPV : public smsPV {
public:
	smsRunNumberPV(const std::string &prefix);

	caStatus read(const casCtx &ctx, gdd &prototype);

	virtual aitEnum bestExternalType(void) const;

	casChannel *createChannel(const casCtx &ctx, const char * const user,
				  const char * const host);

private:
	gddAppFuncTable<smsRunNumberPV>	m_read_table;

	gddAppFuncTableStatus getValue(gdd &value);

	void update(uint32_t run, struct timespec *ts);

	friend class SMSControl;
};

class smsRecordingPV : public smsPV {
public:
	smsRecordingPV(const std::string &prefix, SMSControl *sms);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	void update(bool recording, struct timespec *ts);

	virtual aitEnum bestExternalType(void) const;

private:
	SMSControl *m_sms;
	gddAppFuncTable<smsRecordingPV>	m_read_table;

	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);
};

#endif /* __SMS_CONTROL_PV_H */
