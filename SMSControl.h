#ifndef __SMS_CONTROL_H
#define __SMS_CONTROL_H

#include <boost/smart_ptr.hpp>
#include <string>
#include <map>

#include <casdef.h>

class smsRunNumberPV;
class smsRecordingPV;

class SMSControl : public caServer {
public:
	SMSControl(const std::string &beamline);
	~SMSControl();

	void show(unsigned level) const;

	pvExistReturn pvExistTest(const casCtx &, const caNetAddr &,
				  const char *pv_name);
	pvAttachReturn pvAttach(const casCtx &ctx, const char *pv_name);


private:
	std::map<std::string, boost::shared_ptr<casPV> > m_pv_map;
	uint32_t m_nextRunNumber;
	bool m_recording;
	boost::shared_ptr<smsRunNumberPV> m_pvRunNumber;
	boost::shared_ptr<smsRecordingPV> m_pvRecording;

	void addPV(boost::shared_ptr<casPV> pv);

	pvExistReturn pvExistTest(const casCtx &, const char *pv_name);

	bool setRecording(bool val);

	friend class smsRecordingPV;
};

#endif /* __SMSCAS_H */
