#include <stdio.h>
#include <syslog.h>
#include <string>
#include "ComBusSMSMon.h"

SMSRunStatus::SMSRunStatus(unsigned long a_run_num, std::string &a_status,
                struct timespec a_start_time) :
                m_run_num(a_run_num), m_status(a_status),
                m_start_time(a_start_time) 
{}
SMSRunStatus::SMSRunStatus(unsigned long a_run_num, std::string &a_status) :
                m_run_num(a_run_num), m_status(a_status)
{}
   
ComBusSMSMon::ComBusSMSMon(std::string a_beam_sname, 
                           std::string a_facility = std::string("SNS")) :
                    m_combus(0),
                    m_beam_sname(a_beam_sname), 
                    m_facility(a_facility),
                    m_stop(false),
                    m_inqueue(
                          new epicsMessageQueue(100,sizeof(SMSRunStatus *)))
{ }

ComBusSMSMon::~ComBusSMSMon() {
   if (m_comm_thread) {
      m_stop = true;
      m_comm_thread->join();
      delete m_comm_thread;
   }
}

void ComBusSMSMon::sendOriginal ( uint32_t a_run_num,
                     const struct timespec &a_start_time,
                     std::string a_run_state )
{
   SMSRunStatus *outp = new SMSRunStatus(a_run_num, a_run_state, a_start_time);

   if (m_inqueue->trySend(&outp, sizeof(SMSRunStatus *))) 
      syslog( LOG_WARNING, "ComBusSMSMon::SendOriginal() failed" );

}

void ComBusSMSMon::sendUpdate ( uint32_t a_run_num,
                     std::string a_run_state )
{
   SMSRunStatus *outp = new SMSRunStatus(a_run_num, a_run_state);

   if (m_inqueue->trySend(&outp, sizeof(SMSRunStatus *))) 
      syslog( LOG_WARNING, "ComBusSMSMon::SendUpdate() failed" );
}


void
ComBusSMSMon::start( const std::string &a_broker_uri,
                     const std::string &a_broker_user, 
                     const std::string &a_broker_pass)
{
    if ( !m_comm_thread )
    {
        m_broker_uri = a_broker_uri;
        m_broker_user = a_broker_user;
        m_broker_pass = a_broker_pass;
        m_domain = m_facility + "." + m_beam_sname;
        m_stop = false;

        m_comm_thread = new boost::thread( boost::bind( &ComBusSMSMon::commThread, this ));
    }
}

void ComBusSMSMon::commThread() {

   unsigned long hb = 0;
   SMSRunStatus *inpu, *lookup;
   int bytesrec =0;

   syslog( LOG_INFO, "SMS ComBus thread started" );

   if (m_combus) {
      delete m_combus;
      m_combus = 0;
   }
 
   m_combus = new ADARA::ComBus::Connection( m_domain, "SMS", getpid(), 
                                             m_broker_uri, m_broker_user, 
                                             m_broker_pass );
   if (!m_combus->waitForConnect( 5 )) {
      syslog( LOG_WARNING, "SMS ComBus Connection Timeout" );
   }

   while(!m_stop) {
      bytesrec = m_inqueue->receive(&inpu, sizeof(SMSRunStatus *), 1.0);
      if (bytesrec == -1) {
          // Send status every 5 seconds
          if ( !( hb % 5 ))
             m_combus->status( ADARA::ComBus::STATUS_OK );
          ++hb;
          continue;
      }
      if (!inpu) continue;
      if (m_run_dict.count(inpu->m_run_num)) {
         lookup = m_run_dict[inpu->m_run_num];
         lookup->m_status = inpu->m_status;
         delete inpu; inpu = 0;
      } else {
         m_run_dict[inpu->m_run_num] = inpu;
         lookup = inpu;
      }
      
      ADARA::ComBus::SMS::StatusUpdateMsg newmsg(m_facility, m_beam_sname, 
                                          lookup->m_start_time, 
                                          lookup->m_run_num,
                                          lookup->m_status);
      m_combus->broadcast(newmsg);
   }
   syslog( LOG_INFO, "ComBus SMS thread exiting" );
}
         
      
      
      
