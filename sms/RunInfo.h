#ifndef __RUNINFO_H
#define __RUNINFO_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>

#include <stdint.h>

#include <string>
#include <map>

#include "SMSControl.h"

class RunUserInfoPV;
class RunInfoResetPV;
class RunInfoFloat64PV;
class RunInfoPV;

class RunInfo : boost::noncopyable {
public:
	typedef boost::shared_ptr<RunUserInfoPV> RunUserInfoPVSharedPtr;
	typedef boost::shared_ptr<RunInfoResetPV> RunInfoResetPVSharedPtr;
	typedef boost::shared_ptr<RunInfoFloat64PV> RunInfoFloat64PVSharedPtr;
	typedef boost::shared_ptr<RunInfoPV> RunInfoPVSharedPtr;
	typedef std::map<std::string, RunInfoPVSharedPtr> RunInfoMap;

	RunInfo(const std::string &beamline, SMSControl *ctrl);
	~RunInfo();

	void lock(void);
	void unlock(void);

	bool valid(std::string &reason);
	void reset(void);

	void setRunNumber(uint32_t run) {
		m_packetValid = false;
		m_runNumber = run;
	}

	std::string getPropId(void);

	void pvChanged(RunInfoPV* pv);

	void invalidateCache(void) { m_packetValid = false; }

private:
	std::string m_beamline;
	SMSControl *m_ctrl;
	RunInfoPVSharedPtr m_propId;
	RunInfoMap m_required;
	RunInfoMap m_optional;
	RunInfoMap m_sample;
	RunInfoResetPVSharedPtr m_resetPV;
	RunUserInfoPVSharedPtr m_userPV;
	RunInfoPVSharedPtr m_massPV;
	RunInfoPVSharedPtr m_densityPV;
	RunInfoFloat64PVSharedPtr m_massFloat64PV;
	RunInfoFloat64PVSharedPtr m_densityFloat64PV;

	uint32_t m_runNumber;
	bool m_packetValid;
	uint8_t *m_packet;
	uint32_t m_packetSize;

	boost::signals2::connection m_connection;

	RunInfoPVSharedPtr addPV(const std::string &prefix, const char *pv_name,
		   const char *xml_name, RunInfoMap &map);
	void generatePacket(void);
	void onPrologue(void);
};

#endif /* __RUNINFO_H */
