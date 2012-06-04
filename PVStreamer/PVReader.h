#ifndef PVREADER
#define PVREADER

#include "PVStreamer.h"

namespace SNS { namespace PVS {

class PVStreamer;

/**
 * The PVReader class is the base class for all protocol-specific pv input
 * instances. PVReaders are protocol-specific and there can be mupltiple
 * instances of PVReaders supporting multiple protocols; however, there can
 * be only PVReader instance for each protocol.
 */
class PVReader
{
public:
    PVReader( PVStreamer &a_streamer, Protocol a_protocol )
        : m_streamer(a_streamer), m_protocol(a_protocol),m_reader_services(0)
    {
        m_reader_services = m_streamer.attach(*this);
    }

    virtual ~PVReader()
    {
        //TODO Detach reader?
    }

    inline Protocol getProtocol() { return m_protocol; }

protected:
    PVStreamer         &m_streamer;
    Protocol            m_protocol;
    IPVReaderServices  *m_reader_services;
};

}}

#endif
