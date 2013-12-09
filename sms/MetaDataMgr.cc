#include <boost/bind.hpp>

#include "MetaDataMgr.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.MetaDataMgr"));

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

	for (vit = vars.begin(); vit != vend; vit++) {
		PacketSharedPtr orig = vit->second;

		len = orig->packet_length();
		if (len > pktSize) {
			delete pkt;
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
	}

	delete pkt;
}

void MetaDataMgr::dropTag(uint32_t tag)
{
	DeviceMap::iterator dit, dend = m_devices.end();
	bool dropped = false;

	for (dit = m_devices.begin(); dit != dend; ) {
		DeviceVariables &dev = dit->second;

		if (dev.m_tag == tag) {
			upstreamDisconnected(dev.m_variables);
			m_devices.erase(dit++);
			dropped = true;
		} else
			++dit;
	}

	if (dropped)
		StorageManager::notify();

	/* Remove the mapped device ids for this tag */
	std::map<uint64_t, uint32_t>::iterator it, end;
	end = m_devIdMap.end();
	for (it = m_devIdMap.begin(); it != end; ) {
		if (it->first >> 32 == tag) {
			m_activeDevId.erase(it->second);
			m_devIdMap.erase(it++);
		} else
			it++;
	}
}

uint32_t MetaDataMgr::allocDev(uint32_t dev, uint32_t tag)
{
	uint64_t key = ((uint64_t) tag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator val;

	val = m_devIdMap.find(key);
	if (val != m_devIdMap.end())
		return val->second;

	while (m_activeDevId.count(m_nextDevId))
		m_nextDevId++;

	m_activeDevId.insert(m_nextDevId);
	m_devIdMap[key] = m_nextDevId;
	return m_nextDevId++;
}

uint32_t MetaDataMgr::remapDevice(uint32_t dev, uint32_t tag)
{
	uint64_t key = ((uint64_t) tag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator val;

	val = m_devIdMap.find(key);
	if (val == m_devIdMap.end())
		return 0;
	return val->second;
}

void MetaDataMgr::updateDescriptor(const ADARA::DeviceDescriptorPkt &in,
				   uint32_t tag)
{
	uint32_t mapped_dev = remapDevice(in.devId(), tag);

	if (!mapped_dev)
		mapped_dev = allocDev(in.devId(), tag);

	DeviceMap::iterator it = m_devices.find(mapped_dev);
	if (it != m_devices.end()) {
		/* Device exists already, ignore it if it didn't change. */
		DeviceVariables &dev = it->second;
		ADARA::Packet *dev_pkt = dev.m_descriptor.get();

		if (it->second.m_tag != tag) {
			/* XXX ratelimited log that we got a descriptor from
			 * an incorrect tag (ie, wrong source)
			 */
			return;
		}

		if (dev_pkt->packet_length() == in.packet_length() &&
				!memcmp(dev_pkt->payload(), in.payload(),
					dev_pkt->payload_length())) {
			DEBUG("Inbound descriptor was indentical");
			return;
		}

		/* XXX It changed on us; need to flush out old variables
		 * (don't notify, we'll be pushing the new descriptor in a
		 * moment).
		 */
		m_devices.erase(it);
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::DeviceDescriptorPkt> ddp;
	ddp.reset(new ADARA::DeviceDescriptorPkt(in));
	ddp->remapDevice(mapped_dev);
	PacketSharedPtr pkt(ddp);

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 */
	StorageManager::addPacket(pkt->packet(), pkt->packet_length());
	m_devices[mapped_dev].m_descriptor = pkt;
	m_devices[mapped_dev].m_tag = tag;
}

void MetaDataMgr::addFastMetaDDP(const timespec &ts, uint32_t mapped_dev,
				 const std::string &ddp)
{
	DeviceMap::iterator it = m_devices.find(mapped_dev);
	if (it != m_devices.end()) {
		ERROR("addFastMetaDDP() adding existing (mapped) device 0x"
			<< std::hex << mapped_dev);
		return;
	}

	uint32_t size = (ddp.size() + 3) & ~3;
	size += 2 * sizeof(uint32_t) + sizeof(ADARA::Header);

	/* Build the base DDP packet */
	uint32_t pkt[size / sizeof(uint32_t)];
	memset(pkt, 0, sizeof(pkt));
	pkt[0] = size - sizeof(ADARA::Header);
	pkt[1] = ADARA::PacketType::DEVICE_DESC_V0;
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
	m_devices[mapped_dev].m_tag = 0;
}

void MetaDataMgr::updateValue(const ADARA::VariableU32Pkt &in, uint32_t tag)
{
	uint32_t mapped_dev = remapDevice(in.devId(), tag);

	if (!mapped_dev) {
		ERROR("Unable to remap variable 0x" << std::hex << in.devId()
			<< ":" << std::hex << in.varId()
			<< ":" << std::hex << tag);
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32Pkt> vup;
	vup.reset(new ADARA::VariableU32Pkt(in));
	vup->remapDevice(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableDoublePkt &in, uint32_t tag)
{
	uint32_t mapped_dev = remapDevice(in.devId(), tag);

	if (!mapped_dev) {
		ERROR("Unable to remap variable 0x" << std::hex << in.devId()
			<< ":" << std::hex << in.varId()
			<< ":" << std::hex << tag);
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoublePkt> vup;
	vup.reset(new ADARA::VariableDoublePkt(in));
	vup->remapDevice(mapped_dev);
	PacketSharedPtr pkt(vup);

	updateVariable(mapped_dev, in.varId(), pkt, tag);
}

void MetaDataMgr::updateValue(const ADARA::VariableStringPkt &in, uint32_t tag)
{
	uint32_t mapped_dev = remapDevice(in.devId(), tag);

	if (!mapped_dev) {
		ERROR("Unable to remap variable 0x" << std::hex << in.devId()
			<< ":" << std::hex << in.varId()
			<< ":" << std::hex << tag);
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableStringPkt> vup;
	vup.reset(new ADARA::VariableStringPkt(in));
	vup->remapDevice(mapped_dev);
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
		/* XXX ratelimited log that we got a variable update without
		 * the corresponding device descriptor.
		 */
		ERROR("Got variable 0x" << std::hex << dev << ":"
					<< std::hex << var << ":"
					<< std::hex << tag
					<< " without a descriptor");
		return;
	}

	if (it->second.m_tag != tag) {
		/* XXX ratelimited log that we got a variable update with
		 * an incorrect tag (ie, wrong source)
		 */
		ERROR("Got variable 0x" << std::hex << dev << ":"
					<< std::hex << var << ":"
					<< std::hex << tag
					<< " but expected tag "
					<< std::hex << it->second.m_tag);
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
