#ifndef __METADATAMGR_H
#define __METADATAMGR_H

#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <boost/smart_ptr.hpp>
#include <string>
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

	void dropSourceTag(uint32_t srcTag);

	void updateDescriptor(const ADARA::DeviceDescriptorPkt &inPkt,
			uint32_t srcTag);

	void updateValue(const ADARA::VariableU32Pkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::VariableDoublePkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::VariableStringPkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::VariableU32ArrayPkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::VariableDoubleArrayPkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::MultVariableU32Pkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::MultVariableDoublePkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::MultVariableStringPkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::MultVariableU32ArrayPkt &inPkt,
			uint32_t srcTag);
	void updateValue(const ADARA::MultVariableDoubleArrayPkt &inPkt,
			uint32_t srcTag);

	void extractLastValue(ADARA::MultVariableU32Pkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableDoublePkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableStringPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableU32ArrayPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);
	void extractLastValue(ADARA::MultVariableDoubleArrayPkt inPkt,
			ADARA::PacketSharedPtr &outPkt);

	/* addFastMetaDDP() and updateMappedVariable() require the use of
	 * the remapped device identifier from allocDev() -- they do not
	 * handle the remapping for the user.
	 */
	void addFastMetaDDP(const struct timespec &ts, uint32_t mapped_dev,
			    const std::string &ddp);
	void updateMappedVariable(uint32_t mapped_dev, uint32_t varId,
				  const uint8_t *data, uint32_t size);

	/* Allocate a unique output device identifier for a given input
	 * source's device.
	 */
	uint32_t allocDev(uint32_t dev, uint32_t srcTag,
			bool do_log, bool &reconnected);

private:
	typedef std::map<uint32_t, ADARA::PacketSharedPtr> VariablePktMap;

	struct DeviceVariables {
		uint32_t	m_devId;
		uint32_t	m_srcTag;
		ADARA::PacketSharedPtr	m_descriptorPkt;
		VariablePktMap	m_variablePkts;
	};

	typedef std::map<uint32_t, DeviceVariables> DeviceMap;

	DeviceMap m_devices;
	DeviceMap m_oldDevices;
	boost::signals2::connection m_connection;
	std::map<uint64_t, uint32_t> m_devIdMap;
	std::map<uint64_t, uint32_t> m_oldDevIdMap;
	std::set<uint32_t> m_activeDevId;
	uint32_t m_nextMappedDevId;

	void upstreamDisconnected(VariablePktMap &varPkts);

	uint32_t lookupMappedDeviceId(uint32_t dev, uint32_t srcTag);
	uint32_t lookupOldMappedDeviceId(uint32_t dev, uint32_t srcTag,
			bool &reconnected);

	void updateVariable(uint32_t dev, uint32_t varId,
			    ADARA::PacketSharedPtr &inPkt, uint32_t srcTag);

	void onPrologue( bool capture_last );
};

#endif /* __METADATAMGR_H */
