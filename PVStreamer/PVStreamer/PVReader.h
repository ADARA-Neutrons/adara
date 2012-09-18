/**
 * \file PVReader.h
 * \brief Header file for PVReader class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVREADER
#define PVREADER

#include "PVStreamer.h"

namespace SNS { namespace PVS {

class PVStreamer;

/**
 * The PVReader class is the base class for all protocol-specific pv input
 * instances. PVReaders are protocol-specific and there can be mupltiple
 * instances of PVReaders supporting multiple protocols; however, there can be
 * only PVReader instance for each protocol. The PVReader base class provides
 * basic glue logic to automatically bind a reader subclass to the owning
 * PVStreamer instance.
 */
class PVReader
{
public:

    /**
     * \brief Constructor for PVReader class.
     * \param a_streamer - PVStreamer instance that will own/manage this reader
     * \param a_protocol - Protocol that this reader handles
     */
    PVReader( PVStreamer &a_streamer, Protocol a_protocol )
        : m_streamer(a_streamer), m_protocol(a_protocol), m_reader_services(0)
    {
        m_reader_services = m_streamer.attach(*this);
    }

    /**
     * \brief Destructor for PVReader class.
     */
    virtual ~PVReader()
    {
    }

    /**
     * \brief This method returns the protocol handled by this reader.
     * \return Protocol identifier associated with reader.
     */
    inline Protocol getProtocol()
    {
        return m_protocol;
    }

protected:

    PVStreamer         &m_streamer;         ///< PVStreamer instance that owns this reader
    Protocol            m_protocol;         ///< Protocol ID for this reader
    IPVReaderServices  *m_reader_services;  ///< Reader services interface acquired from owning PVStreamer
};

}}

#endif
