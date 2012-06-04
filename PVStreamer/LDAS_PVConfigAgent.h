#ifndef LDAS_PVCONFIGAGENT
#define LDAS_PVCONFIGAGENT

#include <string>

#include "LegacyDAS.h"
#include "PVConfig.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"
#include "NiUiComponent.h"
#include "NiUiCommonComponent.h"
#include "LDAS_XmlParser.h"

namespace SNS { namespace PVS {
    
class PVStreamer;    

namespace LDAS {

class LDAS_PVConfigMgr;

class LDAS_PVConfigAgent : public PVConfig
{
public:
    LDAS_PVConfigAgent( PVStreamer &a_streamer, LDAS_PVConfigMgr &a_owner, const std::string &a_hostname );
    ~LDAS_PVConfigAgent();


private:
    void        fileSocketData( NI::CNiDataSocketData &a_data );

    void        parseConfigFile( const std::string &a_filename );
    void        parseOptionsFile( const std::string &a_filename );
    void        parseUnitsFile( const std::string &a_filename );

    // ----- Methods extracted from ListenerLib -------------------------------

    void        ExtractDeviceInfo( PELE_STRUCT pStruct, CStringParser *myParser, std::string a_rootdevice );
    void        GetDevInfo( PELE_STRUCT pStruct, std::string a_rootname, std::string & a_devicename, Identifier & a_app_id );
    void        parseVarMapValue( CString value, std::string &a_connection, std::string &a_name, std::map<int,std::string> &a_enum );
    DataType    GetDataType(PELE_STRUCT pStruct);
    Access      GetDataAccess(PELE_STRUCT pStruct);

    enum LoadState
    {
        LS_INITIAL,
        LS_WAITING_FILENAMES,
        LS_LOADING_FILES,
        LS_DONE
    };

    LoadState           m_state;
    std::string         m_hostname;
    std::string         m_config_file;
    std::string         m_options_file;
    std::string         m_units_file;
    NI::CNiDataSocket   m_file_socket;
    NI::CNiDataSocket   m_notify_socket;

    //std::string     m_device_file;
    //std::string     m_pv_file;

    static Identifier   m_next_dev_id; //TODO HACK
    //static Identifier   m_next_pv_id; //TODO HACK
    static std::map<Identifier,Identifier> m_next_pv_id;
};

}}}

#endif
