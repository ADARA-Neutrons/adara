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

void MetaDataMgr::upstreamDisconnected(
		MetaDataMgr::VariablePktMap &varPkts)
{
	/* For each variable, modify the packet to indicate that we
	 * lost the upstream connection and feed it into the stream.
	 */
	VariablePktMap::iterator vit, vend = varPkts.end();
	uint32_t len, pktSize = 0;
	uint32_t *fields = NULL;
	uint8_t *pkt = NULL;

	std::stringstream var_log_ss;

	for (vit = varPkts.begin(); vit != vend; vit++) {

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

void MetaDataMgr::dropSourceTag(uint32_t srcTag)
{
	/* Rate-limited log that we got disconnected from a DataSource (srcTag)
	 * and are Dropping All Associated Devices...
	 */
	std::string log_info;
	std::stringstream ss;
	ss << srcTag;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_DROP_DEVICES_FOR_TAG, ss.str(),
			600, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG(log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "dropSourceTag():"
			<< " Dropping Devices for Data Source, srcTag=" << srcTag);
	}

	DeviceMap::iterator dit, dend = m_devices.end();
	bool dropped = false;

	for (dit = m_devices.begin(); dit != dend; ) {
		DeviceVariables &dev = dit->second;

		if (dev.m_srcTag == srcTag) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropSourceTag(): Sending Upstream Disconnected for"
				<< " srcTag=" << dev.m_srcTag
				<< " devId=" << dev.m_devId
				<< " mapped_dev=" << dit->first);
			upstreamDisconnected(dev.m_variablePkts);
			m_devices.erase(dit++);
			dropped = true;
		} else
			++dit;
	}

	if (dropped)
		StorageManager::notify();
	else {
		/* Rate-limited log that there were No Associated Devices to Drop
		 * from the disconnected DataSource (srcTag)...
		 */
		// re-use "ss" from above...
		log_info.clear();
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_NO_DEVICES_TO_DROP, ss.str(),
				600, 3, 10, log_info ) ) {
			DEBUG(log_info
				<< "dropSourceTag():"
				<< " Warning No Devices Found! srcTag=" << srcTag);
		}
	}

	/* Remove the mapped device ids for this srcTag */
	std::map<uint64_t, uint32_t>::iterator it, end;
	end = m_devIdMap.end();
	for (it = m_devIdMap.begin(); it != end; ) {
		if (it->first >> 32 == srcTag) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropSourceTag(): Removing Mapped Device"
				<< " srcTag=" << ( it->first >> 32 )
				<< " devId=" << ( it->first & 0xffff )
				<< " mapped_dev=" << it->second);
			m_activeDevId.erase(it->second);
			m_devIdMap.erase(it++);
		} else
			it++;
	}
}

uint32_t MetaDataMgr::lookupMappedDeviceId(uint32_t dev, uint32_t srcTag)
{
	uint64_t key = ((uint64_t) srcTag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator val;

	val = m_devIdMap.find(key);
	if ( val == m_devIdMap.end() )
		return 0;
	return val->second;
}

uint32_t MetaDataMgr::allocDev(uint32_t dev, uint32_t srcTag, bool do_log)
{
	uint32_t mapped_dev;
	if ( (mapped_dev = lookupMappedDeviceId( dev, srcTag )) ) {
		if ( do_log ) {
			DEBUG("Device Lookup returned mapped_dev=" << mapped_dev);
		}
		return mapped_dev;
	}

	while ( m_activeDevId.count( m_nextDevId ) )
		m_nextDevId++;

	m_activeDevId.insert( m_nextDevId );

	uint64_t key = ((uint64_t) srcTag << 32) | dev;
	m_devIdMap[key] = m_nextDevId;

	if ( do_log ) {
		DEBUG("New Input Device"
			<< " srcTag=" << srcTag
			<< " devId=" << dev
			<< " Mapped to SMS Output Device"
			<< " mapped_dev=" << m_nextDevId);
	}

	return m_nextDevId++;
}

void MetaDataMgr::updateDescriptor(const ADARA::DeviceDescriptorPkt &inPkt,
				   uint32_t srcTag)
{
	/* Rate-limited log that we received a DeviceDescriptorPkt. */
	std::string log_info;
	std::stringstream ss;
	ss << inPkt.devId() << "/" << srcTag;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_UPDATE_DESCRIPTOR, ss.str(),
			60, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG(log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Update Descriptor"
			<< " srcTag=" << srcTag
			<< " devId=" << inPkt.devId() );
		do_log = true; // link this rate-limited log to other related logs
	}

	uint32_t mapped_dev = allocDev(inPkt.devId(), srcTag, do_log);

	DeviceMap::iterator it = m_devices.find(mapped_dev);
	if (it != m_devices.end()) {
		/* Device exists already, ignore it if it didn't change. */
		DeviceVariables &dev = it->second;
		ADARA::Packet *dev_pkt = dev.m_descriptorPkt.get();

		if (it->second.m_srcTag != srcTag) {
			/* Rate-limited log that we got a Descriptor from
			 * an Incorrect Source Tag (i.e., wrong/unexpected Data Source)
			 * [*** Can this ever actually happen? The Lookup Key Includes
			 * the Source Tag!]
			 */
			std::string log_info;
			std::stringstream ss;
			ss << srcTag;
			if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
					RLL_DESC_INCORRECT_TAG, ss.str(),
					10, 3, 50, log_info ) ) {
				SMSControl *ctrl = SMSControl::getInstance();
				DEBUG(log_info
					<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Got Descriptor from Incorrect Source Tag: "
					<< it->second.m_srcTag << " != " << srcTag
					<< " (devId=" << inPkt.devId() << ")");
			}
			return;
		}

		if (dev_pkt->packet_length() == inPkt.packet_length() &&
				!memcmp(dev_pkt->payload(), inPkt.payload(),
					dev_pkt->payload_length())) {
			if ( do_log ) {
				DEBUG("Inbound Descriptor is Identical");
			}
			return;
		}

		/* XXX It changed on us; need to flush out old variables
		 * (don't notify, we'll be pushing the new descriptor in a
		 * moment).
		 */
		if ( do_log ) {
			DEBUG("Updating Existing Descriptor");
		}
		m_devices.erase(it);
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::DeviceDescriptorPkt> ddp;
	ddp.reset(new ADARA::DeviceDescriptorPkt(inPkt));
	ddp->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(ddp);

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 */
	StorageManager::addPacket(pkt->packet(), pkt->packet_length());
	m_devices[mapped_dev].m_descriptorPkt = pkt;
	m_devices[mapped_dev].m_devId = inPkt.devId();
	m_devices[mapped_dev].m_srcTag = srcTag;
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
			<< " (srcTag=0 devId=-1)");
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
				<< "addFastMetaDDP(): Tried to Add Existing (Mapped) Device"
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
	m_devices[mapped_dev].m_descriptorPkt.reset(new ADARA::Packet(wrapped));
	m_devices[mapped_dev].m_devId = -1; // FastMetaDDP...!
	m_devices[mapped_dev].m_srcTag = 0;
}

void MetaDataMgr::updateValue(const ADARA::VariableU32Pkt &inPkt,
		uint32_t srcTag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(inPkt.devId(), srcTag);

	if (!mapped_dev) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32): Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32Pkt> vup;
	vup.reset(new ADARA::VariableU32Pkt(inPkt));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, inPkt.varId(), pkt, srcTag);
}

void MetaDataMgr::updateValue(const ADARA::VariableDoublePkt &inPkt,
		uint32_t srcTag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(inPkt.devId(), srcTag);

	if (!mapped_dev) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double): Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoublePkt> vup;
	vup.reset(new ADARA::VariableDoublePkt(inPkt));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, inPkt.varId(), pkt, srcTag);
}

void MetaDataMgr::updateValue(const ADARA::VariableStringPkt &inPkt,
		uint32_t srcTag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(inPkt.devId(), srcTag);

	if (!mapped_dev) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_STR_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(String): Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableStringPkt> vup;
	vup.reset(new ADARA::VariableStringPkt(inPkt));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, inPkt.varId(), pkt, srcTag);
}

void MetaDataMgr::updateValue(const ADARA::VariableU32ArrayPkt &inPkt,
		uint32_t srcTag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(inPkt.devId(), srcTag);

	if (!mapped_dev) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32 Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32ArrayPkt> vup;
	vup.reset(new ADARA::VariableU32ArrayPkt(inPkt));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, inPkt.varId(), pkt, srcTag);
}

void MetaDataMgr::updateValue(const ADARA::VariableDoubleArrayPkt &inPkt,
		uint32_t srcTag)
{
	uint32_t mapped_dev = lookupMappedDeviceId(inPkt.devId(), srcTag);

	if (!mapped_dev) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId());
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoubleArrayPkt> vup;
	vup.reset(new ADARA::VariableDoubleArrayPkt(inPkt));
	vup->remapDeviceId(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, inPkt.varId(), pkt, srcTag);
}

void MetaDataMgr::updateMappedVariable(uint32_t mapped_dev, uint32_t varId,
				       const uint8_t *data, uint32_t size)
{
	/* We need to make a copy of the packet, but can only copy from
	 * another Packet object, so create a wrapper object then copy it.
	 */
	ADARA::Packet pkt(data, sizeof(size));
	PacketSharedPtr copy(new ADARA::Packet(pkt));
	updateVariable(mapped_dev, varId, copy, 0);
}

void MetaDataMgr::updateVariable(uint32_t dev, uint32_t varId,
				 PacketSharedPtr &inPkt, uint32_t srcTag)
{
	DeviceMap::iterator it = m_devices.find(dev);

	if (it == m_devices.end()) {
		/* Rate-limited log that we got a variable update without
		 * the corresponding device descriptor.
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_NO_DESC, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable(): Got Variable Without a Descriptor!"
				<< " srcTag=" << srcTag
				<< " devId=" << dev
				<< " varId=" << varId);
		}
		return;
	}

	if ( it->second.m_srcTag != srcTag ) {
		/* Rate-limited log that we got a Variable Update with
		 * an Incorrect srcTag (ie, Wrong Data Source)
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_BAD_TAG, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR(log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable():"
				<< " Device Source Tag Mismatch for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << dev
				<< " varId=" << varId
				<< ", Expected srcTag=" << it->second.m_srcTag);
		}
		return;
	}

	/* Remove the old variable value from our map before we add the
	 * update to the stream; this keeps us from writing out the old
	 * value in a prologue if we start a new file on this update.
	 */
	VariablePktMap &varPktMap = it->second.m_variablePkts;
	VariablePktMap::iterator vit = varPktMap.find(varId);

	if (vit != varPktMap.end())
		varPktMap.erase(vit);

	StorageManager::addPacket(inPkt->packet(), inPkt->packet_length());
	varPktMap[varId] = inPkt;
}

void MetaDataMgr::onPrologue(void)
{
	DeviceMap::iterator dit, dend = m_devices.end();
	for (dit = m_devices.begin(); dit != dend; ++dit) {
		DeviceVariables &dev = dit->second;
		ADARA::Packet *dev_pkt = dev.m_descriptorPkt.get();

		/* Push out the device descriptor before the variable values */
		StorageManager::addPrologue(dev_pkt->packet(),
					    dev_pkt->packet_length());

		VariablePktMap &varPkts = dev.m_variablePkts;
		VariablePktMap::iterator vit, vend = varPkts.end();
		for (vit = varPkts.begin(); vit != vend; ++vit) {
			ADARA::Packet *var_pkt = vit->second.get();
			StorageManager::addPrologue(var_pkt->packet(),
						    var_pkt->packet_length());
		}
	}
}
