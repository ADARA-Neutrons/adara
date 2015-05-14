#include <stdio.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <string>
#include "Logging.h"
#include "ComBusSMSMon.h"
#include "SMSControl.h"

static LoggerPtr logger( Logger::getLogger("SMS.ComBus") );

std::string ComBusSMSMon::m_domain;
std::string ComBusSMSMon::m_broker_uri;
std::string ComBusSMSMon::m_broker_user;
std::string ComBusSMSMon::m_broker_pass;

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

void ComBusSMSMon::config(const boost::property_tree::ptree &conf) {

	m_domain = conf.get<std::string>("storage.domain", "SNS.TEST");
	m_broker_uri = conf.get<std::string>("storage.broker_uri", "localhost");
	m_broker_user = conf.get<std::string>("storage.broker_user", "DAS");
	m_broker_pass = conf.get<std::string>("storage.broker_pass", "fish");
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
ComBusSMSMon::start(void)
{
	if ( !m_comm_thread )
	{
		SMSControl *ctrl = SMSControl::getInstance();
		if (!ctrl) {
			throw std::logic_error(
	       		"uninitialized SMSControl obj for ComBusSMSMon!");
		}
		std::string prefix(ctrl->getBeamlineId());
		prefix += ":SMS";
		prefix += ":Combus";

		SOCKET newfd = eventfd(1, EFD_NONBLOCK);
		if (newfd <= 0) { 
			throw std::logic_error(
	       		"uninitialized restart fd ComBusSMSMon!");
		}
		m_pvRestartCombus = boost::shared_ptr<smsMTBoolPV>(new 
			smsMTBoolPV( prefix + ":RestartCombus", newfd));
		m_pvDomain = boost::shared_ptr<smsMTStrPV>( new
			smsMTStrPV(prefix + ":Domain"));
		m_pvBrokerUri = boost::shared_ptr<smsMTStrPV>( new
			smsMTStrPV(prefix + ":BrokerUri"));
		m_pvBrokerUser = boost::shared_ptr<smsMTStrPV>( new
			smsMTStrPV(prefix + ":BrokerUser"));
		m_pvBrokerPass = boost::shared_ptr<smsMTStrPV>( new
			smsMTStrPV(prefix + ":BrokerPass"));

		ctrl->addPV(m_pvRestartCombus);
		ctrl->addPV(m_pvDomain);
		ctrl->addPV(m_pvBrokerUri);
		ctrl->addPV(m_pvBrokerUser);
		ctrl->addPV(m_pvBrokerPass);

 		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
  		m_pvDomain->update(m_domain, &now);
		m_pvBrokerUri->update(m_broker_uri, &now);
		m_pvBrokerUser->update(m_broker_user, &now);
		m_pvBrokerPass->update(m_broker_pass, &now);
	
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

void ComBusSMSMon::reOpenComm()
{
	m_combus->setConnection(m_domain, m_broker_uri, m_broker_user, 
				m_broker_pass);

	if ( !m_combus->waitForConnect( 5 ) )
	{
		ERROR( "SMS ComBus Reconnection Timeout"
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user );
	}
	else
	{
		INFO( "SMS ComBus Reconnection Succeeded"
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user );
	}
}

void ComBusSMSMon::commThread()
{
	unsigned long hb = 0;
	SMSRunStatus *inpu, *lookup;
	int bytesrec = 0;
	struct timespec now;

	INFO( "SMS ComBus thread started" );

	while (!m_stop) {
		if (!m_combus) {
			m_domain = m_pvDomain->value();
			m_broker_uri = m_pvBrokerUri->value();
  			m_broker_user = m_pvBrokerUser->value();
			m_broker_pass = m_pvBrokerPass->value();
			openComm();
			continue; 
		}
		m_restart_combus = m_pvRestartCombus->value();
 		if (m_restart_combus) {
			m_domain = m_pvDomain->value();
			m_broker_uri = m_pvBrokerUri->value();
  			m_broker_user = m_pvBrokerUser->value();
			m_broker_pass = m_pvBrokerPass->value();
 			reOpenComm();
			clock_gettime(CLOCK_REALTIME, &now);
			m_pvRestartCombus->mtUpdate(0, &now);
			m_restart_combus = 0;
			continue;
		}
		bytesrec = m_inqueue->receive( &inpu, sizeof(SMSRunStatus *), 
		1.0 );
		if ( bytesrec == -1 || !inpu ) {
			hb++;
			if (hb > 5) {
				m_combus->status( ADARA::ComBus::STATUS_OK );
				hb = 0;
			}
			continue;
		}
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
	}

	INFO( "ComBus SMS thread exiting" );
}

