#ifndef __RUNINFO_H
#define __RUNINFO_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>

#include "SMSControl.h"
#include "SMSControlPV.h"

class RunInfoResetPV;
class RunInfoPV;

class RunInfo : boost::noncopyable {
public:
	typedef boost::shared_ptr<RunInfoPV> RunInfoPVSharedPtr;
	typedef std::map<std::string, RunInfoPVSharedPtr> RunInfoMap;

	RunInfo(const std::string &beamline, SMSControl *sms);
	~RunInfo();

	bool valid(void);
	void reset(void);

	void setRunNumber(uint32_t run) {
		m_packetValid = false;
		m_runNumber = run;
	}

	void invalidateCache(void) { m_packetValid = false; }

private:
	std::string m_beamline;
	RunInfoMap m_required;
	RunInfoMap m_optional;
	RunInfoMap m_sample;
	SMSControl::PVSharedPtr m_resetPV;

	uint32_t m_runNumber;
	bool m_packetValid;
	uint8_t *m_packet;
	uint32_t m_packetSize;

	void addPV(const std::string &prefix, const char *name,
		   RunInfoMap &map, SMSControl *sms);
	void generatePacket(void);
	void onPrologue(void);
};

#endif /* __RUNINFO_H */
