#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <string>
#include <map>

#include "RunInfo.h"
#include "StorageManager.h"

class RunInfoResetPV : public smsTriggerPV {
public:
	RunInfoResetPV(const std::string &prefix, RunInfo *master) :
		smsTriggerPV(prefix + "reset"), m_master(master) {}

private:
	RunInfo *m_master;
	void triggered(void) { m_master->reset(); }
};

class RunInfoPV : public smsStringPV {
public:
	RunInfoPV(const std::string &name, RunInfo *ri) :
		smsStringPV(name), m_runInfo(ri) {}

private:
	RunInfo *m_runInfo;

	void changed(void) { m_runInfo->invalidateCache(); }
};

static void xmlEncodeTo(const std::string &in, std::string &out)
{
	/* From stackoverflow.com */
	for(size_t pos = 0; pos != in.size(); pos++) {
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
			xmlEncodeTo(it->second->value(), out);
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

	/* These fields are required */
	addPV(prefix, "proposal_id", m_required, sms);
	addPV(prefix, "collection_number", m_required, sms);

	/* These fields are optional */
	addPV(prefix, "proposal_title", m_optional, sms);
	addPV(prefix, "collection_title", m_optional, sms);
	addPV(prefix, "run_title", m_optional, sms);

	/* These fields describe the sample, and are optional */
	prefix += "sample:";
	addPV(prefix, "id", m_sample, sms);
	addPV(prefix, "name", m_sample, sms);
	addPV(prefix, "nature", m_sample, sms);
	addPV(prefix, "formula", m_sample, sms);
	addPV(prefix, "environment", m_sample, sms);

	/* Elements das_version, facility_name, instrument_name, and run_number
	 * will be provided by this class rather than by CAS.
	 */
	// TODO handle "user" element

	m_connection = StorageManager::onPrologue(boost::bind(&RunInfo::onPrologue, this));
}

RunInfo::~RunInfo()
{
	m_connection.disconnect();
	delete [] m_packet;
}

void RunInfo::addPV(const std::string &prefix, const char *name,
		    RunInfoMap &map, SMSControl *sms)
{
	std::string pvName = prefix + name;
	map[name].reset(new RunInfoPV(pvName, this));
	sms->addPV(map[name]);
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
