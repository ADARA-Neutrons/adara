#ifndef STSMESSAGES_H
#define STSMESSAGES_H

namespace ADARA {
namespace ComBus {
namespace STS {

class TranslationCompleteMessage : public Message
{
public:
    TranslationCompleteMessage( const cms::Message &a_msg )
        : Message( a_msg )
    {
        translateFrom( a_msg );
    }

    TranslationCompleteMessage( const std::string &a_beam_sname, unsigned long a_run_num,
                                const std::string &a_nexus_file, const std::string &a_adara_file )
        : m_beam_sname(a_beam_sname), m_run_num(a_run_num), m_nexus_file(a_nexus_file), m_adara_file(a_adara_file)
    {
    }

    MessageType         getMessageType() const
                        { return MSG_STATUS_STS_TRANS_COMPLETE; }

    const std::string  &getBeamShortName() const
                        { return m_beam_sname; }

    unsigned long       getRunNumber() const
                        { return m_run_num; }

    const std::string  &getNexusFile() const
                        { return m_nexus_file; }

    const std::string  &getADARAFile() const
                        { return m_adara_file; }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            Message::translateTo( a_msg );
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

    std::string         m_beam_sname;
    unsigned long       m_run_num;
    std::string         m_nexus_file;
    std::string         m_adara_file;
};

}}}

#endif // STSMESSAGES_H
