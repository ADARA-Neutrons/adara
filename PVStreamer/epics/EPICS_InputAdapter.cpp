#include <iostream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>
#include <boost/algorithm/string.hpp>
#include <cadef.h>
#include <syslog.h>

#include "TraceException.h"
#include "ConfigManager.h"
#include "EPICS_InputAdapter.h"
#include "EPICS_DeviceAgent.h"
#include "ComBus.h"
#include "ComBusMessages.h"

using namespace std;

namespace PVS {
namespace EPICS {


//=============================================================================
//----- Public Methods --------------------------------------------------------


/** \brief EPICS::InputAdapter constructor
  * \param a_stream_serv - Parent StreamService instance
  * \param a_config_file - EPICS configuration file
  */
InputAdapter::InputAdapter( StreamService &a_stream_serv, const std::string &a_config_file, bool a_track_logged )
  : IInputAdapter(a_stream_serv), m_active(true), m_config_file(a_config_file), m_track_logged(a_track_logged),
    m_cfg_mon_thread(0), m_source("epics"), m_gc_thread(0)
{
    // Enable pre-emptive callbacks from EPICS
    ca_context_create( ca_enable_preemptive_callback );
    // Save EPICS thread context so other thread can join
    m_epics_context = ca_current_context();

    // Start monitor and GC threads
    m_cfg_mon_thread = new boost::thread( boost::bind( &InputAdapter::configFileMonitorThread, this ));
    m_gc_thread = new boost::thread( boost::bind( &InputAdapter::gcThread, this ));
}


/** \brief EPICS::InputAdapter destructor
  */
InputAdapter::~InputAdapter()
{
    m_active = false;

    stopAllDevices();

    m_gc_thread->join();
    m_cfg_mon_thread->join();
}


//=============================================================================
//----- Private Methods -------------------------------------------------------


/** \brief Starts a DeviceAgent for the given descriptor
  * \param a_device - [in] The DeviceDescriptor that will be handled by agent
  *
  * This method either starts or updates a DeviceAgent.
  */
void
InputAdapter::startDevice( DeviceDescriptor *a_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<string,DeviceAgent*>::iterator idev =
        m_dev_agents.find( a_device->m_name );

    if ( idev != m_dev_agents.end())
    {
        syslog( LOG_DEBUG, "Updating Device Agent for: %s",
            a_device->m_name.c_str() );

        idev->second->update( a_device );
    }
    else
    {
        syslog( LOG_DEBUG, "Starting New Device Agent for: %s",
            a_device->m_name.c_str() );

        m_dev_agents[a_device->m_name] =
            new DeviceAgent( *m_srteam_api, a_device, m_epics_context );
    }
}


/** \brief Stops the specified DeviceAgent instance
  * \param a_dev_name - Name of agent to stop
  *
  * Stopped agent is placed in the garbage for eventual destruction.
  */
void
InputAdapter::stopDevice( const std::string &a_dev_name )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<string,DeviceAgent*>::iterator idev =
        m_dev_agents.find( a_dev_name );

    if ( idev != m_dev_agents.end())
    {
        syslog( LOG_DEBUG,
            "Stopping old device agent (device no longer defined) for: %s",
            a_dev_name.c_str() );

        idev->second->stop();
        m_garbage.push_back( idev->second );
        m_dev_agents.erase( idev );
    }
    else
    {
        syslog( LOG_ERR, "%s Error Stopping Device Agent: %s Not Found!",
            "PVSD ERROR:", a_dev_name.c_str() );
    }
}


/** \brief Stops all running DeviceAgent instances
  *
  * Stopped agents are placed in the garbage for eventual destruction.
  */
void
InputAdapter::stopAllDevices()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    for ( map<string,DeviceAgent*>::iterator idev = m_dev_agents.begin(); idev != m_dev_agents.end(); ++idev )
    {
        idev->second->stop();
        m_garbage.push_back( idev->second );
    }

    m_dev_agents.clear();
}


/** \brief Garbage collection thread (for DeviceAgent instances)
  *
  * This thread deletes old DeviceAgent instances after they report their
  * connections have been closed. (This is probably not required b/c the EPICS
  * ca_clear_channel() method is apparently smart enough to block until all
  * active callbacks have returned.)
  */
void
InputAdapter::gcThread()
{
    // Attach context just in case DevAgent destructor needs to make epics calls
    ca_attach_context( m_epics_context );

    while( 1 )
    {
        sleep(1);

        boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

        for ( list<DeviceAgent*>::iterator idev = m_garbage.begin(); idev != m_garbage.end(); )
        {
            if ( (*idev)->stopped())
            {
                delete *idev;
                idev = m_garbage.erase( idev );
            }
            else
            {
                ++idev;
            }
        }

        if ( !m_active && !m_garbage.size() && !m_dev_agents.size() )
            break;
    }
}


/** \brief Configuration file monitoring thread
  *
  * This method slowly polls the update time on the provided EPICS beamline
  * configuration file, and when changed, reads and parses the xml content
  * to extract a payload of DeviceDescriptors for configured devices (and
  * PVs). These descriptors are used to start (or update) DeviceAgent
  * instances, and any previously configured devices that are no longer
  * needed are stopped. Starting/updating and stopping of DeviceAgents
  * is handled by the startDevice() and stopDevice() methods, respectively.
  */
void
InputAdapter::configFileMonitorThread()
{
    vector<DeviceDescriptor*>::iterator idev;
    vector<PVDescriptor*>::iterator ipv;
    set<string>::iterator icur;
    boost::filesystem::path cfg_path( m_config_file );
    bool            changed;
    unsigned short  count = 0;
    vector<char>    buffer;
    int             poll_rate = 15;

    ca_attach_context( m_epics_context );

    while ( m_active )
    {
        try
        {
            if ( count++ == poll_rate ) // Check based on current poll rate
            {
                count = 0;

                // Open, read, and compare contents of config file
                // If contents differ (binary compare), then attempt to parse the buffer
                // If parsing succeeds, apply new configuration
                ifstream inf( m_config_file.c_str() );
                if ( inf.is_open())
                {
                    // Calculate size of config file
                    filebuf *fbuf = inf.rdbuf();
                    size_t  fsz = fbuf->pubseekoff( 0, inf.end, inf.in );

                    // Open, read, and compare contents of config file
                    // If contents differ (binary compare), then attempt
                    // to parse the buffer. If parsing succeeds, apply
                    // new configuration

                    fbuf->pubseekpos( 0, inf.in );
                    if ( fsz > 0 )
                    {
                        // Adjust buffer size and read entire file
                        buffer.resize( fsz );
                        fsz = fbuf->sgetn( buffer.data(), fsz );

                        // If file was truncated while reading, adjust buffer size
                        if ( fsz < buffer.size() )
                            buffer.resize( fsz );
                    }

                    inf.close();

                    // See if contents have actually change
                    changed = false;
                    if ( buffer.size() != m_config_buffer.size() )
                        changed = true;
                    else
                    {
                        if ( memcmp( buffer.data(), m_config_buffer.data(), buffer.size() ) != 0 )
                                changed = true;
                    }

                    if ( changed )
                    {
                        syslog( LOG_INFO, "EPICS beam config file %s has changed", m_config_file.c_str() );

                        boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

                        if ( !m_active )
                            break;

                        vector<DeviceDescriptor*> devices;

                        if ( parseConfigBuffer( buffer.data(), buffer.size(), devices ))
                        {
                            // Parsed successfully
                            syslog( LOG_INFO, "EPICS beam config file parse OK" );

                            // Keep track of new device names
                            set<string> new_devices;
                            for ( idev = devices.begin(); idev != devices.end(); ++idev )
                                new_devices.insert( (*idev)->m_name );

                            // Start device agents for all configured devices
                            // It's OK if agents are already running
                            // NOTE: The DeviceDescriptor ptrs in devices vector will be deleted by the startDevice call
                            for ( idev = devices.begin(); idev != devices.end(); ++idev )
                            {
                                startDevice( *idev );
                            }

                            // Stop device agents that are no longer configured
                            for ( icur = m_cur_devices.begin(); icur != m_cur_devices.end(); ++icur )
                            {
                                if ( new_devices.find( *icur ) == new_devices.end())
                                    stopDevice( *icur );
                            }

                            // Save new device names and new buffer
                            m_cur_devices = new_devices;
                            m_config_buffer = buffer;

                            ADARA::ComBus::SignalRetractMessage msg( "SID_EPICS_CFG_ERROR" );
                            ADARA::ComBus::Connection::getInst().broadcast( msg );
                        }
                        else
                        {
                            stringstream ss;
                            ss << "PVSD ERROR:"
                                << " Failed to parse"
                                << " EPICS beamline.xml config file!";

                            syslog( LOG_ERR, "%s", ss.str().c_str() );

                            ADARA::ComBus::SignalAssertMessage msg(
                                "SID_EPICS_CFG_ERROR", "CONFIG", ss.str(),
                                ADARA::ERROR );
                            ADARA::ComBus::Connection::getInst().broadcast(
                                msg );
                        }
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            stringstream ss;
            ss << "PVSD ERROR:"
                << " Exception parsing"
                << " EPICS beamline.xml config file!";

            syslog( LOG_ERR, "%s", ss.str().c_str() );
            syslog( LOG_ERR, e.what() );

            ADARA::ComBus::SignalAssertMessage msg(
                "SID_EPICS_CFG_ERROR", "CONFIG", ss.str(), ADARA::ERROR );
            ADARA::ComBus::Connection::getInst().broadcast( msg );
        }
        catch( ... )
        {
            stringstream ss;
            ss << "PVSD ERROR:"
                << " Unexpected exception parsing"
                << " EPICS beamline.xml config file!";

            syslog( LOG_ERR, "%s", ss.str().c_str() );

            ADARA::ComBus::SignalAssertMessage msg(
                "SID_EPICS_CFG_ERROR", "CONFIG", ss.str(), ADARA::ERROR );
            ADARA::ComBus::Connection::getInst().broadcast( msg );
        }

        sleep(1);
    }
}


/** \brief Parses configuration file for DeviceDescriptor payload
  * \param a_buffer - [in] Buffer containing file contents
  * \param a_buffer_size - [in] Size of file buffer
  * \param a_devices - [out] Extracted DeviceDescriptor payload
  * \return True if buffered parsed succeddfully; false otherwise
  *
  * This method tries to parse the xml configuration data stored in the provided
  * buffer and outputs a vector of device descriptors in the a_devices
  * paramter. The process variables associated with the output devices have
  * unknown type and unit information as a CA connection is needed to
  * determine these values. Only devices and PVs that are configured for logging
  * will be returned.
  */
bool
InputAdapter::parseConfigBuffer( const char* a_buffer, int a_buffer_size, vector<DeviceDescriptor*> &a_devices )
{
    /* This method parses XML in the following format
    <beamline>
        <devices>
            <device active=true/false>          [only devices with active=true are used]
                <name>Device Name</name>        [name of device]
                <pv>
                    <name>pv_name</name>        [name is the EPICS CA connection string]
                    <alias>pv_active</alias>    [used as name if defined; otherwise connection is name]
                    <log/>                      [only PVs with log tag are used]
                    <scan/>                     [not used]
                </pv>
            </device>
        </devices>
    </beamline>
    */

    bool res = false;
    stringstream sstr;

    xmlDocPtr doc = xmlReadMemory( a_buffer, a_buffer_size, 0, 0, 0 );
    if ( doc )
    {
        try
        {
            xmlNode *beam_node;
            xmlNode *devices_node;
            xmlNode *tmp_node;
            string  value;
            string  dev_name;
            string  pv_name;
            string  pv_conn;
            vector<pair<string,string> > pvs;

            // Skip to beamline section
            beam_node = xmlDocGetRootElement(doc);

            if ( xmlStrcmp( beam_node->name, (const xmlChar*)"beamline" ) == 0 )
            {
                if (( devices_node = xmlFind( "devices", beam_node )))
                {
                    for ( xmlNode *node = devices_node->children; node; node = node->next )
                    {
                        res = true;

                        if ( xmlStrcmp( node->name, (const xmlChar*)"device" ) == 0 )
                        {
                            if ( xmlGetAttribute( node, "active", value ))
                            {
                                if ( value == "true" )
                                {
                                    dev_name.clear();
                                    pvs.clear();

                                    for ( xmlNode *cnode = node->children; cnode; cnode = cnode->next )
                                    {
                                        if ( xmlStrcmp( cnode->name, (const xmlChar*)"name" ) == 0 )
                                        {
                                            xmlGetValue( cnode, dev_name );
                                        }
                                        else if ( xmlStrcmp( cnode->name, (const xmlChar*)"pv" ) == 0 )
                                        {
                                            // If track logged only, then make sure log tag is present
                                            if ( !m_track_logged || xmlFind( "log", cnode ))
                                            {
                                                pv_name.clear();
                                                pv_conn.clear();

                                                tmp_node = xmlFind( "name", cnode );
                                                if ( !tmp_node )
                                                {
                                                    syslog( LOG_ERR,
                                                        "PVSD ERROR: PV Name is missing or empty" );
                                                    throw -1;
                                                }

                                                xmlGetValue( tmp_node, pv_conn ); // 'name' field is connection in xml

                                                tmp_node = xmlFind( "alias", cnode ); // 'alias' is name
                                                if ( tmp_node )
                                                    xmlGetValue( tmp_node, pv_name );
                                                else
                                                    pv_name = pv_conn;

                                                pvs.push_back( make_pair( pv_name, pv_conn ));
                                            }
                                        }
                                    }

                                    if ( dev_name.empty())
                                    {
                                        // It's an error to omit the device name
                                        syslog( LOG_ERR,
                                            "PVSD ERROR: Device name is missing or empty" );
                                        throw -1;
                                    }
                                    else if ( pvs.size())
                                    {
                                        DeviceDescriptor *dev = new DeviceDescriptor( dev_name, m_source, EPICS_PROTOCOL );
                                        for ( vector<pair<string,string> >::iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
                                        {
                                            // Don't know type or units yet - just use any value
                                            dev->definePV( ipv->first, ipv->second, PV_INT, 0, "" );
                                        }
                                        a_devices.push_back( dev );
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        catch(...)
        {
            syslog( LOG_ERR,
                "%s Exception while parsing EPICS beamline XML",
                "PVSD ERROR:" );
            res = false;
        }
    }

    // If parse failed, clean-up
    if ( !res )
    {
        for ( vector<DeviceDescriptor*>::iterator idev = a_devices.begin(); idev != a_devices.end(); ++idev )
            delete *idev;

        a_devices.clear();
    }

    return res;
}


/** \brief Helper function to find a child node by name (tag)
  * \param a_tag - [in] Tag to search for
  * \param a_parent_node - [in] Parent node to search
  * \return xmlNode pointer if found, null otherwise
  */
xmlNode*
InputAdapter::xmlFind( const char *a_tag, xmlNode *a_parent_node ) const
{
    for ( xmlNode *node = a_parent_node->children; node; node = node->next )
    {
        if ( xmlStrcmp( node->name, (const xmlChar*)a_tag ) == 0 )
            return node;
    }

    return 0;
}


/** \brief Helper function to read a node value
  * \param a_node - [in] Node to get value of
  * \param a_value - [out] Value read
  *
  * Retrieves trimmed value of a node.
  */
void
InputAdapter::xmlGetValue( xmlNode *a_node, string &a_value ) const
{
    if ( a_node->children && a_node->children->content )
    {
        a_value = (char*)a_node->children->content;
        boost::algorithm::trim( a_value );
    }
    else
        a_value = "";
}


/** \brief Helper function to read a node attribute
  * \param a_node - [in] Node that contains attribute
  * \param a_attrib - [in] Name of attribute to read
  * \param a_value - [out] Value of attribute read (if found)
  * \return True if attribute found; false otherwise
  *
  * Retrieves trimmed value of an attribute.
  */
bool
InputAdapter::xmlGetAttribute( xmlNode *a_node, const char *a_attrib, string &a_value ) const
{
    xmlAttr* attribute = a_node->properties;
    while( attribute && attribute->name )
    {
        if ( xmlStrcmp( attribute->name, (const xmlChar*)a_attrib ) == 0 )
        {
            if ( attribute->children && attribute->children->content )
                a_value = (char*)attribute->children->content;
            else
                a_value = "";

            boost::algorithm::trim( a_value );
            return true;
        }

        attribute = attribute->next;
    }

    a_value = "";
    return false;
}

}}

