#ifndef __FAST_META_H
#define __FAST_META_H

#define BOOST_BIND_GLOBAL_PLACEHOLDERS // Duh...
#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>

#include <stdint.h>
#include <string>
#include <map>

#include "SMSControl.h"

class MetaDataMgr;

class FastMeta {
public:

	struct Variable {
		uint32_t	m_devId;
		uint32_t	m_varId;
		bool		m_persist;
		bool		m_is_counter;
		std::string	m_name;
	};

	FastMeta(boost::shared_ptr<MetaDataMgr> mgr)
		: m_meta(mgr), m_numDevs(0) {}

	void addDevices(const boost::property_tree::ptree &conf);

	struct Variable *validVariable(uint32_t pixel, uint32_t &key);

	void addGenericDevice(uint32_t pixel, uint32_t &key);

	void sendUpdate(uint64_t pulse_id, uint32_t pixel, uint32_t tof);

	void sendMultUpdate(uint64_t pulse_id, SMSControl::EventVector events);

private:
	typedef std::map<uint32_t, Variable> VarMap;

	void addDevice(const std::string &name,
		       const boost::property_tree::ptree &info);

	boost::shared_ptr<MetaDataMgr> m_meta;
	VarMap 		m_vars;
	uint32_t	m_numDevs;
};

#endif /* __FAST_META_H */
