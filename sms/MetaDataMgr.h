#ifndef __METADATAMGR_H
#define __METADATAMGR_H

#include <boost/noncopyable.hpp>
#include <boost/signal.hpp>
#include <boost/smart_ptr.hpp>
#include <map>

#include <stdint.h>

#include "ADARA.h"
#include "ADARAPackets.h"

class MetaDataMgr : public boost::noncopyable {
public:
	MetaDataMgr();
	~MetaDataMgr();

	void dropTag(uint32_t tag);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &, uint32_t);
	void updateValue(const ADARA::VariableU32Pkt &in, uint32_t tag) {
		updateVariable(in.devId(), in.varId(), in, tag);
	}
	void updateValue(const ADARA::VariableDoublePkt &in, uint32_t tag) {
		updateVariable(in.devId(), in.varId(), in, tag);
	}
	void updateValue(const ADARA::VariableStringPkt &in, uint32_t tag) {
		updateVariable(in.devId(), in.varId(), in, tag);
	}

	void addFastMetaDDP(const ADARA::Packet &, uint32_t, uint32_t);

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

	void upstreamDisconnected(VariableMap &vars);

	void updateVariable(uint32_t dev, uint32_t var,
			    const ADARA::Packet &in, uint32_t tag);
	void onPrologue(void);
};

#endif /* __METADATAMGR_H */
