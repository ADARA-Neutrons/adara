
#include "Logging.h"

static LoggerPtr logger( Logger::getLogger("SMS.ComBusSMSMon") );

#include <string>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <boost/bind.hpp>

#include "StorageManager.h"
#include "SMSControl.h"
#include "SMSControlPV.h"
#include "ComBusSMSMon.h"
#include "EventFd.h"

SMSRunStatus::SMSRunStatus(
		unsigned long a_run_num, std::string &a_proposal_id,
		std::string &a_reason, struct timespec a_start_time ) :
	m_run_num(a_run_num), m_proposal_id(a_proposal_id),
	m_reason(a_reason), m_start_time(a_start_time)
{}

SMSRunStatus::SMSRunStatus(
		unsigned long a_run_num, std::string &a_proposal_id,
		std::string &a_reason ) :
	m_run_num(a_run_num), m_proposal_id(a_proposal_id),
	m_reason(a_reason)
{
	m_start_time.tv_sec = 0;
	m_start_time.tv_nsec = 0;
}

bool SMSRunStatus::hasTime()
{
	return m_start_time.tv_sec || m_start_time.tv_nsec;
}

EventFd *ComBusSMSMon::m_commRestart;

bool ComBusSMSMon::m_restart_combus;

struct ComBusSMSMon::restart_comm_struct ComBusSMSMon::m_restartCommData;

class RestartComBusPV : public smsBooleanPV {
public:
	RestartComBusPV( const std::string &name, ComBusSMSMon *cbsm ) :
		smsBooleanPV(name), m_comBusSMSMon(cbsm) {}

	void changed(void)
	{
		if ( value() ) {
			m_comBusSMSMon->restartComBus();
		}
	}

private:
	ComBusSMSMon *m_comBusSMSMon;
};

ComBusSMSMon::ComBusSMSMon( std::string a_beam_sname,
		std::string a_facility = std::string("SNS") ) :
	m_combus(0),
	m_beam_sname(a_beam_sname),
	m_facility(a_facility),
	m_comm_thread(0),
	m_stop(false)
{
	// Channel Access Exception Handler Now Installed in commThread()... :-D

	m_commRestart = NULL;

	m_restart_combus = false;

	try
	{
		// Make Sure We Got A Message Queue...!
		m_inqueue = new epicsMessageQueue(100, sizeof(SMSRunStatus *));
		if ( !m_inqueue ) {
			ERROR("ComBusSMSMon(): Failed to Create EPICS Message Queue...!"
				<< " Facility=" << m_facility
				<< " Beamline=" << m_beam_sname
				<< " Count=" << 100
				<< " MaxMsgSize=" << sizeof(SMSRunStatus *));
			return;
		}
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon() Exception"
			<< " Trying to Create EPICS Message Queue...!"
			<< " [" << e.what() << "]"
			<< " Facility=" << m_facility
			<< " Beamline=" << m_beam_sname
			<< " Count=" << 100
			<< " MaxMsgSize=" << sizeof(SMSRunStatus *));
		m_inqueue = 0;   // just to be safe...?
		return;
	}
	catch (...)
	{
		ERROR("ComBusSMSMon() Unknown Exception"
			<< " Trying to Create EPICS Message Queue...!"
			<< " Facility=" << m_facility
			<< " Beamline=" << m_beam_sname
			<< " Count=" << 100
			<< " MaxMsgSize=" << sizeof(SMSRunStatus *));
		m_inqueue = 0;   // just to be safe...?
		return;
	}

	DEBUG("ComBusSMSMon(): Created EPICS Message Queue..."
		<< " Facility=" << m_facility
		<< " Beamline=" << m_beam_sname
		<< " Count=" << 100
		<< " MaxMsgSize=" << sizeof(SMSRunStatus *));

	try
	{
		// Create EventFd for commThread() Restart Communication...
		m_commRestart = new EventFd( true ); // Non-Blocking = true
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon() Exception"
			<< " Trying to Create EventFd Signaler...!"
			<< " [" << e.what() << "]"
			<< " Facility=" << m_facility
			<< " Beamline=" << m_beam_sname);
		if ( m_commRestart ) {
			delete m_commRestart;
			m_commRestart = NULL;
		}
	}
	catch (...)
	{
		ERROR("ComBusSMSMon() Unknown Exception"
			<< " Trying to Create EventFd Signaler...!"
			<< " Facility=" << m_facility
			<< " Beamline=" << m_beam_sname);
		if ( m_commRestart ) {
			delete m_commRestart;
			m_commRestart = NULL;
		}
	}
}

ComBusSMSMon::~ComBusSMSMon()
{
	if ( m_comm_thread ) {
		m_stop = true;
		m_comm_thread->join();
		delete m_comm_thread;
		m_comm_thread = 0;
	}

	if ( m_inqueue ) {
		delete m_inqueue;
		m_inqueue = 0;
	}

	if ( m_commRestart ) {
		delete m_commRestart;
		m_commRestart = NULL;
	}
}

std::string ComBusSMSMon::m_domain;
std::string ComBusSMSMon::m_broker_uri;
std::string ComBusSMSMon::m_broker_user;
std::string ComBusSMSMon::m_broker_pass;

void ComBusSMSMon::config(const boost::property_tree::ptree &conf)
{
	m_domain = conf.get<std::string>("combus.domain", "SNS.TEST");
	m_broker_uri = conf.get<std::string>("combus.broker_uri", "localhost");
	m_broker_user = conf.get<std::string>("combus.broker_user", "DAS");
	m_broker_pass = conf.get<std::string>("combus.broker_pass", "fish");

	// If base path is specified, ensure it ends with a '.' character
	m_domain = ADARA::ComBus::Connection::checkDomain( m_domain );

	// Apply default protocol & port if not set
	m_broker_uri = ADARA::ComBus::Connection::checkBrokerURI(
		m_broker_uri );
}

void ComBusSMSMon::sendOriginal(
		uint32_t a_run_num, std::string a_proposal_id,
		std::string a_run_state,
		const struct timespec &a_start_time )
{
	// Make Sure We Have a Queue...
	if ( !m_inqueue ) {
		ERROR("ComBusSMSMon::SendOriginal()"
			<< " NO QUEUE to Send Message"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
		return;
	}

	SMSRunStatus *outp = new SMSRunStatus( a_run_num, a_proposal_id,
		a_run_state, a_start_time );

	try
	{
		if ( m_inqueue->trySend( &outp, sizeof(SMSRunStatus *) ) )
			ERROR("ComBusSMSMon::SendOriginal() failed"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon::SendOriginal() Exception [" << e.what() << "]"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
	catch (...)
	{
		ERROR("ComBusSMSMon::SendOriginal() Unknown Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
}

void ComBusSMSMon::sendUpdate(
		uint32_t a_run_num, std::string a_proposal_id,
		std::string a_run_state )
{
	// Make Sure We Have a Queue...
	if ( !m_inqueue ) {
		ERROR("ComBusSMSMon::SendUpdate()"
			<< " NO QUEUE to Send Message"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
		return;
	}

	SMSRunStatus *outp = new SMSRunStatus( a_run_num, a_proposal_id,
		a_run_state );

	try
	{
		if ( m_inqueue->trySend( &outp, sizeof(SMSRunStatus *) ) )
			ERROR("ComBusSMSMon::SendUpdate() failed"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon::SendUpdate() Exception [" << e.what() << "]"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
	catch (...)
	{
		ERROR("ComBusSMSMon::SendUpdate() Unknown Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
}

void
ComBusSMSMon::start(void)
{
	DEBUG("ComBusSMSMon::start() Entry");

	if ( !m_comm_thread )
	{
		DEBUG("ComBusSMSMon::start(): Comm Thread Not Found"
			<< " - Initializing...");

		SMSControl *ctrl = SMSControl::getInstance();
		if (!ctrl) {
			throw std::logic_error(
				"uninitialized SMSControl obj for ComBusSMSMon!");
		}
		std::string prefix(ctrl->getPVPrefix());
		prefix += ":Combus";

		m_pvRestartComBus = boost::shared_ptr<RestartComBusPV>(new
			RestartComBusPV( prefix + ":Restart", this ));

		m_pvDomain = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":Domain", /* AutoSave */ true));
		m_pvBrokerURI = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerURI", /* AutoSave */ true));
		m_pvBrokerUser = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerUser", /* AutoSave */ true));
		m_pvBrokerPass = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerPass", /* AutoSave */ true));

		ctrl->addPV(m_pvRestartComBus);

		ctrl->addPV(m_pvDomain);
		ctrl->addPV(m_pvBrokerURI);
		ctrl->addPV(m_pvBrokerUser);
		ctrl->addPV(m_pvBrokerPass);

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		m_pvRestartComBus->update(m_restart_combus, &now);

		m_pvDomain->update(m_domain, &now);
		m_pvBrokerURI->update(m_broker_uri, &now);
		m_pvBrokerUser->update(m_broker_user, &now);
		m_pvBrokerPass->update(m_broker_pass, &now);

		// Restore Any PVs to AutoSaved Config Values...

		struct timespec ts;
		std::string value;

		if ( StorageManager::getAutoSavePV( m_pvDomain->getName(),
				value, ts ) ) {
			m_domain = value;
			m_pvDomain->update(value, &ts);
		}

		if ( StorageManager::getAutoSavePV( m_pvBrokerURI->getName(),
				value, ts ) ) {
			m_broker_uri = value;
			m_pvBrokerURI->update(value, &ts);
		}

		if ( StorageManager::getAutoSavePV( m_pvBrokerUser->getName(),
				value, ts ) ) {
			m_broker_user = value;
			m_pvBrokerUser->update(value, &ts);
		}

		if ( StorageManager::getAutoSavePV( m_pvBrokerPass->getName(),
				value, ts ) ) {
			m_broker_pass = value;
			m_pvBrokerPass->update(value, &ts);
		}

		// Start ComBus Comm Thread...
		m_comm_thread = new boost::thread(
			boost::bind( &ComBusSMSMon::commThread, this ) );
	}
}

void ComBusSMSMon::restartComBus()
{
	// A ComBus Restart has been Requested via the Live Control PV...
	ERROR("restartComBus(): A ComBus Restart has been Requested...");

	// Check Whether Any Previous ComBus Restart has Completed as Yet...?
	if ( m_restart_combus ) {
		ERROR("restartComBus():"
			<< " Warning: A ComBus Restart Is Already/Still In Progress!"
			<< " Ignoring...");
	}

	// Proceed with the Next ComBus Restart...
	else {

		// Update All the ActiveMQ Communication Settings from their PVs
		m_domain = m_pvDomain->value();
		m_broker_uri = m_pvBrokerURI->value();
		m_broker_user = m_pvBrokerUser->value();
		m_broker_pass = m_pvBrokerPass->value();

		// If base path is specified ensure it ends with a '.' character
		m_domain = ADARA::ComBus::Connection::checkDomain( m_domain );
		INFO("ComBus Domain (Checked) = " << m_domain);

		// Apply default protocol & port if not set
		m_broker_uri = ADARA::ComBus::Connection::checkBrokerURI(
			m_broker_uri );
		INFO("ComBus Broker URI (Checked) = " << m_broker_uri);

		// Now Package It All Up for the commThread() to do the Restart.
		m_restartCommData.domain = m_domain;
		m_restartCommData.broker_uri = m_broker_uri;
		m_restartCommData.broker_user = m_broker_user;
		m_restartCommData.broker_pass = m_broker_pass;

		// Send the Restart Struct to commThread() to Trigger the Restart!
		if ( m_commRestart == NULL || !m_commRestart->signal( 1 ) ) {
			ERROR("restartComBus(): Error Signaling via EventFd...!");
		}
	}

	// Reset the ComBus Restart PV, We've Set Things in Motion (or Not!) ;-D
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	m_pvRestartComBus->update(false, &now);
}

void ComBusSMSMon::openComm()
{
	DEBUG("ComBusSMSMon::openComm(): Opening Connection"
		<< " for Domain " << m_domain
		<< " to URI " << m_broker_uri
		<< " as User " << m_broker_user);

	try
	{
		m_combus = new ADARA::ComBus::Connection(
			m_domain, "SMS", getpid(),
			m_broker_uri, m_broker_user, m_broker_pass,
			"SMS ComBus", "ERROR SMS ComBus" );

		if ( !m_combus->waitForConnect( 5 ) )
		{
			ERROR("SMS ComBus Connection Timeout"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
		}
		else
		{
			INFO("SMS ComBus Connection Succeeded"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
		}
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon::openComm() Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user
			<< " - " << e.what());
	}
	catch (...)
	{
		ERROR("ComBusSMSMon::openComm() Unknown Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
}

void ComBusSMSMon::reOpenComm(
		std::string a_domain,
		std::string a_broker_uri,
		std::string a_broker_user,
		std::string a_broker_pass )
{
	DEBUG("ComBusSMSMon::reOpenComm(): Re-Opening Connection"
		<< " for Domain " << a_domain
		<< " to URI " << a_broker_uri
		<< " as User " << a_broker_user);

	try
	{
		m_combus->setConnection(
			a_domain, a_broker_uri, a_broker_user, a_broker_pass );

		if ( !m_combus->waitForConnect( 5 ) )
		{
			ERROR("SMS ComBus Reconnection Timeout"
				<< " for Domain " << a_domain
				<< " to URI " << a_broker_uri
				<< " as User " << a_broker_user);
		}
		else
		{
			INFO("SMS ComBus Reconnection Succeeded"
				<< " for Domain " << a_domain
				<< " to URI " << a_broker_uri
				<< " as User " << a_broker_user);
		}
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon::reOpenComm() Exception"
			<< " for Domain " << a_domain
			<< " to URI " << a_broker_uri
			<< " as User " << a_broker_user
			<< " - " << e.what());
	}
	catch (...)
	{
		ERROR("ComBusSMSMon::reOpenComm() Unknown Exception"
			<< " for Domain " << a_domain
			<< " to URI " << a_broker_uri
			<< " as User " << a_broker_user);
	}
}

void ComBusSMSMon::checkRestart()
{
	uint64_t val;

	if ( m_commRestart == NULL ) {
		ERROR("checkRestart(): NULL CommRestart EventFd...!");
	}

	// Got Something on the EventFd...
	else if ( m_commRestart->read( val ) ) {

		// Got a Restart Request...! :-D
		if ( val == 1 ) {
			ERROR("checkRestart():"
				<< " Got ComBus Restart Request from Main Thread!");
			m_restart_combus = true;
		}

		// Hmmm... Got a _Not_ Restart Request...? ;-D
		else {
			ERROR("checkRestart():"
				<< " Got ComBus _NOT_ Restart Request from Main Thread...?"
				<< " Ignoring...");
		}
	}
}

void ComBusSMSMon::commThread()
{
	unsigned long hb = 0;
	SMSRunStatus *inpu = 0, *lookup = 0;
	int bytesrec = 0;
	unsigned long spam_count = 0;

	INFO("SMS ComBus commThread() started");

	while ( !m_stop ) {

		if ( !m_combus ) {
			// Only happens at beginning. Take values from config.
			openComm();
			continue;
		}

		checkRestart();

		if ( m_restart_combus ) {

			INFO("ComBus Restart True");

			reOpenComm( m_restartCommData.domain,
				m_restartCommData.broker_uri,
				m_restartCommData.broker_user,
				m_restartCommData.broker_pass );

			m_restart_combus = false;

			continue;
		}

		// Make Sure We Have a Queue...
		// (Also Make Sure This Never Spams Us... ;-D)
		if ( !m_inqueue ) {
			if ( !( (spam_count++) % 3600 ) ) {
				ERROR("ComBusSMSMon::commThread()"
					<< " NO QUEUE to Receive Message"
					<< " for Domain " << m_domain
					<< " to URI " << m_broker_uri
					<< " as User " << m_broker_user);
			}
			continue;
		}

		try
		{
			bytesrec = m_inqueue->receive( &inpu,
				sizeof(SMSRunStatus *), 1.0 );
			if ( bytesrec == -1 || !inpu ) {
				hb++;
				if (hb > 5) {
					// Naw... DEBUG("Sending ComBus Heartbeat...");
					m_combus->status( ADARA::ComBus::STATUS_OK );
					hb = 0;
				}
				continue;
			}

			DEBUG("ComBusSMSMon::commThread()"
				<< " Received Next Input from Message Queue...");

			if ( m_run_dict.count( inpu->m_run_num ) ) {
				lookup = m_run_dict[ inpu->m_run_num ];
				lookup->m_proposal_id = inpu->m_proposal_id;
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
				lookup->m_proposal_id,
				lookup->m_reason );

			if ( !m_combus->broadcast( newmsg ) )
			{
				WARN("SMS ComBus run " << lookup->m_run_num
					<< " proposal " << lookup->m_proposal_id
					<< " status <" << lookup->m_reason
					<< "> send failed"
					<< " for Domain " << m_domain
					<< " to URI " << m_broker_uri
					<< " as User " << m_broker_user);
			}
			else
			{
				INFO("SMS ComBus run " << lookup->m_run_num
					<< " proposal " << lookup->m_proposal_id
					<< " status <" << lookup->m_reason << "> sent"
					<< " for Domain " << m_domain
					<< " to URI " << m_broker_uri
					<< " as User " << m_broker_user);
			}
		}
		catch ( std::exception &e )
		{
			ERROR("ComBusSMSMon::commThread() Receive Exception"
				<< " for run " << lookup->m_run_num
				<< " proposal " << lookup->m_proposal_id
				<< " status <" << lookup->m_reason
				<< "> send failed"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user
				<< " - " << e.what());
		}
		catch (...)
		{
			ERROR("ComBusSMSMon::commThread() Unknown Receive Exception"
				<< " for run " << lookup->m_run_num
				<< " proposal " << lookup->m_proposal_id
				<< " status <" << lookup->m_reason
				<< "> send failed"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
		}
	}

	INFO("ComBus SMS thread exiting");
}

