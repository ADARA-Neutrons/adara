#ifndef __METADATAMGR_H
#define __METADATAMGR_H

#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>
#include <boost/smart_ptr.hpp>
#include <map>
#include <set>

#include <stdint.h>

#include "ADARA.h"
#include "ADARAPackets.h"

struct timespec;

class MetaDataMgr : public boost::noncopyable {
public:
	MetaDataMgr();
	~MetaDataMgr();

	void dropTag(uint32_t tag);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &, uint32_t);
	void updateValue(const ADARA::VariableU32Pkt &in, uint32_t tag);
	void updateValue(const ADARA::VariableDoublePkt &in, uint32_t tag);
	void updateValue(const ADARA::VariableStringPkt &in, uint32_t tag);

	/* addFastMetaDDP() and updateMappedVariable() require the use of
	 * the remapped device identifier from allocDev() -- they do not
	 * handle the remapping for the user.
	 */
	void addFastMetaDDP(const struct timespec &ts, uint32_t mapped_dev,
			    const std::string &ddp);
	void updateMappedVariable(uint32_t mapped_dev, uint32_t var,
				  const uint8_t *data, uint32_t size);

	/* Allocate a unique output device identifier for a given input
	 * source's device.
	 */
	uint32_t allocDev(uint32_t dev, uint32_t tag);

private:
	typedef boost::shared_ptr<ADARA::Packet> PacketSharedPtr;
	typedef std::map<uint32_t, PacketSharedPtr> VariableMap;

	struct DeviceVariables {
		uint32_t	m_tag;
		PacketSharedPtr	m_descriptor;
		VariableMap	m_variables;
	};

	typedef std::map<uint32_t, DeviceVariables> DeviceMap;

	DeviceMap m_devices;
	boost::signals::connection m_connection;
	std::map<uint64_t, uint32_t> m_devIdMap;
	std::set<uint32_t> m_activeDevId;
	uint32_t m_nextDevId;

	void upstreamDisconnected(VariableMap &vars);

	uint32_t remapDevice(uint32_t dev, uint32_t tag);
	void updateVariable(uint32_t dev, uint32_t var,
			    PacketSharedPtr &in, uint32_t tag);
	void onPrologue(void);
};

#endif /* __METADATAMGR_H */
