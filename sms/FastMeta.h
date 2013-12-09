#ifndef __FAST_META_H
#define __FAST_META_H

#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <stdint.h>
#include <string>
#include <map>

class MetaDataMgr;

class FastMeta {
public:
	FastMeta(boost::shared_ptr<MetaDataMgr> mgr)
		: m_meta(mgr), m_numDevs(0) {}

	void addDevices(const boost::property_tree::ptree &conf);

	bool validVariable(uint32_t pixel) {
		/* Our variables are indexed by the type and device ID,
		 * which are the upper 15 bits of the pixel.
		 */
		return !!m_vars.count(pixel & ~0xffff);
	}

	void sendUpdate(uint64_t pulse_id, uint32_t pixel, uint32_t tof);

private:
	struct Variable {
		uint32_t	m_devId;
		uint32_t	m_varId;
		bool		m_persist;
	};

	typedef std::map<uint32_t, Variable> VarMap;

	void addDevice(const std::string &name,
		       const boost::property_tree::ptree &info);

	boost::shared_ptr<MetaDataMgr> m_meta;
	VarMap 		m_vars;
	uint32_t	m_numDevs;
};

#endif /* __FAST_META_H */
