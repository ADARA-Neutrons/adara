
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.SMSControlPV"));

#include <sstream>

#include <stdint.h>
#include <string.h>

#include <gddApps.h>

#include "EPICS.h"
#include "DataSource.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "ADARAUtils.h"

RateLimitedLogging::History RLLHistory_SMSControlPV;

// Rate-Limited Logging IDs...
#define RLL_CONNPV_CONNECTED          0
#define RLL_CONNPV_DISCONNECTED       1
#define RLL_CONNPV_FAILED             2
#define RLL_CONNPV_TRYING             3
#define RLL_CONNPV_WAITING            4
#define RLL_PTPV_IGNORE               5
#define RLL_PTPV_PASSTHRU             6
#define RLL_PTPV_EXECUTE              7
#define RLL_PV_READ                   8
#define RLL_PV_WRITE                  9

/* gcc 4.4.6 on RHEL 6 cannot figure out that gdd::get(T &) will actually
 * initiallize the variable, so it warns. This conflicts with a clean build
 * using -Werror, but we can quiet the compiler easily.
 */
#define uninitialized_var(x) x = 0

/* -------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------- */

static gddAppFuncTableStatus getPassThruEnums(gdd &in)
{
	aitFixedString *str;
	fixedStringDestructor *des;

	str = new aitFixedString[3];
	if (!str)
		return S_casApp_noMemory;

	des = new fixedStringDestructor;
	if (!des) {
		delete [] str;
		return S_casApp_noMemory;
	}
	strncpy(str[0].fixed_string, "Ignore", sizeof(str[0].fixed_string));
	strncpy(str[1].fixed_string, "PassThru", sizeof(str[1].fixed_string));
	strncpy(str[2].fixed_string, "Execute", sizeof(str[2].fixed_string));

	in.setDimension(1);
	in.setBound(0, 0, 3);
	in.putRef(str, des);

	return S_cas_success;
}

/* -------------------------------------------------------------------- */

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
	m_read_table.installReadFunc("controlHigh", &smsPV::maximumNumber);
	m_read_table.installReadFunc("controlLow", &smsPV::minimumNumber);
	m_read_table.installReadFunc("graphicHigh", &smsPV::maximumNumber);
	m_read_table.installReadFunc("graphicLow", &smsPV::minimumNumber);
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

void smsPV::timestamp(struct timespec &ts)
{
    m_value->getTimeStamp(&ts); // Wallclock Time...!
}

void smsPV::destroy(void)
{
	/* PVs are pre-allocated; SMControl will clean us up */
}

void smsPV::changed(void)
{
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

gddAppFuncTableStatus smsPV::minimumNumber(gdd &in)
{
	in.put(-1.7976931348623157E308);
	return S_cas_success;
}

gddAppFuncTableStatus smsPV::maximumNumber(gdd &in)
{
	in.put(1.7976931348623157E308);
	return S_cas_success;
}

/* -------------------------------------------------------------------- */

smsReadOnlyChannel::smsReadOnlyChannel(const casCtx &cas) : casChannel(cas)
{
}

bool smsReadOnlyChannel::writeAccess(void) const
{
	return false;
}

/* -------------------------------------------------------------------- */

smsRunNumberPV::smsRunNumberPV(const std::string &prefix)
{
	/* Default to not recording on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumUint32);
	m_value->put(0);

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	m_value->setTimeStamp(&ts); // Wallclock Time...!

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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsRunNumberPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

void smsRunNumberPV::update(uint32_t run, struct timespec *ts)
{
	aitUint32 uninitialized_var(v);
	gdd *val;

	m_value->get(v);

	// No Change
	if (v == run) {
		DEBUG("smsRunNumberPV::update() m_pv_name=" << m_pv_name
			<< " Value Did Not Change - Ignore..."
			<< " Still Update ts="
			<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(ts); // Wallclock Time...!
		notify();
		return;
	}

	val = new gddScalar(gddAppType_value, aitEnumUint32);
	val->put(run);
	val->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = val;

	notify();
	changed();
}

void smsRunNumberPV::changed(void)
{
}

/* -------------------------------------------------------------------- */

smsRecordingPV::smsRecordingPV(const std::string &prefix,
	SMSControl *ctrl) :
		m_ctrl(ctrl)
{
	/* Default to not recording on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->put(0);

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	m_value->setTimeStamp(&ts); // Wallclock Time...!

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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsRecordingPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

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

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsRecordingPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	if (v > 1) {
		ERROR("smsRecordingPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 1 - Ignoring...");
		return S_casApp_noSupport;
	}

	if (m_ctrl->setRecording(v, &ts)) {  // Wallclock Time...!
		// Note: SMSControl::setRecording() calls
		// smsRecordingPV::update() on success,
		// which handles the notify() and changed()... :-D
		return S_casApp_success;
	}

	/* don't cause disruption at the CA level just because the run 
	 * couldn't start
	 */
	notify();
	changed();
	return S_casApp_success;
}

void smsRecordingPV::update(bool recording, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *val;

	m_value->get(v);

	// No Change
	if (v == recording) {
		DEBUG("smsRecordingPV::update() m_pv_name=" << m_pv_name
			<< " Value Did Not Change - Ignore..."
			<< " Still Update ts="
			<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(ts); // Wallclock Time...!
		notify();
		return;
	}

	val = new gddScalar(gddAppType_value, aitEnumEnum16);
	val->put(recording);
	val->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = val;

	notify();
	changed();
}

void smsRecordingPV::changed(void)
{
}

/* -------------------------------------------------------------------- */

smsStringPV::smsStringPV(const std::string &name, bool auto_save)
		: smsPV(name), m_first_set(true), m_auto_save(auto_save)
{
	unset( true );
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
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		char *str = (char *) m_value->dataPointer();
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsStringPV::read() m_pv_name=" << m_pv_name
			<< " value=[" << str << "]"
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}
	return m_read_table.read(*this, prototype);
}

caStatus smsStringPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	unsigned int start, nelem;

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	/* caput sends a null string as a scalar, so interpret that
	 * as an unset request.
	 */
	if (val.isScalar() && val.primitiveType() == aitEnumUint8) {
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
				RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
			DEBUG(log_info
				<< "smsStringPV::write() m_pv_name=" << m_pv_name
				<< " Null String, Unset Value"
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		unset(false, &ts);
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
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
				RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
			DEBUG(log_info
				<< "smsStringPV::write() m_pv_name=" << m_pv_name
				<< " Writing No Elements, Unset Value"
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		unset(false, &ts);
		return S_cas_success;
	}

	if (!allowUpdate(val)) {
		/* We don't want to update the PV at this time; still
		 * send a notification to any watchers, and just return
		 * success.
		 */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
				RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
			DEBUG(log_info
				<< "smsStringPV::write() m_pv_name=" << m_pv_name
				<< " Updates Not Allowed, Ignore Value."
				<< " Don't Update ts="
				<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		notify();
		return S_casApp_success;
	}

	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, val.dataPointer(), nelem);

	char *old_str = (char *) m_value->dataPointer();

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsStringPV::write() m_pv_name=" << m_pv_name
			<< " new_value=[" << new_str << "]"
			<< " old_value=[" << old_str << "]"
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	if ( !strcmp(new_str, old_str)
			&& m_value->getStat() == epicsAlarmNone ) {
		if ( m_first_set ) {
			DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
				<< " String Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			if ( do_log ) {
				DEBUG("smsStringPV::write() m_pv_name=" << m_pv_name
					<< " String Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts.tv_nsec);
			}
			// Still Update TimeStamp
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			notify();
		}
		return S_casApp_success;
	}

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
					MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);

	nv->setTimeStamp(&ts); // Wallclock Time...!
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	m_value = nv;

	m_first_set = false;

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
	if ( str.size() == 0 ) {
		DEBUG("smsStringPV::update() m_pv_name=" << m_pv_name
			<< " Setting to Empty String, Unset Value"
			<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		unset( false, ts );
		return;
	}

	/* We ensure we have room for MAX_LENGTH characters, plus a
	 * trailing nul to make live easier when converting to a std::string.
	 */
	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	memcpy(new_str, (void *)str.c_str(), str.length());

	char *old_str = (char *) m_value->dataPointer();

	if ( !strcmp(new_str, old_str)
			&& m_value->getStat() == epicsAlarmNone ) {
		if ( m_first_set ) {
			DEBUG("smsStringPV::update() m_pv_name=" << m_pv_name
				<< " String Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				DEBUG("smsStringPV::update() m_pv_name=" << m_pv_name
					<< " String Value Did Not Change, But Time Earlier"
					<< " - Likely AutoSave Recovery, Call changed()..."
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				changed();
			}
			else {
				DEBUG("smsStringPV::update() m_pv_name=" << m_pv_name
					<< " String Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	gddAtomic *nv = new gddAtomic(gddAppType_value, aitEnumUint8, 1,
					MAX_LENGTH);
	nv->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	nv->setTimeStamp(ts); // Wallclock Time...!
	nv->setStat(epicsAlarmNone);
	nv->setSevr(epicsSevNone);

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nv;

	m_first_set = false;

	notify();
	changed();
}

void smsStringPV::unset(bool init, struct timespec *ts)
{
	// Get Wallclock TimeStamp If One Not Provided...
	struct timespec new_ts;
	if ( ts != NULL ) {
		new_ts = *ts;
	}
	else {
		clock_gettime(CLOCK_REALTIME, &new_ts);
	}

	if ( m_value.valid() && m_value->getStat() == epicsAlarmUDF ) {
		char *str = (char *) m_value->dataPointer();
		DEBUG("smsStringPV::unset() m_pv_name=" << m_pv_name
			<< " String Value Already Unset/Invalid - Ignore..."
			<< " Keep value=[" << str << "]"
			<< " Still Update ts="
			<< new_ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< new_ts.tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(&new_ts); // Wallclock Time...!
		notify();
		return;
	}

	char *new_str = new char[MAX_LENGTH+1];
	memset(new_str, 0, MAX_LENGTH+1);
	strcpy(new_str, "(unset)");

	if ( !init )
	{
		char *old_str = (char *) m_value->dataPointer();

		if ( !strcmp(new_str, old_str)
				&& m_value->getStat() == epicsAlarmNone ) {
			if ( m_first_set ) {
				DEBUG("smsStringPV::unset() m_pv_name=" << m_pv_name
					<< " String Value Did Not Change, But First Setting"
					<< " - Call changed()..."
					<< " ts=" << new_ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< new_ts.tv_nsec);
				m_value->setTimeStamp(&new_ts); // Wallclock Time...!
				m_first_set = false;
				notify();
				changed();
			}
			else {
				struct timespec old_ts;
				m_value->getTimeStamp(&old_ts); // Wallclock Time...!
				if ( compareTimeStamps( new_ts, old_ts ) < 0 ) {
					DEBUG("smsStringPV::unset() m_pv_name=" << m_pv_name
						<< " String Value Did Not Change, But Time Earlier"
						<< " - Likely AutoSave Recovery,"
						<< " Call changed()..."
						<< " ts=" << new_ts.tv_sec
							- ADARA::EPICS_EPOCH_OFFSET
						<< "." << std::setfill('0') << std::setw(9)
						<< new_ts.tv_nsec);
					m_value->setTimeStamp(&new_ts); // Wallclock Time...!
					notify();
					changed();
				}
				else {
					DEBUG("smsStringPV::unset() m_pv_name=" << m_pv_name
						<< " String Value Did Not Change - Ignore..."
						<< " Still Update ts="
						<< new_ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
						<< "." << std::setfill('0') << std::setw(9)
						<< new_ts.tv_nsec);
					// Still Update TimeStamp
					m_value->setTimeStamp(&new_ts); // Wallclock Time...!
					notify();
				}
			}
			return;
		}
		else {
			DEBUG("smsStringPV::unset() m_pv_name=" << m_pv_name
				<< " Unsetting Valid String Value"
				<< " new_value=[" << new_str << "]"
				<< " old_value=[" << old_str << "]"
				<< " ts=" << new_ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< new_ts.tv_nsec);
		}
	}
	else {
		DEBUG("smsStringPV::unset() Init m_pv_name=" << m_pv_name
			<< " to value=[" << new_str << "]"
			<< " ts=" << new_ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< new_ts.tv_nsec);
	}

	m_value = new gddAtomic(gddAppType_value, aitEnumUint8, 1, MAX_LENGTH);
	m_value->putRef((const aitUint8 *) new_str, new charArrayDestructor);
	m_value->setStat(epicsAlarmUDF);
	m_value->setSevr(epicsSevNone);
	m_value->setTimeStamp(&new_ts); // Wallclock Time...!

	unsigned int start, nelem;
	m_value->getBound(0, start, nelem);

	if ( !init )
		m_first_set = false;

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
	if ( m_auto_save && !m_first_set )
	{
		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		StorageManager::autoSavePV( m_pv_name, value(), &ts );
	}
}

/* -------------------------------------------------------------------- */

smsBooleanPV::smsBooleanPV(const std::string &name,
		bool auto_save, bool no_changed_on_update)
	: smsPV(name), m_first_set(true), m_auto_save(auto_save),
	m_no_changed_on_update(no_changed_on_update)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsBooleanPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsBooleanPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		ERROR("smsBooleanPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsBooleanPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	if (v > 1) {
		ERROR("smsBooleanPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 1 - Ignoring...");
		return S_casApp_noSupport;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( m_first_set ) {
			DEBUG("smsBooleanPV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			if ( do_log ) {
				DEBUG("smsBooleanPV::write() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts.tv_nsec);
			}
			// Still Update TimeStamp
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			notify();
		}
		return S_casApp_success;
	}

	update(v, &ts, true /* force_changed */);

	return S_casApp_success;
}

bool smsBooleanPV::allowUpdate(const gdd &)
{
	return true;
}

void smsBooleanPV::update(bool val, struct timespec *ts,
		bool force_changed)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val) {
		if ( m_first_set ) {
			DEBUG("smsBooleanPV::update() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting - "
				<< ( ( !m_no_changed_on_update || force_changed )
					? "" : "*Don't* " )
				<< "Call changed()..."
				<< " (m_no_changed_on_update=" << m_no_changed_on_update
				<< " force_changed=" << force_changed << ")"
				<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			if ( !m_no_changed_on_update || force_changed )
				changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				DEBUG("smsBooleanPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change, But Time Earlier"
					<< " - Likely AutoSave Recovery, "
					<< ( ( !m_no_changed_on_update || force_changed )
						? "" : "*Don't* " )
					<< "Call changed()..."
					<< " (m_no_changed_on_update="
						<< m_no_changed_on_update
					<< " force_changed=" << force_changed << ")"
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				if ( !m_no_changed_on_update || force_changed )
					changed();
			}
			else {
				DEBUG("smsBooleanPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	m_first_set = false;

	notify();

	// _Conditionally_ Call "changed()" in "update()" for smsBooleanPV...
	//    - needed _Not_ to call for "MarkerPausedPV" in SMS Markers Class
	if ( !m_no_changed_on_update || force_changed )
		changed();
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
	if ( m_auto_save && !m_first_set )
	{
		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		bool bval = value();
		// Use String Representation of Boolean for AutoSave File... :-D
		std::string bvalstr = ( bval ) ? "true" : "false";
		StorageManager::autoSavePV( m_pv_name, bvalstr, &ts );
	}
}

/* -------------------------------------------------------------------- */

smsEnabledPV::smsEnabledPV(const std::string &name,
		DataSource *dataSource, bool auto_save) :
	smsBooleanPV(name, auto_save), m_dataSource(dataSource)
{
}

gddAppFuncTableStatus smsEnabledPV::getEnums(gdd &in)
{
	return getEnabledEnums(in);
}

aitEnum smsEnabledPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

void smsEnabledPV::update(bool val, struct timespec *ts,
		bool UNUSED(force_changed))
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val) {
		if ( m_first_set ) {
			DEBUG("smsEnabledPV::update() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				DEBUG("smsEnabledPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change, But Time Earlier"
					<< " - Likely AutoSave Recovery, Call changed()..."
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				changed();
			}
			else {
				DEBUG("smsEnabledPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

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

	m_first_set = false;

	notify();
	changed();
}

/* -------------------------------------------------------------------- */

smsErrorPV::smsErrorPV(const std::string &name)
	: smsPV(name)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
}

aitEnum smsErrorPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsErrorPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsErrorPV::getEnums(gdd &in)
{
	return getErrorEnums(in);
}

caStatus smsErrorPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsErrorPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsErrorPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		ERROR("smsErrorPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsErrorPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	if (v > 1) {
		ERROR("smsErrorPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 1 - Ignoring...");
		return S_casApp_noSupport;
	}

	if (!allowUpdate(val)) {
		/* We don't want to update the PV at this time; still
		 * send a notification to any watchers, and just return
		 * success.
		 */
		std::string log_info;
		if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
				RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
			DEBUG(log_info
				<< "smsErrorPV::write() m_pv_name=" << m_pv_name
				<< " Updates Not Allowed, Ignore Value."
				<< " Don't Update ts="
				<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		notify();
		return S_casApp_success;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( do_log ) {
			DEBUG("smsErrorPV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change - Ignore..."
				<< " Still Update ts="
				<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		// Still Update TimeStamp
		m_value->setTimeStamp(&ts); // Wallclock Time...!
		notify();
		return S_casApp_success;
	}

	update(v, val.getSevr(), &ts);

	return S_casApp_success;
}

// No External Writes Allowed...!
bool smsErrorPV::allowUpdate(const gdd &)
{
	return false;
}

void smsErrorPV::set() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(1, true /* major */, &ts);
}

void smsErrorPV::reset() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(0, false /* major */, &ts);
}

void smsErrorPV::update(bool val, bool major, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val) {
		DEBUG("smsErrorPV::update() m_pv_name=" << m_pv_name
			<< " Value Did Not Change - Ignore..."
			<< " Still Update ts="
			<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(ts); // Wallclock Time...!
		notify();
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	if (val != 0) {
		nval->setStat(epicsAlarmState);
		if (major)
			nval->setSevr(epicsSevMajor);
		else
			nval->setSevr(epicsSevMinor);
	}

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
	changed();
}

bool smsErrorPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

bool smsErrorPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsErrorPV::changed(void)
{
}

/* -------------------------------------------------------------------- */

smsConnectedPV::smsConnectedPV(const std::string &name) : smsPV(name)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsConnectedPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsConnectedPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		ERROR("smsConnectedPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	bool do_log = false;
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsConnectedPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	if (v > 4) {
		ERROR("smsConnectedPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 4 - Ignoring...");
		return S_casApp_noSupport;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( do_log ) {
			DEBUG("smsConnectedPV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change - Ignore..."
				<< " Still Update ts="
				<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		// Still Update TimeStamp
		m_value->setTimeStamp(&ts); // Wallclock Time...!
		notify();
		return S_casApp_success;
	}

	update(v, &ts);

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
	if (v == val) {
		DEBUG("smsConnectedPV::update() m_pv_name=" << m_pv_name
			<< " Value Did Not Change - Ignore..."
			<< " Still Update ts="
			<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(ts); // Wallclock Time...!
		notify();
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

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
	changed();
}

void smsConnectedPV::connected() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(0, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_CONNPV_CONNECTED, m_pv_name,
			60, 3, 10, log_info ) ) {
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
			60, 3, 10, log_info ) ) {
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
			60, 3, 10, log_info ) ) {
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
			60, 3, 10, log_info ) ) {
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
			60, 3, 10, log_info ) ) {
		DEBUG(log_info
			<< "smsConnectedPV::waiting_for_connect_ack() m_pv_name="
			<< m_pv_name << " (4)");
	}
}

bool smsConnectedPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

uint32_t smsConnectedPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsConnectedPV::changed(void)
{
}

/* -------------------------------------------------------------------- */

smsPassThruPV::smsPassThruPV(const std::string &name, bool auto_save)
		: smsPV(name), m_first_set(true), m_auto_save(auto_save)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	/* Default all booleans (and derivatives) to false on startup */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
}

aitEnum smsPassThruPV::bestExternalType(void) const
{
	return aitEnumEnum16;
}

gddAppFuncTableStatus smsPassThruPV::getValue(gdd &in)
{
	if (gddApplicationTypeTable::app_table.smartCopy(&in, m_value.get()))
		return S_cas_noConvert;

	return S_cas_success;
}

gddAppFuncTableStatus smsPassThruPV::getEnums(gdd &in)
{
	return getPassThruEnums(in);
}

caStatus smsPassThruPV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint16 uninitialized_var(v);
	m_value->get(v);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsPassThruPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsPassThruPV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint16 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		ERROR("smsPassThruPV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsPassThruPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	if (v > 2) {
		ERROR("smsPassThruPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 2 - Ignoring...");
		return S_casApp_noSupport;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( m_first_set ) {
			DEBUG("smsPassThruPV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			if ( do_log ) {
				DEBUG("smsPassThruPV::write() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts.tv_nsec);
			}
			// Still Update TimeStamp
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			notify();
		}
		return S_casApp_success;
	}

	update(v, &ts);

	return S_casApp_success;
}

bool smsPassThruPV::allowUpdate(const gdd &)
{
	return true;
}

void smsPassThruPV::update(uint16_t val, struct timespec *ts)
{
	aitUint16 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val) {
		if ( m_first_set ) {
			DEBUG("smsPassThruPV::update() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				DEBUG("smsPassThruPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change, But Time Earlier"
					<< " - Likely AutoSave Recovery, Call changed()..."
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				changed();
			}
			else {
				DEBUG("smsPassThruPV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
						<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
						<< "." << std::setfill('0') << std::setw(9)
						<< ts->tv_nsec);
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumEnum16);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	m_first_set = false;

	notify();
	changed();
}

void smsPassThruPV::ignore() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(0, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PTPV_IGNORE, m_pv_name,
			60, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsPassThruPV::ignore() m_pv_name="
			<< m_pv_name << " (0)");
	}
}

void smsPassThruPV::passthru() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(1, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PTPV_PASSTHRU, m_pv_name,
			60, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsPassThruPV::passthru() m_pv_name="
			<< m_pv_name << " (1)");
	}
}

void smsPassThruPV::execute() {

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	update(2, &ts);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PTPV_EXECUTE, m_pv_name,
			60, 3, 10, log_info ) ) {
		DEBUG(log_info << "smsPassThruPV::execute() m_pv_name="
			<< m_pv_name << " (2)");
	}
}

bool smsPassThruPV::valid(void)
{
	return m_value.valid() && m_value->getStat() != epicsAlarmUDF;
}

uint32_t smsPassThruPV::value(void)
{
	aitUint16 v = 0;
	if (m_value.valid())
		m_value->get(v);
	return v;
}

void smsPassThruPV::changed(void)
{
	if ( m_auto_save && !m_first_set )
	{
		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		std::stringstream ss;
		ss << value();
		StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
	}
}

/* -------------------------------------------------------------------- */

smsUint32PV::smsUint32PV(const std::string &name,
		uint32_t min, uint32_t max, bool auto_save) :
	smsPV(name), m_first_set(true), m_auto_save(auto_save)
{
	// Apply Min and Max Limits
	m_min = min;
	m_max = max;

	initReadTable();

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumUint32);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
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

gddAppFuncTableStatus smsUint32PV::getEnums(gdd &)
{
	return S_gddAppFuncTable_badType;
}

void smsUint32PV::initReadTable(void)
{
	m_read_table.installReadFunc("value", &smsUint32PV::getValue);
	m_read_table.installReadFunc("enums", &smsUint32PV::getEnums);

	/* These are not currently used by any child classes */
	/* However, we are applying defaults so that clients requesting
	 * DBR_CTRL types won't complain so much.
	 */
	m_read_table.installReadFunc("alarmHigh",
		&smsUint32PV::defaultNumber);
	m_read_table.installReadFunc("alarmLow",
		&smsUint32PV::defaultNumber);
	m_read_table.installReadFunc("alarmHighWarning",
		&smsUint32PV::defaultNumber);
	m_read_table.installReadFunc("alarmLowWarning",
		&smsUint32PV::defaultNumber);
	m_read_table.installReadFunc("controlHigh",
		&smsUint32PV::maximumNumber);
	m_read_table.installReadFunc("controlLow",
		&smsUint32PV::minimumNumber);
	m_read_table.installReadFunc("graphicHigh",
		&smsUint32PV::maximumNumber);
	m_read_table.installReadFunc("graphicLow",
		&smsUint32PV::minimumNumber);
	m_read_table.installReadFunc("precision",
		&smsUint32PV::defaultNumber);
	m_read_table.installReadFunc("units",
		&smsUint32PV::defaultString);
}

gddAppFuncTableStatus smsUint32PV::defaultNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumUint32);
	val->put(0);
	in.put(val);
	return S_cas_success;
}

gddAppFuncTableStatus smsUint32PV::defaultString(gdd &in)
{
	in.put("");
	return S_cas_success;
}

gddAppFuncTableStatus smsUint32PV::minimumNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumUint32);
	val->put(m_min);
	in.put(val);
	return S_cas_success;
}

gddAppFuncTableStatus smsUint32PV::maximumNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumUint32);
	val->put(m_max);
	in.put(val);
	return S_cas_success;
}

caStatus smsUint32PV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitUint32 uninitialized_var(v);
	m_value->get(v);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsUint32PV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsUint32PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitUint32 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		DEBUG("smsUint32PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsUint32PV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( m_first_set ) {
			DEBUG("smsUint32PV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			if ( do_log ) {
				DEBUG("smsUint32PV::write() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts.tv_nsec);
			}
			// Still Update TimeStamp
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			notify();
		}
		return S_casApp_success;
	}

	update(v, &ts);

	return S_casApp_success;
}

bool smsUint32PV::allowUpdate(const gdd &)
{
	return true;
}

void smsUint32PV::update(uint32_t val, struct timespec *ts, bool no_log)
{
	aitUint32 uninitialized_var(v);
	gdd *nval;

	m_value->get(v);
	if (v == val) {
		if ( m_first_set ) {
			if ( !no_log ) {
				DEBUG("smsUint32PV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change, But First Setting"
					<< " - Call changed()..."
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
			}
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				if ( !no_log ) {
					DEBUG("smsUint32PV::update() m_pv_name=" << m_pv_name
						<< " Value Did Not Change, But Time Earlier"
						<< " - Likely AutoSave Recovery, Call changed()..."
						<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
						<< "." << std::setfill('0') << std::setw(9)
						<< ts->tv_nsec);
				}
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				changed();
			}
			else {
				if ( !no_log ) {
					DEBUG("smsUint32PV::update() m_pv_name=" << m_pv_name
						<< " Value Did Not Change - Ignore..."
						<< " Still Update ts="
						<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
						<< "." << std::setfill('0') << std::setw(9)
						<< ts->tv_nsec);
				}
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumUint32);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	m_first_set = false;

	notify();
	changed();
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
	if ( m_auto_save && !m_first_set )
	{
		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		std::stringstream ss;
		ss << value();
		StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
	}
}

/* -------------------------------------------------------------------- */

smsInt32PV::smsInt32PV(const std::string &name) : smsPV(name)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumInt32);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsInt32PV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsInt32PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitInt32 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		DEBUG("smsInt32PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsInt32PV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	m_value->get(cur);
	if (v == cur) {
		if ( do_log ) {
			DEBUG("smsInt32PV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change - Ignore..."
				<< " Still Update ts="
				<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
		}
		// Still Update TimeStamp
		m_value->setTimeStamp(&ts); // Wallclock Time...!
		notify();
		return S_casApp_success;
	}

	update(v, &ts);

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
	if (v == val) {
		DEBUG("smsInt32PV::update() m_pv_name=" << m_pv_name
			<< " Value Did Not Change - Ignore..."
			<< " Still Update ts="
			<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts->tv_nsec);
		// Still Update TimeStamp
		m_value->setTimeStamp(ts); // Wallclock Time...!
		notify();
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumInt32);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	notify();
	changed();
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

/* -------------------------------------------------------------------- */

smsTriggerPV::smsTriggerPV(const std::string &name)
{
	m_pv_name = name;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	/* We'll stay false except for briefly when someone sets us true */
	m_value = new gddScalar(gddAppType_value, aitEnumEnum16);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0);
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

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsTriggerPV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

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

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsTriggerPV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	if (v > 1) {
		ERROR("smsTriggerPV::write() m_pv_name=" << m_pv_name
			<< " Value is Out of Range!"
			<< " value=" << v << " > 1 - Ignoring...");
		return S_casApp_noSupport;
	}

	// Always Set smsTriggerPV Timestamp for Latest Trigger...! ;-D
	m_value->setTimeStamp(&ts); // Wallclock Time...!

	if (v) {
		triggered(&ts);

		if (m_interested) {
			caServer *cas = getCAS();
			casEventMask mask = cas->valueEventMask() |
						cas->logEventMask();
			smartGDDPointer edge;

			edge = new gddScalar(gddAppType_value, aitEnumEnum16);
			edge->setTimeStamp(&ts); // Wallclock Time...!
			edge->put(1);
			postEvent(mask, *edge);

			ts.tv_nsec++;
			if (ts.tv_nsec >= NANO_PER_SECOND_LL) {
				ts.tv_sec++;
				ts.tv_nsec -= NANO_PER_SECOND_LL;
			}
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			postEvent(mask, *m_value);
		}

		changed();
	}

	return S_casApp_success;
}

void smsTriggerPV::changed(void)
{
}

/* -------------------------------------------------------------------- */

smsFloat64PV::smsFloat64PV(const std::string &name,
		double min, double max, double epsilon, bool auto_save)
	: smsPV(name), m_min(min), m_max(max), m_epsilon(epsilon),
		m_first_set(true), m_auto_save(auto_save)
{
	initReadTable();

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	m_value = new gddScalar(gddAppType_value, aitEnumFloat64);
	m_value->setTimeStamp(&ts); // Wallclock Time...!
	m_value->put(0.0);
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

gddAppFuncTableStatus smsFloat64PV::getEnums(gdd &)
{
	return S_gddAppFuncTable_badType;
}

void smsFloat64PV::initReadTable(void)
{
	m_read_table.installReadFunc("value", &smsFloat64PV::getValue);
	m_read_table.installReadFunc("enums", &smsFloat64PV::getEnums);

	/* These are not currently used by any child classes */
	/* However, we are applying defaults so that clients requesting
	 * DBR_CTRL types won't complain so much.
	 */
	m_read_table.installReadFunc("alarmHigh",
		&smsFloat64PV::defaultNumber);
	m_read_table.installReadFunc("alarmLow",
		&smsFloat64PV::defaultNumber);
	m_read_table.installReadFunc("alarmHighWarning",
		&smsFloat64PV::defaultNumber);
	m_read_table.installReadFunc("alarmLowWarning",
		&smsFloat64PV::defaultNumber);
	m_read_table.installReadFunc("controlHigh",
		&smsFloat64PV::maximumNumber);
	m_read_table.installReadFunc("controlLow",
		&smsFloat64PV::minimumNumber);
	m_read_table.installReadFunc("graphicHigh",
		&smsFloat64PV::maximumNumber);
	m_read_table.installReadFunc("graphicLow",
		&smsFloat64PV::minimumNumber);
	m_read_table.installReadFunc("precision",
		&smsFloat64PV::defaultPrecision);
	m_read_table.installReadFunc("units",
		&smsFloat64PV::defaultString);
}

gddAppFuncTableStatus smsFloat64PV::defaultNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumFloat64);
	val->put(0.0);
	in.put(val);
	return S_cas_success;
}

gddAppFuncTableStatus smsFloat64PV::defaultPrecision(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumFloat64);
	val->put(9.0);
	in.put(val);
	return S_cas_success;
}

gddAppFuncTableStatus smsFloat64PV::defaultString(gdd &in)
{
	in.put("");
	return S_cas_success;
}

gddAppFuncTableStatus smsFloat64PV::minimumNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumFloat64);
	val->put(m_min);
	in.put(val);
	return S_cas_success;
}

gddAppFuncTableStatus smsFloat64PV::maximumNumber(gdd &in)
{
	gdd *val = new gddScalar(gddAppType_value, aitEnumFloat64);
	val->put(m_max);
	in.put(val);
	return S_cas_success;
}

caStatus smsFloat64PV::read(const casCtx &UNUSED(ctx), gdd &prototype)
{
	aitFloat64 uninitialized_var(v);
	m_value->get(v);

	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_READ, m_pv_name, 60, 5, 60, log_info ) ) {
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		DEBUG(log_info << "smsFloat64PV::read() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
	}

	return m_read_table.read(*this, prototype);
}

caStatus smsFloat64PV::write(const casCtx &UNUSED(ctx), const gdd &val)
{
	aitFloat64 uninitialized_var(v), uninitialized_var(cur);
	smartGDDPointer nval;

	if (!val.isScalar()) {
		DEBUG("smsFloat64PV::write() m_pv_name=" << m_pv_name
			<< " Value is Not a Scalar!");
		return S_casApp_noSupport;
	}

	val.get(v);

	struct timespec ts;
	val.getTimeStamp(&ts); // Wallclock Time...!

	std::string log_info;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_SMSControlPV,
			RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
		DEBUG(log_info << "smsFloat64PV::write() m_pv_name=" << m_pv_name
			<< " value=" << v
			<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
			<< "." << std::setfill('0') << std::setw(9)
			<< ts.tv_nsec);
		do_log = true;
	}

	m_value->get(cur);
	if ( approximatelyEqual( v, cur, m_epsilon ) ) {
		if ( m_first_set ) {
			DEBUG("smsFloat64PV::write() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts.tv_nsec);
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			if ( do_log ) {
				DEBUG("smsFloat64PV::write() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts.tv_nsec);
			}
			// Still Update TimeStamp
			m_value->setTimeStamp(&ts); // Wallclock Time...!
			notify();
		}
		return S_casApp_success;
	}

	update(v, &ts);

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
	if ( approximatelyEqual( v, val, m_epsilon ) ) {
		if ( m_first_set ) {
			DEBUG("smsFloat64PV::update() m_pv_name=" << m_pv_name
				<< " Value Did Not Change, But First Setting"
				<< " - Call changed()..."
				<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
				<< "." << std::setfill('0') << std::setw(9)
				<< ts->tv_nsec);
			m_value->setTimeStamp(ts); // Wallclock Time...!
			m_first_set = false;
			notify();
			changed();
		}
		else {
			struct timespec old_ts;
			m_value->getTimeStamp(&old_ts); // Wallclock Time...!
			if ( compareTimeStamps( *ts, old_ts ) < 0 ) {
				DEBUG("smsFloat64PV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change, But Time Earlier"
					<< " - Likely AutoSave Recovery, Call changed()..."
					<< " ts=" << ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
				changed();
			}
			else {
				DEBUG("smsFloat64PV::update() m_pv_name=" << m_pv_name
					<< " Value Did Not Change - Ignore..."
					<< " Still Update ts="
					<< ts->tv_sec - ADARA::EPICS_EPOCH_OFFSET
					<< "." << std::setfill('0') << std::setw(9)
					<< ts->tv_nsec);
				// Still Update TimeStamp
				m_value->setTimeStamp(ts); // Wallclock Time...!
				notify();
			}
		}
		return;
	}

	nval = new gddScalar(gddAppType_value, aitEnumFloat64);
	nval->put(val);
	nval->setTimeStamp(ts); // Wallclock Time...!

	/* This does the unref/ref for us, so each event posted will
	 * get its own copy of the value at that time.
	 */
	m_value = nval;

	m_first_set = false;

	notify();
	changed();
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
	if ( m_auto_save && !m_first_set )
	{
		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts); // Wallclock Time...!
		std::stringstream ss;
		ss << std::setprecision(17) << value();
		StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
	}
}

