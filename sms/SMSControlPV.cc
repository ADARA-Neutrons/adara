#include "EPICS.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

#include <gddApps.h>

/* gcc 4.4.6 on RHEL 6 cannot figure out that gdd::get(T &) will actually
 * initiallize the variable, so it warns. This conflicts with a clean build
 * using -Werror, but we can quiet the compiler easily.
 */
#define uninitialized_var(x) x = 0

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

/* ----------------------------------------------------------------------- */

static gddAppFuncTableStatus getBooleanEnums(gdd &in)
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

/* ----------------------------------------------------------------------- */

smsPV::smsPV() : m_interested(false)
{
	initReadTable();
}

smsPV::smsPV(const std::string &name) :
	m_pv_name(name), m_interested(false)
{
	initReadTable();
}

smsPV::~smsPV()
{
}

void smsPV::initReadTable(void)
{
	m_read_table.installReadFunc("value", &smsPV::getValue);
	m_read_table.installReadFunc("enums", &smsPV::getEnums);

	/* These are not currently used by any child classes */
	m_read_table.installReadFunc("alarmHigh", &smsPV::unusedType);
	m_read_table.installReadFunc("alarmLow", &smsPV::unusedType);
	m_read_table.installReadFunc("alarmHighWarning", &smsPV::unusedType);
	m_read_table.installReadFunc("alarmLowWarning", &smsPV::unusedType);
	m_read_table.installReadFunc("controlHigh", &smsPV::unusedType);
	m_read_table.installReadFunc("controlLow", &smsPV::unusedType);
	m_read_table.installReadFunc("graphicHigh", &smsPV::unusedType);
	m_read_table.installReadFunc("graphicLow", &smsPV::unusedType);
	m_read_table.installReadFunc("precision", &smsPV::unusedType);
	m_read_table.installReadFunc("units", &smsPV::unusedType);
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

gddAppFuncTableStatus smsPV::getEnums(gdd &)
{
	return S_gddAppFuncTable_badType;
}

gddAppFuncTableStatus smsPV::unusedType(gdd &)
{
	return S_gddAppFuncTable_badType;
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
	return getBooleanEnums(in);
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
	unset();
}

unsigned int smsStringPV::maxDimension(void) const
{
	return 1;
}

aitIndex smsStringPV::maxBound(unsigned int dim) const
{
	return dim ? 0 : MAX_LENGTH;
}

aitEnum smsStringPV::bestExternalType(void) const
{
	return aitEnumUint8;
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
	unsigned int start, nelem;

	/* caput sends a null string as a scalar, so interpret that
	 * as an unset request.
	 */
	if (val.isScalar() && val.primitiveType() == aitEnumUint8) {
		unset();
		return S_cas_success;
	}

	if (!val.isAtomic())
		return S_casApp_noSupport;

	if (val.dimension() != 1)
		return S_casApp_outOfBounds;

	val.getBound(0, start, nelem);
	if (start || nelem > MAX_LENGTH)
		return S_casApp_outOfBounds;

	/* Writing no elements will be considered an unset request.
	 */
	if (!nelem) {
		unset();
		return S_cas_success;
	}

	if (!allowUpdate(val)) {
		/* We don't want to update the PV at this time; still
		 * send a notification to any watchers, and just return
		 * success.
		 */
		notify();
		return S_casApp_success;
	}

	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, val.dataPointer(), nelem);

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
				      MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);

	struct timespec ts;
	val.getTimeStamp(&ts);
	nv->setTimeStamp(&ts);
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	m_value = nv;
	notify();
	changed();

	return S_casApp_success;
}

bool smsStringPV::allowUpdate(const gdd &)
{
	return true;
}

void smsStringPV::unset(void)
{
	if (m_value.valid() && m_value->getStat() == epicsAlarmUDF)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	strcpy(new_str, "(unset)");

	m_value = new gddAtomic(gddAppType_value, aitEnumUint8, 1, MAX_LENGTH);
	m_value->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	m_value->setStat(epicsAlarmUDF);
	m_value->setSevr(epicsSevNone);
	m_value->setTimeStamp(&ts);

	unsigned int start, nelem;
	m_value->getBound(0, start, nelem);

	notify();
	changed();
}

bool smsStringPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

std::string smsStringPV::value(void)
{
	return std::string((char *) m_value->dataPointer());
}

void smsStringPV::changed(void)
{
}

/* ----------------------------------------------------------------------- */

smsBooleanPV::smsBooleanPV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts);
	m_value->put(0);

	m_pv_name = name;
}

aitEnum smsBooleanPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsBooleanPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsBooleanPV::getEnums(gdd &in)
{
	return getBooleanEnums(in);
}

caStatus smsBooleanPV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

caStatus smsBooleanPV::write(const casCtx &ctx, const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar())
		return S_casApp_noSupport;

	val.get(v);
	if (v > 1)
		return S_casApp_noSupport;

	m_value->get(cur);
	if (v == cur)
		return S_casApp_success;

	val.getTimeStamp(&ts);
	update(v, &ts);
	changed();

	return S_casApp_success;
}

bool smsBooleanPV::allowUpdate(const gdd &)
{
	return true;
}

void smsBooleanPV::update(bool val, struct timespec *ts)
{
	aitUint32 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

bool smsBooleanPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

bool smsBooleanPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsBooleanPV::changed(void)
{
}

/* ----------------------------------------------------------------------- */

smsUint32PV::smsUint32PV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumUint32);
	m_value->setTimeStamp(&ts);
	m_value->put(0);

	m_pv_name = name;
}

aitEnum smsUint32PV::bestExternalType(void) const
{
	return aitEnumUint32;
}

gddAppFuncTableStatus smsUint32PV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

caStatus smsUint32PV::read(const casCtx &ctx, gdd &prototype)
{
	return m_read_table.read(*this, prototype);
}

caStatus smsUint32PV::write(const casCtx &ctx, const gdd &val)
{
	aitUint32 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar())
		return S_casApp_noSupport;

	val.get(v);
	m_value->get(cur);
	if (v == cur)
		return S_casApp_success;

	val.getTimeStamp(&ts);
	update(v, &ts);
	changed();

	return S_casApp_success;
}

bool smsUint32PV::allowUpdate(const gdd &)
{
	return true;
}

void smsUint32PV::update(uint32_t val, struct timespec *ts)
{
	aitUint32 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumUint32);
	nval->put(val);
	nval->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

bool smsUint32PV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

uint32_t smsUint32PV::value(void)
{
	aitUint32 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsUint32PV::changed(void)
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
	return getBooleanEnums(in);
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
