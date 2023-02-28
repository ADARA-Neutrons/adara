
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.RunInfo"));

#include <string>
#include <sstream>

#include <stdint.h>

#include <gddApps.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "EPICS.h"
#include "ADARAUtils.h"
#include "RunInfo.h"
#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"

RateLimitedLogging::History RLLHistory_RunInfo;

// Rate-Limited Logging IDs...
#define RLL_PV_WRITE                  0

class RunInfoResetPV : public smsTriggerPV {
public:
	RunInfoResetPV(const std::string &prefix, RunInfo *master) :
		smsTriggerPV(prefix + "Reset"), m_master(master),
		m_unlocked(true) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

	bool allowUpdate(const gdd &) { return m_unlocked; }

	void triggered(struct timespec *ts) { m_master->reset(ts); }

private:
	RunInfo *m_master;
	bool m_unlocked;
};

class RunInfoPV : public smsStringPV {
public:
	RunInfoPV(const std::string &name, const std::string &label,
			bool isRequired, RunInfo *ri) :
		smsStringPV(name), m_label(label), m_isRequired(isRequired),
		m_runInfo(ri), m_unlocked(true), m_lastValid(false) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

	std::string label(void) { return m_label; }

	bool isRequired(void) { return m_isRequired; }

	bool lastValid(void) { return m_lastValid; }

	bool allowUpdate(const gdd &) { return m_unlocked; }

	void changed(void)
	{
		INFO("RunInfoPV::changed()");

		m_runInfo->pvChanged(this);
		m_lastValid = this->valid();
		m_runInfo->checkPacket();

		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts);
		StorageManager::autoSavePV( m_pv_name, value(), &ts );
	}

private:
	std::string m_label;
	bool m_isRequired;
	RunInfo *m_runInfo;
	bool m_unlocked;
	bool m_lastValid;

	friend class RunInfoFloat64PV;
};

class RunInfoFloat64PV : public smsFloat64PV {
public:
	RunInfoFloat64PV(const std::string &name,
			RunInfo::RunInfoPVSharedPtr stringRunInfoPV,
			double min = FLOAT64_MIN, double max = FLOAT64_MAX,
			double epsilon = FLOAT64_EPSILON) :
		smsFloat64PV(name, min, max, epsilon),
		m_stringRunInfoPV(stringRunInfoPV) {}

	// Defer to Regular *String* Version of This PV for Locking/Unlocking...
	bool allowUpdate(const gdd &val)
		{ return m_stringRunInfoPV->allowUpdate(val); }

	caStatus write(const casCtx &ctx, const gdd &val)
	{
		if ( !allowUpdate(val) ) {
			/* We don't want to update the PV at this time; still
			 * send a notification to any watchers, and just return
			 * success.
			 */
			std::string log_info;
			if ( RateLimitedLogging::checkLog( RLLHistory_RunInfo,
					RLL_PV_WRITE, m_pv_name, 60, 3, 15, log_info ) ) {
				DEBUG(log_info
					<< "RunInfoFloat64PV::write() m_pv_name=" << m_pv_name
					<< " Updates Not Allowed, Ignore Value.");
			}
			notify();
			return S_casApp_success;
		}

		return( smsFloat64PV::write( ctx, val ) );
	}

	void changed(void)
	{
		INFO("RunInfoFloat64PV::changed()");

		// Just Set the Associated String RunInfoPV to Latest Float Value...
		struct timespec ts;
		m_value->getTimeStamp(&ts);
		std::stringstream ss;
		ss << std::setprecision(17) << value();
		m_stringRunInfoPV->update( ss.str(), &ts );

		// AutoSave PV Value Change...
		StorageManager::autoSavePV( m_pv_name, ss.str(), &ts );
	}

private:
	RunInfo::RunInfoPVSharedPtr m_stringRunInfoPV;
};

class RunUserInfoPV : public smsStringPV {
public:
	RunUserInfoPV(const std::string &prefix, RunInfo *ri) :
		smsStringPV(prefix + "UserInfo"), m_runInfo(ri),
		m_unlocked(true) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

	bool validateUser(const std::string &val) {
		size_t sep, next;

		/* There are Now Two Possible Formats for UserInfo as of 7/2016...!
		 *
		 *    Name1:Uid1:Role1; Name2:Uid2:Role2; . . .
		 * or
		 *    Uid1; Uid2; Uid3; . . .
		 *
		 * We will handle Either One, and since we're postponing the
		 * resolution of Uid-to-Name until "Later" (either STC or AutoRedux)
		 * We will Now Accept "Empty" strings for the Name and Role, as in:
		 *    :Uid1:; Name2:Uid2:; :Uid3:Role3; . . .
		 */

		/* See if there is a separator for the name,
		 * if not then this is a Plain Uid format...
		 */
		sep = val.find_first_of(':');
		if (sep == std::string::npos) {
			DEBUG("validateUser(): Plain Uid Format");
			return true;
		}

		/* Found a separator, so this is the Full Name:Uid:Role Format...
		 * Make sure there is another separator for the uid,
		 * and that it isn't directly following the name separator.
		 * (We _Have_ to At Least have a Uid per person...! ;-)
		 */
		next = sep + 1;
		sep = val.find_first_of(':', next);
		if (sep == next || sep == std::string::npos) {
			DEBUG("validateUser(): Empty Uid or Missing Separator");
			return false;
		}

		/* Ok now if the role is empty, and there's
		 * no additional separator after it.
		 */
		sep++;
		// if (sep >= val.length())
			// return false;

		/* Make sure there are no additional separators...!
		 */
		sep = val.find_first_of(':', sep);
		return sep == std::string::npos;
	}

	bool allowUpdate(const gdd &in) {
		if (!m_unlocked)
			return false;

		unsigned int start, nelem;
		in.getBound(0, start, nelem);

		/* Some clients will send the trailing nul, other
		 * don't. Normalize the length to not include it.
		 */
		if (!((char *) in.dataPointer())[nelem - 1])
			nelem--;

		std::string val((char *) in.dataPointer(), nelem);
		size_t begin, end;

		begin = 0;
		end = val.find_first_of(';');
		while (end != std::string::npos) {
			if (!validateUser(val.substr(begin, end - begin)))
				return false;

			begin = end + 1;
			end = val.find_first_of(';', begin);
		}

		/* Now get the last one */
		if ( begin < val.size() )
			return( validateUser(val.substr(begin, val.size() - begin)) );
		else
			return( true );
	}

	void changed(void) {

		INFO("RunUserInfoPV::changed()");

		m_runInfo->invalidateCache();
		m_runInfo->checkPacket();

		// AutoSave PV Value Change...
		struct timespec ts;
		m_value->getTimeStamp(&ts);
		StorageManager::autoSavePV( m_pv_name, value(), &ts );
	}

private:
	RunInfo *m_runInfo;
	bool m_unlocked;
};

static void xmlEncodeTo(std::string &out, const std::string &in,
			size_t pos = 0, size_t end = std::string::npos);

static void xmlEncodeTo(std::string &out, const std::string &in,
			size_t pos, size_t end)
{
	if (end == std::string::npos)
		end = in.size();

	/* From stackoverflow.com */
	for( ; pos != end; pos++) {
		switch(in[pos]) {
			// The usual suspects... (supported directly by latest XML spec)
			case '&':	out.append("&amp;");	break;
			case '\"':	out.append("&quot;");	break;
			case '\'':	out.append("&apos;");	break;
			case '<':	out.append("&lt;");	break;
			case '>':	out.append("&gt;");	break;
			// Everything Else (Normal and Odd)...
			default:
				// Carefully convert chars to ints (without sign extension!)
				int keycode = (unsigned char) in[pos];
				int thresh = (unsigned char) '\x7F';
				// Odd characters a la UTF-*...
				if ( keycode >= thresh ) {
					// Convert to literal HTML key code "&#ddd" (decimal!)
					std::stringstream ss;
					ss << "&#";
					ss << std::dec << keycode << std::dec;
					ss << ";";
					out.append(ss.str());
				}
				// Plain text character...
				else {
					out.append(1, in[pos]);
				}
				break;
		}
	}
}

static void addUserInfo(std::string &out, const std::string &info)
{
	size_t begin, end, next_user;

	out += "   <users>\n";

	for ( begin = end = 0;
			begin != std::string::npos && begin < info.size(); )
	{
		next_user = info.find_first_of(';', begin);
		if ( next_user == begin )
		{
			begin++;
			continue;
		}

		out += "      <user>\n";

		end = info.find_first_of(':', begin);
		// Name:Uid:Role
		if ( end != begin && end < next_user )
		{
			out += "         <name>";
			xmlEncodeTo(out, info, begin, end);
			out += "</name>\n";
			begin = end + 1;
		}
		// Uid
		else
		{
			out += "         <name>XXX_UNRESOLVED_NAME_XXX</name>\n";
			if ( end == begin )
				begin = end + 1;
		}

		end = info.find_first_of(':', begin);
		// Missing Uid (...::...), Shouldn't Happen...? (validateUser()...)
		if ( end == begin || begin >= info.size() )
		{
			out += "         <id>XXX_UNRESOLVED_UID_XXX</id>\n";
			begin = end + 1;
		}
		// ...:Uid:...
		else if ( end < next_user )
		{
			out += "         <id>";
			xmlEncodeTo(out, info, begin, end);
			out += "</id>\n";
			begin = end + 1;
		}
		// Plain Uid...
		else
		{
			out += "         <id>";
			xmlEncodeTo(out, info, begin, next_user);
			out += "</id>\n";
			begin = next_user;
		}

		// Role, if present...
		if ( begin < next_user && begin < info.size() )
		{
			out += "         <role>";
			xmlEncodeTo(out, info, begin, next_user);
			out += "</role>\n";
		}
		// No Role...
		else
		{
			out += "         <role>XXX_UNRESOLVED_ROLE_XXX</role>\n";
		}

		out += "      </user>\n";

		// Next User of End of Loop...
		begin = next_user;
		if ( begin != std::string::npos )
			begin++;
	}

	out += "   </users>\n";
}

static void addElements(std::string &out, RunInfo::RunInfoMap &map,
			const char *stanza = NULL);

static void addElements(std::string &out, RunInfo::RunInfoMap &map,
			const char *stanza)
{
	RunInfo::RunInfoMap::iterator it;
	bool stanza_added = !stanza;
	std::string indent = ( stanza ) ? "   " : "";

	for (it = map.begin(); it != map.end(); it++) {
		if (it->second->valid()) {
			if (!stanza_added) {
				out += "   <"; out += stanza; out += ">\n";
				stanza_added = true;
			}

			out += indent + "   <"; out += it->first; out += ">";
			xmlEncodeTo(out, it->second->value());
			out += "</"; out += it->first; out += ">\n";
		}
	}

	if (stanza && stanza_added) {
		out += "   </"; out += stanza; out += ">\n";
	}
}

RunInfo::RunInfo(const std::string &facility, const std::string &beamline,
		SMSControl *ctrl, bool sendSampleInRunInfo, bool savePixelMap) :
	m_facility(facility), m_beamline(beamline), m_ctrl(ctrl),
	m_sendSampleInRunInfo(sendSampleInRunInfo),
	m_savePixelMap(savePixelMap),
	m_runNumber(0), m_lastRunNumber(0),
	m_packetValid(false), m_packet(NULL), m_packetSize(0)
{
	std::string prefix(m_ctrl->getPVPrefix());
	prefix += ":RunInfo:";

	m_resetPV.reset(new RunInfoResetPV(prefix, this));
	m_ctrl->addPV(m_resetPV);

	m_userPV.reset(new RunUserInfoPV(prefix, this));
	m_ctrl->addPV(m_userPV);

	/* These fields are required */
	m_propId = addPV(prefix, "ProposalId", "proposal_id", m_required);

	/* These fields are optional */
	addPV(prefix, "ProposalTitle", "proposal_title", m_optional);
	addPV(prefix, "RunTitle", "run_title", m_optional);

	// Send Sample Info in RunInfo...?
	m_sendSampleInRunInfoPV.reset(new smsBooleanPV(prefix
		+ "SendSampleInRunInfo", /* AutoSave */ true));
	m_ctrl->addPV(m_sendSampleInRunInfoPV);

	// Save Pixel Mapping Table into NeXus Data File...?
	m_savePixelMapPV.reset(new smsBooleanPV(prefix
		+ "SavePixelMap", /* AutoSave */ true));
	m_ctrl->addPV(m_savePixelMapPV);

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_sendSampleInRunInfoPV->update( m_sendSampleInRunInfo, &now );
	m_savePixelMapPV->update( m_savePixelMap, &now );

	/* These fields describe the sample, and are optional */
	prefix += "Sample:";
	addPV(prefix, "Id", "id", m_sample);
	addPV(prefix, "Name", "name", m_sample);
	addPV(prefix, "Nature", "nature", m_sample);
	addPV(prefix, "Formula", "chemical_formula", m_sample);

	m_massPV = addPV(prefix, "MassString", "mass", m_sample);
	m_massFloat64PV.reset(new RunInfoFloat64PV(prefix + "Mass",
		m_massPV, 0.0));
	m_ctrl->addPV(m_massFloat64PV);
	addPV(prefix, "MassUnits", "mass_units", m_sample);

	// NOTE: This is for *Mass* Density, _Not_ "Number Density"...!
	m_densityPV = addPV(prefix, "DensityString",
		"mass_density", m_sample);
	m_densityFloat64PV.reset(new RunInfoFloat64PV(prefix + "Density",
		m_densityPV, 0.0 ));
	m_ctrl->addPV(m_densityFloat64PV);
	addPV(prefix, "DensityUnits", "mass_density_units", m_sample);

	// For Minor Backwards Compat - "Container Name"... (Temporary)
	addPV(prefix, "Container", "container", m_sample);

	m_containerIdPV = addPV(prefix, "ContainerId",
		"container_id", m_sample);
	m_containerNamePV = addPV(prefix, "ContainerName",
		"container_name", m_sample);

	// Create Container Concatenation "Component" PV...
	// (value is: ContainerId + ":" + ContainerName, lol... ;-D
	m_componentPV = addPV(prefix, "Component", "component", m_sample);

	addPV(prefix, "CanIndicator", "can_indicator", m_sample);
	addPV(prefix, "CanBarcode", "can_barcode", m_sample);
	addPV(prefix, "CanName", "can_name", m_sample);
	addPV(prefix, "CanMaterials", "can_materials", m_sample);

	addPV(prefix, "Description", "description", m_sample);
	addPV(prefix, "Comments", "comments", m_sample);

	m_heightInContainerPV = addPV(prefix, "HeightInContainerString",
		"height_in_container", m_sample);
	m_heightInContainerFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "HeightInContainer",
			m_heightInContainerPV, 0.0));
	m_ctrl->addPV(m_heightInContainerFloat64PV);
	addPV(prefix, "HeightInContainerUnits",
		"height_in_container_units", m_sample);

	m_interiorDiameterPV = addPV(prefix, "InteriorDiameterString",
		"interior_diameter", m_sample);
	m_interiorDiameterFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "InteriorDiameter",
			m_interiorDiameterPV, 0.0));
	m_ctrl->addPV(m_interiorDiameterFloat64PV);
	addPV(prefix, "InteriorDiameterUnits",
		"interior_diameter_units", m_sample);

	m_interiorHeightPV = addPV(prefix, "InteriorHeightString",
		"interior_height", m_sample);
	m_interiorHeightFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "InteriorHeight",
			m_interiorHeightPV, 0.0));
	m_ctrl->addPV(m_interiorHeightFloat64PV);
	addPV(prefix, "InteriorHeightUnits",
		"interior_height_units", m_sample);

	m_interiorWidthPV = addPV(prefix, "InteriorWidthString",
		"interior_width", m_sample);
	m_interiorWidthFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "InteriorWidth",
			m_interiorWidthPV, 0.0));
	m_ctrl->addPV(m_interiorWidthFloat64PV);
	addPV(prefix, "InteriorWidthUnits",
		"interior_width_units", m_sample);

	m_interiorDepthPV = addPV(prefix, "InteriorDepthString",
		"interior_depth", m_sample);
	m_interiorDepthFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "InteriorDepth",
			m_interiorDepthPV, 0.0));
	m_ctrl->addPV(m_interiorDepthFloat64PV);
	addPV(prefix, "InteriorDepthUnits",
		"interior_depth_units", m_sample);

	m_outerDiameterPV = addPV(prefix, "OuterDiameterString",
		"outer_diameter", m_sample);
	m_outerDiameterFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "OuterDiameter",
			m_outerDiameterPV, 0.0));
	m_ctrl->addPV(m_outerDiameterFloat64PV);
	addPV(prefix, "OuterDiameterUnits", "outer_diameter_units", m_sample);

	m_outerHeightPV = addPV(prefix, "OuterHeightString",
		"outer_height", m_sample);
	m_outerHeightFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "OuterHeight", m_outerHeightPV, 0.0));
	m_ctrl->addPV(m_outerHeightFloat64PV);
	addPV(prefix, "OuterHeightUnits", "outer_height_units", m_sample);

	m_outerWidthPV = addPV(prefix, "OuterWidthString",
		"outer_width", m_sample);
	m_outerWidthFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "OuterWidth", m_outerWidthPV, 0.0));
	m_ctrl->addPV(m_outerWidthFloat64PV);
	addPV(prefix, "OuterWidthUnits", "outer_width_units", m_sample);

	m_outerDepthPV = addPV(prefix, "OuterDepthString",
		"outer_depth", m_sample);
	m_outerDepthFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "OuterDepth", m_outerDepthPV, 0.0));
	m_ctrl->addPV(m_outerDepthFloat64PV);
	addPV(prefix, "OuterDepthUnits", "outer_depth_units", m_sample);

	m_volumeCubicPV = addPV(prefix, "VolumeCubicString",
		"volume_cubic", m_sample);
	m_volumeCubicFloat64PV.reset(
		new RunInfoFloat64PV(prefix + "VolumeCubic", m_volumeCubicPV, 0.0));
	m_ctrl->addPV(m_volumeCubicFloat64PV);
	addPV(prefix, "VolumeCubicUnits", "volume_cubic_units", m_sample);

	/* Elements das_version, facility_name, instrument_name, and run_number
	 * will be provided by this class rather than by CAS.
	 */

	m_connection = StorageManager::onPrologue(
		boost::bind(&RunInfo::onPrologue, this, _1));

	// Restore Any PVs to AutoSaved Config Values...

	struct timespec ts;
	std::string value;
	double dvalue;
	bool bvalue;

	// SendSampleInRunInfo

	if ( StorageManager::getAutoSavePV(
			m_sendSampleInRunInfoPV->getName(), bvalue, ts ) ) {
		m_sendSampleInRunInfo = bvalue;
		m_sendSampleInRunInfoPV->update(bvalue, &ts);
	}

	// SavePixelMap

	if ( StorageManager::getAutoSavePV(
			m_savePixelMapPV->getName(), bvalue, ts ) ) {
		m_savePixelMap = bvalue;
		m_savePixelMapPV->update(bvalue, &ts);
	}

	// RunInfoFloat64PVs...

	if ( StorageManager::getAutoSavePV(
			m_massFloat64PV->getName(), dvalue, ts ) ) {
		m_massFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_densityFloat64PV->getName(), dvalue, ts ) ) {
		m_densityFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_heightInContainerFloat64PV->getName(), dvalue, ts ) ) {
		m_heightInContainerFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_interiorDiameterFloat64PV->getName(), dvalue, ts ) ) {
		m_interiorDiameterFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_interiorHeightFloat64PV->getName(), dvalue, ts ) ) {
		m_interiorHeightFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_interiorWidthFloat64PV->getName(), dvalue, ts ) ) {
		m_interiorWidthFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_interiorDepthFloat64PV->getName(), dvalue, ts ) ) {
		m_interiorDepthFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_outerDiameterFloat64PV->getName(), dvalue, ts ) ) {
		m_outerDiameterFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_outerHeightFloat64PV->getName(), dvalue, ts ) ) {
		m_outerHeightFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_outerWidthFloat64PV->getName(), dvalue, ts ) ) {
		m_outerWidthFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_outerDepthFloat64PV->getName(), dvalue, ts ) ) {
		m_outerDepthFloat64PV->update(dvalue, &ts);
	}

	if ( StorageManager::getAutoSavePV(
			m_volumeCubicFloat64PV->getName(), dvalue, ts ) ) {
		m_volumeCubicFloat64PV->update(dvalue, &ts);
	}

	// UserInfo PV
	if ( StorageManager::getAutoSavePV( m_userPV->getName(), value, ts ) ) {
		m_userPV->update(value, &ts);
	}

	// RunInfoPVs by Map...

	RunInfoMap::iterator it;

	// Required RunInfoPVs...
	for ( it = m_required.begin(); it != m_required.end(); it++ ) {
		if ( StorageManager::getAutoSavePV( it->second->getName(),
				value, ts ) ) {
			it->second->update(value, &ts);
		}
	}

	// Optional RunInfoPVs...
	for ( it = m_optional.begin(); it != m_optional.end(); it++ ) {
		if ( StorageManager::getAutoSavePV( it->second->getName(),
				value, ts ) ) {
			it->second->update(value, &ts);
		}
	}

	// Sample RunInfoPVs...
	for ( it = m_sample.begin(); it != m_sample.end(); it++ ) {
		if ( StorageManager::getAutoSavePV( it->second->getName(),
				value, ts ) ) {
			it->second->update(value, &ts);
		}
	}
}

RunInfo::~RunInfo()
{
	m_connection.disconnect();
	delete [] m_packet;
}

RunInfo::RunInfoPVSharedPtr RunInfo::addPV(
		const std::string &prefix, const char *pv_name,
		const char *xml_name, RunInfoMap &map)
{
	std::string pvName = prefix + pv_name;
	map[xml_name].reset(
		new RunInfoPV(pvName, pv_name, (map == m_required), this));
	RunInfoPVSharedPtr pv = map[xml_name];
	m_ctrl->addPV(pv);
	return(pv);
}

std::string RunInfo::getPropId(void)
{
	return m_propId->value();
}

void RunInfo::lock(void)
{
	RunInfoMap::iterator it;

	for (it = m_required.begin(); it != m_required.end(); it++)
		it->second->lock();
	for (it = m_optional.begin(); it != m_optional.end(); it++)
		it->second->lock();
	for (it = m_sample.begin(); it != m_sample.end(); it++)
		it->second->lock();
	m_resetPV->lock();
}

void RunInfo::unlock(void)
{
	RunInfoMap::iterator it;

	for (it = m_required.begin(); it != m_required.end(); it++)
		it->second->unlock();
	for (it = m_optional.begin(); it != m_optional.end(); it++)
		it->second->unlock();
	for (it = m_sample.begin(); it != m_sample.end(); it++)
		it->second->unlock();
	m_resetPV->unlock();
}

void RunInfo::reset( struct timespec *ts )
{
	RunInfoMap::iterator it;

	for (it = m_required.begin(); it != m_required.end(); it++)
		it->second->unset( false, ts );
	for (it = m_optional.begin(); it != m_optional.end(); it++)
		it->second->unset( false, ts );
	for (it = m_sample.begin(); it != m_sample.end(); it++)
		it->second->unset( false, ts );
}

bool RunInfo::valid(std::string &reason)
{
	RunInfoMap::iterator it;
	std::stringstream why;
	bool isValid = true;

	for (it = m_required.begin(); it != m_required.end(); it++) {
		if (!it->second->valid()) {
			if (isValid)
				why << "RunInfo Invalid";
			why << ", Missing Required ";
			why << it->second->label();
			isValid = false;
		}
	}

	if (isValid)
		why << "RunInfo Valid, All Required Values Set";

	reason = why.str();

	return isValid;
}

void RunInfo::pvChanged( RunInfoPV* pv )
{
	// Invalidate Cache...
	invalidateCache();

	// Update Container "Component" PV, As Needed...
	if ( !(pv->label().compare("ContainerId"))
			|| !(pv->label().compare("ContainerName")) )
	{
		// Concatenate Container Id and Name to Get "Component"... ;-D
		struct timespec ts;
		pv->timestamp(ts);
		std::stringstream ss;
		ss << m_containerIdPV->value()
			<< ": " << m_containerNamePV->value();
		DEBUG("pvChanged(): PV " << pv->label() << " Changed"
			<< " - Re-Concatenating Sample Component PV: " << ss.str());
		m_componentPV->update( ss.str(), &ts );
	}

	// Check for Change in "Required" PV Status...
	if ( pv->isRequired() )
	{
		RunInfoMap::iterator it;
		std::stringstream why;
		bool changedValid = false;
		bool isValid = true;

		for ( it = m_required.begin(); it != m_required.end(); it++ ) {
			bool valid = it->second->valid();
			if ( valid != it->second->lastValid() )
				changedValid = true;
			if (!valid) {
				if (isValid)
					why << "RunInfo Invalid";
				why << ", Missing Required ";
				why << it->second->label();
				isValid = false;
			}
		}

		if (isValid) {
			why << "RunInfo Valid, ";
			why << pv->label();
			why << " Value Set";
		}

		m_ctrl->updateValidRunInfo( isValid, why.str(), changedValid );
	}
}

extern std::string SMSD_VERSION;

bool RunInfo::generatePacket( uint32_t runNumber )
{
	if (m_packetValid)
		return(false);

	std::string xml;
	xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	xml += "<runinfo "
		"xmlns=\"http://public.sns.gov/schema/runinfo.xsd\" "
		"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		"xsi:schemaLocation=\"http://public.sns.gov/schema/runinfo.xsd "
		"http://public.sns.gov/schema/runinfo.xsd\">\n";
	xml += "   <das_version>ADARA SMS ";
	xml += SMSD_VERSION;
	xml += ", Common ";
	xml += ADARA::VERSION;
	xml += ", ComBus ";
	xml += ADARA::ComBus::VERSION;
	xml += ", Tag ";
	xml += ADARA::TAG_NAME;
	xml += "</das_version>\n";
	xml += "   <facility_name>";
	xml += m_facility;
	xml += "</facility_name>\n";
	xml += "   <instrument_name>";
	xml += m_beamline;
	xml += "</instrument_name>\n";

	xml += "   <run_number>";
	xml += boost::lexical_cast<std::string>(runNumber);
	xml += "</run_number>\n";

	addUserInfo(xml, m_userPV->value());

	addElements(xml, m_required);
	addElements(xml, m_optional);

	// Get Latest "Send Sample In RunInfo" Value from PV...
	m_sendSampleInRunInfo = m_sendSampleInRunInfoPV->value();
	if ( m_sendSampleInRunInfo ) {
		addElements(xml, m_sample, "sample");
	} else {
		xml += "   <no_sample_info/>\n";
	}

	// Get Latest "Save Pixel Map" Value from PV...
	m_savePixelMap = m_savePixelMapPV->value();
	if ( m_savePixelMap ) {
		xml += "   <save_pixel_map/>\n";
	}

	// Get Latest "Run Notes Updates Enabled" Value from PV...
	if ( m_ctrl->getRunNotesUpdatesEnabled() ) {
		xml += "   <run_notes_updates_enabled/>\n";
	}

	xml += "</runinfo>";

	DEBUG("generatePacket() xml=[" << xml << "]");

	/* Now that we've generated the new XML content, rebuild our packet.
	 */
	delete [] m_packet;
	m_packet = NULL;

	/* We need to add tailing bytes to make the payload a multiple of 4,
	 * and we need 4 bytes for the string length field.
	 */
	size_t payload_len = (4 + (xml.size() + 3)) & ~3;
	struct timespec ts;
	uint32_t *fields;

	m_packetSize = payload_len + sizeof(ADARA::Header);
	m_packet = new uint8_t[m_packetSize];
	fields = (uint32_t *) m_packet;

	clock_gettime(CLOCK_REALTIME, &ts);

	fields[0] = payload_len;
	fields[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::RUN_INFO_TYPE,
		ADARA::PacketType::RUN_INFO_VERSION );
	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
	fields[4] = xml.size();

	/* Zero fill the last word in the packet, then copy in the XML */
	fields[3 + (payload_len / 4)] = 0;
	memcpy(fields + 5, xml.c_str(), xml.size());

	m_packetValid = true;

	return(true);
}

void RunInfo::onPrologue( bool capture_last )
{
	// When Capturing Prologue Headers, Always Refer to *Last* Run Number
	// (Or the Lack Thereof), as We Capture Prologue Headers Only
	// _After_ the Run Number has Already Changed... ;-D

	if ( capture_last ) {
		if ( m_lastRunNumber ) {
			generatePacket( m_lastRunNumber );
			StorageManager::addPrologue(m_packet, m_packetSize);
		}
	}

	// Normal Operation, Only Dump RunInfo Packet When In A Run...

	else if ( m_runNumber ) {
		generatePacket( m_runNumber );
		StorageManager::addPrologue(m_packet, m_packetSize);
	}
}

