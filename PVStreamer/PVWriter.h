#ifndef PVWRITER
#define PVWRITER

#include "PVStreamer.h"

namespace SNS { namespace PVS {

/**
 * The PVWriter class is the base class for all protocol-specific pv output
 * instances. PVWriters are protocol-specific and there can be mupltiple
 * instances of PVWriter supporting multiple protocols; however, there can
 * be only PVWriter instance for each protocol.
 */
class PVWriter
{
public:
    PVWriter( PVStreamer &a_streamer, Protocol a_protocol )
        : m_streamer(a_streamer), m_protocol(a_protocol)
    {
        m_writer_services = m_streamer.attach(*this);
    }

    virtual ~PVWriter()
    {
        //TODO Detach writer?
    }

    inline Protocol getProtocol()
    {
        return m_protocol;
    }

protected:
    PVStreamer         &m_streamer;
    Protocol            m_protocol;
    IPVWriterServices  *m_writer_services;
};

}}

#endif
