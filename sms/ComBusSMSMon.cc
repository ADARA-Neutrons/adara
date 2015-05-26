#include <stdio.h>
#include <stdint.h>
#include <string>
#include "Logging.h"
#include "ComBusSMSMon.h"

static LoggerPtr logger( Logger::getLogger("SMS.ComBus") );

SMSRunStatus::SMSRunStatus( unsigned long a_run_num, std::string &a_reason,
		struct timespec a_start_time ) :
	m_run_num(a_run_num), m_reason(a_reason),
	m_start_time(a_start_time)
{}

SMSRunStatus::SMSRunStatus( unsigned long a_run_num,
		std::string &a_reason ) :
	m_run_num(a_run_num), m_reason(a_reason)
{}

bool SMSRunStatus::hasTime()
{
	return m_start_time.tv_sec || m_start_time.tv_nsec;
}

ComBusSMSMon::ComBusSMSMon( std::string a_beam_sname,
		std::string a_facility = std::string("SNS") ) :
	m_combus(0),
	m_beam_sname(a_beam_sname),
	m_facility(a_facility),
	m_comm_thread(0),
	m_stop(false),
	m_inqueue( new epicsMessageQueue( 100, sizeof(SMSRunStatus *) ) )
{ }

ComBusSMSMon::~ComBusSMSMon()
{
	if ( m_comm_thread ) {
		m_stop = true;
		m_comm_thread->join();
		delete m_comm_thread;
		m_comm_thread = 0;
	}
}

void ComBusSMSMon::sendOriginal( uint32_t a_run_num,
		std::string a_run_state,
		const struct timespec &a_start_time )
{
	SMSRunStatus *outp =
		new SMSRunStatus( a_run_num, a_run_state, a_start_time );

	if ( m_inqueue->trySend( &outp, sizeof(SMSRunStatus *) ) )
		ERROR( "ComBusSMSMon::SendOriginal() failed" );
}

void ComBusSMSMon::sendUpdate( uint32_t a_run_num,
		std::string a_run_state )
{
	SMSRunStatus *outp = new SMSRunStatus( a_run_num, a_run_state );

	if ( m_inqueue->trySend( &outp, sizeof(SMSRunStatus *) ) )
		ERROR( "ComBusSMSMon::SendUpdate() failed" );
}

void
ComBusSMSMon::start( const std::string &a_domain,
					const std::string &a_broker_uri,
					const std::string &a_broker_user,
					const std::string &a_broker_pass )
{
	if ( !m_comm_thread )
	{
		m_domain = a_domain;
		m_broker_uri = a_broker_uri;
		m_broker_user = a_broker_user;
		m_broker_pass = a_broker_pass;

		m_stop = false;

		m_comm_thread = new boost::thread(
			boost::bind( &ComBusSMSMon::commThread, this ) );
	}
}

void ComBusSMSMon::openComm()
{
	m_combus = new ADARA::ComBus::Connection( m_domain, "SMS", getpid(),
		m_broker_uri, m_broker_user, m_broker_pass );

	if ( !m_combus->waitForConnect( 5 ) )
	{
		ERROR( "SMS ComBus Connection Timeout"
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user );
	}
	else
	{
		INFO( "SMS ComBus Connection Succeeded"
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user );
	}
}

void ComBusSMSMon::commThread()
{
	unsigned long hb = 0;
	SMSRunStatus *inpu, *lookup;
	int bytesrec = 0;

	INFO( "SMS ComBus thread started" );

	// Loop 1
	while ( !m_stop )
	{
		bytesrec = m_inqueue->receive( &inpu, sizeof(SMSRunStatus *), 1.0 );

		// Loop 2
		while ( !m_stop )
		{
			if ( bytesrec == -1 || !inpu )
			{
				if ( !m_combus ) {
					openComm();
				}
				else {
					// Send status (heartbeat) every 5 seconds
					if ( !( hb % 5 ) ) {
						m_combus->status( ADARA::ComBus::STATUS_OK );
					}
				}
				++hb;
				break;	// Repeat Loop 1
			}

			if (!m_combus) {
				openComm();
				continue; 	// Repeat Loop 2
			}
			else
			{
				if ( m_run_dict.count( inpu->m_run_num ) ) {
					lookup = m_run_dict[ inpu->m_run_num ];
					lookup->m_reason = inpu->m_reason;
					if ( inpu->hasTime() ) {
						lookup->m_start_time = inpu->m_start_time;
					}
					delete inpu; inpu = 0;
				}
				else {
					m_run_dict[ inpu->m_run_num ] = inpu;
					lookup = inpu;
				}

				ADARA::ComBus::SMS::StatusUpdateMsg newmsg( m_facility,
					m_beam_sname,
					lookup->m_start_time,
					lookup->m_run_num,
					lookup->m_reason );

				if ( !m_combus->broadcast( newmsg ) )
				{
					WARN( "SMS ComBus run " << lookup->m_run_num
						<< " status <" << lookup->m_reason
						<< "> send failed"
						<< " to URI " << m_broker_uri
						<< " as User " << m_broker_user );
				}
				else
				{
					INFO( "SMS ComBus run " << lookup->m_run_num
						<< " status <" << lookup->m_reason << "> sent"
						<< " to URI " << m_broker_uri
						<< " as User " << m_broker_user );
				}

				break;	// Repeat Loop 1
			}
		}
	}

	INFO( "ComBus SMS thread exiting" );
}

