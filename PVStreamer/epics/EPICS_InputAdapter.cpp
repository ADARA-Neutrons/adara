#include <iostream>
#include <fstream>
#include <sstream>
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

void ca_exception_handler( struct exception_handler_args args )
{
    const char *pName = ( args.chid ) ? ca_name( args.chid ) : "(Unknown)";

    syslog( LOG_ERR,
        "%s %s: %s! %s=[%s] - %s %s=[%s] %s=%ld %s=%ld %s=[%s] %s=%ld [%s]",
        "PVSD ERROR:", "EPICSInputAdapter::ca_exception_handler()",
        "Caught EPICS Exception", "Context", args.ctx,
        "with Request", "ChannelId", pName,
        "Stat", args.stat, "Operation", args.op,
        "DataType", dbr_type_to_text( args.type ), "Count", args.count,
        "Continuing...!" );
    usleep(33333); // give syslog a chance...
}

/** \brief EPICS::InputAdapter constructor
  * \param a_stream_serv - Parent StreamService instance
  * \param a_config_file - EPICS configuration file
  */
InputAdapter::InputAdapter( StreamService &a_stream_serv,
        const std::string &a_config_file, bool a_track_logged,
        time_t a_device_init_timeout )
    : IInputAdapter(a_stream_serv), m_active(true),
      m_config_file(a_config_file), m_track_logged(a_track_logged),
      m_cfg_mon_thread(0), m_source("epics"), m_gc_thread(0),
      m_device_init_timeout(a_device_init_timeout)
{
    // Enable pre-emptive callbacks from EPICS
    ca_context_create( ca_enable_preemptive_callback );

    // Save EPICS thread context so other thread can join
    m_epics_context = ca_current_context();

    // Capture the StreamService's ConfigManager's Device ID Offset... ;-Q
    m_offset = a_stream_serv.getCfgMgr().getOffset();

    // Install Non-Default (And Non-Terminating!) Channel Access
    // Exception Handler for the PVSD...! ;-D
    ca_add_exception_event ( ca_exception_handler , 0 );

    // Start monitor and GC threads
    m_cfg_mon_thread = new boost::thread(
        boost::bind( &InputAdapter::configFileMonitorThread, this ) );
    m_gc_thread = new boost::thread(
        boost::bind( &InputAdapter::gcThread, this ) );
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


/** \brief Method to return number of Active Devices (Running Device Agents)
  */
uint32_t
InputAdapter::numActiveDevices()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_dev_agents.size();
}


/** \brief Method to return number of Inactive Devices (in Config File)
  */
uint32_t
InputAdapter::numInactiveDevices()
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    return m_inactive_device_ids.size();
}


/** \brief Method to return Device Status Counts
  */
void
InputAdapter::getDevicesStatus(
        uint32_t &a_partialDeviceCount, uint32_t &a_hungDeviceCount,
        uint32_t &a_inactiveDeviceCount, // via Active Status PV State
        uint32_t &a_readyPVCount, uint32_t &a_totalPVCount )
{
    boost::lock_guard<boost::recursive_mutex> lock(m_mutex);

    a_partialDeviceCount = 0;
    a_hungDeviceCount = 0;
    a_inactiveDeviceCount = 0;

    a_readyPVCount = 0;
    a_totalPVCount = 0;

    for ( map<string,DeviceAgent*>::iterator idev = m_dev_agents.begin();
            idev != m_dev_agents.end(); ++idev )
    {
        uint32_t ready_pvs, total_pvs;
        bool hung, active;

        idev->second->deviceStatus( ready_pvs, total_pvs, hung, active );

        if ( !active )
            a_inactiveDeviceCount++;

        else if ( hung )
            a_hungDeviceCount++;

        else if ( ready_pvs < total_pvs )
            a_partialDeviceCount++;

        a_readyPVCount += ready_pvs;
        a_totalPVCount += total_pvs;
    }
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

    if ( idev != m_dev_agents.end() )
    {
        syslog( LOG_DEBUG, "%s: Updating Device Agent for [%s]",
            "InputAdapter::startDevice()",
            a_device->m_name.c_str() );
        usleep(33333); // give syslog a chance...

        idev->second->update( a_device );
    }
    else
    {
        syslog( LOG_DEBUG, "%s: Starting New Device Agent for [%s]",
            "InputAdapter::startDevice()",
            a_device->m_name.c_str() );
        usleep(33333); // give syslog a chance...

        m_dev_agents[a_device->m_name] =
            new DeviceAgent( *m_stream_api, a_device, m_epics_context,
                m_device_init_timeout );
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
        syslog( LOG_DEBUG, "%s: Stopping old device agent (%s) for [%s]",
            "InputAdapter::stopDevice()", "device no longer defined",
            a_dev_name.c_str() );
        usleep(33333); // give syslog a chance...

        idev->second->stop();
        m_garbage.push_back( idev->second );
        m_dev_agents.erase( idev );
    }
    else
    {
        syslog( LOG_ERR,
            "%s %s: Error Stopping Device Agent: [%s] Not Found!",
            "PVSD ERROR:", "InputAdapter::stopDevice()",
            a_dev_name.c_str() );
        usleep(33333); // give syslog a chance...
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
    map<string,DeviceAgent*>::iterator ida;
    vector<Identifier>::iterator iid;
    set<string>::iterator icur;
    boost::filesystem::path cfg_path( m_config_file );
    bool            changed;
    vector<char>    buffer;
    unsigned short  poll_rate = 15;
    unsigned short  count = poll_rate;  // Check "Right Away" on Startup!

    ca_attach_context( m_epics_context );

    while ( m_active )
    {
        try
        {
            if ( count++ == poll_rate ) // Check based on current poll rate
            {
                count = 0;

                // Open, read, and compare contents of config file
                // If contents differ (binary compare), then
                // attempt to parse the buffer.
                // If parsing succeeds, apply new configuration
                ifstream inf( m_config_file.c_str() );
                if ( inf.is_open() )
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

                        // If file was truncated while reading,
                        // adjust buffer size
                        if ( fsz < buffer.size() )
                            buffer.resize( fsz );
                    }

                    inf.close();

                    // See if contents have actually change
                    changed = false;
                    if ( buffer.size() != m_config_buffer.size() )
                    {
                        changed = true;
                    }
                    else
                    {
                        if ( memcmp( buffer.data(), m_config_buffer.data(),
                                buffer.size() ) != 0 )
                        {
                            changed = true;
                        }
                    }

                    if ( changed )
                    {
                        syslog( LOG_INFO,
                            "%s: EPICS Beam Config File %s has Changed",
                            "InputAdapter::configFileMonitorThread():",
                            m_config_file.c_str() );
                        usleep(33333); // give syslog a chance...

                        boost::lock_guard<boost::recursive_mutex>
                            lock(m_mutex);

                        if ( !m_active )
                            break;

                        vector<DeviceDescriptor*> devices;
                        vector<Identifier> inactive_device_ids;

                        if ( parseConfigBuffer(
                            buffer.data(), buffer.size(),
                            devices, inactive_device_ids ) )
                        {
                            // Parsed successfully
                            syslog( LOG_INFO,
                                "%s: EPICS Beam Config File Parse OK",
                                "InputAdapter::configFileMonitorThread():");
                            usleep(33333); // give syslog a chance...

                            // Keep track of new device names
                            set<string> new_device_names;
                            for ( idev = devices.begin();
                                    idev != devices.end(); ++idev )
                            {
                                new_device_names.insert( (*idev)->m_name );
                            }

                            // Stop device agents that are
                            // no longer configured
                            // (Do This *Before* Starting New Devices,
                            // to Avoid Any PV Name Clashes from
                            // Device Re-Naming/Shuffling... ;-D)
                            for ( icur = m_cur_device_names.begin();
                                    icur != m_cur_device_names.end();
                                    ++icur )
                            {
                                if ( new_device_names.find( *icur )
                                        == new_device_names.end() )
                                {
                                    stopDevice( *icur );
                                }
                            }

                            // Now "Undefine" Any Old Device IDs that are
                            // No Longer In Use...! ;-D
                            for ( iid = inactive_device_ids.begin();
                                    iid != inactive_device_ids.end();
                                    ++iid )
                            {
                                bool found = false;

                                for ( ida = m_dev_agents.begin();
                                        ida != m_dev_agents.end(); ++ida )
                                {
                                    DeviceDescriptor *desc =
                                        ida->second->get_desc();
                                    if ( desc && desc->m_id == (*iid) )
                                    {
                                        ida->second->undefine();
                                        found = true;
                                    }
                                }

                                if ( !found )
                                {
                                    syslog( LOG_ERR, "%s %s::%s: %s %d %s",
                                        "PVSD ERROR:",
                                        "InputAdapter",
                                        "configFileMonitorThread()",
                                        "Inactive Device ID", *iid,
                                        "Not Found!");
                                    usleep(33333); // give syslog a chance
                                }
                            }

                            // Save new device names
                            m_cur_device_names = new_device_names;

                            // Save inactive device ids
                            m_inactive_device_ids = inactive_device_ids;

                            // Start device agent for all configured devices
                            // It's OK if agents are already running
                            // NOTE: The DeviceDescriptor ptrs in devices
                            // vector will be deleted by startDevice call
                            for ( idev = devices.begin();
                                    idev != devices.end(); ++idev )
                            {
                                startDevice( *idev );
                            }

                            // Save new config buffer
                            m_config_buffer = buffer;

                            ADARA::ComBus::SignalRetractMessage
                                msg( "SID_EPICS_CFG_ERROR" );
                            ADARA::ComBus::Connection::getInst().broadcast(
                                msg );
                        }
                        else
                        {
                            stringstream ss;
                            ss << "PVSD ERROR:"
                                << " InputAdapter::"
                                << "configFileMonitorThread():"
                                << " Failed to Parse"
                                << " EPICS Beam Config File!"
                                << " [" << m_config_file << "]";

                            syslog( LOG_ERR, "%s", ss.str().c_str() );
                            usleep(33333); // give syslog a chance...

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
                << " InputAdapter::configFileMonitorThread():"
                << " Exception Parsing"
                << " EPICS Beam Config File!"
                << " [" << m_config_file << "]"
                << " [" << e.what() << "]";

            syslog( LOG_ERR, "%s", ss.str().c_str() );

            ADARA::ComBus::SignalAssertMessage msg(
                "SID_EPICS_CFG_ERROR", "CONFIG", ss.str(), ADARA::ERROR );
            ADARA::ComBus::Connection::getInst().broadcast( msg );
        }
        catch( ... )
        {
            stringstream ss;
            ss << "PVSD ERROR:"
                << " InputAdapter::configFileMonitorThread():"
                << " Unexpected Exception Parsing"
                << " EPICS Beam Config File!"
                << " [" << m_config_file << "]";

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
  * \return True if buffered parsed successfully; false otherwise
  *
  * This method tries to parse the xml configuration data stored in the
  * provided buffer and outputs a vector of device descriptors in the
  * a_devices parameter. The process variables associated with the output
  * devices have unknown type and unit information as a CA connection is
  * needed to determine these values. Only devices and PVs that are
  * configured for logging will be returned, unless m_track_logged is false.
  */
bool
InputAdapter::parseConfigBuffer( const char* a_buffer, int a_buffer_size,
        vector<DeviceDescriptor*> &a_devices,
        vector<Identifier> &a_inactive_device_ids )
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

    vector<pair<string,string> >::iterator ipv;
    bool res = false;

    // Count Device IDs from Here (_Not_ in ConfigManager::defineDevice()!)
    // -> Don't Wait for Devices to Go Active!
    // This makes for a more consistent and dependable numbering scheme;
    // be sure to count _Every_ Device, not just "Active" or Valid ones...!
    // NOTE: 1st Device ID should be "1 + Offset", Pre-Increment Before Use!
    Identifier id = m_offset;

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
            string  active_pv_conn;
            vector<pair<string,string> > pvs;

            // Skip to beamline section
            beam_node = xmlDocGetRootElement(doc);

            if ( xmlStrcmp( beam_node->name,
                (const xmlChar*)"beamline" ) == 0 )
            {
                if (( devices_node = xmlFind( "devices", beam_node )))
                {
                    for ( xmlNode *node = devices_node->children;
                        node; node = node->next )
                    {
                        res = true;

                        if ( xmlStrcmp( node->name,
                            (const xmlChar*)"device" ) == 0 )
                        {
                            // Get Next Device ID...
                            // Note: If we get to id == 4294967295
                            // ( (uint32_t) -1 ) then, aside from being
                            // totally screwed in general, we will also
                            // overwrite the ADARA OutputAdapter's
                            // Heartbeat Device... Lol... ;-D
                            // (Probably not an issue, just saying... ;-)
                            ++id;

                            bool is_active = false;

                            active_pv_conn.clear();

                            if ( xmlGetAttribute( node, "active", value ))
                            {
                                // "Active" Device
                                // - Either Static "true" or Via Status PV
                                if ( value.compare( "false" ) )
                                {
                                    is_active = true;

                                    if ( value.compare( "true" ) )
                                        active_pv_conn = value;

                                    dev_name.clear();
                                    pvs.clear();

                                    for ( xmlNode *cnode = node->children;
                                        cnode; cnode = cnode->next )
                                    {
                                        if ( xmlStrcmp( cnode->name,
                                            (const xmlChar*)"name" ) == 0 )
                                        {
                                            xmlGetValue( cnode, dev_name );
                                        }
                                        else if ( xmlStrcmp( cnode->name,
                                            (const xmlChar*)"pv" ) == 0 )
                                        {
                                            // If track logged only, then
                                            // make sure log tag is present
                                            if ( !m_track_logged
                                                || xmlFind( "log", cnode ) )
                                            {
                                                pv_name.clear();
                                                pv_conn.clear();

                                                tmp_node = xmlFind( "name",
                                                    cnode );
                                                if ( !tmp_node )
                                                {
                                                    stringstream ss;
                                                    ss << "InputAdapter::"
                                                    << "parseConfigBuffer()"
                                                    << ": PV Name is"
                                                    << " missing or empty";
                                                    syslog( LOG_ERR,
                                                        "%s %s",
                                                        "PVSD ERROR:",
                                                        ss.str().c_str() );
                                                    throw -1;
                                                }

                                                // 'name' field is
                                                // connection in xml
                                                xmlGetValue( tmp_node,
                                                    pv_conn );

                                                // 'alias' is name
                                                tmp_node = xmlFind( "alias",
                                                    cnode );
                                                if ( tmp_node )
                                                {
                                                    xmlGetValue( tmp_node,
                                                        pv_name );
                                                }
                                                else
                                                    pv_name = pv_conn;

                                                pvs.push_back( make_pair(
                                                    pv_name, pv_conn ));
                                            }
                                            // If Not Logging PV, Log It...
                                            // Lol...! :-D
                                            else
                                            {
                                                pv_name.clear();
                                                pv_conn.clear();

                                                tmp_node = xmlFind( "name",
                                                    cnode );
                                                if ( tmp_node )
                                                {
                                                    // 'name' field is
                                                    // connection in xml
                                                    xmlGetValue( tmp_node,
                                                        pv_conn );

                                                    // 'alias' is name
                                                    tmp_node = xmlFind(
                                                        "alias", cnode );
                                                    if ( tmp_node )
                                                    {
                                                        xmlGetValue(
                                                            tmp_node,
                                                            pv_name );
                                                    }
                                                    else
                                                        pv_name = pv_conn;

                                                    stringstream ss;
                                                    ss << "InputAdapter::"
                                                    << "parseConfigBuffer()"
                                                    << ": Device ["
                                                    << dev_name << "]: "
                                                    << "Ignoring Non-Logged"
                                                    << " PV <" << pv_conn
                                                    << "> (" << pv_name
                                                    << ")";
                                                    syslog( LOG_WARNING,
                                                        "%s",
                                                        ss.str().c_str() );
                                                    // give syslog a chance
                                                    usleep(33333);
                                                }
                                            }
                                        }
                                    }

                                    if ( dev_name.empty() )
                                    {
                                        // It's an Error to Omit the
                                        // Device Name
                                        syslog( LOG_ERR,
                                            "%s: %s::%s(): %s -> %s = %d",
                                            "PVSD ERROR", "InputAdapter",
                                            "parseConfigBuffer",
                                            "Device Name Missing or Empty",
                                            "Device ID", id );
                                        throw -1;
                                    }
                                    else if ( pvs.size() )
                                    {
                                        DeviceDescriptor *dev =
                                            new DeviceDescriptor( dev_name,
                                                m_source, EPICS_PROTOCOL,
                                                active_pv_conn );

                                        dev->m_id = id;

                                        string activeStr = "";
                                        if ( !active_pv_conn.empty() )
                                        {
                                            activeStr = " (Active PV Conn ["
                                                + active_pv_conn + "])";
                                        }

                                        syslog( LOG_ERR,
                                            "%s::%s: %s [%s] -> %s = %d%s",
                                            "InputAdapter",
                                            "parseConfigBuffer()",
                                            "Active Device",
                                            dev_name.c_str(),
                                            "Device ID", id,
                                            activeStr.c_str() );
                                        // give syslog a chance...
                                        usleep(33333);

                                        for ( ipv = pvs.begin();
                                            ipv != pvs.end(); ++ipv )
                                        {
                                            // Subsume Any PV References
                                            // to the Active Status PV...!
                                            if ( !ipv->second.compare(
                                                    active_pv_conn ) )
                                            {
                                                // *Don't* Ignore This
                                                // Active Status PV, Send
                                                // Its Value Updates Thru!
                                                dev->m_active_pv->m_ignore =
                                                    false;

                                                // Capture Any Alias from
                                                // the PV Being Subsumed...!
                                                string aliasStr = "";
                                                if ( ipv->second.compare(
                                                        ipv->first ) )
                                                {
                                                    dev->m_active_pv->m_name
                                                        = ipv->first;

                                                    aliasStr =
                                                        " (Use PV Alias ["
                                                        + ipv->first + "])";
                                                }

                                                stringstream ss;
                                                ss << "InputAdapter::"
                                                << "parseConfigBuffer()"
                                                << ": Active Status PV"
                                                << " Subsumes Device PV!"
                                                << " [" << dev_name << "]"
                                                << " -> Device ID = " << id
                                                << activeStr << aliasStr;
                                                syslog( LOG_ERR, "%s",
                                                    ss.str().c_str() );
                                                // give syslog a chance...
                                                usleep(33333);
                                            }

                                            else
                                            {
                                                // Don't know type/units yet
                                                // - just use any value
                                                dev->definePV(
                                                    ipv->first, ipv->second,
                                                    PV_INT, 1, 0, "" );
                                            }
                                        }

                                        a_devices.push_back( dev );
                                    }
                                }
                            }

                            // Inactive Device - Parse Enough to Log...
                            if ( !is_active )
                            {
                                dev_name.clear();

                                for ( xmlNode *cnode = node->children;
                                    cnode; cnode = cnode->next )
                                {
                                    if ( xmlStrcmp( cnode->name,
                                        (const xmlChar*)"name" ) == 0 )
                                    {
                                        xmlGetValue( cnode, dev_name );
                                    }
                                }

                                if ( !dev_name.empty())
                                {
                                    syslog( LOG_WARNING,
                                        "%s: %s [%s] -> %s = %d",
                                        "InputAdapter::parseConfigBuffer()",
                                        "Ignoring Inactive Device",
                                        dev_name.c_str(), "Device ID", id );
                                }
                                else
                                {
                                    syslog( LOG_WARNING,
                                        "%s: %s! -> %s = %d",
                                        "InputAdapter::parseConfigBuffer()",
                                        "Ignoring Unnamed Inactive Device",
                                        "Device ID", id );
                                }
                                usleep(33333); // give syslog a chance...

                                // Save Device ID to Inactive List...
                                a_inactive_device_ids.push_back( id );
                            }
                        }
                    }
                }
            }
        }
        catch( std::exception &e )
        {
            syslog( LOG_ERR,
                "%s %s: Exception While Parsing EPICS beamline XML - %s",
                "PVSD ERROR:", "InputAdapter::parseConfigBuffer()",
                e.what() );
            res = false;
        }
        catch(...)
        {
            syslog( LOG_ERR,
                "%s %s: Exception While Parsing EPICS beamline XML",
                "PVSD ERROR:", "InputAdapter::parseConfigBuffer()" );
            res = false;
        }
    }

    // If parse failed, clean-up
    if ( !res )
    {
        for ( vector<DeviceDescriptor*>::iterator idev = a_devices.begin();
            idev != a_devices.end(); ++idev )
        {
            delete *idev;
        }

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

// vim: expandtab

