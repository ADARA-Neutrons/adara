#include "EPICS.h"
#include "DataSource.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "ADARAUtils.h"

#include <stdint.h>

#include <gddApps.h>

#include "Logging.h"
#include "EventFd.h"

static LoggerPtr logger(Logger::getLogger("SMS.SMSControlPV"));

RateLimitedLogging::History RLLHistory_SMSControlPV;

// Rate-Limited Logging IDs...
#define RLL_CONNPV_CONNECTED          0
#define RLL_CONNPV_DISCONNECTED       1
#define RLL_CONNPV_FAILED             2
#define RLL_CONNPV_TRYING             3
#define RLL_CONNPV_WAITING            4

/* gcc 4.4.6 on RHEL 6 cannot figure out that gdd::get(T &) will actually
 * initiallize the variable, so it warns. This conflicts with a clean build
 * using -Werror, but we can quiet the compiler easily.
 */
#define uninitialized_var(x) x = 0

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

static gddAppFuncTableStatus getEnabledEnums(gdd &in)
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
	strncpy(str[0].fixed_string, "Disabled", sizeof(str[0].fixed_string));
	strncpy(str[1].fixed_string, "Enabled", sizeof(str[1].fixed_string));

	in.setDimension(1);
	in.setBound(0, 0, 2);
	in.putRef(str, des);

	return S_cas_success;
}

/* ----------------------------------------------------------------------- */

static gddAppFuncTableStatus getErrorEnums(gdd &in)
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
	strncpy(str[0].fixed_string, "OK", sizeof(str[0].fixed_string));
	strncpy(str[1].fixed_string, "Error", sizeof(str[1].fixed_string));

	in.setDimension(1);
	in.setBound(0, 0, 2);
	in.putRef(str, des);

	return S_cas_success;
}

/* ----------------------------------------------------------------------- */

static gddAppFuncTableStatus getConnectedEnums(gdd &in)
{
	aitFixedString *str;
	fixedStringDestructor *des;

	str = new aitFixedString[5];
	if (!str)
		return S_casApp_noMemory;

	des = new fixedStringDestructor;
	if (!des) {
		delete [] str;
		return S_casApp_noMemory;
	}
	strncpy(str[0].fixed_string, "Connected", sizeof(str[0].fixed_string));
	strncpy(str[1].fixed_string, "Disconnected",
		sizeof(str[1].fixed_string));
	strncpy(str[2].fixed_string, "Failed", sizeof(str[2].fixed_string));
	strncpy(str[3].fixed_string, "TryingToConnect",
		sizeof(str[3].fixed_string));
	strncpy(str[4].fixed_string, "WaitingForConnectAck",
		sizeof(str[4].fixed_string));

	in.setDimension(1);
	in.setBound(0, 0, 5);
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
	/* However, we are applying defaults so that clients requesting
	 * DBR_CTRL types won't complain so much.
	 */
	m_read_table.installReadFunc("alarmHigh", &smsPV::defaultNumber);
	m_read_table.installReadFunc("alarmLow", &smsPV::defaultNumber);
	m_read_table.installReadFunc("alarmHighWarning", &smsPV::defaultNumber);
	m_read_table.installReadFunc("alarmLowWarning", &smsPV::defaultNumber);
	m_read_table.installReadFunc("controlHigh", &smsPV::defaultNumber);
	m_read_table.installReadFunc("controlLow", &smsPV::defaultNumber);
	m_read_table.installReadFunc("graphicHigh", &smsPV::defaultNumber);
	m_read_table.installReadFunc("graphicLow", &smsPV::defaultNumber);
	m_read_table.installReadFunc("precision", &smsPV::defaultNumber);
	m_read_table.installReadFunc("units", &smsPV::defaultString);
}

const char *smsPV::getName(void) const
{
	return m_pv_name.c_str();
}

void smsPV::show(unsigned UNUSED(level)) const
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

caStatus smsPV::read(const casCtx &UNUSED(ctx), gdd &UNUSED(prototype))
{
	return S_casApp_noSupport;
}

caStatus smsPV::write(const casCtx &UNUSED(ctx), const gdd &UNUSED(value))
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

gddAppFuncTableStatus smsPV::defaultNumber(gdd &in)
{
	in.put(0.0);
	return S_cas_success;
}

gddAppFuncTableStatus smsPV::defaultString(gdd &in)
{
	in.put("");
	return S_cas_success;
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
					  const char * const UNUSED(user),
					  const char * const UNUSED(host))
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

caStatus smsRunNumberPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint32 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsRunNumberPV::read() m_pv_name=" << m_pv_name
		<< " value=" << v);
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

caStatus smsRecordingPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsRecordingPV::read() m_pv_name=" << m_pv_name
		<< " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsRecordingPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v);

	if (!val.isScalar()) {
		ERROR("smsRecordingPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsRecordingPV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
	if (v > 1)
		return S_casApp_noSupport;

	if (m_sms->setRecording(v))
		return S_casApp_success;

	/* don't cause disruption at the CA level just because the run 
	 * couldn't start
	 */
	return S_casApp_success;
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

caStatus smsStringPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	DEBUG("smsStringPV::read() m_pv_name=" << m_pv_name
		<< " value=" << m_value.get());
	return m_read_table.read(*this, prototype);
}

caStatus smsStringPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	unsigned int start, nelem;

	/* caput sends a null string as a scalar, so interpret that
	 * as an unset request.
	 */
	if (val.isScalar() && val.primitiveType() == aitEnumUint8) {
		DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Null String, Unset Value.");
		unset();
		return S_cas_success;
	}

	if (!val.isAtomic()) {
		ERROR("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not Atomic!");
		return S_casApp_noSupport;
	}

	if (val.dimension() != 1) {
		ERROR("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Bounds (Multi-Dimensional)!");
		return S_casApp_outOfBounds;
	}

	val.getBound(0, start, nelem);
	if (start || nelem > MAX_LENGTH) {
		ERROR("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Bounds (" << nelem << " > MAX_LENGTH)!");
		return S_casApp_outOfBounds;
	}

	/* Writing no elements will be considered an unset request.
	 */
	if (!nelem) {
		DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Writing No Elements, Unset Value.");
		unset();
		return S_cas_success;
	}

	if (!allowUpdate(val)) {
		/* We don't want to update the PV at this time; still
		 * send a notification to any watchers, and just return
		 * success.
		 */
		DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
			<< " Updates Not Allowed, Ignore Value.");
		notify();
		return S_casApp_success;
	}

	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, val.dataPointer(), nelem);

	DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
		<< " value=" << new_str);

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

void smsStringPV::update(const std::string str, struct timespec *ts)
{
	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, (void *)str.c_str(), str.length());

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
					MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	nv->setTimeStamp(ts);
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nv;

	notify();
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

smsMTStrPV::smsMTStrPV(const std::string &name) : smsStringPV(name),
 						m_readLock(new epicsMutex()) 
{
}

std::string smsMTStrPV::value(void)
{
	m_readLock->lock();
	std::string retval((char *) m_value->dataPointer());
	m_readLock->unlock();
	return retval;
}


caStatus smsMTStrPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	unsigned int start, nelem;

	/* caput sends a null string as a scalar, so interpret that
	 * as an unset request.
	 */
	if (val.isScalar() && val.primitiveType() == aitEnumUint8) {
		DEBUG("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Null String, Unset Value.");
		unset();
		return S_cas_success;
	}

	if (!val.isAtomic()) {
		ERROR("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not Atomic!");
		return S_casApp_noSupport;
	}

	if (val.dimension() != 1) {
		ERROR("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Bounds (Multi-Dimensional)!");
		return S_casApp_outOfBounds;
	}

	val.getBound(0, start, nelem);
	if (start || nelem > MAX_LENGTH) {
		ERROR("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Bounds (" << nelem << " > MAX_LENGTH)!");
		return S_casApp_outOfBounds;
	}

	/* Writing no elements will be considered an unset request.
	 */
	if (!nelem) {
		DEBUG("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Writing No Elements, Unset Value.");
		unset();
		return S_cas_success;
	}

	if (!allowUpdate(val)) {
		/* We don't want to update the PV at this time; still
		 * send a notification to any watchers, and just return
		 * success.
		 */
		DEBUG("smsMTStrPV::write() m_pv_name=" << m_pv_name
			<< " Updates Not Allowed, Ignore Value.");
		notify();
		return S_casApp_success;
	}

	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, val.dataPointer(), nelem);

	DEBUG("smsMTStrPV::write() m_pv_name=" << m_pv_name
		<< " value=" << new_str);

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
					MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);

	struct timespec ts;
	val.getTimeStamp(&ts);
	nv->setTimeStamp(&ts);
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	m_readLock->lock();
	m_value = nv;
	m_readLock->unlock();
	notify();
	changed();

	return S_casApp_success;
}

void smsMTStrPV::update(const std::string str, struct timespec *ts)
{
	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, (void *)str.c_str(), str.length());

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
					MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	nv->setTimeStamp(ts);
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
        m_readLock->lock();
	m_value = nv;
        m_readLock->unlock();

	notify();
}

void smsMTStrPV::unset(void)
{
	if (m_value.valid() && m_value->getStat() == epicsAlarmUDF)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	strcpy(new_str, "(unset)");

	m_readLock->lock();
	m_value = new gddAtomic(gddAppType_value, aitEnumUint8, 1, MAX_LENGTH);
	m_value->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	m_value->setStat(epicsAlarmUDF);
	m_value->setSevr(epicsSevNone);
	m_value->setTimeStamp(&ts);

	unsigned int start, nelem;
	m_value->getBound(0, start, nelem);
	m_readLock->unlock();

	notify();
	changed();
}

/* ----------------------------------------------------------------------- */

smsBooleanPV::smsBooleanPV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
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

caStatus smsBooleanPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsBooleanPV::read() m_pv_name=" << m_pv_name
		<< " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsBooleanPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar()) {
		ERROR("smsBooleanPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsBooleanPV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
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
	aitUint16 uninitialized_var(v);
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
//
// fdIn must be produced by 
// #include <sys/eventfd.h>
// fdIn = eventfd(1, EFD_NONBLOCK);
//
smsMTBoolPV::smsMTBoolPV(const std::string &name, const SOCKET fdIn) : 
			smsBooleanPV(name),
			fdReg(fdIn, fdrRead),
 			m_readLock(new epicsMutex()),
			m_doneEvent(new epicsEvent()),
			m_updatefd(fdIn)
{
}

bool smsMTBoolPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_readLock->lock();
		m_value->get(v);
		m_readLock->unlock();
	return v;
}

void smsMTBoolPV::update(bool val, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
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
	m_readLock->lock();
	m_value = nval;
	m_readLock->unlock();

	notify();
}

void smsMTBoolPV::mtUpdate(bool val, struct timespec *ts)
{
   	uint64_t uval = val+1;

        m_updateLock->lock();	

 	m_update_ts = ts;
        ::write(m_updatefd, &uval, sizeof(uint64_t));
        m_doneEvent->wait();		// for remoteUpdate to be done
        m_updateLock->unlock();	
}

void smsMTBoolPV::callBack() {

	aitUint16 uninitialized_var(v);
    	uint64_t val;
	gdd *nval;

        ::read(m_updatefd, &val, sizeof(uint64_t));
	val = val - 5;

	m_value->get(v);
	if (v == val) {
		m_doneEvent->signal();
		return;
 	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put((uint16_t)val);
	nval->setTimeStamp(m_update_ts);

	m_readLock->lock();
	m_value = nval;
	m_readLock->unlock();

	notify();
	m_doneEvent->signal();
}

/* ----------------------------------------------------------------------- */
smsEnabledPV::smsEnabledPV(const std::string &name,
		DataSource *dataSource) :
	smsBooleanPV(name), m_dataSource(dataSource) { }

gddAppFuncTableStatus smsEnabledPV::getEnums(gdd &in)
{
	return getEnabledEnums(in);
}

aitEnum smsEnabledPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

void smsEnabledPV::update(bool val, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts);

	if (val != 0) {
		m_dataSource->enabled();
	}
	else {
		m_dataSource->disabled();
	}

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

/* ----------------------------------------------------------------------- */

smsErrorPV::smsErrorPV(const std::string &name) :
						smsBooleanPV(name) { }

gddAppFuncTableStatus smsErrorPV::getEnums(gdd &in)
{
	return getErrorEnums(in);
}

aitEnum smsErrorPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

void smsErrorPV::set() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(1, &ts);
}

void smsErrorPV::reset() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(0, &ts);
}

void smsErrorPV::update(bool val, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts);

	if (val != 0) {
		nval->setStat(epicsAlarmState);
		nval->setSevr(epicsSevMajor);
	}

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

/* ----------------------------------------------------------------------- */

smsConnectedPV::smsConnectedPV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts);
	m_value->put(0);

	m_pv_name = name;
}

aitEnum smsConnectedPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsConnectedPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsConnectedPV::getEnums(gdd &in)
{
	return getConnectedEnums(in);
}

caStatus smsConnectedPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsConnectedPV::read() m_pv_name=" << m_pv_name
		<< " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsConnectedPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar()) {
		ERROR("smsConnectedPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsConnectedPV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
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

bool smsConnectedPV::allowUpdate(const gdd &)
{
	return true;
}

void smsConnectedPV::update(uint16_t val, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts);

	// Failed...
	if (val == 2) {
		nval->setStat(epicsAlarmState);
		nval->setSevr(epicsSevMajor);
	}

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

void smsConnectedPV::connected() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(0, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_CONNECTED, m_pv_name,
			600, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsConnectedPV::connected() m_pv_name="
			<< m_pv_name << " (0)");
	}
}

void smsConnectedPV::disconnected() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(1, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_DISCONNECTED, m_pv_name,
			600, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsConnectedPV::disconnected() m_pv_name="
			<< m_pv_name << " (1)");
	}
}

void smsConnectedPV::failed() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(2, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_FAILED, m_pv_name,
			600, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsConnectedPV::failed() m_pv_name="
			<< m_pv_name << " (2)");
	}
}

void smsConnectedPV::trying_to_connect() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(3, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_TRYING, m_pv_name,
			600, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsConnectedPV::trying_to_connect() m_pv_name="
			<< m_pv_name << " (3)");
	}
}

void smsConnectedPV::waiting_for_connect_ack() {

	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	update(4, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_WAITING, m_pv_name,
			600, 3, 10, log_info ) ) {
		DEBUG(log_info
			<< "smsConnectedPV::waiting_for_connect_ack() m_pv_name="
			<< m_pv_name << " (4)");
	}
}

bool smsConnectedPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

bool smsConnectedPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsConnectedPV::changed(void)
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

caStatus smsUint32PV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint32 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsUint32PV::read() m_pv_name=" << m_pv_name << " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsUint32PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint32 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar()) {
		DEBUG("smsUint32PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsUint32PV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
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

smsInt32PV::smsInt32PV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumInt32);
	m_value->setTimeStamp(&ts);
	m_value->put(0);

	m_pv_name = name;
}

aitEnum smsInt32PV::bestExternalType(void) const
{
	return aitEnumInt32;
}

gddAppFuncTableStatus smsInt32PV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

caStatus smsInt32PV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitInt32 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsInt32PV::read() m_pv_name=" << m_pv_name << " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsInt32PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitInt32 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar()) {
		DEBUG("smsInt32PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsInt32PV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
	m_value->get(cur);
	if (v == cur)
		return S_casApp_success;

	val.getTimeStamp(&ts);
	update(v, &ts);
	changed();

	return S_casApp_success;
}

bool smsInt32PV::allowUpdate(const gdd &)
{
	return true;
}

void smsInt32PV::update(int32_t val, struct timespec *ts)
{
	aitInt32 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumInt32);
	nval->put(val);
	nval->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

bool smsInt32PV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

int32_t smsInt32PV::value(void)
{
	aitInt32 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsInt32PV::changed(void)
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

caStatus smsTriggerPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsTriggerPV::read() m_pv_name=" << m_pv_name
		<< " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsTriggerPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v);

	if (!val.isScalar()) {
		DEBUG("smsTriggerPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsTriggerPV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
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

/* ----------------------------------------------------------------------- */

smsFloat64PV::smsFloat64PV(const std::string &name) : smsPV(name)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumFloat64);
	m_value->setTimeStamp(&ts);
	m_value->put(0.0);

	m_pv_name = name;
}

aitEnum smsFloat64PV::bestExternalType(void) const
{
	return aitEnumFloat64;
}

gddAppFuncTableStatus smsFloat64PV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

caStatus smsFloat64PV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitFloat64 uninitialized_var(v);
	m_value->get(v);
	DEBUG("smsFloat64PV::read() m_pv_name=" << m_pv_name << " value=" << v);
	return m_read_table.read(*this, prototype);
}

caStatus smsFloat64PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitFloat64 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;
	struct timespec ts;

	if (!val.isScalar()) {
		DEBUG("smsFloat64PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);
	DEBUG("smsFloat64PV::write() m_pv_name=" << m_pv_name
		<< " value=" << v);
	m_value->get(cur);
	if (v == cur)
		return S_casApp_success;

	val.getTimeStamp(&ts);
	update(v, &ts);
	changed();

	return S_casApp_success;
}

bool smsFloat64PV::allowUpdate(const gdd &)
{
	return true;
}

void smsFloat64PV::update(double val, struct timespec *ts)
{
	aitFloat64 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val)
		return;

	nval = new gddScalar(gddAppType_value, aitEnumFloat64);
	nval->put(val);
	nval->setTimeStamp(ts);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
}

bool smsFloat64PV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

double smsFloat64PV::value(void)
{
	aitFloat64 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsFloat64PV::changed(void)
{
}

