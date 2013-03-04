#ifndef STSMESSAGES_H
#define STSMESSAGES_H

#include "ComBusMessages.h"

namespace ADARA {
namespace ComBus {
namespace STS {

class TranslationCompleteMessage : public MessageBase
{
public:
    TranslationCompleteMessage()
    {}

    TranslationCompleteMessage( const std::string &a_beam_sname, unsigned long a_run_num,
                                const std::string &a_nexus_file, const std::string &a_adara_file )
        : m_beam_sname(a_beam_sname), m_run_num(a_run_num), m_nexus_file(a_nexus_file), m_adara_file(a_adara_file)
    {
    }

    MessageType         getMessageType() const
                        { return MSG_STS_TRANS_COMPLETE; }

    std::string         m_beam_sname;
    unsigned long       m_run_num;
    std::string         m_nexus_file;
    std::string         m_adara_file;

protected:
    virtual void read( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::read( a_prop_tree );

        m_beam_sname = a_prop_tree.get( "beam_name", "" );
        m_run_num = a_prop_tree.get( "run_num", 0 );
        m_nexus_file = a_prop_tree.get( "nexus_file", "" );
        m_adara_file = a_prop_tree.get( "adara_file", "" );
    }

    virtual void write( boost::property_tree::ptree &a_prop_tree )
    {
        MessageBase::write( a_prop_tree );

        a_prop_tree.put( "beam_name", m_beam_sname );
        a_prop_tree.put( "run_num", m_run_num );
        a_prop_tree.put( "nexus_file", m_nexus_file );
        a_prop_tree.put( "adara_file", m_adara_file );
    }

    /*
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setStringProperty( "beamname", m_beam_sname );
                            a_msg.setIntProperty( "runnum", m_run_num );
                            a_msg.setStringProperty( "nexusfile", m_nexus_file );
                            a_msg.setStringProperty( "adarafile", m_adara_file );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_beam_sname = a_msg.getStringProperty( "beamname" );
                            m_run_num = a_msg.getIntProperty( "runnum" );
                            m_nexus_file  = a_msg.getStringProperty( "nexusfile" );
                            m_adara_file = a_msg.getStringProperty( "adarafile" );
                        }
    */
};

}}}

#endif // STSMESSAGES_H
