#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <string>
#include <map>

#include "RunInfo.h"
#include "StorageManager.h"

class RunInfoResetPV : public smsTriggerPV {
public:
	RunInfoResetPV(const std::string &prefix, RunInfo *master) :
		smsTriggerPV(prefix + "Reset"), m_master(master),
		m_unlocked(true) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

private:
	RunInfo *m_master;
	bool m_unlocked;

	bool allowUpdate(const gdd &) { return m_unlocked; }
	void triggered(void) { m_master->reset(); }
};

class RunInfoPV : public smsStringPV {
public:
	RunInfoPV(const std::string &name, RunInfo *ri) :
		smsStringPV(name), m_runInfo(ri), m_unlocked(true) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

private:
	RunInfo *m_runInfo;
	bool m_unlocked;

	bool allowUpdate(const gdd &) { return m_unlocked; }
	void changed(void) { m_runInfo->invalidateCache(); }
};

class RunUserInfoPV : public smsStringPV {
public:
	RunUserInfoPV(const std::string &prefix, RunInfo *ri) :
		smsStringPV(prefix + "UserInfo"), m_runInfo(ri),
		m_unlocked(true) {}

	void lock(void) { m_unlocked = false; }
	void unlock(void) { m_unlocked = true; }

private:
	RunInfo *m_runInfo;
	bool m_unlocked;

	bool validateUser(const std::string &val) {
		size_t sep, next;

		/* Make sure there is a separator for the name,
		 * and it isn't at the beginning of the string.
		 */
		sep = val.find_first_of(':');
		if (sep == 0 || sep == std::string::npos)
			return false;

		/* Make sure there is a separator for the uid,
		 * and it isn't directly following the name
		 * separator.
		 */
		next = sep + 1;
		sep = val.find_first_of(':', next);
		if (sep == next || sep == std::string::npos)
			return false;

		/* Make sure that role is non-empty, and there
		 * is no additional separator after it.
		 */
		sep++;
		if (sep >= val.length())
			return false;

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
		return validateUser(val.substr(begin, end - begin));
	}

	void changed(void) { m_runInfo->invalidateCache(); }
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
		case '&':	out.append("&amp;");	break;
		case '\"':	out.append("&quot;");	break;
		case '\'':	out.append("&apos;");	break;
		case '<':	out.append("&lt;");	break;
		case '>':	out.append("&gt;");	break;
		default:	out.append(1, in[pos]);break;
		}
	}
}

static void addUserInfo(std::string &out, const std::string &info)
{
	size_t begin, end;

	out += "<users>";

	for (begin = end = 0; end != std::string::npos; begin = end + 1) {
		end = info.find_first_of(':', begin);
		out += "<user><name>";
		xmlEncodeTo(out, info, begin, end);
		out += "</name>";
		begin = end + 1;
		end = info.find_first_of(':', begin);
		out += "<id>";
		xmlEncodeTo(out, info, begin, end);
		out += "</id>";
		begin = end + 1;
		end = info.find_first_of(';', begin);
		out += "<role>";
		xmlEncodeTo(out, info, begin, end);
		out += "</role></user>";
	}

	out += "</users>";
}

static void addElements(std::string &out, RunInfo::RunInfoMap &map,
			const char *stanza = NULL);

static void addElements(std::string &out, RunInfo::RunInfoMap &map,
			const char *stanza)
{
	RunInfo::RunInfoMap::iterator it;
	bool stanza_added = !stanza;

	for (it = map.begin(); it != map.end(); it++) {
		if (it->second->valid()) {
			if (!stanza_added) {
				out += "<"; out += stanza; out += ">";
				stanza_added = true;
			}

			out += "<"; out += it->first; out += ">";
			xmlEncodeTo(out, it->second->value());
			out += "</"; out += it->first; out += ">";
		}
	}

	if (stanza && stanza_added) {
		out += "</"; out += stanza; out += ">";
	}
}

RunInfo::RunInfo(const std::string &beamline, SMSControl *sms) :
	m_beamline(beamline), m_runNumber(0), m_packetValid(false),
	m_packet(NULL), m_packetSize(0)
{
	std::string prefix(beamline);
	prefix += ":SMS:RunInfo:";

	m_resetPV.reset(new RunInfoResetPV(prefix, this));
	sms->addPV(m_resetPV);

	m_userPV.reset(new RunUserInfoPV(prefix, this));
	sms->addPV(m_userPV);

	/* These fields are required */
	addPV(prefix, "ProposalId", "proposal_id", m_required, sms);

	/* These fields are optional */
	addPV(prefix, "ProposalTitle", "proposal_title", m_optional, sms);
	addPV(prefix, "RunTitle", "run_title", m_optional, sms);

	/* These fields describe the sample, and are optional */
	prefix += "Sample:";
	addPV(prefix, "Id", "id", m_sample, sms);
	addPV(prefix, "Name", "name", m_sample, sms);
	addPV(prefix, "Nature", "nature", m_sample, sms);
	addPV(prefix, "Formula", "chemical_formula", m_sample, sms);
	addPV(prefix, "Environment", "environment", m_sample, sms);

	/* Elements das_version, facility_name, instrument_name, and run_number
	 * will be provided by this class rather than by CAS.
	 */

	m_connection = StorageManager::onPrologue(boost::bind(&RunInfo::onPrologue, this));
}

RunInfo::~RunInfo()
{
	m_connection.disconnect();
	delete [] m_packet;
}

void RunInfo::addPV(const std::string &prefix, const char *pv_name,
		    const char *xml_name, RunInfoMap &map, SMSControl *sms)
{
	std::string pvName = prefix + pv_name;
	map[xml_name].reset(new RunInfoPV(pvName, this));
	sms->addPV(map[xml_name]);
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

void RunInfo::reset(void)
{
	RunInfoMap::iterator it;

	for (it = m_required.begin(); it != m_required.end(); it++)
		it->second->unset();
	for (it = m_optional.begin(); it != m_optional.end(); it++)
		it->second->unset();
	for (it = m_sample.begin(); it != m_sample.end(); it++)
		it->second->unset();
}

bool RunInfo::valid(void)
{
	RunInfoMap::iterator it;

	for (it = m_required.begin(); it != m_required.end(); it++)
		if (!it->second->valid())
			return false;

	return true;
}

void RunInfo::generatePacket(void)
{
	if (m_packetValid)
		return;

	std::string xml;
	xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	xml += "<runinfo "
		"xmlns=\"http://public.sns.gov/schema/runinfo.xsd\" "
		"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		"xsi:schemaLocation=\"http://public.sns.gov/schema/runinfo.xsd "
		"http://public.sns.gov/schema/runinfo.xsd\">";
	xml += "<das_version>ADARA v0.1</das_version>";
	xml += "<facility_name>SNS</facility_name>";
	xml += "<instrument_name>";
	xml += m_beamline;
	xml += "</instrument_name>";

	xml += "<run_number>";
	xml += boost::lexical_cast<std::string>(m_runNumber);
	xml += "</run_number>";

	addUserInfo(xml, m_userPV->value());

	addElements(xml, m_required);
	addElements(xml, m_optional);
	addElements(xml, m_sample, "sample");

	xml += "</runinfo>";

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
	fields[1] = ADARA::PacketType::RUN_INFO_V0;
	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
	fields[4] = xml.size();

	/* Zero fill the last word in the packet, then copy in the XML */
	fields[3 + (payload_len / 4)] = 0;
	memcpy(fields + 5, xml.c_str(), xml.size());

	m_packetValid = true;
}

void RunInfo::onPrologue(void)
{
	/* TODO we generate this for the beginning of a data file, but should
	 * we also update this mid-file if the user sets our PVs?
	 * I think we probably shouldn't, as this info is primarily for
	 * STS. But it may not be a bad idea to give this to live viewers
	 * as well.
	 */
	if (m_runNumber) {
		generatePacket();
		StorageManager::addPrologue(m_packet, m_packetSize);
	}
}
