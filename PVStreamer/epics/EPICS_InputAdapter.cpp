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

using namespace std;

namespace PVS {
namespace EPICS {


InputAdapter::InputAdapter( const std::string &a_config_file )
  : IInputAdapter(), m_active(true), m_config_file(a_config_file), m_source("epics")
{
    // Enable pre-emptive callbacks from EPICS
    ca_context_create( ca_enable_preemptive_callback );
    m_epics_context = ca_current_context();

    m_config_file_monitor_thread = new boost::thread( boost::bind( &InputAdapter::configFileMonitorThread, this ));
    m_gc_thread = new boost::thread(boost::bind( &InputAdapter::gcThread, this ));
}


InputAdapter::~InputAdapter()
{
    m_active = false;

    stopAllDevices();

    m_gc_thread->join();
    m_config_file_monitor_thread->join();
}


void
InputAdapter::startDevice( DeviceDescriptor *a_device )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<string,DeviceAgent*>::iterator idev = m_dev_agents.find( a_device->m_name );
    if ( idev != m_dev_agents.end())
    {
        idev->second->update( a_device );
    }
    else
    {
        m_dev_agents[a_device->m_name] = new DeviceAgent( *m_srteam_api, a_device, m_epics_context );
    }
}


void
InputAdapter::stopDevice( const std::string &a_dev_name )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    map<string,DeviceAgent*>::iterator idev = m_dev_agents.find( a_dev_name );
    if ( idev != m_dev_agents.end())
    {
        idev->second->stop();
        m_garbage.push_back( idev->second );
        m_dev_agents.erase( idev );
    }
}


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


void
InputAdapter::configFileMonitorThread()
{
    vector<DeviceDescriptor*>::iterator idev;
    vector<PVDescriptor*>::iterator ipv;
    set<string>::iterator icur;
    boost::filesystem::path cfg_path( m_config_file );
    time_t  t_now, t_last = 0;
    bool    changed;
    unsigned short count = 0;
    vector<char>  buffer;

    ca_attach_context( m_epics_context );

    while( m_active )
    {
        try
        {
            if ( !count % 5 ) // Only check every 5 seconds
            {
                // If config file has been updated, read & process
                // (last_write_time will throw if it can't access the file)
                t_now = boost::filesystem::last_write_time( cfg_path );
                if ( t_now > t_last )
                {
                    // Open, read, and compare contents of config file
                    // If contents differ (binary compare), then attempt to parse the buffer
                    // If parsing succeeds, apply new configuration

                    ifstream inf( m_config_file.c_str() );
                    if ( inf.is_open())
                    {
                        filebuf *fbuf = inf.rdbuf();
                        size_t  fsz = fbuf->pubseekoff( 0, inf.end, inf.in );

                        fbuf->pubseekpos( 0, inf.in );
                        if ( fsz > 0 )
                        {
                            buffer.resize( fsz );
                            fbuf->sgetn( buffer.data(), fsz );
                        }

                        inf.close();
                        t_last = t_now; // Reset last write time even if buffer doesn't parse

                        // See if contents have actually change
                        changed = true;
                        if ( buffer.size() == m_config_buffer.size() )
                        {
                            if ( memcmp( buffer.data(), m_config_buffer.data(), buffer.size() ) == 0 )
                                 changed = false;
                        }

                        if ( changed )
                        {
                            syslog( LOG_INFO, "EPICS beam config file has changed" );
cout << endl << "CFG CHANGE" << endl << endl;
                            boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

                            if ( !m_active )
                                break;

                            vector<DeviceDescriptor*> devices;

                            if ( parseConfigBuffer( buffer.data(), buffer.size(), devices ))
                            {
                                // Parsed successfully

                                // Keep track of new device names
                                set<string> new_devices;
                                for ( idev = devices.begin(); idev != devices.end(); ++idev )
                                {
                                    cout << "Adding " << (*idev)->m_name << endl;
                                    new_devices.insert( (*idev)->m_name );
                                }

                                // Start device agents for all configured devices
                                // It's OK if agents are already running
                                // NOTE: The DeviceDescriptor ptrs in devices vector will be deleted by the startDevice call
                                for ( idev = devices.begin(); idev != devices.end(); ++idev )
                                {
                                    //syslog( LOG_DEBUG, "Starting device agent for: %s", (*idev)->m_name.c_str() );
                                    startDevice( *idev );
                                }

                                // Stop device agents that are no longer configured
                                for ( icur = m_cur_devices.begin(); icur != m_cur_devices.end(); ++icur )
                                {
                                    if ( new_devices.find( *icur ) == new_devices.end())
                                    {
                                        cout << "Stop " << *icur << endl;
                                        stopDevice( *icur );
                                    }
                                }

                                // Save new device names and new buffer
                                m_cur_devices = new_devices;
                                m_config_buffer = buffer;
                            }
                            else
                            {
                                syslog( LOG_ERR, "EPICS beam config file failed to parse" );
                            }
                        }
                    }
                }
            }
        }
        catch(...)
        {
        }

        sleep(1);
    }
}


/**
  * This method tries to parse the xml configuration data stored in the provided
  * buffer and returns a vector of device descriptors in the a_devices output
  * paramter. The process variables associated with the output devices have
  * arbitrary type and unit information as a CA connection is needed to
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
                    <name>pv_name</name>        [name is the EPICS CA name without any prefix]
                    <alias>pv_active</alias>    [not used]
                    <log/>                      [only PVs with log tag are used]
                    <scan/>                     [not used]
                </pv>
            </device>
        </devices>
    </beamline>
    */

    bool res = false;

    xmlDocPtr doc = xmlReadMemory( a_buffer, a_buffer_size, 0, 0, 0 );
    if ( doc )
    {
        try
        {
            xmlNode *beam_node;
            xmlNode *devices_node;
            xmlNode *tmp_node;
            string  value;
            string dev_name;
            vector<string> pvs;

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
                                             if ( xmlFind( "log", cnode ))
                                             {
                                                 tmp_node = xmlFind( "name", cnode );
                                                 if ( tmp_node )
                                                 {
                                                     xmlGetValue( tmp_node, value );
                                                     pvs.push_back( value );
                                                 }
                                             }
                                        }
                                    }

                                    if ( dev_name.empty())
                                        res = false; // It's an error to omit the device name
                                    else if ( pvs.size())
                                    {
                                        syslog( LOG_DEBUG, "Found device: %s", dev_name.c_str() );
                                        DeviceDescriptor *dev = new DeviceDescriptor( dev_name, m_source, EPICS_PROTOCOL );
                                        for ( vector<string>::iterator ipv = pvs.begin(); ipv != pvs.end(); ++ipv )
                                        {
                                            syslog( LOG_DEBUG, "With PV: %s", (*ipv).c_str() );

                                            // Don't know type or units yet - just use any value
                                            dev->definePV( *ipv, *ipv, PV_INT, 0, "" );
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
            return true;
        }

        attribute = attribute->next;
    }

    a_value = "";
    return false;
}

}}

