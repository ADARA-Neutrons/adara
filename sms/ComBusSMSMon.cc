#include <stdio.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <string>
#include <boost/bind.hpp>

#include <cadef.h>

#include "Logging.h"
#include "ComBusSMSMon.h"
#include "SMSControl.h"

static LoggerPtr logger( Logger::getLogger("SMS.ComBusSMSMon") );

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

ComBusSMSMon::ComBusSMSMon( std::string a_beam_sname,
		std::string a_facility = std::string("SNS") ) :
	m_combus(0),
	m_beam_sname(a_beam_sname),
	m_facility(a_facility),
	m_comm_thread(0),
	m_stop(false)
{
	// Install Channel Access Exception Handler in commThread() now... :-D

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
		ERROR("ComBusSMSMon::SendOriginal() Unknown Exception"
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
}

std::string ComBusSMSMon::m_domain;
std::string ComBusSMSMon::m_broker_uri;
std::string ComBusSMSMon::m_broker_user;
std::string ComBusSMSMon::m_broker_pass;
uint16_t ComBusSMSMon::m_restart_combus;

void ComBusSMSMon::config(const boost::property_tree::ptree &conf)
{
	m_domain = conf.get<std::string>("combus.domain", "SNS.TEST");
	m_broker_uri = conf.get<std::string>("combus.broker_uri", "localhost");
	m_broker_user = conf.get<std::string>("combus.broker_user", "DAS");
	m_broker_pass = conf.get<std::string>("combus.broker_pass", "fish");

	// Apply default protocol & port if not set
	m_broker_uri = ADARA::ComBus::Connection::checkBrokerURI(
		m_broker_uri );

	// If base path is specified, ensure it ends with a '.' character
	m_domain = ADARA::ComBus::Connection::checkDomain( m_domain );
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
		std::string prefix(ctrl->getBeamlineId());
		prefix += ":SMS";
		prefix += ":Combus";

		m_pvRestartCombus = boost::shared_ptr<smsBooleanPV>(new
			smsBooleanPV( prefix + ":Restart"));
		m_pvDomain = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":Domain"));
		m_pvBrokerURI = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerURI"));
		m_pvBrokerUser = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerUser"));
		m_pvBrokerPass = boost::shared_ptr<smsStringPV>(new
			smsStringPV(prefix + ":BrokerPass"));

		ctrl->addPV(m_pvRestartCombus);
		ctrl->addPV(m_pvDomain);
		ctrl->addPV(m_pvBrokerURI);
		ctrl->addPV(m_pvBrokerUser);
		ctrl->addPV(m_pvBrokerPass);

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		m_pvDomain->update(m_domain, &now);
		m_pvBrokerURI->update(m_broker_uri, &now);
		m_pvBrokerUser->update(m_broker_user, &now);
		m_pvBrokerPass->update(m_broker_pass, &now);

		m_comm_thread = new boost::thread(
			boost::bind( &ComBusSMSMon::commThread, this ) );
	}
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

void ComBusSMSMon::reOpenComm()
{
	DEBUG("ComBusSMSMon::reOpenComm(): Re-Opening Connection"
		<< " for Domain " << m_domain
		<< " to URI " << m_broker_uri
		<< " as User " << m_broker_user);

	try
	{
		m_combus->setConnection(m_domain, m_broker_uri, m_broker_user,
			m_broker_pass);

		if ( !m_combus->waitForConnect( 5 ) )
		{
			ERROR("SMS ComBus Reconnection Timeout"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
		}
		else
		{
			INFO("SMS ComBus Reconnection Succeeded"
				<< " for Domain " << m_domain
				<< " to URI " << m_broker_uri
				<< " as User " << m_broker_user);
		}
	}
	catch ( std::exception &e )
	{
		ERROR("ComBusSMSMon::reOpenComm() Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user
			<< " - " << e.what());
	}
	catch (...)
	{
		ERROR("ComBusSMSMon::reOpenComm() Unknown Exception"
			<< " for Domain " << m_domain
			<< " to URI " << m_broker_uri
			<< " as User " << m_broker_user);
	}
}

void ComBusSMSMon::restartCallback(struct event_handler_args args) {
	if (args.status != ECA_NORMAL) {
		WARN("Restart Callback status: " << ca_message(args.status));
		return;
	}
	if (args.type == DBR_SHORT && *((uint16_t *)args.dbr)) {
		*((uint16_t *)args.usr) = 1;
	}
}

void ca_exception_handler( struct exception_handler_args args )
{
	const char *pName = ( args.chid ) ? ca_name( args.chid ) : "(Unknown)";

	ERROR("ComBusSMSMon::ca_exception_handler(): Caught EPICS Exception!"
		<< " Context=[" << args.ctx << "]"
		<< " - with Request ChannelId=[" << pName << "]"
		<< " Operation=" << args.op
		<< " DataType=[" << dbr_type_to_text( args.type ) << "]"
		<< " Count=" << args.count
		<< " [Continuing...!]");
}

void ComBusSMSMon::commThread()
{
	unsigned long hb = 0;
	SMSRunStatus *inpu = 0, *lookup = 0;
	int bytesrec = 0;
	chid uri_chid, restart_chid, user_chid, domain_chid, pass_chid;
	char inbuf[smsStringPV::MAX_LENGTH];
	unsigned long spam_count = 0;

	INFO("SMS ComBus thread started");

	// The CA_ADDR_LIST is set to the local host "broadcast address",
	// although local host would work. The broadcast address can be more
	// robust if there are multiple iocs on the same host. ALL the PVs I
	// am looking at are there, always.  Never changes. (CA used as IPC.)
	// The AUTO_ADDR_LIST one tells the client not to bother checking
	// the real Ethernet ports.  It'll find what it wants without that.
	// Its a minor optimization; I figured if I'm messing around with
	// the environment variables anyway I might as well do it to some
	// level of completeness. - Carl A. Lionberger, Tue, 9 Jun 2015
	// (Note to Jeeem: *Don't Delete These!* Lol, Oops... ;-b)
	setenv("EPICS_CA_ADDR_LIST","127.255.255.255", 1);
	setenv("EPICS_CA_AUTO_ADDR_LIST","NO", 1);

	// explicitly specify single threaded context. CA being used as ipc.
	SMSSEVCHK(ca_context_create(ca_disable_preemptive_callback),
		"create ca context");

	// Install Non-Default (And Non-Terminating!) Channel Access
	// Exception Handler for the SMS...! ;-D
	SMSSEVCHK(ca_add_exception_event(ca_exception_handler, 0),
		"add EPICS exception handler");

	SMSSEVCHK(ca_create_channel(m_pvDomain->getName(), 0, 0, 0,
			&domain_chid),
		"create domain channel");
	SMSSEVCHK(ca_create_channel(m_pvBrokerURI->getName(), 0, 0, 0,
			&uri_chid),
		"create broker uri channel");
	SMSSEVCHK(ca_create_channel(m_pvBrokerUser->getName(), 0, 0, 0,
			&user_chid),
		"create broker user channel");
	SMSSEVCHK(ca_create_channel(m_pvBrokerPass->getName(), 0, 0, 0,
			&pass_chid),
		"create broker passwd channel");
	SMSSEVCHK(ca_create_channel(m_pvRestartCombus->getName(), 0, 0, 0, 
			&restart_chid),
		"create combus restart channel");

	SMSSEVCHK(ca_create_subscription(DBR_SHORT, 1, restart_chid, DBE_VALUE,
			restartCallback, &m_restart_combus, 0), 
			"monitor combus restart");

	SMSSEVCHK(ca_pend_io(1.0), "Combus thread monitor");

	while (!m_stop) {

		if (!m_combus) {
			// only happens at beginning. Take values from config
			openComm();
			continue;
		}

		ca_poll();	// Where restartCallback() gets called

		if (m_restart_combus) {

			INFO("Combus Restart True");

			/*
			 * These are pended separately so that the same input
			 * buffer can be reused for each one.
			 */
			SMSSEVCHK(ca_array_get(DBR_CHAR, smsStringPV::MAX_LENGTH,
				domain_chid, inbuf),
				"get combus domain");
			SMSSEVCHK(ca_pend_io(1.0), "pend get combus domain");
			m_domain = inbuf;
			INFO("Combus Domain = " << m_domain);

			// If base path is specified ensure it ends with a '.' character
			m_domain = ADARA::ComBus::Connection::checkDomain( m_domain );
			INFO("Combus Domain (Checked) = " << m_domain);

			SMSSEVCHK(ca_array_get(DBR_CHAR, smsStringPV::MAX_LENGTH,
				uri_chid, inbuf),
				"get combus broker uri");
			SMSSEVCHK(ca_pend_io(1.0), "pend get broker uri");
			m_broker_uri = inbuf;
			INFO("Combus Broker URI = " << m_broker_uri);

			// Apply default protocol & port if not set
			m_broker_uri = ADARA::ComBus::Connection::checkBrokerURI(
				m_broker_uri );
			INFO("Combus Broker URI (Checked) = " << m_broker_uri);

			SMSSEVCHK(ca_array_get(DBR_CHAR, smsStringPV::MAX_LENGTH,
				user_chid, inbuf),
				"get combus broker user");
			SMSSEVCHK(ca_pend_io(1.0), "pend get broker user");
			m_broker_user = inbuf;
			INFO("Combus Broker User = " << m_broker_user);

			SMSSEVCHK(ca_array_get(DBR_CHAR, smsStringPV::MAX_LENGTH,
				pass_chid, inbuf),
				"get combus broker passwd");
			SMSSEVCHK(ca_pend_io(1.0), "pend get broker password");
			m_broker_pass = inbuf;

			reOpenComm();

			m_restart_combus = 0;

			SMSSEVCHK(ca_put(DBR_SHORT, restart_chid,
				&m_restart_combus),
				"reset of combus restart PV");

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

