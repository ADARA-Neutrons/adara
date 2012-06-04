#ifndef LDAS_PVCONFIGMGR
#define LDAS_PVCONFIGMGR

#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#include "PVConfig.h"

namespace SNS { namespace PVS { namespace LDAS {

class LDAS_PVConfigAgent;

class LDAS_PVConfigMgr
{
public:
    LDAS_PVConfigMgr( PVStreamer & a_streamer );
    ~LDAS_PVConfigMgr();

    void    connectSatCompFile( const std::string &a_file );
    void    connectHost( const std::string &a_hostname );

private:
    void    parseSatCompFile( const std::string & a_file, std::vector<std::string> &a_hostnames );

    PVStreamer&         m_streamer;
    boost::mutex        m_agent_mutex;
    std::map<std::string,LDAS_PVConfigAgent*>   m_agents;
};

}}}

#endif

