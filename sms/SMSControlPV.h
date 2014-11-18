#ifndef __SMS_CONTROL_PV_H
#define __SMS_CONTROL_PV_H

#include <time.h>
#include <string>

#include <gddAppFuncTable.h>
#include <smartGDDPointer.h>
#include <casdef.h>

class DataSource;
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
	gddAppFuncTableStatus defaultNumber(gdd &in);
	gddAppFuncTableStatus defaultString(gdd &in);
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

	void update(const std::string str, struct timespec *ts);

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

	virtual void update(bool val, struct timespec *ts);

	bool valid(void);
	bool value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsEnabledPV : public smsBooleanPV {
public:
	smsEnabledPV(const std::string &name, DataSource *dataSource);

	virtual aitEnum bestExternalType(void) const;

	void update(bool val, struct timespec *ts);

	gddAppFuncTableStatus getEnums(gdd &value);

private:
	DataSource *m_dataSource;
};

class smsErrorPV : public smsBooleanPV {
public:
	smsErrorPV(const std::string &name);

	virtual aitEnum bestExternalType(void) const;

	void update(bool val, struct timespec *ts);
	void set(void);
	void reset(void);

	gddAppFuncTableStatus getEnums(gdd &value);
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

class smsConnectedPV : public smsPV {
public:
	smsConnectedPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(uint16_t val, struct timespec *ts);

	void connected(void);
	void disconnected(void);
	void failed(void);

	bool valid(void);
	bool value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

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

class smsFloat64PV : public smsPV {
public:
	smsFloat64PV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(double val, struct timespec *ts);
	bool valid(void);
	double value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

#endif /* __SMS_CONTROL_PV_H */
