#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

#include <gddApps.h>

/* gcc 4.4.6 on RHEL 6 cannot figure out that gdd::get(T &) will actually
 * initiallize the variable, so it warns. This conflicts with a clean build
 * using -Werror, but we can quiet the compiler easily.
 */
#define uninitialized_var(x) x = 0

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

smsPV::smsPV() : m_interested(false)
{
}

smsPV::smsPV(const std::string &name) :
	m_pv_name(name), m_interested(false)
{
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

	m_pv_name = prefix + ":RunNumber";
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
	aitUint32 uninitialized_var(v);
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

	m_pv_name = prefix + ":Recording";
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
	aitUint16 uninitialized_var(v);

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
	aitUint16 uninitialized_var(v);
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

/* ----------------------------------------------------------------------- */

smsStringPV::smsStringPV(const std::string &name) : smsPV(name)
{
	m_pv_name = name;
	m_read_table.installReadFunc ("value", &smsStringPV::getValue);
	unset();
}

aitEnum smsStringPV::bestExternalType(void) const
{
	return aitEnumString;
}

gddAppFuncTableStatus smsStringPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

caStatus smsStringPV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

caStatus smsStringPV::write(const casCtx &ctx, const gdd &val)
{
	aitString old_str, new_str;

	if (!val.isScalar())
		return S_casApp_noSupport;

	/* TODO need to be able to conditionally deny this change request;
	 * ie, we don't want to change RunInfo during a run
	 */

	m_value->get(old_str);
	val.get(new_str);
	if (strcmp(old_str.string(), new_str.string())) {
		struct timespec ts;
		val.getTimeStamp(&ts);

		/* One would think nv->copy(&val) would work, but no. It
		 * cauaes nv to become a container, which won't convert
		 * back to string.
		 */
		gdd *nv = new gddScalar(gddAppType_value, aitEnumString);
		nv->put(new_str);
		nv->setTimeStamp(&ts);
		m_value = nv;
		notify();
		changed();
	}

	return S_casApp_success;
}

void smsStringPV::unset(void)
{
	if (m_value.valid() && m_value->getStat() == epicsAlarmUDF)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumString);
	m_value->put(aitString("(unset)"));
	m_value->setStat(epicsAlarmUDF);
	m_value->setSevr(epicsSevNone);
	m_value->setTimeStamp(&ts);

	notify();
	changed();
}

bool smsStringPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

const std::string smsStringPV::value(void)
{
	std::string val;

	if (m_value.valid()) {
		aitString str;
		m_value->get(str);
		val = str.string();
	}

	return val;
}

void smsStringPV::changed(void)
{
}

/* ----------------------------------------------------------------------- */

smsTriggerPV::smsTriggerPV(const std::string &name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	/* We'll stay false except for briefly when someone sets us true */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts);
	m_value->put(0);

	m_read_table.installReadFunc ("value", &smsTriggerPV::getValue);
	m_read_table.installReadFunc ("enums", &smsTriggerPV::getEnums);

	m_pv_name = name;
}

aitEnum smsTriggerPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsTriggerPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsTriggerPV::getEnums(gdd &in)
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

caStatus smsTriggerPV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

caStatus smsTriggerPV::write(const casCtx &ctx, const gdd &val)
{
	aitUint16 uninitialized_var(v);

	if (!val.isScalar())
		return S_casApp_noSupport;

	val.get(v);
	if (v > 1)
		return S_casApp_noSupport;

	if (v) {
		triggered();

		if (m_interested) {
			caServer *cas = getCAS();
			casEventMask mask = cas->valueEventMask() |
					    cas->logEventMask();
			smartGDDPointer edge;
			struct timespec ts;

			val.getTimeStamp(&ts);
			edge = new gddScalar(gddAppType_value, aitEnumEnum16);
			edge->setTimeStamp(&ts);
			edge->put(1);
			postEvent(mask, *edge);

			ts.tv_nsec++;
			if (ts.tv_nsec > 1000000000) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000;
			}
			m_value->setTimeStamp(&ts);
			postEvent(mask, *m_value);
		}
	}

	return S_casApp_success;
}
