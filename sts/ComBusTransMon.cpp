#include <boost/thread/thread_time.hpp>
#include <boost/thread/locks.hpp>
#include "ComBusTransMon.h"
#include "StreamParser.h"
#include "combus/ComBusMessages.h"
#include "combus/STSMessages.h"
#include <syslog.h>

using namespace std;


/** \brief ComBusMon constructor
  *
  */
ComBusTransMon::ComBusTransMon()
    : m_stream_parser(0), m_combus(0), m_comm_thread(0), m_stop(false), m_send_to_workflow(false), m_terminal_msg(0)
{
    openlog( "sts", 0, LOG_DAEMON );
    syslog( LOG_INFO, "STS service started." );
}


/** \brief ComBusMon destructor
  *
  * If the comm thread is running, the destructor will initiate a controlled
  * shutdown which will cause the destructor to block until any queued messages
  * are transmitted.
  */
ComBusTransMon::~ComBusTransMon()
{
    if ( m_comm_thread )
    {
        // Request comm thread to exit
        m_stop = true;

        // Wait forever for thread to exit (there may be a queued message waiting to be sent)
        m_comm_thread->join();

        // CLean-up
        delete m_comm_thread;
    }

    delete m_combus;
    delete m_terminal_msg;
}


/** \brief Starts the background ComBus comm thread
  *
  * This method starts the background communication thread which will begin
  * the delyaed process of connecting to the ComBus service. The ComBus service
  * can ONLY be joined after sufficient information has bee extracted from the
  * ADARA stream.
  */
void
ComBusTransMon::start( STS::StreamParser &a_stream_parser, const std::string &a_broker_uri,
                       const std::string &a_broker_user, const std::string &a_broker_pass,
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

        m_comm_thread = new boost::thread( boost::bind( &ComBusTransMon::commThread, this ));
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
    syslog( LOG_INFO, "STS success on file %s", a_nexus_file.c_str() );

    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    // Can only set one terminal message
    if ( m_terminal_msg )
        return;

    m_terminal_msg = new ADARA::ComBus::STS::TranslationFinishedMsg( m_stream_parser->getFacilityName(),
        m_stream_parser->getBeamShortName(), m_stream_parser->getProposalID(),
        m_stream_parser->getRunNumber(), a_nexus_file );

    m_send_to_workflow = a_moved;
}


/** \brief Posts a translation failure message
  *
  * This method posts a translation failed message to the comm queue. Only
  * one translation status call should be made (success or failure) at the
  * end of translation.
  */
void
ComBusTransMon::failure( STS::TranslationStatusCode a_code, const std::string a_reason )
{
    syslog( LOG_INFO, "STS failed for %s %s Run %lu (%s).", m_stream_parser->getBeamShortName().c_str(),
     m_stream_parser->getProposalID().c_str(), m_stream_parser->getRunNumber(), a_reason.c_str() );

    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    // Can only set one terminal message
    if ( m_terminal_msg )
        return;

    m_terminal_msg = new ADARA::ComBus::STS::TranslationFailedMsg( m_stream_parser->getBeamShortName(),
        m_stream_parser->getProposalID(), m_stream_parser->getRunNumber(), a_code, a_reason );
}


/** \brief Background ComBus communication thread
  *
  * The commThread method performs all tasks related to maintain the connection
  * to ComBus, sending any queued ComBus messages, and emitting heartbeats at
  * appropriate intervals. The connection to ComBus can only be attempted after
  * the facility name and beam name are received in the ADARA stream. Any messages
  * received until then will be queued and sent when the connection is established.
  * If the connection is not established prior to the client calling the stop()
  * method, the connection will be attempted for a short timeout period before the
  * comm thread gives up and exits without sending any messages.
  */
void
ComBusTransMon::commThread()
{
    unsigned long hb = 0;

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
                if ( !( hb % 5 ))
                    m_combus->status( ADARA::ComBus::STATUS_OK );
            }

            ++hb;
        }
        else
        {
            // infoReady() call is thread safe
            if ( m_stream_parser->infoReady() )
            {
                // After infoReady() returns true, it is safe to access run information
                if ( m_domain.empty())
                    m_domain = m_stream_parser->getFacilityName() + "." + m_stream_parser->getBeamShortName();

                syslog( LOG_INFO, "Got stream info. domain = %s", m_domain.c_str() );

                m_combus = new ADARA::ComBus::Connection( m_domain, "STS", getpid(), m_broker_uri, m_broker_user, m_broker_pass );
                m_combus->waitForConnect( 5 );

                // Send Translation Started message
                ADARA::ComBus::STS::TranslationStartedMsg msg( m_stream_parser->getRunNumber() );

                m_combus->broadcast( msg );
            }
        }

        // Has client ask comm thread to stop?
        if ( m_stop )
        {
            syslog( LOG_INFO, "Aborting" );

            // If combus or term msg is still null, a valid stream was never received
            // Probably due to a more fundamental problem, so quit now
            if ( !m_combus || !m_terminal_msg )
                break;
        }
    }
}
