#ifndef STSMESSAGES_H
#define STSMESSAGES_H

#include "ComBusDefs.h"
#include "stsdefs.h"

namespace ADARA {
namespace ComBus {
namespace STS {


class TranslationStartedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STS_TRANS_STARTED,TranslationStartedMsg>
{
public:
    TranslationStartedMsg()
        : m_run_num(0)
    {}

    TranslationStartedMsg( unsigned long a_run_num )
        : m_run_num(a_run_num)
    {}

    unsigned long m_run_num;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_run_num = a_prop_tree.get( "run_num", 0 );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "run_num", m_run_num );
    }
};


class TranslationFinishedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STS_TRANS_FINISHED,TranslationFinishedMsg>
{
public:
    TranslationFinishedMsg()
        : m_run_num(0)
    {}

    TranslationFinishedMsg( const std::string &a_facility, const std::string &a_beam_sname,
                            const std::string &a_proposal_id, unsigned long a_run_num,
                            const std::string &a_nexus_file )
        : m_facility(a_facility), m_beam_sname(a_beam_sname), m_proposal_id(a_proposal_id),
          m_run_num(a_run_num), m_nexus_file(a_nexus_file)
    {}

    std::string         m_facility;
    std::string         m_beam_sname;
    std::string         m_proposal_id;
    unsigned long       m_run_num;
    std::string         m_nexus_file;

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
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "facility", m_facility );
        a_prop_tree.put( "instrument", m_beam_sname );
        a_prop_tree.put( "ipts", m_proposal_id );
        a_prop_tree.put( "run_number", m_run_num );
        a_prop_tree.put( "data_file", m_nexus_file );
    }
};


class TranslationFailedMsg : public ADARA::ComBus::TemplMessageBase<MSG_STS_TRANS_FAILED,TranslationFailedMsg>
{
public:
    TranslationFailedMsg()
        : m_run_num(0), m_code(::STS::TS_SUCCESS)
    {}

    TranslationFailedMsg( const std::string &a_beam_sname, const std::string &a_proposal_id,
                                unsigned long a_run_num, ::STS::TranslationStatusCode a_code, const std::string &a_reason )
        : m_beam_sname(a_beam_sname), m_proposal_id(a_proposal_id), m_run_num(a_run_num), m_code(a_code),
          m_reason(a_reason)
    {}

    std::string                 m_beam_sname;
    std::string                 m_proposal_id;
    unsigned long               m_run_num;
    ::STS::TranslationStatusCode  m_code;
    std::string                 m_reason;

protected:
    virtual void read( const boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_beam_sname = a_prop_tree.get( "instrument", "" );
        m_proposal_id = a_prop_tree.get( "ipts", "" );
        m_run_num = a_prop_tree.get( "run_number", 0 );
        m_code = (::STS::TranslationStatusCode) a_prop_tree.get( "code", 0 );
        m_reason = a_prop_tree.get( "reason", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "instrument", m_beam_sname );
        a_prop_tree.put( "ipts", m_proposal_id );
        a_prop_tree.put( "run_number", m_run_num );
        a_prop_tree.put( "code", (unsigned short)m_code );
        a_prop_tree.put( "reason", m_reason );
    }
};

}}}

#endif // STSMESSAGES_H
