#ifndef COMBUSSMSMON_H
#define COMBUSSMSMON_H

#include <stdint.h>
#include <string>
#include <time.h>
#include <map>
#include <epicsMessageQueue.h>
#include "combus/ComBus.h"
#include "combus/SMSMessages.h"

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

	void start( const std::string & a_domain,
		const std::string & a_broker_uri,
		const std::string & a_broker_user,
		const std::string & a_broker_pass );

	// sendOriginal() is called at startup scan time,
	// or when a new run is started.
	void sendOriginal( uint32_t a_run_num,
		std::string a_run_state,
		const struct timespec & a_start_time );

	// sendUpdate() sends subsequent developments.
	// Must be preceded by a sendOriginal for a given run.
	void sendUpdate( uint32_t a_run_num, std::string a_run_state );

private:
	void openComm();
	void commThread();

	std::map<uint32_t, SMSRunStatus *> m_run_dict;

	ADARA::ComBus::Connection *m_combus;

	std::string		m_beam_sname;
	std::string		m_facility;
	std::string		m_domain;
	std::string		m_broker_uri;
	std::string		m_broker_user;
	std::string		m_broker_pass;

	boost::thread	*m_comm_thread;

	bool			m_stop;

	epicsMessageQueue	*m_inqueue;
};

#endif

