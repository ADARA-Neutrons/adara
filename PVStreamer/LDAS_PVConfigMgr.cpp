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
: m_streamer(a_streamer)
{
}

/**
 * \brief Destructor for LDAS_PVConfigMgr class.
 */
LDAS_PVConfigMgr::~LDAS_PVConfigMgr()
{
    // Note: Agents are deleted by PVStreamer
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
        m_agents[hostname] = new LDAS_PVConfigAgent(m_streamer, *this, hostname);
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

	    CFile f(a_file.c_str(), CFile::modeRead);
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
    	

	    if (!myParser.CheckValidHeader(csTemp))
            throw -1;

	    if (myParser.GetRootName(csTemp) != "Satellite")
            throw -1;

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
			    throw -1;

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
    catch(...)
    {
        if ( buffer )
            delete[] buffer;

        LOG_ERROR( "Failed parsing satellite computers file: " << a_file );
        throw;
    }
}

}}}
