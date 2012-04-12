#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

#include <gddApps.h>

/* We need to a specialized destructor to delete enum strings when
 * the gdd holding them drops its last reference.
 */
class fixedStringDestructor : public gddDestructor {
	virtual void run(void *);
};

void fixedStringDestructor::run(void *p)
{
	aitFixedString *s = (aitFixedString *)p;
	delete [] s;
}

/* ----------------------------------------------------------------------- */

smsPV::smsPV()
{
	m_interested = false;
}

smsPV::~smsPV()
{
}

const char *smsPV::getName(void) const
{
	return m_pv_name.c_str();
}

void smsPV::show(unsigned level) const
{
	/* TODO perhaps we'll want this some day */
}

caStatus smsPV::interestRegister(void)
{
	if (getCAS())
		m_interested = true;
	return S_casApp_success;
}

void smsPV::interestDelete(void)
{
	m_interested = false;
}

aitEnum smsPV::bestExternalType(void) const
{
	return casPV::bestExternalType();
}

caStatus smsPV::read(const casCtx &ctx, gdd &prototype)
{
	return S_casApp_noSupport;
}

caStatus smsPV::write(const casCtx &ctx, const gdd &value)
{
	return S_casApp_noSupport;
}

casChannel *smsPV::createChannel(const casCtx &ctx, const char * const user,
				 const char * const host)
{
	return casPV::createChannel(ctx, user, host);
}

void smsPV::notify(void)
{
	if (m_interested && m_value.valid()) {
		caServer *cas = getCAS();
		casEventMask mask = cas->valueEventMask() | cas->logEventMask();
		postEvent(mask, *m_value);
	}
}

void smsPV::destroy(void)
{
	/* PVs are pre-allocated; SMControl will clean us up */
}

/* ----------------------------------------------------------------------- */

smsReadOnlyChannel::smsReadOnlyChannel(const casCtx &cas) : casChannel(cas)
{
}

bool smsReadOnlyChannel::writeAccess(void) const
{
	return false;
}

/* ----------------------------------------------------------------------- */

smsRunNumberPV::smsRunNumberPV(const std::string &prefix)
{
	struct timespec ts;

	/* Default to not recording on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumUint32);
	m_value->put(0);

	clock_gettime(CLOCK_REALTIME, &ts);
	m_value->setTimeStamp(&ts);

	m_read_table.installReadFunc ("value", &smsRunNumberPV::getValue);

	m_pv_name = prefix + ":run_number";
}

casChannel *smsRunNumberPV::createChannel(const casCtx &ctx,
					  const char * const user,
					  const char * const host)
{
	return new smsReadOnlyChannel(ctx);
}

aitEnum smsRunNumberPV::bestExternalType(void) const
{
	return aitEnumUint32;
}

gddAppFuncTableStatus smsRunNumberPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

caStatus smsRunNumberPV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

void smsRunNumberPV::update(uint32_t run, struct timespec *ts)
{
	aitUint32 v;
	gdd *val;

	m_value->get(v);
	if (v == run)
		return;

	val = new gddScalar(gddAppType_value, aitEnumUint32);
	val->put(run);
	val->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = val;

	notify();
}
/* ----------------------------------------------------------------------- */

smsRecordingPV::smsRecordingPV(const std::string &prefix, SMSControl *sms) :
		m_sms(sms)
{
	struct timespec ts;

	/* Default to not recording on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->put(0);

	clock_gettime(CLOCK_REALTIME, &ts);
	m_value->setTimeStamp(&ts);

	m_read_table.installReadFunc ("value", &smsRecordingPV::getValue);
	m_read_table.installReadFunc ("enums", &smsRecordingPV::getEnums);

	m_pv_name = prefix + ":recording";
}

aitEnum smsRecordingPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsRecordingPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsRecordingPV::getEnums(gdd &in)
{
	aitFixedString *str;
	fixedStringDestructor *des;

	str = new aitFixedString[2];
	if (!str)
		return S_casApp_noMemory;

	des = new fixedStringDestructor;
	if (!des) {
		delete [] str;
		return S_casApp_noMemory;
	}

	strncpy(str[0].fixed_string, "false", sizeof(str[0].fixed_string));
	strncpy(str[1].fixed_string, "true", sizeof(str[1].fixed_string));

	in.setDimension(1);
	in.setBound(0, 0, 2);
	in.putRef(str, des);

	return S_cas_success;
}

caStatus smsRecordingPV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

caStatus smsRecordingPV::write(const casCtx &ctx, const gdd &val)
{
	aitUint16 v;

	if (!val.isScalar())
		return S_casApp_noSupport;

	val.get(v);
	if (v > 1)
		return S_casApp_noSupport;

	if (m_sms->setRecording(v))
		return S_casApp_success;

	/* TODO is there a better return code? Should we return success
	 * even if we fail?
	 */
	return S_casApp_outOfBounds;
}

void smsRecordingPV::update(bool recording, struct timespec *ts)
{
	aitUint16 v;
	gdd *val;

	m_value->get(v);
	if (v == recording)
		return;

	val = new gddScalar(gddAppType_value, aitEnumEnum16);
	val->put(recording);
	val->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = val;

	notify();
}
