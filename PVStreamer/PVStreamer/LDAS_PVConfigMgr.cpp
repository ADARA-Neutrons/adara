/**
 * \file LDAS_PVConfigMgr.h
 * \brief Header file for LDAS_PVConfigMgr class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"

#include <algorithm>
#include <boost/thread/locks.hpp>

#include "LDAS_PVConfigMgr.h"
#include "LDAS_PVConfigAgent.h"
#include "LDAS_XmlParser.h"

namespace SNS { namespace PVS { namespace LDAS {

using namespace std;

/**
 * \brief Constructor for LDAS_PVConfigMgr class.
 * \param a_streamer - Owning PVStreamer insatnce.
 */
LDAS_PVConfigMgr::LDAS_PVConfigMgr( PVStreamer & a_streamer, const std::string &a_file )
 : PVConfig(a_streamer, LDAS_PROTOCOL)
{
    // Create a legacy DAS PV reader (handles per-host configuraion, monitoring, reading)
    // Instance will be owned by PVStreamer
    LDAS_PVReader* ldas_reader = new LDAS_PVReader( *pvs );

    vector<string>  hostnames;
    readLDASConfigFile( a_file, hostnames );

    for ( vector<string>::iterator ih = hostnames.begin(); ih != hostnames.end(); ++ih )
        connectHost(*ih);
}

/**
 * \brief Destructor for LDAS_PVConfigMgr class.
 */
LDAS_PVConfigMgr::~LDAS_PVConfigMgr()
{
    // Delete config agents
    boost::unique_lock<boost::mutex> lock(m_agent_mutex);

    for ( map<string,LDAS_PVConfigAgent*>::iterator ia = m_agents.begin(); ia != m_agents.end(); ++ia )
        delete ia->second;
}


/**
 * \brief Requests that configuration loading proceed from a given host (satellite omputer).
 * \param a_hostname - Hostname of satellite computer.
 */
void
LDAS_PVConfigMgr::connectHost( const string & a_hostname )
{
    string hostname = a_hostname;
    transform( hostname.begin(), hostname.end(), hostname.begin(), ::tolower );

    boost::unique_lock<boost::mutex> lock(m_agent_mutex);

    // Make sure we're not already connected or connecting to this host
    if ( m_agents.find(hostname) == m_agents.end())
    {
        m_agents[hostname] = new LDAS_PVConfigAgent( m_streamer, *m_cfg_service, hostname);
    }
    else
    {
        LOG_ERROR( "Already connected to and monitoring host " << hostname );
    }
}


/**
 * \brief Parses legacy DAS configuration file.
 * \param a_file - Filename of configuration file (input).
 * \param a_hostnames - Vector of hostnames found in file (output).
 *
 * The legacy configuration file is currently an unadorned list of satellite
 * computer hostnames. The hostnames should be white-space delimited.
 */
void
LDAS_PVConfigMgr::readLDASConfigFile( const string & a_file, vector<string> &a_hostnames )
{
    try
    {
        string      tmp;
        ifstream    inf( a_file.c_str() );

        if ( !inf.is_open())
            EXC( EC_INVALID_CONFIG_DATA, "Could not open file" );

        while( 1 )
        {
            inf >> tmp;
            if ( inf.eof())
                break;

            a_hostnames.push_back( tmp );
        }

        inf.close();

        if ( !a_hostnames.size())
            LOG_WARN( "No host computers defined in LDAS config file" );
    }
    catch( TraceException &e )
    {
        LOG_ERROR( "Failed processing LDAS config file: " << a_file );
        EXC_ADD(e,"Failed processing LDAS config file: " << a_file );
        throw;
    }
    catch(...)
    {
        LOG_ERROR( "Failed processing LDAS config file: " << a_file );
        throw;
    }
}

}}}
