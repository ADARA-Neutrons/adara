#include <libxml/tree.h>
#include "EPICS_PVConfigMgr.h"

namespace SNS { namespace PVS { namespace LDAS {

using namespace std;

EPICS_PVConfigMgr::EPICS_PVConfigMgr( PVStreamer & a_streamer, const std::string &a_file )
 : PVConfig( a_streamer, EPICS_PROTOCOL ), m_config_file(a_file), m_filemon_thread(0)
{
    // Create an EPICS DAS PV reader (will be owned by PVStreamer)
    new LDAS_PVReader( *pvs );

    m_filemon_thread = new boost::thread( boost::bind(&EPICS_PVConfigMgr::fileMonThread, this));
}

EPICS_PVConfigMgr::~EPICS_PVConfigMgr()
{
}

/** \brief Loads and processes EPICS configuration file contents
  *
  * This method loads and parses the XML content of the EPICS configuration
  * file (provided in the ctor for this class). The contents of this file
  * includes a list of devices and the associated process variables for each.
  * Due to the current design of the SNS EPICS slow controls system, there is
  * no indication of when devices are added, removed, or changed in the config
  * file, so this method must analyze the definitions of each device and
  * process variable to determine their status and make the appropriate
  * changes and notifications on the streaming side.
  *
  * If a new device is found, a DDP will be emitted describing the new device.
  * If a previously defined device is no longer found, a device inactive
  * message will be sent to the internal stream.
  */
void
EPICS_PVConfigMgr::readConfigFile()
{
    xmlDocPtr doc = xmlReadFile( m_config_file.c_str(), 0, XML_PARSE_NOERROR | XML_PARSE_NOWARNING );

    if ( !doc )
        EXC( EC_INVALID_CONFIG_DATA, "Could not read/parse EPICS config file: " << a_file );

    xmlNodePtr  node;
    xmlChar    *val;
    int         d,p;
    bool        log;
    string      pv_name;
    PVInfo     *pv_info;

    try
    {
        // Use xpath to retrieve device nodes
        xmlXPathContextPtr  xcon = xmlXPathNewContext( doc );
        xmlXPathObjectPtr   devs = xmlXPathEvalExpression( "/beamline/devices/device", xcon );
        xmlXPathObjectPtr   pvs;

        for ( d = 0; d < devs->nodeNR; ++d )
        {
            // Is this device active?
            val = xmlGetProp( res->nodeTab[d], "active" );
            if ( val && !xmlStrcmp( val, (const xmlChar *)"true") )
            {
                // Request all PV nodes
                xcon->node = res->nodeTab[i];
                pvs = xmlXPathEvalExpression( "pv", xcon );

                for ( p = 0; p < pvs->nodeNR; ++p )
                {
                    log = false;
                    pv_name = "";

                    node = pvs->nodeTab[p]->xmlChildrenNode;
                    while ( node )
                    {
                        if ( !xmlStrcmp( node->name, (const xmlChar *)"name"))
                            pv_name = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
                        if ( !xmlStrcmp( node->name, (const xmlChar *)"log"))
                            log = true;
                        node = node->next;
                    }

                    if ( log && !pv_name.empty())
                    {
                        info = new PVInfo();

                        info->m_source = "";
                        info->m_protocol = EPCICS_PROTOCOL;
                        info->m_device_id = m_cfg_service.
                    }
                }

                xmlXPathFreeObject( pvs );
            }
        }

        xmlXPathFreeObject( devs );
    }
    catch( std::exception &e )
    {
        //EXC( EC_INVALID_CONFIG_DATA, "Failed parsing Device Descriptor packet on tag: " << tag << ", value: " << value << "\n" << e.what() )
    }
    catch( ... )
    {
        //EXC( EC_INVALID_CONFIG_DATA, "Failed parsing Device Descriptor packet on tag: " << tag << ", value: " << value )
    }

    xmlFreeDoc( doc );
}


/** \brief Thread for monitoring EPICS config file
  *
  * This method is a thread that polls the update time of the EPICS beamline
  * configuration file and reloads its content when changed. This is required
  * due to a lack of a configuration notification mechanism with the current
  * SNS EPICS slow control design.
  */
void
EPICS_PVConfigMgr::fileMonThread()
{
    time_t last = 0;
    time_t cur;

    while( 1 )
    {
        boost::this_thread::sleep( boost::posix_time::seconds( 5 ));

        cur = boost::filesystem::last_write_time( m_config_file );
        if ( cur > last )
        {
            readConfigFile();
            last = cur;
        }
    }
}

}}}
