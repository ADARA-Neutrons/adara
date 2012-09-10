#ifndef __RUNINFO_H
#define __RUNINFO_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signal.hpp>

#include "SMSControl.h"
#include "SMSControlPV.h"

class RunUserInfoPV;
class RunInfoResetPV;
class RunInfoPV;

class RunInfo : boost::noncopyable {
public:
	typedef boost::shared_ptr<RunUserInfoPV> RunUserInfoPVSharedPtr;
	typedef boost::shared_ptr<RunInfoResetPV> RunInfoResetPVSharedPtr;
	typedef boost::shared_ptr<RunInfoPV> RunInfoPVSharedPtr;
	typedef std::map<std::string, RunInfoPVSharedPtr> RunInfoMap;

	RunInfo(const std::string &beamline, SMSControl *sms);
	~RunInfo();

	void lock(void);
	void unlock(void);

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
	RunInfoResetPVSharedPtr m_resetPV;
	RunUserInfoPVSharedPtr m_userPV;

	uint32_t m_runNumber;
	bool m_packetValid;
	uint8_t *m_packet;
	uint32_t m_packetSize;

	boost::signals::connection m_connection;

	void addPV(const std::string &prefix, const char *pv_name,
		   const char *xml_name, RunInfoMap &map, SMSControl *sms);
	void generatePacket(void);
	void onPrologue(void);
};

#endif /* __RUNINFO_H */
