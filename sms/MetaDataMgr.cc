#include <boost/bind.hpp>

#include "MetaDataMgr.h"
#include "StorageManager.h"

#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.MetaDataMgr"));

MetaDataMgr::MetaDataMgr()
{
	m_connection = StorageManager::onPrologue(
				boost::bind(&MetaDataMgr::onPrologue, this));
}

MetaDataMgr::~MetaDataMgr()
{
	m_connection.disconnect();
}

void MetaDataMgr::dropTag(uint32_t tag)
{
	DeviceMap::iterator dit, dend = m_devices.end();
	for (dit = m_devices.begin(); dit != dend; ) {
		// TODO should we send a severity update for these variables?
		if (dit->second.m_tag == tag)
			m_devices.erase(dit++);
		else
			++dit;
	}
}

void MetaDataMgr::updateDescriptor(const ADARA::DeviceDescriptorPkt &in,
				   uint32_t tag)
{
	DeviceMap::iterator it = m_devices.find(in.devId());

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

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 */
	StorageManager::addPacket(in.packet(), in.packet_length());
	m_devices[in.devId()].m_descriptor.reset(new ADARA::Packet(in));
	m_devices[in.devId()].m_tag = tag;
}

void MetaDataMgr::updateVariable(uint32_t dev, uint32_t var,
				 const ADARA::Packet &in, uint32_t tag)
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

	StorageManager::addPacket(in.packet(), in.packet_length());
	varmap[var].reset(new ADARA::Packet(in));
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
