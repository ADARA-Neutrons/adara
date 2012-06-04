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
 * client access to and internal management of the pv data itself.
 *
 * All config data is sent to the PVStreamer object where it is processed and
 * dispatched.
 */
class PVConfig
{
public:
    PVConfig( PVStreamer & a_streamer, Protocol a_protocol )
        : m_streamer(a_streamer), m_protocol(a_protocol), m_cfg_service(0)
    {
        m_cfg_service = m_streamer.attach(*this);
    }

    virtual ~PVConfig()
    {
        //TODO detach from streamer?
    }

    inline Protocol getProtocol() { return m_protocol; }

protected:

    PVStreamer         &m_streamer;
    Protocol            m_protocol;
    IPVConfigServices  *m_cfg_service;
};

}}


#endif
