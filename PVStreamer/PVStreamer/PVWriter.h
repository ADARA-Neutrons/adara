/**
 * \file PVWriter.h
 * \brief Header file for PVWriter class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVWRITER
#define PVWRITER

#include "PVStreamer.h"

namespace SNS { namespace PVS {

/**
 * The PVWriter class is the base class for all protocol-specific pv output
 * instances. PVWriters are protocol-specific and there can be mupltiple
 * instances of PVWriter supporting multiple protocols; however, there can be
 * only PVWriter instance for each protocol. The PVWriter base class provides
 * basic glue logic to automatically bind a writer subclass to the owning
 * PVStreamer instance.
 */
class PVWriter
{
public:

    /**
     * \brief Constructor for PVWriter class.
     * \param a_streamer - PVStreamer instance that will own/manage this writer
     * \param a_protocol - Protocol that this writer handles
     */
    PVWriter( PVStreamer &a_streamer, Protocol a_protocol )
        : m_streamer(a_streamer), m_protocol(a_protocol)
    {
        m_writer_services = m_streamer.attach(*this);
    }

    /**
     * \brief Destructor for PVWriter class.
     */
    virtual ~PVWriter()
    {
    }

    /**
     * \brief This method returns the protocol handled by this writer.
     * \return Protocol identifier associated with writer.
     */
    inline Protocol getProtocol()
    {
        return m_protocol;
    }

protected:

    PVStreamer         &m_streamer;             ///< PVStreamer instance that owns this writer
    Protocol            m_protocol;             ///< Protocol ID for this writer
    IPVWriterServices  *m_writer_services;      ///< Writer services interface acquired from owning PVStreamer
};

}}

#endif
