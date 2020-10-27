#ifndef __SMS_CONTROL_PV_H
#define __SMS_CONTROL_PV_H

#include <time.h>
#include <string>
#include <stdint.h>

#ifndef FLOAT64_MAX
#define FLOAT64_MAX (1.7976931348623157E308)
#endif

#ifndef FLOAT64_MIN
#define FLOAT64_MIN (-1.7976931348623157E308)
#endif

#ifndef FLOAT64_EPSILON
#define FLOAT64_EPSILON (0.0000000000001)
#endif

#ifndef INT32_MAX
#define INT32_MAX (2147483647)
#endif

#include <gddAppFuncTable.h>
#include <smartGDDPointer.h>
#include <casdef.h>

class DataSource;
class SMSControl;

/* We need to a specialized destructor to delete allocated data when
 * the gdd holding them drops its last reference.
 */
class fixedStringDestructor : public gddDestructor {
	virtual void run(void *p) {
		aitFixedString *s = (aitFixedString *)p;
		delete [] s;
	}
};

class charArrayDestructor : public gddDestructor {
	virtual void run(void *p) {
		char *s = (char *)p;
		delete [] s;
	}
};

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

	void timestamp(struct timespec &ts); // Wallclock Time...!

	void destroy(void);

	virtual void changed(void);

protected:
	smartGDDPointer m_value;
	std::string m_pv_name;
	bool m_interested;

	gddAppFuncTable<smsPV>	m_read_table;

	virtual gddAppFuncTableStatus getValue(gdd &value) = 0;
	virtual gddAppFuncTableStatus getEnums(gdd &value);
	gddAppFuncTableStatus defaultNumber(gdd &in);
	gddAppFuncTableStatus defaultString(gdd &in);
	gddAppFuncTableStatus maximumNumber(gdd &in);
	gddAppFuncTableStatus minimumNumber(gdd &in);
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

	virtual void changed(void);

private:
	gddAppFuncTableStatus getValue(gdd &value);

	void update(uint32_t run, struct timespec *ts); // Wallclock Time...!

	friend class SMSControl;
};

class smsRecordingPV : public smsPV {
public:
	smsRecordingPV(const std::string &prefix, SMSControl *ctrl);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	void update(bool recording, struct timespec *ts); // Wallclock Time...!

	virtual aitEnum bestExternalType(void) const;

	virtual void changed(void);

private:
	SMSControl *m_ctrl;

	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);
};

class smsStringPV : public smsPV {
public:
	enum { MAX_LENGTH = 2048 };

	smsStringPV(const std::string &name, bool auto_save = false);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	void update(const std::string str,
		struct timespec *ts); // Wallclock Time...!

	virtual unsigned int maxDimension(void) const;
	virtual aitIndex maxBound(unsigned int dim) const;
	virtual aitEnum bestExternalType(void) const;

	void unset(bool init = false,
		struct timespec *ts = (struct timespec *) NULL); // Wallclock Time

	bool valid(void);
	std::string value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);

	bool m_first_set;

private:
	bool m_auto_save;
};

class smsBooleanPV : public smsPV {
public:
	smsBooleanPV(const std::string &name,
		bool auto_save = false,
		bool no_changed_on_update = false);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	virtual void update(bool val, struct timespec *ts, // Wallclock Time...!
		bool force_changed = false);

	bool valid(void);
	bool value(void);

	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);

	bool m_first_set;

private:
	bool m_auto_save;
	bool m_no_changed_on_update;
};

class smsEnabledPV : public smsBooleanPV {
public:
	smsEnabledPV(const std::string &name, DataSource *dataSource,
		bool auto_save = false);

	virtual aitEnum bestExternalType(void) const;

	void update(bool val, struct timespec *ts, // Wallclock Time...!
		bool force_changed = false);

	gddAppFuncTableStatus getEnums(gdd &value);

private:
	DataSource *m_dataSource;
};

class smsErrorPV : public smsPV {
public:
	smsErrorPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(bool val, bool major,
		struct timespec *ts); // Wallclock Time...!

	void set(void);
	void reset(void);

	bool valid(void);
	bool value(void);

	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsUint32PV : public smsPV {
public:
	smsUint32PV(const std::string &name,
		// Note: Uint32's in EPICS are Really Int32's...
		uint32_t min = 0, uint32_t max = INT32_MAX,
		bool auto_save = false);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(uint32_t val, struct timespec *ts, // Wallclock Time...!
		bool no_log = false);

	bool valid(void);
	uint32_t value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);

	uint32_t m_min, m_max;

	bool m_first_set;

protected:
	gddAppFuncTable<smsUint32PV>	m_read_table;

	gddAppFuncTableStatus defaultNumber(gdd &in);
	gddAppFuncTableStatus defaultString(gdd &in);
	gddAppFuncTableStatus maximumNumber(gdd &in);
	gddAppFuncTableStatus minimumNumber(gdd &in);

	void initReadTable(void);

private:
	bool m_auto_save;
};

class smsInt32PV : public smsPV {
public:
	smsInt32PV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(int32_t val, struct timespec *ts); // Wallclock Time...!
	bool valid(void);
	int32_t value(void);

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

	void update(uint16_t val, struct timespec *ts); // Wallclock Time...!

	void connected(void);
	void disconnected(void);
	void failed(void);
	void trying_to_connect(void);
	void waiting_for_connect_ack(void);

	bool valid(void);
	uint32_t value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);
};

class smsPassThruPV : public smsPV {
public:
	smsPassThruPV(const std::string &name, bool auto_save = false);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(uint16_t val, struct timespec *ts); // Wallclock Time...!

	void ignore(void);
	void passthru(void);
	void execute(void);

	bool valid(void);
	uint32_t value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);

	bool m_first_set;

private:
	bool m_auto_save;
};

class smsTriggerPV : public smsPV {
public:
	smsTriggerPV(const std::string &name);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	virtual void changed(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual void triggered(struct timespec *ts) = 0; // Wallclock Time...!
};

class smsFloat64PV : public smsPV {
public:
	smsFloat64PV(const std::string &name,
		double min = FLOAT64_MIN, double max = FLOAT64_MAX,
		double epsilon = FLOAT64_EPSILON,
		bool auto_save = false);

	caStatus read(const casCtx &ctx, gdd &prototype);
	caStatus write(const casCtx &ctx, const gdd &value);

	virtual aitEnum bestExternalType(void) const;

	void update(double val, struct timespec *ts); // Wallclock Time...!
	bool valid(void);
	double value(void);

public:
	gddAppFuncTableStatus getValue(gdd &value);
	gddAppFuncTableStatus getEnums(gdd &value);

	virtual bool allowUpdate(const gdd &val);
	virtual void changed(void);

	double m_min, m_max;
	double m_epsilon;

	bool m_first_set;

protected:
	gddAppFuncTable<smsFloat64PV>    m_read_table;

	gddAppFuncTableStatus defaultNumber(gdd &in);
	gddAppFuncTableStatus defaultPrecision(gdd &in);
	gddAppFuncTableStatus defaultString(gdd &in);
	gddAppFuncTableStatus maximumNumber(gdd &in);
	gddAppFuncTableStatus minimumNumber(gdd &in);

	void initReadTable(void);

private:
	bool m_auto_save;
};

#endif /* __SMS_CONTROL_PV_H */
