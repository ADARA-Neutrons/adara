#ifndef STCMESSAGES_H
#define STCMESSAGES_H

#include "ComBusDefs.h"
#include "stcdefs.h"

namespace ADARA {
namespace ComBus {
namespace STC {


class TranslationStartedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STC_TRANS_STARTED,TranslationStartedMsg>
{
public:
    TranslationStartedMsg()
        : m_run_num(0)
    {}

    TranslationStartedMsg( uint32_t a_run_num, const std::string &a_host )
        : m_run_num(a_run_num), m_host(a_host)
    {}

    uint32_t        m_run_num;
    std::string     m_host;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_run_num = a_prop_tree.get( "run_num", 0 );
        m_host = a_prop_tree.get( "host", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "run_num", m_run_num );
        a_prop_tree.put( "host", m_host );
    }
};


class TranslationFinishedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STC_TRANS_FINISHED,TranslationFinishedMsg>
{
public:
    TranslationFinishedMsg()
        : m_run_num(0)
    {}

    TranslationFinishedMsg( const std::string &a_facility, const std::string &a_beam_sname,
                            const std::string &a_proposal_id, uint32_t a_run_num,
                            const std::string &a_nexus_file, const std::string &a_host )
        : m_facility(a_facility), m_beam_sname(a_beam_sname), m_proposal_id(a_proposal_id),
        m_run_num(a_run_num), m_nexus_file(a_nexus_file), m_host(a_host)
    {}

    std::string         m_facility;
    std::string         m_beam_sname;
    std::string         m_proposal_id;
    uint32_t            m_run_num;
    std::string         m_nexus_file;
    std::string         m_host;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        // Note: the JSON property names here were selected for compatibility with the
        // existing workflow manager "data ready" message definition.

        m_facility = a_prop_tree.get( "facility", "" );
        m_beam_sname = a_prop_tree.get( "instrument", "" );
        m_proposal_id = a_prop_tree.get( "ipts", "" );
        m_run_num = a_prop_tree.get( "run_number", 0 );
        m_nexus_file = a_prop_tree.get( "data_file", "" );
        m_host = a_prop_tree.get( "host", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "facility", m_facility );
        a_prop_tree.put( "instrument", m_beam_sname );
        a_prop_tree.put( "ipts", m_proposal_id );
        a_prop_tree.put( "run_number", m_run_num );
        a_prop_tree.put( "data_file", m_nexus_file );
        a_prop_tree.put( "host", m_host );
    }
};


class TranslationFailedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STC_TRANS_FAILED,TranslationFailedMsg>
{
public:
    TranslationFailedMsg()
        : m_run_num(0), m_code(::STC::TS_SUCCESS)
    {}

    TranslationFailedMsg( const std::string &a_beam_sname, const std::string &a_proposal_id,
                          uint32_t a_run_num, ::STC::TranslationStatusCode a_code,
                          const std::string &a_reason, const std::string &a_host )
        : m_beam_sname(a_beam_sname), m_proposal_id(a_proposal_id), m_run_num(a_run_num), m_code(a_code),
          m_reason(a_reason), m_host(a_host)
    {}

    std::string                 m_beam_sname;
    std::string                 m_proposal_id;
    uint32_t                    m_run_num;
    ::STC::TranslationStatusCode  m_code;
    std::string                 m_reason;
    std::string                 m_host;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_beam_sname = a_prop_tree.get( "instrument", "" );
        m_proposal_id = a_prop_tree.get( "ipts", "" );
        m_run_num = a_prop_tree.get( "run_number", 0 );
        m_code = (::STC::TranslationStatusCode) a_prop_tree.get( "code", 0 );
        m_reason = a_prop_tree.get( "reason", "" );
        m_host = a_prop_tree.get( "host", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "instrument", m_beam_sname );
        a_prop_tree.put( "ipts", m_proposal_id );
        a_prop_tree.put( "run_number", m_run_num );
        a_prop_tree.put( "code", (unsigned short)m_code );
        a_prop_tree.put( "reason", m_reason );
        a_prop_tree.put( "host", m_host );
    }
};

}}}

#endif // STCMESSAGES_H

// vim: expandtab

