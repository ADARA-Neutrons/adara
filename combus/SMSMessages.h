#ifndef SMSMESSAGES_H
#define SMSMESSAGES_H

#include <time.h>
#include "ComBusDefs.h"

namespace ADARA {
namespace ComBus {
namespace SMS {


class StatusUpdateMsg : public ADARA::ComBus::TemplMessageBase<MSG_SMS_RUN_STATUSUPDATE,StatusUpdateMsg>
{
public:
    StatusUpdateMsg()
        : m_run_num(0)
    {}

    StatusUpdateMsg( const std::string &a_facility,
            const std::string &a_beam_sname,
            struct timespec a_start_time, 
            unsigned long a_run_num,
            const std::string &a_proposal_id,
            const std::string &a_reason) 
        : m_facility(a_facility), m_beam_sname(a_beam_sname), 
          m_start_time(a_start_time), m_run_num(a_run_num),
          m_proposal_id(a_proposal_id), m_reason(a_reason) 
    {}

    std::string         m_facility;
    std::string         m_beam_sname;
    struct timespec     m_start_time;
    unsigned long       m_run_num;
    std::string         m_proposal_id;
    std::string         m_reason;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        // Note: the JSON property names here were selected for compatibility with the
        // existing workflow manager "data ready" message definition.
        // "reason" chosen because its a string. fhl

        m_facility = a_prop_tree.get( "facility", "" );
        m_beam_sname = a_prop_tree.get( "instrument", "" );
        m_start_time.tv_sec = a_prop_tree.get( "start_sec", 0 );
        m_start_time.tv_nsec = a_prop_tree.get( "start_nsec", 0 );
        m_run_num = a_prop_tree.get( "run_number", 0 );
        m_proposal_id = a_prop_tree.get( "ipts", "" );
        m_reason = a_prop_tree.get( "reason", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "facility", m_facility );
        a_prop_tree.put( "instrument", m_beam_sname );
        a_prop_tree.put( "start_nsec", m_start_time.tv_nsec );
        a_prop_tree.put( "start_sec", m_start_time.tv_sec );
        a_prop_tree.put( "run_number", m_run_num );
        a_prop_tree.put( "ipts", m_proposal_id );
        a_prop_tree.put( "reason", m_reason );
    }
};


}}}

#endif // SMSMESSAGES_H

// vim: expandtab

