#ifndef __SMS_CONTROL_PV_H
#define __SMS_CONTROL_PV_H

#include <time.h>
#include <string>

#include <gddAppFuncTable.h>
#include <smartGDDPointer.h>
#include <casdef.h>

class SMSControl;

class smsPV : public casPV {
public:
	smsPV();
	smsPV(const std::string &name);
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
	bool m_interested;

	gddAppFuncTable<smsPV>	m_read_table;

	virtual gddAppFuncTableStatus getValue(gdd &value) = 0;
	virtual gddAppFuncTableStatus getEnums(gdd &value);
	gddAppFuncTableStatus unusedType(gdd &in);

	void initReadTable(void);
	void notify(void);
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

	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);
};

class smsStringPV : public smsPV {
public:
	enum { MAX_LENGTH = 1024 };

	smsStringPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual unsigned int maxDimension(void) const;
	virtual aitIndex maxBound(unsigned int dim) const;
	virtual aitEnum bestExternalType(void) const;

	void unset(void);
	bool valid(void);
	std::string value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsBooleanPV : public smsPV {
public:
	smsBooleanPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(bool val, struct timespec *ts);
	bool valid(void);
	bool value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsUint32PV : public smsPV {
public:
	smsUint32PV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(uint32_t val, struct timespec *ts);
	bool valid(void);
	uint32_t value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsTriggerPV : public smsPV {
public:
	smsTriggerPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual void triggered(void) = 0;
};

#endif /* __SMS_CONTROL_PV_H */
