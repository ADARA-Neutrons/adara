#include "stdafx.h"
#include "LDAS_PVConfigAgent.h"
#include "LDAS_PVConfigMgr.h"
#include "PVConfig.h"

using namespace NI;
using namespace std;

namespace SNS { namespace PVS { namespace LDAS {

Identifier   LDAS_PVConfigAgent::m_next_dev_id = 1;
//Identifier   LDAS_PVConfigAgent::m_next_pv_id = 1;
map<Identifier,Identifier> LDAS_PVConfigAgent::m_next_pv_id;

LDAS_PVConfigAgent::LDAS_PVConfigAgent( PVStreamer &a_streamer, LDAS_PVConfigMgr &a_owner, const std::string &a_hostname )
:PVConfig(a_streamer, LDAS_PROTOCOL), m_state(LS_INITIAL), m_hostname(a_hostname)
{
    m_file_socket.InstallEventHandler( *this, &LDAS_PVConfigAgent::fileSocketData );
    string uri = string("dstp://") + m_hostname + "/filenames";
    m_file_socket.Connect( uri.c_str(), CNiDataSocket::ReadAutoUpdate );
}

LDAS_PVConfigAgent::~LDAS_PVConfigAgent()
{
}


void
LDAS_PVConfigAgent::fileSocketData( NI::CNiDataSocketData &a_data )
{
    // ===== Begin Imported Code ==============================================
    // ----- Copied and modified from ListenerLib -----------------------------

	CString csTemp=a_data;
	CString csTemp2;
	//AfxMessageBox(csTemp);
	//GLB_pLogHelper->WriteMsg("Waiting for Parsing to complete");
	//m_bConnected=true;
    //now parse the string to get the filenames
	int i,j;
	i=csTemp.Find("configuration=");
	i=csTemp.Find('\"',i+1);
	j=csTemp.Find('\"',i+1);
	if (j<0)  //would normally get here for testing
	{
		//GLB_pLogHelper->PopMsg("Configfile socket contains bad data!");
	}
	else
	{
		m_config_file = CT2CA(csTemp.Mid(i+1,j-i-1));
	}
    //now find options filename
	i=csTemp.Find("options=");
	if (i<0)
	{
		//strcpy(m_optionsFile,"");
        //TODO Is no options file acceptable?
	}
	else
	{
		i=csTemp.Find('\"',i+1);
		j=csTemp.Find('\"',i+1);
		if (j<0)  //would normally get here for testing
		{
			//GLB_pLogHelper->PopMsg("Configfile socket contains bad data!");
		}
		else
		{
			//strcpy(m_optionsFile,csTemp.Mid(i+1,j-i-1));
            m_options_file = CT2CA(csTemp.Mid(i+1,j-i-1));
		}
	}
    //now find units filename

	i=csTemp.Find("units=");
	if (i<0)
	{
		//strcpy(m_unitsFile,"");
        // TODO Is no units file acceptable?
	}
	else
	{
		i=csTemp.Find('\"',i+1);
		j=csTemp.Find('\"',i+1);
		if (j<0)  //would normally get here for testing
		{
			//GLB_pLogHelper->PopMsg("Configfile socket contains bad data!");
		}
		else
		{
			//strcpy(m_unitsFile,csTemp.Mid(i+1,j-i-1));
            m_units_file = csTemp.Mid(i+1,j-i-1).GetBuffer();
		}
	}

    // ===== End Imported Code ================================================

    parseConfigFile(m_config_file);

    if ( m_options_file.size())
        parseOptionsFile(m_options_file);

    if ( m_units_file.size())
        parseUnitsFile(m_units_file);

    // Notify config service that this source is now loaded
    m_cfg_service->configurationLoaded( LDAS_PROTOCOL, m_hostname );
}

void
LDAS_PVConfigAgent::parseConfigFile( const std::string &a_filename )
{
    // ===== Begin Imported Code ==============================================
    // ----- Copied and modified from ListenerLib -----------------------------

    CStringParser myParser;
	CString csElement1;
	CString csTemp2; 
	ELE_STRUCT eStruct;
	int returnValue=0;
    CString csTemp;
    char *buffer = 0;

	TRY
	{
		CFile f(a_filename.c_str(), CFile::modeRead);
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
		throw -2;

    if (myParser.GetRootName(csTemp) != m_hostname.c_str() )
		throw -5;

    csTemp=myParser.StripHeader(csTemp); 	
	csTemp=myParser.StripFirstTag(csTemp);
	csTemp=myParser.StripLastTag(csTemp);

    string rootname;
    int e = 1;
    while(1)
    {
	    csElement1=myParser.GetElement(csTemp,e++);
	    if ( csElement1.IsEmpty())
            break;
	    eStruct=myParser.GetElementStructure(csElement1);
	    if (eStruct.csName == "Device")
	    {
		    ExtractDeviceInfo(&eStruct,&myParser, rootname );
	    }
    }
}


void
LDAS_PVConfigAgent::parseOptionsFile( const std::string &a_filename )
{
    // ===== Begin Imported Code ==============================================
    // ----- Copied and modified from ListenerLib -----------------------------

    CStringParser myParser;
	CString csElement1;
	CString csTag; 
	CString csTemp2;
	//char friendlyname[MAXSTRINGLENGTH];
    string friendlyname;
	CString csLimits;
	//is used to hold the xml file
	ELE_STRUCT eStruct;
	int returnValue=0;
	int i,j;
    CString csTemp;
    char *buffer = 0;

	TRY
	{
		CFile f(a_filename.c_str(), CFile::modeRead);
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
    
	//AfxMessageBox(csTemp); //prints out entire config file
	if (!myParser.CheckValidHeader(csTemp))
        throw -2;

	if (myParser.GetRootName(csTemp) != m_hostname.c_str())
        throw -5;

    //strip first and last tags, for the SNS DAS format this is extraneous
	csTemp=myParser.StripHeader(csTemp);  
	csTemp=myParser.StripFirstTag(csTemp);
	csTemp=myParser.StripLastTag(csTemp);

    PVInfo *pv_info;

    //you should now have a flat list of elements.
	i=1;
	csElement1=myParser.GetElement(csTemp,i);

    while (!csElement1.IsEmpty())
	{
		if (myParser.GetElementName(csElement1)=="Optionsmap")
		{
			csTag=myParser.GetElementTag(csElement1);
			csLimits=myParser.GetElementContent(csElement1);
			csTemp2=myParser.GetAttributeValue(csTag,"Option");
			friendlyname = myParser.GetAttributeValue(csTag,"Name");
			if (csTemp2=="hardwarealarm")
			{
				j=csLimits.Find(",");
				if (j<0)
                    throw -1;

                pv_info = m_cfg_service->getWriteablePV( friendlyname );
                if ( pv_info )
                {
                    pv_info->m_hw_alarms.m_min = atof(csLimits.Left(j));
                    pv_info->m_hw_alarms.m_max = atof(csLimits.Right(csLimits.GetLength()-j-1));
                    pv_info->m_hw_alarms.m_active = true;
                }
			}
			else if (csTemp2=="hardwarelimits")
			{
				j=csLimits.Find(",");
				if (j<0)
                    throw -1;

                pv_info = m_cfg_service->getWriteablePV( friendlyname );
                if ( pv_info )
                {
                    pv_info->m_hw_limits.m_min = atof(csLimits.Left(j));
                    pv_info->m_hw_limits.m_max = atof(csLimits.Right(csLimits.GetLength()-j-1));
                    pv_info->m_hw_limits.m_active = true;
                }
			}
		
		}
		i++;
		csElement1=myParser.GetElement(csTemp,i);
	}
}

void
LDAS_PVConfigAgent::parseUnitsFile( const std::string &a_filename )
{
    // ===== Begin Imported Code ==============================================
    // ----- Copied and modified from ListenerLib -----------------------------

	CStringParser myParser;
	CString csElement1;
	//UNITS_INFO units; //holds units map content
	//is used to hold the xml file
	ELE_STRUCT eStruct;
	int returnValue=0;
	int i;
    CString csTemp;
    char *buffer = 0;

	TRY
	{
		CFile f(a_filename.c_str(), CFile::modeRead);
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
        throw -2;

	if (myParser.GetRootName(csTemp) != m_hostname.c_str() )
        throw -5;

    //strip first and last tags, for the SNS DAS format this is extraneous
	csTemp=myParser.StripHeader(csTemp);  
	csTemp=myParser.StripFirstTag(csTemp);
	csTemp=myParser.StripLastTag(csTemp);

    string friendlyname;
    PVInfo *pv_info;

    //you should now have a flat list of elements.
	i=1;
	csElement1=myParser.GetElement(csTemp,i);

    while (!csElement1.IsEmpty())
	{
		if (myParser.GetElementName(csElement1)=="Unitsmap")
		{
			csElement1=myParser.GetElementTag(csElement1);
			friendlyname = myParser.GetAttributeValue(csElement1,"Name");

            pv_info = m_cfg_service->getWriteablePV( friendlyname );
            if ( pv_info )
            {
                pv_info->m_units = myParser.GetAttributeValue(csElement1,"Neumonic"); // Did they mean mnemonic? :)
            }
            
            //strcpy(units.unitsclass,myParser.GetAttributeValue(csElement1,"Class"));
			//strcpy(units.units,myParser.GetAttributeValue(csElement1,"Neumonic"));
            //m_pAssociate->AddUnits(&units);
			//GLB_pLogHelper->SetUnits((CString)units.friendlyname,(CString)(units.unitsclass),(CString)(units.units));
		}
		i++;
		csElement1=myParser.GetElement(csTemp,i);
	}
}

void
LDAS_PVConfigAgent::ExtractDeviceInfo( PELE_STRUCT pStruct, CStringParser *myParser, string rootdevice )
{
    // Note: This is a recursive method

	ELE_STRUCT  eSubStruct;
    string      devicename;
    Identifier  app_id;
    Identifier  dev_id;

    GetDevInfo( pStruct, rootdevice, devicename, app_id );

    // If this is the first encounter with this application, define it
    if ( !m_streamer.isAppDefined( app_id ))
        m_cfg_service->defineApp( LDAS_PROTOCOL, app_id, m_hostname );


    //TODO This is a HACK b/c we have no device ID input
    dev_id = m_next_dev_id++;

    // If this is the first encounter with this device, define it and load PVs
    if ( !m_streamer.isDeviceDefined( dev_id ))
    {
        PVInfo* info = 0;
        map<int,string> enum_vals;

        m_cfg_service->defineDevice( LDAS_PROTOCOL, dev_id, devicename, m_hostname, app_id );

        // Source and protocol will be the same for all following PVs

        for ( unsigned int i = 0; i < pStruct->uiNumberOfSubElements; i++ )
	    {
		    eSubStruct=myParser->GetElementStructure(pStruct->csSubElement[i]);
		    if (eSubStruct.csName=="VarMap")
		    {
                try
                {
                    Access da = GetDataAccess( &eSubStruct );
                    if ( da == PV_READ )
                    {
                        info = new PVInfo();

                        info->m_source = m_hostname;
                        info->m_protocol = LDAS_PROTOCOL;
                        info->m_device_id = dev_id;

                        parseVarMapValue( eSubStruct.csValue, info->m_connection, info->m_name, enum_vals );

                        if ( enum_vals.size())
                            info->m_enum = m_cfg_service->defineEnum( enum_vals );
                        else
                            info->m_enum = 0;

                        info->m_type = GetDataType( &eSubStruct );
                        info->m_access = da;

                        // HACK - assign a global pv_id to this variable (no device.xml available yet)
                        map<Identifier,Identifier>::iterator iid = m_next_pv_id.find( info->m_device_id );
                        if ( iid == m_next_pv_id.end())
                        {
                            m_next_pv_id[info->m_device_id] = 2;
                            info->m_id = 1;
                        }
                        else
                            info->m_id = iid->second++;

                        m_cfg_service->definePV( *info );
                    }
                }
                catch(...)
                {
                    if ( info )
                        delete info;
                    throw;
                }
		    }
		    else if (eSubStruct.csName == "Device")
		    {
			    ExtractDeviceInfo(&eSubStruct,myParser,devicename);
		    }
	    }

        // If no PVs are associated with this device, remove it.
        // This must be done here as there is not an easy way to know if there will be PVs for a device above
        m_cfg_service->undefineDeviceIfNoPVs( dev_id );
    }
    else
    {
        // We've already seen this device. Why?
        // TODO In the future, this would be an error I think. Probabaly should abort loading this config file.
    }
}

void
LDAS_PVConfigAgent::GetDevInfo( PELE_STRUCT pStruct, string a_rootname, string & a_devicename, Identifier & a_app_id )
{
	bool name_found = false;
    bool appid_found = false;

    for ( unsigned int i = 0; i < pStruct->uiNumberOfAttributes; i++ )
	{
		if ( pStruct->sAttribute[i].csName.CompareNoCase("Name") == 0 )
		{
            if (a_rootname.empty())
				a_devicename = CT2CA(pStruct->sAttribute[i].csValue);
            else
                a_devicename = a_rootname + "." + string(CT2CA(pStruct->sAttribute[i].csValue));

            name_found = true;
		}
		else if ( pStruct->sAttribute[i].csName.CompareNoCase("AppID") == 0 )
		{
            a_app_id = atol(pStruct->sAttribute[i].csValue);
            appid_found = true;
		}
	}

    if ( !name_found || !appid_found )
        throw -1;
}


void
LDAS_PVConfigAgent::parseVarMapValue( CString a_value, string &a_connection, string &a_name, map<int,string> &a_enum )
{
    string value = CT2CA(a_value);

    size_t i = value.find_first_of(",");
    if ( i == string::npos )
		throw -1;

    size_t j = value.find_first_of(";",i+1);

    a_connection = value.substr( 0, i ); //CT2CA(value.Left(i).TrimLeft().TrimRight());

    //size_t l = value.size();
    if ( j == string::npos )
        a_name = value.substr(i+1); //CT2CA(value.Mid(i+1).TrimLeft().TrimRight());
    else
        a_name = value.substr(i+1,j-i-1); //CT2CA(value.Mid(i+1,j-i-1).TrimLeft().TrimRight());

    a_enum.clear();

    // If there is a ; then there is an enumeartion definition present
    if ( j != string::npos )
    {
        int eval;
        string ename;

        while(1)
        {
            i = j+1;
            j = value.find(",",i);
            if ( j == string::npos )
                throw -1;

            ename = value.substr(i,j-i);

            i = j+1;
            j = value.find(";",i);
            if ( j == string::npos )
            {
                eval = atoi(value.substr(i).c_str());
                a_enum[eval] = ename;
                break;
            }
            else
            {
                eval = atoi(value.substr(i,j-i).c_str());
                a_enum[eval] = ename;
            }
        }
    }
}

DataType
LDAS_PVConfigAgent::GetDataType(PELE_STRUCT pStruct)
{
	for ( unsigned int i = 0; i < pStruct->uiNumberOfAttributes; i++ )
	{
		if (pStruct->sAttribute[i].csName=="DataType")
		{
            switch(pStruct->sAttribute[i].csValue.GetAt(0))
            {
            case 'd':
            case 'D':
                return PV_DOUBLE;
            case 'e':
            case 'E':
                return PV_ENUM;
            case 'i':
            case 'I':
                return PV_INT;
            case 'u':
            case 'U':
                return PV_UINT;
            }
		}
	}

    throw -1;
}

Access
LDAS_PVConfigAgent::GetDataAccess(PELE_STRUCT pStruct)
{
	for ( unsigned int i = 0; i < pStruct->uiNumberOfAttributes; i++ )
	{
		if (pStruct->sAttribute[i].csName=="Access")
		{
            switch(pStruct->sAttribute[i].csValue.GetAt(0))
            {
            case 'r':
            case 'R':
                return PV_READ;
            case 'w':
            case 'W':
                return PV_WRITE;
            }
		}
	}

    throw -1;
}

}}}
