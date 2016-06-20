#include <boost/bind.hpp>

#include <string>
#include <sstream>
#include <map>

#include <stdint.h>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "ADARAUtils.h"
#include "MetaDataMgr.h"
#include "StorageManager.h"
#include "SMSControl.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.MetaDataMgr"));

RateLimitedLogging::History RLLHistory_MetaDataMgr;

// Rate-Limited Logging IDs...
#define RLL_DROP_DEVICES_FOR_TAG   0
#define RLL_NO_DEVICES_TO_DROP     1
#define RLL_UPDATE_DESCRIPTOR      2
#define RLL_ADD_FAST_META_DDP      3
#define RLL_DESC_INCORRECT_TAG     4
#define RLL_ADD_EXISTING_DEVICE    5
#define RLL_UNABLE_REMAP_U32_VAR   6
#define RLL_UNABLE_REMAP_DBL_VAR   7
#define RLL_UNABLE_REMAP_STR_VAR   8
#define RLL_VAR_UPDATE_NO_DESC     9
#define RLL_VAR_UPDATE_BAD_TAG    10

MetaDataMgr::MetaDataMgr() : m_nextDevId(1)
{
	m_connection = StorageManager::onPrologue(
				boost::bind(&MetaDataMgr::onPrologue, this));
}

MetaDataMgr::~MetaDataMgr()
{
	m_connection.disconnect();
}

void MetaDataMgr::upstreamDisconnected(MetaDataMgr::VariableMap &vars)
{
	/* For each variable, modify the packet to indicate that we
	 * lost the upstream connection and feed it into the stream.
	 */
	VariableMap::iterator vit, vend = vars.end();
	uint32_t len, pktSize = 0;
	uint32_t *fields = NULL;
	uint8_t *pkt = NULL;

	std::stringstream var_log_ss;

	for (vit = vars.begin(); vit != vend; vit++) {

		PacketSharedPtr orig = vit->second;

		len = orig->packet_length();
		if (len > pktSize) {
			delete[] pkt;
			pktSize = len;
			pkt = new uint8_t[pktSize];
			fields = (uint32_t *) (pkt +
					ADARA::PacketHeader::header_length());
		}

		memcpy(pkt, orig->packet(), len);
		fields[2] = ADARA::VariableStatus::UPSTREAM_DISCONNECTED;
		fields[2] <<= 16;
		fields[2] |= ADARA::VariableSeverity::INVALID;

		/* Don't notify storage clients about the file size change;
		 * there is no reason to start a new file in the middle of
		 * these drops -- they should be small, and we should not
		 * dump the state of the variables we're dropping.
		 *
		 * We'll push the whole batch out at once when we are done.
		 */
		StorageManager::addPacket(pkt, len, false);

		// Add Variable Id to Log List...
		if (var_log_ss.str().empty())
			var_log_ss << vit->first;
		else
			var_log_ss << ", " << vit->first;
	}

	if ( !var_log_ss.str().empty() ) {
		DEBUG("Sending Upstream Disconnected for"
			<< " varIds = [ " << var_log_ss.str() << " ]");
	}

	delete[] pkt;
}

void MetaDataMgr::dropTag(uint32_t tag)
{
	/* Rate-limited log that we got disconnected from a DataSource (tag)
	 * and are Dropping All Associated Devices...
	 */
	std::string log_info;
	std::stringstream ss;
	ss << tag;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_DROP_DEVICES_FOR_TAG, ss.str(),
			600, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG(log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "dropTag(): Dropping Devices for Data Source, tag=" << tag);
	}

	DeviceMap::iterator dit, dend = m_devices.end();
	bool dropped = false;

	for (dit = m_devices.begin(); dit != dend; ) {
		DeviceVariables &dev = dit->second;

		if (dev.m_tag == tag) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropTag(): Sending Upstream Disconnected for"
				<< " devId=" << dev.m_devId
				<< " tag=" << dev.m_tag
				<< " mapped_dev=" << dit->first);
			upstreamDisconnected(dev.m_variables);
			m_devices.erase(dit++);
			dropped = true;
		} else
			++dit;
	}

	if (dropped)
		StorageManager::notify();
	else {
		/* Rate-limited log that there were No Associated Devices to Drop
		 * from the disconnected DataSource (tag)...
		 */
		// re-use "ss" from above...
		log_info.clear();
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_NO_DEVICES_TO_DROP, ss.str(),
				600, 3, 10, log_info ) ) {
			DEBUG(log_info
				<< "dropTag(): Warning No Devices Found! tag=" << tag);
		}
	}

	/* Remove the mapped device ids for this tag */
	std::map<uint64_t, uint32_t>::iterator it, end;
	end = m_devIdMap.end();
	for (it = m_devIdMap.begin(); it != end; ) {
		if (it->first >> 32 == tag) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropTag(): Removing Mapped Device"
				<< " devId=" << ( it->first & 0xffff )
				<< " tag=" << ( it->first >> 32 )
				<< " mapped_dev=" << it->second);
			m_activeDevId.erase(it->second);
			m_devIdMap.erase(it++);
		} else
			it++;
	}
}

uint32_t MetaDataMgr::lookupMappedDeviceId(uint32_t dev, uint32_t tag)
{
	uint64_t key = ((uint64_t) tag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator val;

	val = m_devIdMap.find(key);
	if ( val == m_devIdMap.end() )
		return 0;
	return val->second;
}

uint32_t MetaDataMgr::allocDev(uint32_t dev, uint32_t tag, bool do_log)
{
	uint32_t mapped_dev;
	if ( (mapped_dev = lookupMappedDeviceId( dev, tag )) ) {
		if ( do_log ) {
			DEBUG("Device Lookup returned mapped_dev=" << mapped_dev);
		}
		return mapped_dev;
	}

	while ( m_activeDevId.count( m_nextDevId ) )
		m_nextDevId++;

	m_activeDevId.insert( m_nextDevId );

	uint64_t key = ((uint64_t) tag << 32) | dev;
	m_devIdMap[key] = m_nextDevId;

	if ( do_log ) {
		DEBUG("New Input Device"
			<< " devId=" << dev << " tag=" << tag
			<< " Mapped to SMS Output Device"
			<< " mapped_dev=" << m_nextDevId);
	}

	return m_nextDevId++;
}

void MetaDataMgr::updateDescriptor(const ADARA::DeviceDescriptorPkt &in,
				   uint32_t tag)
{
	/* Rate-limited log that we received a DeviceDescriptorPkt. */
	std::string log_info;
	std::stringstream ss;
	ss << in.devId() << "/" << tag;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_UPDATE_DESCRIPTOR, ss.str(),
			60, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG(log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Update Descriptor devId=" << in.devId() << " tag=" << tag);
		do_log = true; // link this rate-limited log to other related logs
	}

	uint32_t mapped_dev = allocDev(in.devId(), tag, do_log);

	DeviceMap::iterator it = m_devices.find(mapped_dev);
	if (it != m_devices.end()) {
		/* Device exists already, ignore it if it didn't change. */
		DeviceVariables &dev = it->second;
		ADARA::Packet *dev_pkt = dev.m_descriptor.get();

		if (it->second.m_tag != tag) {
			/* Rate-limited log that we got a Descriptor from
			 * an Incorrect Tag (i.e., wrong/unexpected Data Source)
			 * [*** Can this ever actually happen? Lookup Key Includes Tag!]
			 */
			std::string log_info;
			std::stringstream ss;
			ss << tag;
			if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
					RLL_DESC_INCORRECT_TAG, ss.str(),
					10, 3, 50, log_info ) ) {
				SMSControl *ctrl = SMSControl::getInstance();
				DEBUG(log_info
					<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Got descriptor from incorrect tag "
					<< it->second.m_tag << " != " << tag);
			}
			return;
		}

		if (dev_pkt->packet_length() == in.packet_length() &&
				!memcmp(dev_pkt->payload(), in.payload(),
					dev_pkt->payload_length())) {
			if ( do_log ) {
				DEBUG("Inbound descriptor was identical");
			}
			return;
		}

		/* XXX It changed on us; need to flush out old variables
		 * (don't notify, we'll be pushing the new descriptor in a
		 * moment).
		 */
		if ( do_log ) {
			DEBUG("Updating existing descriptor");
		}
		m_devices.erase(it);
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::DeviceDescriptorPkt> ddp;
	ddp.reset(new ADARA::DeviceDescriptorPkt(in));
	ddp->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(ddp);

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 */
	StorageManager::addPacket(pkt->packet(), pkt->packet_length());
	m_devices[mapped_dev].m_descriptor = pkt;
	m_devices[mapped_dev].m_devId = in.devId();
	m_devices[mapped_dev].m_tag = tag;
}

void MetaDataMgr::addFastMetaDDP(const timespec &ts, uint32_t mapped_dev,
				 const std::string &ddp)
{
	/* Rate-limited log that we Added a New Fast Meta-Data DDP (Descriptor)
	 */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_ADD_FAST_META_DDP, "none",
			60, 10, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG(log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "addFastMetaDDP(): Add New Device mapped_dev=" << mapped_dev
			<< " (devId=-1 tag=0)");
	}

	DeviceMap::iterator it = m_devices.find(mapped_dev);
	if (it != m_devices.end()) {
		/* Rate-limited logging of adding existing device? */
		std::string log_info;
		std::stringstream ss;
		ss << mapped_dev;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_ADD_EXISTING_DEVICE, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "addFastMetaDDP(): tried to add existing (mapped) device"
				<< " mapped_dev=" << mapped_dev);
		}
		return;
	}

	uint32_t size = (ddp.size() + 3) & ~3;
	size += 2 * sizeof(uint32_t) + sizeof(ADARA::Header);

	/* Build the base DDP packet */
	uint32_t pkt[size / sizeof(uint32_t)];
	memset(pkt, 0, sizeof(pkt));
	pkt[0] = size - sizeof(ADARA::Header);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::DEVICE_DESC_TYPE,
		ADARA::PacketType::DEVICE_DESC_VERSION );
	pkt[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	pkt[3] = ts.tv_nsec;
	pkt[4] = mapped_dev;
	pkt[5] = ddp.size();
	memcpy(pkt + 6, ddp.data(), ddp.size());

	/* Wrap our buffered packet in an ADARA packet object so
	 * we can make a copy to store in our device map.
	 */
	ADARA::Packet wrapped((uint8_t *) pkt, size);

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 *
	 * We may get called before the StorageManager is fully initialized;
	 * if we are not currently streaming data, then just store it in
	 * our tracking structures; we'll put it in the stream when it gets
	 * started.
	 */
	if (StorageManager::streaming())
		StorageManager::addPacket(pkt, size);
	m_devices[mapped_dev].m_descriptor.reset(new ADARA::Packet(wrapped));
	m_devices[mapped_dev].m_devId = -1; // FastMetaDDP...!
	m_devices[mapped_dev].m_tag = 0;
}

void MetaDataMgr::updateValue(const ADARA::VariableU32Pkt &in, uint32_t tag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(in.devId(), tag);

	if (!mapped_dev) {
		/* Rate-limited logging of variable device lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << in.devId() << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32): Device Lookup Failed for Variable!"
				<< " devId=" << in.devId()
				<< " tag=" << tag
				<< " varId=" << in.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32Pkt> vup;
	vup.reset(new ADARA::VariableU32Pkt(in));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableDoublePkt &in,
		uint32_t tag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(in.devId(), tag);

	if (!mapped_dev) {
		/* Rate-limited logging of unable to remap variable? */
		std::string log_info;
		std::stringstream ss;
		ss << in.devId() << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double): Device Lookup Failed for Variable!"
				<< " devId=" << in.devId()
				<< " tag=" << tag
				<< " varId=" << in.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoublePkt> vup;
	vup.reset(new ADARA::VariableDoublePkt(in));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableStringPkt &in,
		uint32_t tag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(in.devId(), tag);

	if (!mapped_dev) {
		/* Rate-limited logging of unable to remap variable? */
		std::string log_info;
		std::stringstream ss;
		ss << in.devId() << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_STR_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(String): Device Lookup Failed for Variable!"
				<< " devId=" << in.devId()
				<< " tag=" << tag
				<< " varId=" << in.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableStringPkt> vup;
	vup.reset(new ADARA::VariableStringPkt(in));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableU32ArrayPkt &in,
		uint32_t tag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(in.devId(), tag);

	if (!mapped_dev) {
		/* Rate-limited logging of variable device lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << in.devId() << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32 Array):"
				<< " Device Lookup Failed for Variable!"
				<< " devId=" << in.devId()
				<< " tag=" << tag
				<< " varId=" << in.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32ArrayPkt> vup;
	vup.reset(new ADARA::VariableU32ArrayPkt(in));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableDoubleArrayPkt &in,
		uint32_t tag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(in.devId(), tag);

	if (!mapped_dev) {
		/* Rate-limited logging of unable to remap variable? */
		std::string log_info;
		std::stringstream ss;
		ss << in.devId() << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double Array):"
				<< " Device Lookup Failed for Variable!"
				<< " devId=" << in.devId()
				<< " tag=" << tag
				<< " varId=" << in.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoubleArrayPkt> vup;
	vup.reset(new ADARA::VariableDoubleArrayPkt(in));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateMappedVariable(uint32_t mapped_dev, uint32_t var,
				       const uint8_t *data, uint32_t size)
{
	/* We need to make a copy of the packet, but can only copy from
	 * another Packet object, so create a wrapper object then copy it.
	 */
	ADARA::Packet pkt(data, sizeof(size));
	PacketSharedPtr copy(new ADARA::Packet(pkt));
	updateVariable(mapped_dev, var, copy, 0);
}

void MetaDataMgr::updateVariable(uint32_t dev, uint32_t var,
				 PacketSharedPtr &in, uint32_t tag)
{
	DeviceMap::iterator it = m_devices.find(dev);

	if (it == m_devices.end()) {
		/* Rate-limited log that we got a variable update without
		 * the corresponding device descriptor.
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_NO_DESC, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable(): Got Variable Without a Descriptor!"
				<< " devId=" << dev
				<< " tag=" << tag
				<< " varId=" << var);
		}
		return;
	}

	if (it->second.m_tag != tag) {
		/* Rate-limited log that we got a variable update with
		 * an incorrect tag (ie, wrong source)
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << tag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_BAD_TAG, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable(): Device Tag Mismatch for Variable!"
				<< " devId=" << dev
				<< " tag=" << tag
				<< " varId=" << var
				<< ", Expected tag=" << it->second.m_tag);
		}
		return;
	}

	/* Remove the old variable value from our map before we add the
	 * update to the stream; this keeps us from writing out the old
	 * value in a prologue if we start a new file on this update.
	 */
	VariableMap &varmap = it->second.m_variables;
	VariableMap::iterator vit = varmap.find(var);

	if (vit != varmap.end())
		varmap.erase(vit);

	StorageManager::addPacket(in->packet(), in->packet_length());
	varmap[var] = in;
}

void MetaDataMgr::onPrologue(void)
{
	DeviceMap::iterator dit, dend = m_devices.end();
	for (dit = m_devices.begin(); dit != dend; ++dit) {
		DeviceVariables &dev = dit->second;
		ADARA::Packet *dev_pkt = dev.m_descriptor.get();

		/* Push out the device descriptor before the variable values */
		StorageManager::addPrologue(dev_pkt->packet(),
					    dev_pkt->packet_length());

		VariableMap &vars = dev.m_variables;
		VariableMap::iterator vit, vend = vars.end();
		for (vit = vars.begin(); vit != vend; ++vit) {
			ADARA::Packet *var_pkt = vit->second.get();
			StorageManager::addPrologue(var_pkt->packet(),
						    var_pkt->packet_length());
		}
	}
}
