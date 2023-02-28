
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("FastMeta"));

#include <fstream>
#include <string>
#include <sstream>

#include <stdint.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>

#include "ADARA.h"
#include "FastMeta.h"
#include "MetaDataMgr.h"
#include "StorageManager.h"

using namespace boost::property_tree;

void FastMeta::addDevices(const ptree &conf)
{
	std::string name, prefix("fastmeta ");
	size_t b, e, plen = prefix.length();
	ptree::const_iterator it;

	for (it = conf.begin(); it != conf.end(); ++it) {
		if (it->first.compare(0, plen, prefix))
			continue;

		b = it->first.find_first_of('\"', plen);
		if (b != std::string::npos)
			e = it->first.find_first_of('\"', b + 1);
		else
			e = std::string::npos;

		if (b == std::string::npos || e == std::string::npos) {
			std::string msg("Invalid fastmeta section name '");
			msg += it->first;
			msg += "'";
			throw std::runtime_error(msg);
		}

		name = it->first.substr(b + 1, e - b - 1);

		if (it->second.count("disabled")) {
			INFO("Ignoring disabled fastmeta '" << name << "'");
			continue;
		}

		DEBUG("addDevices(): Found Fast Meta-Data Device"
			<< " [" << name << "]");
		addDevice(name, it->second);
	}
}

static void readFile(const std::string &name, const std::string &path,
		std::stringstream &out)
{
	std::ifstream f(path.c_str());
	if (f.fail()) {
		std::string msg("fastmeta '");
		msg += name;
		msg += "' is unable to open path '";
		msg += path;
		msg += "'";
		throw std::runtime_error(msg);
	}

	out << f.rdbuf();
	if (f.fail()) {
		std::string msg("fastmeta '");
		msg += name;
		msg += "' is unable to read path '";
		msg += path;
		msg += "'";
		throw std::runtime_error(msg);
	}
}

static void parseEntry(const std::string &name, const std::string &var,
		const std::string &val, uint32_t &varId, uint32_t &key,
		bool &persist)
{
	/* Build the common error string */
	std::string msg("fastmeta '");
	msg += name;
	msg += "' key '";
	msg += var;
	msg += "'";

	try {
		varId = boost::lexical_cast<uint32_t>(var);
	} catch (...) {
		msg += " does not convert to a number";
		throw std::runtime_error(msg);
	}

	std::vector<std::string> tokens;
	boost::split(tokens, val, boost::is_any_of(" \t"));
	std::vector<std::string>::iterator arg = tokens.begin();

	if (arg == tokens.end()) {
		msg += " has no data";
		throw std::runtime_error(msg);
	}

	/* What type of fast metadata are we? */
	if (boost::algorithm::iequals(*arg, "trigger")) {
		key = 0x50000000;
	} else if (boost::algorithm::iequals(*arg, "adc")) {
		key = 0x60000000;
	} else {
		msg += " has invalid type specifier '";
		msg += *arg;
		msg += "'";
		throw std::runtime_error(msg);
	}

	/* Which detector device are we? */
	if (++arg == tokens.end()) {
		msg += " is missing detector device number";
		throw std::runtime_error(msg);
	}

	uint32_t dev;
	try {
		dev = boost::lexical_cast<uint32_t>(*arg);
	} catch (...) {
		msg += " has non-numeric device number";
		throw std::runtime_error(msg);
	}

	if (dev > ((1 << 12) - 1)) {
		msg += " has invalid device number";
		throw std::runtime_error(msg);
	}

	key |= dev << 16;

	/* Now for the optional 'persist' flag; it is OK if it doesn't exist,
	 * but if it does, it must be the last token.
	 */
	persist = false;
	if (++arg == tokens.end())
		return;

	if (boost::algorithm::iequals(*arg, "persist"))
		persist = true;
	else {
		msg += " has invalid flag '";
		msg += *arg;
		msg += "'";
		throw std::runtime_error(msg);
	}

	if (++arg != tokens.end()) {
		msg += " has extra tokens after persist";
		throw std::runtime_error(msg);
	}
}

void FastMeta::addDevice(const std::string &name,
			const ptree &info)
{
	ptree::const_assoc_iterator path;
	std::stringstream ddp;

	path = info.find("description");
	if (path == info.not_found()) {
		std::string msg("fastmeta '");
		msg += name;
		msg += "' is missing description";
		throw std::runtime_error(msg);
	}

	DEBUG("addDevice(): Reading Descriptor for Fast Meta-Data Device"
		<< " [" << name << "]"
		<< " at [" << path->second.data() << "]");
	readFile(name, path->second.data(), ddp);

	bool reconnected = false; // ignored for FastMeta devices...
	uint32_t devId = m_meta->allocDev(++m_numDevs,
		0 /* srcTag=0, for SMS Internal */, true /* do_log */, reconnected);
	uint32_t varId, key;
	bool persist;
	BOOST_FOREACH(const ptree::value_type &v, info) {
		if (!v.first.compare("description"))
			continue;

		/* Remove any trailing commend or whitespace. It'd be nice
		 * if boost handled this for us, but it doesn't. It does
		 * trim leading any whitespace, though.
		 */
		const std::string &cval = v.second.data();
		std::string val(cval, 0, cval.find_first_of(";#"));
		boost::trim_right(val);

		DEBUG("addDevice(): Parsing Variable Id for Fast Meta-Data Device"
			<< " [" << name << "]"
			<< " id=" << v.first
			<< " val=[" << val << "]");
		parseEntry(name, v.first, val, varId, key, persist);

		if (m_vars.count(key)) {
			std::string msg("fastmeta '");
			msg += name;
			msg += "' variable '";
			msg += v.first;
			msg += "' adds duplicate pixel ID";
			throw std::runtime_error(msg);
		}

		DEBUG("addDevice():"
			<< " Defining Device/Variable for Fast Meta-Data Device"
			<< " [" << name << "]"
			<< " devId=" << devId
			<< " varId=" << varId
			<< " key=0x" << std::hex << key << std::dec
			<< " persist=" << persist);
		m_vars[key].m_devId = devId;
		m_vars[key].m_varId = varId;
		m_vars[key].m_persist = persist;
	}

	/* Now that we know we can parse the variable map from the config,
	 * add the DDP to the stream. We'll carry it around even if we don't
	 * end up seeing the fast metadata, but we don't have to perform
	 * a mostly useless check for each event.
	 */
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_meta->addFastMetaDDP(now, devId, ddp.str());
}

void FastMeta::addGenericDevice(uint32_t pixel, uint32_t &key)
{
	std::stringstream devName;
	std::stringstream pvName;
	std::stringstream ddp;

	uint32_t devNum = pixel >> 16;

	devName << "GenericFastMetaDevice0x"
		<< std::hex << devNum << std::dec;

	std::string pvType;

	switch ( pixel >> 28 ) {
		case 5:
			pvName << "SignalTrigger0x"
				<< std::hex << devNum << std::dec;
			pvType = "Digital Trigger";
			break;
		case 6:
			pvName << "SignalAnalog0x"
				<< std::hex << devNum << std::dec;
			pvType = "Analog Signal";
			break;
		default:
			pvName << "SignalUnknown0x"
				<< std::hex << devNum << std::dec;
			pvType = "Unknown Signal";
			break;
	}

	// Only 1 PV Per Generic Fast Meta-Data Device... ;-D
	uint32_t varId = 1;

	ddp << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	ddp << "<device"
		<< " xmlns=\"http://public.sns.gov/schema/device.xsd\""
		<< " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
		<< " xsi:schemaLocation=\"http://public.sns.gov/schema/device.xsd"
		<< " http://public.sns.gov/schema/device.xsd\">\n";
	ddp << "<device_name>" << devName.str() << "</device_name>\n";
	ddp << "<process_variables>\n";
	ddp << "    <process_variable>\n";
	ddp << "        <pv_name>" << pvName.str() << "</pv_name>\n";
	ddp << "        <pv_id>1</pv_id>\n";
	ddp << "        <pv_description>"
		<< pvType << " for PixelId 0x"
		<< std::hex << devNum << std::dec
		<< "XXXX"
		<< "</pv_description>\n";
	ddp << "        <pv_type>integer</pv_type>\n";
	ddp << "    </process_variable>\n";
	ddp << "</process_variables>\n";
	ddp << "</device>\n";

	// Construct Key from Pixel ID...
	key = pixel & ~0xffff;

	// Map Next SMS Internal Device ID...
	bool reconnected = false; // ignored for FastMeta devices...
	uint32_t devId = m_meta->allocDev(++m_numDevs,
		0 /* srcTag=0, for SMS Internal */, true /* do_log */, reconnected);

	// Default to Non-Persistent PVs for Generic Fast Meta-Data Devices...
	bool persist = false;

	if (m_vars.count(key)) {
		std::string msg("fastmeta '");
		msg += devName.str();
		msg += "' variable '";
		msg += pvName.str();
		msg += "' adds duplicate pixel ID";
		throw std::runtime_error(msg);
	}

	DEBUG("addGenericDevice(): Creating New Descriptor for"
		<< " Generic Fast Meta-Data Device"
		<< " [" << devName.str() << "]"
		<< " PV=[" << pvName.str() << "]"
		<< " ddp=[" << ddp.str() << "]"
		<< " devId=" << devId
		<< " varId=" << varId
		<< " key=0x" << std::hex << key << std::dec
		<< " persist=" << persist);

	m_vars[key].m_devId = devId;
	m_vars[key].m_varId = varId;
	m_vars[key].m_persist = persist;

	/* Now that we know we can parse the variable map from the config,
	 * add the DDP to the stream. We'll carry it around even if we don't
	 * end up seeing the fast metadata, but we don't have to perform
	 * a mostly useless check for each event.
	 */
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_meta->addFastMetaDDP(now, devId, ddp.str());
}

void FastMeta::sendUpdate(uint64_t pulse_id, uint32_t pixel, uint32_t tof)
{
	// NOTE: All Fast Meta-Data PVs MUST Be of Some Base Integer Type!
	// So We Always Send it as a U32 Variable Value Update.
	// (No "U32 Array" Support Yet Either for Fast Meta-Data!)

	/* TODO Buffer is currently sized for a U32 update; change this
	 * if we start doing math and want to push double updates as well.
	 */
	uint32_t pkt[4 + (sizeof(ADARA::Header) / sizeof(uint32_t))];

	pkt[0] = 4 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_U32_TYPE,
		ADARA::PacketType::VAR_VALUE_U32_VERSION );

	/* Create a different timestamp for each variable update packet by
	 * adding the TOF value to the pulse ID, handling overflow of the
	 * nanoseconds field. TOF is originally in units of 100ns.
	 *
	 * Note that we strip any cycle field from the TOF.
	 */
	tof &= ((1U << 21) - 1);
	tof *= 100;
	uint32_t ns = tof + (pulse_id & 0xffffffff);

	pkt[2] = pulse_id >> 32;
	while (ns >= NANO_PER_SECOND_LL) {
		ns -= NANO_PER_SECOND_LL;
		pkt[2]++;
	}
	pkt[3] = ns;

	/* The device/type ID is the upper 15 bits of the fastmeta
	 * pixel ID; just use that directly as a key. The lower 16 bits
	 * indicate the ADC value, or the rising/falling edge, so it
	 * will be the variable update value.
	 *
	 * TODO perhaps we can do something smart with the ADC values?
	 * Allow user to specify a formula to convert it to a
	 * meaningful unit?
	 *
	 * Then again, it could depend on temperature and other info
	 * that we don't have available to us.
	 */
	uint32_t key = pixel & ~0xffff;
	uint32_t val = pixel & 0xffff;
	const Variable &var = m_vars[key];
	pkt[4] = var.m_devId;
	pkt[5] = var.m_varId;
	pkt[6] = ADARA::VariableStatus::OK << 16;
	pkt[6] |= ADARA::VariableSeverity::OK;
	pkt[7] = val;

	if (var.m_persist) {
		/* Fast metadata updates are implicitly owned by SMS and not
		 * by a data source; it would be nice to change that, but the
		 * corner cases of handling a device with variables coming
		 * through multiple sources (preprocessors) complicate knowing
		 * when we can really drop the device.
		 */
		m_meta->updateMappedVariable(pkt[4], pkt[5], (uint8_t *) pkt,
					sizeof(pkt));
	} else
		StorageManager::addPacket(pkt, sizeof(pkt));
}

void FastMeta::sendMultUpdate( uint64_t pulse_id,
		SMSControl::EventVector events )
{
	// (This should never happen, check caller... ;-D)
	if ( events.size() == 0 ) {
		ERROR("sendMultUpdate(): Fast Meta-Data (Mult) Update"
			<< " has NO DATA...! Ignoring...");
		return;
	}

	// NOTE: All Fast Meta-Data PVs MUST Be of Some Base Integer Type!
	// So We Always Send it as a (Mult) U32 Variable Value Update.
	// (No "U32 Array" Support Yet Either for Fast Meta-Data!)

	// Compute Mult U32 Variable Value Update Packet Size (in Uint32's)
	uint32_t size = (sizeof(ADARA::Header) / sizeof(uint32_t))
		+ 4 + ( events.size() * 2 ); // TOF + Value

	uint32_t pkt[size];

	pkt[0] = ( 4 + ( events.size() * 2 ) ) * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE,
		ADARA::PacketType::MULT_VAR_VALUE_U32_VERSION );

	pkt[2] = pulse_id >> 32;
	pkt[3] = pulse_id & 0xffffffff;

	/* The device/type ID is the upper 15 bits of the fastmeta
	 * pixel ID; just use that directly as a key. The lower 16 bits
	 * indicate the ADC value, or the rising/falling edge, so it
	 * will be the variable update value.
	 *
	 * TODO perhaps we can do something smart with the ADC values?
	 * Allow user to specify a formula to convert it to a
	 * meaningful unit?
	 *
	 * Then again, it could depend on temperature and other info
	 * that we don't have available to us.
	 */

	// Use 1st Event to Get Device Key/Fast Meta-Data Info...
	uint32_t pixel = events.begin()->pixel;
	uint32_t tof;

	uint32_t key = pixel & ~0xffff;
	uint32_t val;
	const Variable &var = m_vars[key];
	pkt[4] = var.m_devId;
	pkt[5] = var.m_varId;
	pkt[6] = ADARA::VariableStatus::OK << 16;
	pkt[6] |= ADARA::VariableSeverity::OK;

	pkt[7] = events.size();

	SMSControl::EventVector::iterator it;

	uint32_t *ptr = &(pkt[8]);

	for (it = events.begin(); it != events.end() ; ++it) {

		pixel = it->pixel;
		tof = it->tof;

		/* TOF is originally in units of 100ns.
		 * Note that we strip any cycle field from the TOF.
		 */
		tof &= ((1U << 21) - 1);
		tof *= 100;

		*ptr++ = tof;

		val = pixel & 0xffff;

		*ptr++ = val;
	}

	if (var.m_persist) {
		/* Fast metadata updates are implicitly owned by SMS and not
		 * by a data source; it would be nice to change that, but the
		 * corner cases of handling a device with variables coming
		 * through multiple sources (preprocessors) complicate knowing
		 * when we can really drop the device.
		 */
		m_meta->updateMappedVariable(pkt[4], pkt[5], (uint8_t *) pkt,
					sizeof(pkt));
	} else
		StorageManager::addPacket(pkt, sizeof(pkt));
}

