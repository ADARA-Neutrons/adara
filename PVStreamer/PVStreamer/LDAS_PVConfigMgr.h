/**
 * \file LDAS_PVConfigMgr.h
 * \brief Header file for LDAS_PVConfigMgr class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef LDAS_PVCONFIGMGR
#define LDAS_PVCONFIGMGR

#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#include "LegacyDAS.h"
#include "PVConfig.h"

namespace SNS { namespace PVS { namespace LDAS {

class LDAS_PVConfigAgent;

/**
 * \class LDAS_PVConfigMgr
 * \brief Provides management of overall legacy DAS configuration.
 *
 * The LDAS_PVConfigMgr class provides the entry-point for configuring PVStreamer
 * from legacy DAS configuration files (xml). The connectSatCompFile() method
 * initiates the configuration process which results in multple LDAS_PVConfigAgent
 * instances being created to load host-specific configuration data.
 */
class LDAS_PVConfigMgr : public PVConfig
{
public:
    LDAS_PVConfigMgr( PVStreamer & a_streamer, const std::string &a_file );
    ~LDAS_PVConfigMgr();

private:
    void            readLDASConfigFile( const std::string & a_file, std::vector<std::string> &a_hostnames );
    void            connectHost( const std::string &a_hostname );

    boost::mutex                                m_agent_mutex;  ///< Mutex used to protect public API
    std::map<std::string,LDAS_PVConfigAgent*>   m_agents;       ///< Active configuration agents
};

}}}

#endif

