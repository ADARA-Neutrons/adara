/**
 * \file LDAS_PVConfigAgent.h
 * \brief Header file for LDAS_PVConfigAgent class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef LDAS_PVCONFIGAGENT
#define LDAS_PVCONFIGAGENT

#include <string>
#include <set>

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

/**
 * @class LDAS_PVConfigAgent
 * @brief Provides host configuration loading using legacy DAS protocol.
 *
 * The LDAS_PVConfigAgent provides legacy DAS configuration loading for an associated
 * satellite computer (host). The configuration loading process is initiated by
 * connecting to a "filename socket" (via NI DataSockets) on the host. A configuration
 * filename, an options filename, and a units filename are received over this
 * connection, and are subsequently openned and parsed. (These file must be accessible
 * from the machine that PVStreamer is runnng on.) Loading these files results in
 * devices and process variables being defined in the PVStreamer object where they
 * can be access by other interested classes. This class uses legacy XML parsing
 * code extracted from the ListenerLib library.
 *
 * Currently device and process variable are assigned numeric identifers on a first-com
 * first-serve basis. This approach will yeild varying identifiers in downstream
 * work products from run-to-run and may need to be changed to static allocation of
 * device identifiers.
 */
class LDAS_PVConfigAgent
{
public:

    LDAS_PVConfigAgent( PVStreamer &a_streamer, IPVConfigServices &a_cfg_service, const std::string &a_hostname );
    ~LDAS_PVConfigAgent();

    //static void loadDisabledPVList( const std::string &a_filename );
    static void     loadPVConfigLocal( const std::string &a_filename, bool a_default = false );
    std::string     standardizeUnits( const CString & a_units_class,  const CString & a_units ) const;

private:
    struct PVConfigLocal
    {
        bool            enabled;
        std::string     hints;
    };

    void            fileSocketData( NI::CNiDataSocketData &a_data );
    void            parseConfigFile( const std::string &a_filename );
    void            parseOptionsFile( const std::string &a_filename );
    void            parseUnitsFile( const std::string &a_filename );
    const PVConfigLocal*  getPVConfigLocal( const std::string &a_devicename, const std::string &a_pvname );

    // ----- Methods extracted from ListenerLib -------------------------------

    void            ExtractDeviceInfo( PELE_STRUCT pStruct, CStringParser *myParser, std::string a_rootdevice );
    void            GetDevInfo( PELE_STRUCT pStruct, std::string a_rootname, std::string & a_devicename, Identifier & a_app_id );
    void            parseVarMapValue( CString value, std::string &a_connection, std::string &a_name, std::map<int,std::string> &a_enum );
    DataType        GetDataType(PELE_STRUCT pStruct);
    Access          GetDataAccess(PELE_STRUCT pStruct);

    PVStreamer         &m_streamer;         ///< PVStreamer instance
    IPVConfigServices  &m_cfg_service;      ///< PVStreamer configuration interface
    std::string         m_hostname;         ///< Hostname that will provide configuration information
    std::string         m_config_file;      ///< Name of main DAS configuration file
    std::string         m_options_file;     ///< Name of options file
    std::string         m_units_file;       ///< Name of units file
    NI::CNiDataSocket   m_file_socket;      ///< DataSocket that receives filename information

    //static std::set<std::string>            m_disabled_pvs; ///< List of "globally" disabled process vars (by name)
    static std::map<std::string,std::map<std::string,PVConfigLocal> > m_pv_config_local;
    static Identifier                       m_next_dev_id;  ///< Next auto-assigned device ID
    static std::map<Identifier,Identifier>  m_next_pv_id;   ///< Next auto-assigned pv ID for each device
};

}}}

#endif
