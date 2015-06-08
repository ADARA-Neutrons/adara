#ifndef COMBUSSMSMON_H
#define COMBUSSMSMON_H

#include <boost/property_tree/ptree.hpp>
#include <stdint.h>
#include <string>
#include <time.h>
#include <map>
#include <epicsMessageQueue.h>
#include <cadef.h>
#include <epicsEvent.h>
#include "combus/ComBus.h"
#include "combus/SMSMessages.h"
#include "SMSControlPV.h"

class SMSRunStatus
{
public:
	// Only needs to be sent once
	SMSRunStatus( unsigned long a_run_num, std::string & a_reason,
		struct timespec a_start_time );

	// The usual item
	SMSRunStatus( unsigned long a_run_num, std::string & a_reason );

	// Check if we have time
	bool hasTime();

	// Data is public for use by ComBusSMSMon
	unsigned long m_run_num;
	std::string m_reason;
	struct timespec m_start_time;
};

class ComBusSMSMon
{
public:
	ComBusSMSMon( std::string a_beam_sname, std::string a_facility );
	~ComBusSMSMon();

	static void config(const boost::property_tree::ptree &conf);

	void start(void);

	// sendOriginal() is called at startup scan time,
	// or when a new run is started.
	void sendOriginal( uint32_t a_run_num,
		std::string a_run_state,
		const struct timespec & a_start_time );

	// sendUpdate() sends subsequent developments.
	// Must be preceded by a sendOriginal for a given run.
	void sendUpdate( uint32_t a_run_num, std::string a_run_state );

	static void restartCB( event_handler_args);

private:
	void openComm();
	void reOpenComm();
	void commThread();

	std::map<uint32_t, SMSRunStatus *> m_run_dict;

	ADARA::ComBus::Connection *m_combus;

	std::string		m_beam_sname;
	std::string		m_facility;

	static std::string		m_domain;
	static std::string		m_broker_uri;
	static std::string		m_broker_user;
	static std::string		m_broker_pass;

	uint16_t		m_restart_combus;

	epicsEvent *m_restartEvent;

	boost::shared_ptr<smsBooleanPV> m_pvRestartCombus;
	boost::shared_ptr<smsStringPV> m_pvDomain;
	boost::shared_ptr<smsStringPV> m_pvBrokerURI;
	boost::shared_ptr<smsStringPV> m_pvBrokerUser;
	boost::shared_ptr<smsStringPV> m_pvBrokerPass;

	boost::thread	*m_comm_thread;

	bool			m_stop;

	epicsMessageQueue	*m_inqueue;
};

#endif

