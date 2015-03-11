#ifndef __BEAMMONITORCONFIG_H
#define __BEAMMONITORCONFIG_H

#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>

class BeamMonitorInfo;

class BeamMonitorConfig : boost::noncopyable {
public:
	BeamMonitorConfig(const boost::property_tree::ptree & conf);
	~BeamMonitorConfig();

private:
	std::vector<BeamMonitorInfo *> bmonInfo;

	boost::signals2::connection m_connection;

	void onPrologue(void);
};

#endif /* __BEAMMONITORCONFIG_H */
