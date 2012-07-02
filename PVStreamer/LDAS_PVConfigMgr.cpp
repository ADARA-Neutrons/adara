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
LDAS_PVConfigMgr::LDAS_PVConfigMgr( PVStreamer & a_streamer )
 : PVConfig(a_streamer, LDAS_PROTOCOL)
{
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
 * \brief Requests that configuration loading be initiated from specified satellite computer file (legacy).
 * \param a_file - Filename of satellite computer file.
 */
void
LDAS_PVConfigMgr::connectSatCompFile( const string & a_file )
{
    vector<string>  hostnames;
    parseSatCompFile( a_file, hostnames );

    for ( vector<string>::iterator ih = hostnames.begin(); ih != hostnames.end(); ++ih )
        connectHost(*ih);
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
 * \brief Parses legacy DAS satellite computer xml file.
 * \param a_file - Filename of satellite computer file (input).
 * \param a_hostnames - Vector of hostnames found in satellite computer file (output).
 */
void
LDAS_PVConfigMgr::parseSatCompFile( const string & a_file, vector<string> &a_hostnames )
{
    char *buffer = 0;

    try
    {
        string          str;

        // ===== Begin Imported Code ==============================================
        // ----- Copied and modified from ListenerLib -----------------------------

        CStringParser myParser;
	    ELE_STRUCT eStruct;
	    ELE_STRUCT eSubStruct;
	    CString csElement;
        CString csTemp;

        try
        {
	        CFile f(a_file.c_str(), CFile::modeRead | CFile::shareDenyWrite );
            UINT len = (UINT)f.GetLength();
            if ( len > 0 )
            {
                buffer = new char[len+1];
	            f.Read(buffer,len);
	            buffer[len]=0;
                csTemp = buffer;
                delete[] buffer;
                buffer = 0;
            }
	        f.Close();
        }
        catch(...)
        {
            if ( buffer )
            {
                delete[] buffer;
                buffer = 0;
            }
            EXC( EC_UNKOWN_ERROR, "Could not open file" );
        }

	    if (!myParser.CheckValidHeader(csTemp))
            EXC( EC_INVALID_CONFIG_DATA, "Invalid header");

	    if (myParser.GetRootName(csTemp) != "Satellite")
            EXC( EC_INVALID_CONFIG_DATA, "Invalid root name");

	    csTemp=myParser.StripHeader(csTemp); 	
	    csTemp=myParser.StripFirstTag(csTemp);
	    csTemp=myParser.StripLastTag(csTemp);

        int k = 1;
	    while(1)
	    {
		    csElement=myParser.GetElement(csTemp,k++);
		    if (csElement.IsEmpty())
                break;

            eStruct=myParser.GetElementStructure(csElement);
    	
		    if (eStruct.csName != "computer")
                EXC( EC_INVALID_CONFIG_DATA, "Invalid element found");

            eStruct=myParser.GetElementStructure(csElement);
		    for (unsigned int i=0;i<eStruct.uiNumberOfAttributes;i++)
		    {
			    if (eStruct.sAttribute[i].csName=="Name")
			    {
                    str = CT2CA(eStruct.sAttribute[i].csValue);
                    a_hostnames.push_back( str );
			    }
		    }
	    }

        // ===== End Imported Code ================================================
    }
    catch( TraceException &e )
    {
        if ( buffer )
            delete[] buffer;

        LOG_ERROR( "Failed processing satellite computer file: " << a_file );
        EXC_ADD(e,"Failed processing satellite computer file " << a_file );
        throw;
    }
    catch(...)
    {
        if ( buffer )
            delete[] buffer;

        LOG_ERROR( "Failed processing satellite computers file: " << a_file );
        throw;
    }
}

}}}
