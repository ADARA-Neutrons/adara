#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <boost/thread/thread_time.hpp>
#include <boost/thread/locks.hpp>
#include "ComBusTransMon.h"
#include "StreamParser.h"
#include "combus/ComBusMessages.h"
#include "combus/STSMessages.h"

using namespace std;


/** \brief ComBusMon constructor
  *
  */
ComBusTransMon::ComBusTransMon() :
    m_stream_parser(0), m_combus(0), m_comm_thread(0), m_stop(false),
    m_send_to_workflow(false), m_terminal_msg(0)
{
    char buf[256];
    buf[255] = 0;
    if ( gethostname( buf, 255 ) == -1 )
        strcpy( buf, "unknown" );

    m_host = buf;
}


/** \brief ComBusMon destructor
  *
  * If the comm thread is running, the destructor will initiate a controlled
  * shutdown which will cause the destructor to block until any queued
  * messages are transmitted.
  */
ComBusTransMon::~ComBusTransMon()
{
    if ( m_comm_thread )
    {
        // Request comm thread to exit
        m_stop = true;

        // Wait forever for thread to exit
        // (there may be a queued message waiting to be sent)
        m_comm_thread->join();

        // CLean-up
        delete m_comm_thread;
    }

    syslog( LOG_INFO, "[%i] Disconnecting ComBus...", g_pid );

    if ( m_combus )
        delete m_combus;

    if ( m_terminal_msg )
        delete m_terminal_msg;

    syslog( LOG_INFO, "[%i] ComBusTransMon exiting", g_pid );
}


/** \brief Starts the background ComBus comm thread
  *
  * This method starts the background communication thread which will begin
  * the delayed process of connecting to the ComBus service. The ComBus
  * service can ONLY be joined after sufficient information has been
  * extracted from the ADARA stream.
  */
void
ComBusTransMon::start( STS::StreamParser &a_stream_parser,
        const std::string &a_broker_uri,
        const std::string &a_broker_user,
        const std::string &a_broker_pass,
        const std::string &a_domain )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( !m_comm_thread )
    {
        m_stream_parser = &a_stream_parser;
        m_broker_uri = a_broker_uri;
        m_broker_user = a_broker_user;
        m_broker_pass = a_broker_pass;
        m_stop = false;
        m_domain = a_domain;

        m_comm_thread = new boost::thread(
            boost::bind( &ComBusTransMon::commThread, this ) );
    }
}


/** \brief Posts a translation success message
  *
  * This method posts a translation success message to the comm queue. Only
  * one translation status call should be made (success or failure) at the
  * end of translation. IF the Nexus file has been moved to the appropriate
  * catalogging path, then theis method will also send a "data ready"
  * message to the workflow manager queue.
  */
void
ComBusTransMon::success( bool a_moved, const string &a_nexus_file )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    // Can only set one terminal message
    if ( m_terminal_msg )
        return;

    m_terminal_msg = new ADARA::ComBus::STS::TranslationFinishedMsg(
        m_stream_parser->getFacilityName(),
        m_stream_parser->getBeamShortName(),
        m_stream_parser->getProposalID(),
        m_stream_parser->getRunNumber(),
        a_nexus_file, m_host );

    m_send_to_workflow = a_moved;

    syslog( LOG_INFO,
        "[%i] ComBus translation success message sent", g_pid );
}


/** \brief Posts a translation failure message
  *
  * This method posts a translation failed message to the comm queue. Only
  * one translation status call should be made (success or failure) at the
  * end of translation.
  */
void
ComBusTransMon::failure( STS::TranslationStatusCode a_code,
        const std::string a_reason )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    // Can only set one terminal message
    if ( m_terminal_msg )
        return;

    m_terminal_msg = new ADARA::ComBus::STS::TranslationFailedMsg(
        m_stream_parser->getBeamShortName(),
        m_stream_parser->getProposalID(),
        m_stream_parser->getRunNumber(),
        a_code, a_reason, m_host );

    syslog( LOG_INFO,
        "[%i] ComBus translation failed message sent", g_pid );
}


/** \brief Background ComBus communication thread
  *
  * The commThread method performs all tasks related to maintain the
  * connection to ComBus, sending any queued ComBus messages, and emitting
  * heartbeats at appropriate intervals. The connection to ComBus can only
  * be attempted after the facility name and beam name are received in the
  * ADARA stream. Any messages received until then will be queued and sent
  * when the connection is established.
  * If the connection is not established prior to the client calling the
  * stop() method, the connection will be attempted for a short timeout
  * period before the comm thread gives up and exits without sending
  * any messages.
  */
void
ComBusTransMon::commThread()
{
    unsigned long hb = 0;

    syslog( LOG_INFO, "[%i] ComBus thread started", g_pid );

    while ( 1 )
    {
        sleep( 1 );

        boost::lock_guard<boost::mutex> lock(m_api_mutex);

        if ( m_combus )
        {
            if ( m_terminal_msg )
            {
                m_combus->broadcast( *m_terminal_msg );

                // Only notify workflow if file was moved to catalog path
                if ( m_send_to_workflow )
                    m_combus->postWorkflow( *m_terminal_msg );

                // Automatically stop comm thread when terminal message sent
                break;
            }
            else
            {
                // Send status every 5 seconds
                if ( !( hb % 5 ) )
                    m_combus->status( ADARA::ComBus::STATUS_OK );
            }

            ++hb;
        }
        else
        {
            // infoReady() call is thread safe
            if ( m_stream_parser->infoReady() )
            {
                // After infoReady() returns true,
                // it is safe to access run information
                if ( m_domain.empty() )
                {
                    m_domain = m_stream_parser->getFacilityName()
                        + "." + m_stream_parser->getBeamShortName();
                }

                // STS ComBus Log Info Prefix
                stringstream ss_info;
                ss_info << "[" << g_pid << "] STS ComBus";

                // STS ComBus Log Error Prefix
                stringstream ss_err;
                ss_err << "[" << g_pid << "] STS Error ComBus";

                // Make STS ComBus Connection...
                m_combus = new ADARA::ComBus::Connection( m_domain, "STS",
                    getpid(), m_broker_uri, m_broker_user, m_broker_pass,
                    ss_info.str(), ss_err.str() );

                if ( !m_combus->waitForConnect( 5 ) ) { 
                    syslog( LOG_WARNING,
                    "[%i] STS Error: %s for Domain %s to URI %s as User %s",
                        g_pid, "ComBus Connection Timeout",
                        m_domain.c_str(), m_broker_uri.c_str(),
                        m_broker_user.c_str() );
                }
                else {
                    syslog( LOG_INFO,
                        "[%i] STS %s for Domain %s to URI %s as User %s",
                        g_pid, "Connected to ComBus", m_domain.c_str(),
                        m_broker_uri.c_str(), m_broker_user.c_str() );
                }
                usleep(30000); // give syslog a chance...

                // Send Translation Started message
                ADARA::ComBus::STS::TranslationStartedMsg msg(
                    m_stream_parser->getRunNumber(), m_host );

                m_combus->broadcast( msg );
            }
        }

        // Has client ask comm thread to stop?
        if ( m_stop )
        {
            // If combus or term msg is still null, a valid stream was
            // never received
            // Probably due to a more fundamental problem, so quit now
            if ( !m_combus || !m_terminal_msg )
                break;
        }
    }

    syslog( LOG_INFO, "[%i] ComBus thread exiting", g_pid );
}

// vim: expandtab

