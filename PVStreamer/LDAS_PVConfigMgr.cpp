#include "stdafx.h"

#include <algorithm>
#include <boost/thread/locks.hpp>

#include "LDAS_PVConfigMgr.h"
#include "LDAS_PVConfigAgent.h"
#include "LDAS_XmlParser.h"

namespace SNS { namespace PVS { namespace LDAS {

using namespace std;

LDAS_PVConfigMgr::LDAS_PVConfigMgr( PVStreamer & a_streamer )
: m_streamer(a_streamer)
{
}

LDAS_PVConfigMgr::~LDAS_PVConfigMgr()
{
}

void
LDAS_PVConfigMgr::connectSatCompFile( const string & a_file )
{
    vector<string>  hostnames;
    parseSatCompFile( a_file, hostnames );

    for ( vector<string>::iterator ih = hostnames.begin(); ih != hostnames.end(); ++ih )
        connectHost(*ih);
}

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
        throw -1;
}

void
LDAS_PVConfigMgr::parseSatCompFile( const string & a_file, vector<string> &a_hostnames )
{
    string          str;

    // ===== Begin Imported Code ==============================================
    // ----- Copied and modified from ListenerLib -----------------------------

    CStringParser myParser;
	ELE_STRUCT eStruct;
	ELE_STRUCT eSubStruct;
	CString csElement;
    CString csTemp;
    char *buffer = 0;

	TRY
	{
		CFile f(a_file.c_str(), CFile::modeRead);
        UINT len = (UINT)f.GetLength();
        if ( len > 0 )
        {
            buffer = new char[len+1];
		    f.Read(buffer,len);
		    buffer[len]=0;
            csTemp = buffer;
            delete[] buffer;
        }
		f.Close();
	
	}
	CATCH( CFileException, e )
	{
        if ( buffer )
            delete[] buffer;
		throw -1;
	}
	END_CATCH

	if (!myParser.CheckValidHeader(csTemp))
    {
		//GLB_pLogHelper->PopMsg("Invalid header in satellite computer file");
		throw -2;
	}

	if (myParser.GetRootName(csTemp) != "Satellite")
    {
		//GLB_pLogHelper->PopMsg("Invalid root in satellite file");
		throw -5;
	}
	csTemp=myParser.StripHeader(csTemp); 	
	csTemp=myParser.StripFirstTag(csTemp);
	csTemp=myParser.StripLastTag(csTemp);

    int k = 1;
	while(1)
	{
		csElement=myParser.GetElement(csTemp,k++);
		if (csElement.IsEmpty())
		{
            break;
		}
		eStruct=myParser.GetElementStructure(csElement);
	
		if (eStruct.csName != "computer")
		{
			throw -4;
		}
		else
		{    
			eStruct=myParser.GetElementStructure(csElement);
			for (unsigned int i=0;i<eStruct.uiNumberOfAttributes;i++)
			{
				if (eStruct.sAttribute[i].csName=="Name")
				{
                    //CT2CA tmp(eStruct.sAttribute[i].csValue);
                    //str = tmp;
                    str = CT2CA(eStruct.sAttribute[i].csValue);
                    a_hostnames.push_back( str );
				}
			}
		}
	}

    // ===== End Imported Code ================================================
}



}}}
