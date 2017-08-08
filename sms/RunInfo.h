#ifndef __RUNINFO_H
#define __RUNINFO_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>

#include <stdint.h>

#include <string>
#include <map>

#include "StorageManager.h"
#include "SMSControl.h"

class RunUserInfoPV;
class RunInfoResetPV;
class RunInfoFloat64PV;
class RunInfoPV;
class smsBooleanPV;

class RunInfo : boost::noncopyable {
public:
	typedef boost::shared_ptr<RunUserInfoPV> RunUserInfoPVSharedPtr;
	typedef boost::shared_ptr<RunInfoResetPV> RunInfoResetPVSharedPtr;
	typedef boost::shared_ptr<RunInfoFloat64PV> RunInfoFloat64PVSharedPtr;
	typedef boost::shared_ptr<RunInfoPV> RunInfoPVSharedPtr;
	typedef std::map<std::string, RunInfoPVSharedPtr> RunInfoMap;

	RunInfo(const std::string &facility, const std::string &beamline,
		SMSControl *ctrl, bool sendSampleInRunInfo);
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

	void checkPacket() {
		if (m_runNumber) {
			if (generatePacket())
				StorageManager::addPacket(m_packet, m_packetSize);
		}
	}

private:
	std::string m_facility;
	std::string m_beamline;
	SMSControl *m_ctrl;

	bool m_sendSampleInRunInfo;

	RunInfoPVSharedPtr m_propId;

	RunInfoMap m_required;
	RunInfoMap m_optional;
	RunInfoMap m_sample;

	RunInfoResetPVSharedPtr m_resetPV;
	RunUserInfoPVSharedPtr m_userPV;

	boost::shared_ptr<smsBooleanPV> m_sendSampleInRunInfoPV;

	RunInfoPVSharedPtr m_massPV;
	RunInfoFloat64PVSharedPtr m_massFloat64PV;

	RunInfoPVSharedPtr m_densityPV;
	RunInfoFloat64PVSharedPtr m_densityFloat64PV;

	RunInfoPVSharedPtr m_containerIdPV;
	RunInfoPVSharedPtr m_containerNamePV;
	RunInfoPVSharedPtr m_componentPV;

	RunInfoPVSharedPtr m_heightInContainerPV;
	RunInfoFloat64PVSharedPtr m_heightInContainerFloat64PV;

	RunInfoPVSharedPtr m_interiorDiameterPV;
	RunInfoFloat64PVSharedPtr m_interiorDiameterFloat64PV;

	RunInfoPVSharedPtr m_interiorHeightPV;
	RunInfoFloat64PVSharedPtr m_interiorHeightFloat64PV;

	RunInfoPVSharedPtr m_interiorWidthPV;
	RunInfoFloat64PVSharedPtr m_interiorWidthFloat64PV;

	RunInfoPVSharedPtr m_interiorDepthPV;
	RunInfoFloat64PVSharedPtr m_interiorDepthFloat64PV;

	RunInfoPVSharedPtr m_outerDiameterPV;
	RunInfoFloat64PVSharedPtr m_outerDiameterFloat64PV;

	RunInfoPVSharedPtr m_outerHeightPV;
	RunInfoFloat64PVSharedPtr m_outerHeightFloat64PV;

	RunInfoPVSharedPtr m_outerWidthPV;
	RunInfoFloat64PVSharedPtr m_outerWidthFloat64PV;

	RunInfoPVSharedPtr m_outerDepthPV;
	RunInfoFloat64PVSharedPtr m_outerDepthFloat64PV;

	RunInfoPVSharedPtr m_volumeCubicPV;
	RunInfoFloat64PVSharedPtr m_volumeCubicFloat64PV;

	uint32_t m_runNumber;
	bool m_packetValid;
	uint8_t *m_packet;
	uint32_t m_packetSize;

	boost::signals2::connection m_connection;

	RunInfoPVSharedPtr addPV(const std::string &prefix, const char *pv_name,
		   const char *xml_name, RunInfoMap &map);
	bool generatePacket(void);
	void onPrologue(void);
};

#endif /* __RUNINFO_H */
