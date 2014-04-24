/**
 * \file PVConfig.h
 * \brief Header file for PVConfig class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVCONFIG
#define PVCONFIG


#include <string>
#include <vector>

#include "PVStreamer.h"

namespace SNS { namespace PVS {

class PVStreamer;

/**
 * The PVConfigProvider class is a base class for acquiring DAS process variable (pv)
 * configuration data for a given DAS protocol. This pv data includes id, device,
 * name, data type, units, access, limits, alarms, etc. This class does not define
 * how pv data is acquired or updated from external sources, but it does provide
 * client access to and internal management of the pv data itself. The PVConfig base
 * class provides basic glue logic to automatically bind a subclass to the owning
 * PVStreamer instance.
 */
class PVConfig
{
public:
    /**
     * \brief Constructor for PVConfig class.
     * \param a_streamer - PVStreamer instance that will own/manage this writer
     * \param a_protocol - Protocol that this writer handles
     */
    PVConfig( PVStreamer & a_streamer, Protocol a_protocol )
        : m_streamer( a_streamer), m_protocol(a_protocol), m_cfg_service(0)
    {
        m_cfg_service = m_streamer.attach(*this);
    }

    /**
     * \brief Destructor for PVConfig class.
     */
    virtual ~PVConfig()
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

    PVStreamer         &m_streamer;             ///< PVStreamer instance that owns this instance
    Protocol            m_protocol;             ///< Protocol ID for this instance
    IPVConfigServices  *m_cfg_service;          ///< Config services interface acquired from owning PVStreamer
};

}}


#endif
