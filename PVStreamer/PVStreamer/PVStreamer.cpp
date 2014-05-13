/**
 * \file PVStreamer.cpp
 * \brief Source file for PVStreamer class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"

#include <boost/thread/locks.hpp>

#include "PVStreamer.h"
#include "PVConfig.h"
#include "PVReader.h"
#include "PVWriter.h"

using namespace std;

namespace SNS { namespace PVS {

// ---------- Static initialization -------------------------------------------

Identifier PVStreamer::m_next_enum_id = 1;

// ---------- Constructors & Destructors --------------------------------------

/**
 * \brief Constructor for PVStreamer class.
 * \param a_pkt_buffer_size - Internal stream buffer size
 * \param a_max_notify_pkts - Max stream listener queue length (0 for default)
 */
PVStreamer::PVStreamer( size_t a_pkt_buffer_size, size_t a_max_notify_pkts )
: m_writer(0), m_pkt_buffer_size(a_pkt_buffer_size), m_max_notify_pkts(a_max_notify_pkts),m_stream_listeners_thread(0)
{
    // Make sure buffer sizes are sane
    if ( m_pkt_buffer_size < 2 || m_max_notify_pkts > m_pkt_buffer_size )
        EXC(EC_INVALID_PARAM,"Invalid buffer size parameter(s)");

    if ( !m_max_notify_pkts )
        m_max_notify_pkts = a_pkt_buffer_size >> 1;

    // Create stream packets and fill free queue;
    for ( size_t i = 0; i < a_pkt_buffer_size; ++i )
    {
        m_stream_pkts.push_back(new PVStreamPacket());
        m_free_que.put( m_stream_pkts.back() );
    }

    // Start stream listener notify thread
    m_stream_listeners_thread = new boost::thread( boost::bind(&PVStreamer::streamListenersNotifyThreadFunc, this));
}

/**
 * \brief Destructor for PVStreamer class.
 */
PVStreamer::~PVStreamer()
{
    // Deactivate all queues - will force waiting threads to wake
    m_free_que.deactivate();
    m_fill_que.deactivate();
    m_notify_que.deactivate();

    // Wait for listerner thread to exit
    m_stream_listeners_thread->join();

    // Delete stream listener thread
    if ( m_stream_listeners_thread )
        delete m_stream_listeners_thread;

    // Delete PVConfig instances
    for ( map<Protocol,PVConfig*>::iterator ic = m_config.begin(); ic != m_config.end(); ++ic )
        delete ic->second;

    // Delete PVReader instances
    for ( map<Protocol,PVReader*>::iterator ir = m_readers.begin(); ir != m_readers.end(); ++ir )
        delete ir->second;

    // Delete PVWriter instance
    if ( m_writer )
        delete m_writer;

    // Delete stream packets
    for ( vector<PVStreamPacket*>::iterator ip = m_stream_pkts.begin(); ip != m_stream_pkts.end(); ++ip )
        delete *ip;

    // Delete enums
    for ( map<unsigned long,Enum*>::iterator e = m_enums.begin(); e != m_enums.end(); ++e )
        delete e->second;

    // Delete PV Info
    for ( map<PVKey,PVInfo*>::iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
        delete ipv->second;

    // Delete Device Info
    for ( map<Identifier,DeviceInfo*>::iterator idev = m_devices.begin(); idev != m_devices.end(); ++idev )
        delete idev->second;

    // Delete App info
    for ( map<Identifier,AppInfo*>::iterator iapp = m_apps.begin(); iapp != m_apps.end(); ++iapp )
        delete iapp->second;
}

// ---------- General public methods ------------------------------------------

/**
 * \brief Attaches a configuration listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::attachConfigListener( IPVConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);
    if ( find(m_config_listeners.begin(), m_config_listeners.end(), &a_listener) == m_config_listeners.end())
    {
        m_config_listeners.push_back(&a_listener);
    }
}

/**
 * \brief Detaches a configuration listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::detachConfigListener( IPVConfigListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);
    vector<IPVConfigListener*>::iterator l = find(m_config_listeners.begin(), m_config_listeners.end(), &a_listener);
    if ( l != m_config_listeners.end())
    {
        m_config_listeners.erase(l);
    }
}

/**
 * \brief Attaches a stream listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::attachStreamListener( IPVStreamListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);
    if ( find(m_stream_listeners.begin(), m_stream_listeners.end(), &a_listener) == m_stream_listeners.end())
    {
        m_stream_listeners.push_back( &a_listener );
    }
}

/**
 * \brief Detaches a stream listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::detachStreamListener( IPVStreamListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);
    vector<IPVStreamListener*>::iterator l = find(m_stream_listeners.begin(), m_stream_listeners.end(), &a_listener);
    if ( l != m_stream_listeners.end())
    {
        m_stream_listeners.erase(l);
    }
}


/**
 * \brief Attaches a stream listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::attachStatusListener( IPVStreamerStatusListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_statlist_mutex);
    if ( find(m_status_listeners.begin(), m_status_listeners.end(), &a_listener) == m_status_listeners.end())
    {
        m_status_listeners.push_back( &a_listener );
    }
}

/**
 * \brief Detaches a stream listener.
 * \param a_listener - Listener instance
 */
void
PVStreamer::detachStatusListener( IPVStreamerStatusListener &a_listener )
{
    boost::lock_guard<boost::mutex> lock(m_statlist_mutex);
    vector<IPVStreamerStatusListener*>::iterator l = find(m_status_listeners.begin(), m_status_listeners.end(), &a_listener);
    if ( l != m_status_listeners.end())
    {
        m_status_listeners.erase(l);
    }
}


/**
 * \brief Attaches a new configuration provider (takes ownership of PVConfig instance).
 * \param a_config - PVConfig instance
 * \return IPVConfigServices interface pointer
 */
IPVConfigServices*
PVStreamer::attach( PVConfig &a_config )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_config.find(a_config.getProtocol()) == m_config.end())
    {
        m_config[a_config.getProtocol()] = &a_config;
        return this;
    }

    EXC(EC_INVALID_OPERATION,"PVConfig instance already attached");
}

/**
 * \brief Attaches a new reader (takes ownership of PVReader instance).
 * \param a_reader - PVReader instance
 * \return IPVReaderServices interface pointer
 */
IPVReaderServices*
PVStreamer::attach( PVReader &a_reader )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( m_readers.find(a_reader.getProtocol()) == m_readers.end())
    {
        m_readers[a_reader.getProtocol()] = &a_reader;
        return this;
    }

    EXC(EC_INVALID_OPERATION,"PVReader instance already attached");
}

/**
 * \brief Attaches a new writer (takes ownership of PVWriter instance).
 * \param a_writer - PVWriter instance
 * \return IPVWriterServices interface pointer
 */
IPVWriterServices*
PVStreamer::attach( PVWriter &a_writer )
{
    boost::lock_guard<boost::mutex> lock(m_api_mutex);

    if ( !m_writer )
    {
        m_writer = &a_writer;
        return this;
    }

    EXC(EC_INVALID_OPERATION,"PVWriter instance already attached");
}


// ---------- Device & PV access methods --------------------------------------

/**
 * \brief Gets a list of active devices by ID
 * \param a_devs - Ref to vector of device IDs (output)
 */
void
PVStreamer::getActiveDevices( vector<Identifier> &a_devs ) const
{
    a_devs.clear();

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    for (map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
    {
        if ( ipv->second->m_active )
        {
            if ( !a_devs.size() || (a_devs.size() && a_devs.back() != ipv->second->m_device_id ))
                a_devs.push_back(ipv->second->m_device_id);
        }
    }
}

/**
 * \brief Determines if a process variable is defined by ID
 * \param a_dev_id - ID of device that variable is associated with
 * \param a_pv_id - ID of process variable to test
 * \return True if variable is defined; false otherwise.
 */
bool
PVStreamer::isPVDefined( Identifier a_dev_id, Identifier a_pv_id ) const
{
    PVKey key(a_dev_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.find(key);
    if ( ipv != m_pv_info.end())
        return true;
    else
        return false;
}

/**
 * \brief Gets a process variable by ID
 * \param a_dev_id - ID of device that variable is associated with
 * \param a_pv_id - ID of process variable to get
 * \return Pointer to relevant PVInfo object (throws if not defined)
 */
const PVInfo*
PVStreamer::getPV( Identifier a_dev_id, Identifier a_pv_id ) const
{
    PVKey key(a_dev_id,a_pv_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.find(key);
    if ( ipv != m_pv_info.end())
        return ipv->second;

    EXCP(EC_INVALID_PARAM,"PV not defined (dev id:" << a_dev_id << ", pv id:" << a_pv_id << ")");
}

/**
 * \brief Gets a map of all defined enums
 * \return Const ref to internal map of defined enums
 */
const map<Identifier,const Enum*> &
PVStreamer::getEnums() const
{
    return (const map<Identifier,const Enum*>&)m_enums;
}

/**
 * \brief Gets all process variables associated with a device
 * \param a_dev_id - ID of device
 * \return Const ref to internal vector of PVInfo objects
 */
const vector<const PVInfo*>&
PVStreamer::getDevicePVs( Identifier a_dev_id ) const
{
    return (const vector<const PVInfo*>&) getWriteableDevicePVs( a_dev_id );
}

/**
 * \brief Determines if a specific device is defined
 * \param a_dev_id - ID of device to test
 * \return True if device is defined; false otherwise
 */
bool
PVStreamer::isDeviceDefined( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end())
        return true;
    else
        return false;
}

/**
 * \brief Gets the name of a specific device
 * \param a_dev_id - ID of device
 * \return Name of device (throws if not defined)
 */
std::string
PVStreamer::getDeviceName( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end() )
        return idev->second->name;

    EXCP(EC_INVALID_PARAM,"Device not defined (dev id:" << a_dev_id  << ")");
}

/**
 * \brief Determines if a specific application is defined
 * \param a_app_id - ID of application to test
 * \return True if application is defined; false otherwise
 */
bool
PVStreamer::isAppDefined( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);
    return m_apps.find(a_app_id) != m_apps.end();
}

/**
 * \brief Sets an application as active
 * \param a_app_id - ID of application
 */
void
PVStreamer::appActive( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);
    map<Identifier,AppInfo*>::const_iterator iapp = m_apps.find(a_app_id);
    if ( iapp != m_apps.end())
        iapp->second->active = true;
}

/**
 * \brief Sets an application as inactive
 * \param a_app_id - ID of application
 */
void
PVStreamer::appInactive( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);
    map<Identifier,AppInfo*>::const_iterator iapp = m_apps.find(a_app_id);
    if ( iapp != m_apps.end())
        iapp->second->active = false;
}

/**
 * \brief Determines if an application is active
 * \param a_app_id - ID of application
 * \return True if app is active; false if not active or undefined
 */
bool
PVStreamer::isAppActive( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);
    map<Identifier,AppInfo*>::const_iterator iapp = m_apps.find(a_app_id);
    if ( iapp != m_apps.end())
        return iapp->second->active;

    return false;
}

/**
 * \brief Gets all configured devices associated with an application
 * \param a_app_id - ID of application
 * \return Const ref to internal vector of device IDs
 */
const vector<Identifier>&
PVStreamer::getAppDevices( Identifier a_app_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,AppInfo*>::const_iterator iapp = m_apps.find(a_app_id);
    if ( iapp != m_apps.end())
    {
        return (const vector<Identifier>&) iapp->second->devices;
    }
    else
        EXCP(EC_INVALID_PARAM,"App not defined (app id:" << a_app_id  << ")");
}


// ---------- IPVCommonServices methods ---------------------------------------

void
PVStreamer::unhandledException( TraceException &e )
{
    boost::lock_guard<boost::mutex> lock(m_statlist_mutex);

    if ( m_status_listeners.size() )
    {
        try
        {
            for ( vector<IPVStreamerStatusListener*>::iterator il = m_status_listeners.begin(); il != m_status_listeners.end(); ++il )
                (*il)->unhandledException( e );
        }
        catch(...)
        {}
    }
}

// ---------- IPVConfigServices methods ---------------------------------------

/**
 * \brief Defines a new application
 * \param a_protocol - Protocol of new application
 * \param a_app_id - ID of new application
 * \param a_source - Source (host) of new application
 */
void
PVStreamer::defineApp( Protocol a_protocol, Identifier a_app_id, const std::string &a_source )
{
    AppInfo *info = 0;

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    if ( m_apps.find(a_app_id) == m_apps.end())
    {
        info = new AppInfo;

        info->app_id = a_app_id;
        info->protocol = a_protocol;
        info->source = a_source;
        info->active = false;

        m_apps[a_app_id] = info;
    }
}

void
PVStreamer::undefineApp( Identifier a_app_id )
{
    // TODO Not currently needed
}

/**
 * \brief Defines a new device
 * \param a_protocol - Protocol of new device
 * \param a_dev_id - ID of new device
 * \param a_name - Name of new device
 * \param a_source - Source (host) of new device
 * \param a_app_id - Application ID associated with new device
 */
void
PVStreamer::defineDevice( Protocol a_protocol, Identifier a_dev_id, const string &a_name, const string &a_source, Identifier a_app_id )
{
    DeviceInfo *info = 0;

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    if ( m_devices.find( a_dev_id ) == m_devices.end())
    {
        info = new DeviceInfo;

        info->device_id = a_dev_id;
        info->app_id = a_app_id;
        info->name = a_name;
        info->protocol = a_protocol;
        info->source = a_source;

        m_devices[a_dev_id] = info;

        // If an application is specified, add this device to it
        if ( a_app_id )
        {
            map<Identifier,AppInfo*>::iterator ia = m_apps.find(a_app_id);
            if ( ia != m_apps.end())
                ia->second->devices.push_back(a_dev_id);
            else
                EXCP(EC_INVALID_PARAM,"Application " << a_app_id << " not defined");
        }
    }
    else
        EXCP(EC_INVALID_PARAM,"Device already defined (dev id:" << a_dev_id  << ")");
}

void
PVStreamer::undefineDevice( Identifier a_dev_id )
{
    //TODO Not currently needed
}

/**
 * \breif Undefines a device if there are no variable associated with it
 * \param a_dev_id - ID of device
 */
void
PVStreamer::undefineDeviceIfNoPVs( Identifier a_dev_id )
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::iterator idev = m_devices.find( a_dev_id );

    if ( idev != m_devices.end())
    {
        if ( !idev->second->pvs.size())
        {
            map<Identifier,AppInfo*>::iterator iapp = m_apps.find( idev->second->app_id );
            if ( iapp != m_apps.end())
            {
                vector<Identifier>::iterator ii = find(iapp->second->devices.begin(),iapp->second->devices.end(),a_dev_id);
                if ( ii != iapp->second->devices.end())
                    iapp->second->devices.erase(ii);
            }

            delete idev->second;
            m_devices.erase(idev);
        }
    }
}

/**
 * \brief Defines a new process variable
 * \param a_info - Ref to new PVInfo object (PVSteamer takes ownership)
 */
void
PVStreamer::definePV( PVInfo & a_info )
{
    PVKey key(a_info.m_device_id,a_info.m_id);

    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::iterator idev = m_devices.find(a_info.m_device_id);

    if ( idev == m_devices.end())
        EXCP( EC_INVALID_PARAM, "Invalid device ID " << a_info.m_device_id );

    if ( m_pv_info.find(key) == m_pv_info.end())
    {
        if ( a_info.m_protocol != idev->second->protocol || a_info.m_source != idev->second->source )
            EXCP( EC_INVALID_PARAM, "Mismatched protocols on PV " << a_info.m_name );

        idev->second->pvs.push_back(&a_info);
        m_pv_info[key] = &a_info;
    }
    else
        EXCP( EC_INVALID_PARAM, "PV " << a_info.m_name << " already defined" );
}

void
PVStreamer::undefinePV( Identifier a_dev_id, Identifier a_pv_id )
{
    //TODO Not currently needed
}

/**
 * \brief Gets an enum by ID
 * \param a_id - ID of enum to get
 * \return Const pointer to Enum (Null if not defined)
 */
const Enum*
PVStreamer::getEnum( Identifier a_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,Enum*>::const_iterator e = m_enums.find(a_id);
    if ( e != m_enums.end())
        return e->second;
    else
        return 0;
}

/**
 * \brief Defines a new Enum
 * \param a_values - Map of enum key-value pairs
 * \return Const pointer to newly created Enum
 */
const Enum*
PVStreamer::defineEnum( const map<int,string> &a_values )
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    for ( map<Identifier,Enum*>::const_iterator e = m_enums.begin(); e != m_enums.end(); ++e )
    {
        if ( *(e->second) == a_values )
            return e->second;
    }

    Enum *new_enum = new Enum( m_next_enum_id, a_values );
    m_enums[m_next_enum_id++] = new_enum;

    return new_enum;
}

/**
 * \brief Callback to receive and distribute configuration loaded events.
 * \param a_protocol - Protocol associated with configuration
 * \param a_source - Source (host) of configuration event
 */
void
PVStreamer::configurationLoaded( Protocol a_protocol, const string &a_source )
{
    // Notify config listeners
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);

    for ( vector<IPVConfigListener*>::iterator il = m_config_listeners.begin(); il != m_config_listeners.end(); ++il )
        (*il)->configurationLoaded( a_protocol, a_source );
}

/**
 * \brief Callback to receive and distribute invalid configuration events.
 * \param a_protocol - Protocol associated with configuration
 * \param a_source - Source (host) of configuration event
 */
void
PVStreamer::configurationInvalid( Protocol a_protocol, const string &a_source )
{
    // Notify config listeners
    boost::lock_guard<boost::mutex> lock(m_cfglist_mutex);

    for ( vector<IPVConfigListener*>::iterator il = m_config_listeners.begin(); il != m_config_listeners.end(); ++il )
        (*il)->configurationInvalid( a_protocol, a_source );
}

/**
 * \brief Gets a writable PVInfo object by "friendly name".
 * \param a_name - Name of process variable
 * \return Writable PVInfo object (Null if not found)
 */
PVInfo*
PVStreamer::getWriteablePV( const string & a_name ) const
{
    for ( map<PVKey,PVInfo*>::const_iterator ipv = m_pv_info.begin(); ipv != m_pv_info.end(); ++ipv )
    {
        if ( ipv->second->m_name == a_name )
            return ipv->second;
    }

    return 0;
}


/** \brief Get or allocate a device ID for a named device
  * \param a_protocol - Protocol of device
  * \param a_device_name - Name of device
  *
  * This method is used by protocol handlers that do not use numeric Identifiers
  * for devices. This method allocates a new ID or finds the currently assigned
  * ID for the named device on the specified protocol. Device names must be
  * unique within a protocol.
  */
Identifier
PVStreamer::getDeviceIdentifier( Protocol a_protocol, const std::string &a_device_name )
{
    // Device key is protocol:device name
    string key = boost::lexical_cast<string>(a_protocol) + ":" + a_device_name;

    map<string,Identifier>::iterator d = m_dev_id.find( key );
    if ( d != m_dev_id.end())
        return d->second;

    // Device name not found, allocate a new one
    // ID range 1 - 999 are reserved for dynamic allocation
    // Look for the lowest ID value not in use

    Identifier id = 1;

    for ( map<Identifier,string>::iterator i = m_id_dev.begin(); i != m_id_dev.end(); ++i )
    {
        if ( id < i->first )
            break;
        id = i->first + 1;
    }

    if ( id > 999 )
        EXC( EC_INVALID_OPERATION, "Too many devices active." );

    // Update indexes
    m_dev_id[key] = id;
    m_id_dev[id] = key;

    return id;
}


/** \brief Release a device ID for a named device
  * \param a_protocol - Protocol of device
  * \param a_device_name - Name of device
  *
  * This method is used by protocol handlers that do not use numeric Identifiers
  * for devices. This method release a previously reserved ID for the named
  * device on the specified protocol.
  */
void
PVStreamer::releaseDeviceIdentifier( Protocol a_protocol, const std::string &a_device_name )
{
    // Device key is protocol:device name
    string key = boost::lexical_cast<string>(a_protocol) + ":" + a_device_name;

    map<string,Identifier>::iterator d = m_dev_id.find( key );
    if ( d != m_dev_id.end())
    {
        Identifier id = d->second;
        m_dev_id.erase( d );

        map<Identifier,string>::iterator i = m_id_dev.find( id );
        if ( i != m_id_dev.end())
        {
            m_id_dev.erase( i );
        }
    }
}


// ---------- IPVReaderServices methods ---------------------------------------

/**
 * \brief Gets a stream packet from the free queue (blocks until available)
 * \return PVStreamPacket pointer on success; null on failure
 */
PVStreamPacket*
PVStreamer::getFreePacket()
{
    PVStreamPacket* pkt = 0;
    m_free_que.get( pkt );
    return pkt;
}

/**
 * \brief Gets a filled stream packet (blocks until available)
 * \param a_timeout - Timeout period in msec
 * \param a_timeout_flag - (output) Indicates if a timeout occurred
 * \return PVStreamPacket pointer on success; null on failure
 */
PVStreamPacket*
PVStreamer::getFreePacket( unsigned long a_timeout, bool & a_timeout_flag )
{
    PVStreamPacket* pkt = 0;
    m_free_que.get_timed( pkt, a_timeout, a_timeout_flag );

    return pkt;
}

/**
 * \brief Puts a stream packet on the filled queue
 * \param a_pkt - PVStreamPacket object to put on queue
 */
void
PVStreamer::putFilledPacket( PVStreamPacket *a_pkt )
{
    m_fill_que.put(a_pkt);
}

/**
 * \brief Gets a writable process variable by ID
 * \param a_dev_id - ID of device that variable is associated with
 * \param a_pv_id - ID of process variable to get
 * \return Pointer to relevant PVInfo object (throws if not defined)
 */
PVInfo*
PVStreamer::getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const
{
    return (PVInfo*)getPV( a_dev_id, a_pv_id );
}

/**
 * \brief Gets all process variables (writable) associated with a device
 * \param a_dev_id - ID of device
 * \return Ref to internal vector of PVInfo objects
 */
vector<PVInfo*>&
PVStreamer::getWriteableDevicePVs( Identifier a_dev_id ) const
{
    boost::lock_guard<boost::mutex> lock(m_cfg_mutex);

    map<Identifier,DeviceInfo*>::const_iterator idev = m_devices.find( a_dev_id );
    if ( idev != m_devices.end() )
        return idev->second->pvs;
    else
        EXCP( EC_INVALID_PARAM, "Device " << a_dev_id << " not defined" );
}


// ---------- IPVWriterServices methods ---------------------------------------

/**
 * \brief Gets a filled stream packet (blocks until available)
 * \return PVStreamPacket pointer on success; null on failure
 */
PVStreamPacket*
PVStreamer::getFilledPacket()
{
    PVStreamPacket* pkt = 0;
    m_fill_que.get( pkt );

    return pkt;
}

/**
 * \brief Gets a filled stream packet (blocks until available)
 * \param a_timeout - Timeout period in msec
 * \param a_timeout_flag - (output) Indicates if a timeout occurred
 * \return PVStreamPacket pointer on success; null on failure
 */
PVStreamPacket*
PVStreamer::getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag )
{
    PVStreamPacket* pkt = 0;
    m_fill_que.get_timed( pkt, a_timeout, a_timeout_flag );

    return pkt;
}

void
PVStreamer::putFreePacket( PVStreamPacket *a_pkt )
{
    // If the notify buffer is backed-up, bypass it. This will cause stream
    // listeners to miss packets under heavy load, but it will maintain the
    // output stream integrity.

    if ( m_notify_que.size() < m_max_notify_pkts )
        m_notify_que.put(a_pkt);
    else
        m_free_que.put(a_pkt);
}

// ---------- IPVStreamListener support methods -------------------------------

/**
 * \brief Stream listener notification thread function.
 */
void
PVStreamer::streamListenersNotifyThreadFunc()
{
    PVStreamPacket* pkt;

    while(1)
    {
        if ( !m_notify_que.get( pkt ))
            break;

        notifyStreamListeners(pkt);
        m_free_que.put(pkt);
    }
}

/**
 * \brief Notifies all stream listeners of new stream event
 * \param a_pkt - Stream packet describing event.
 */
void
PVStreamer::notifyStreamListeners( PVStreamPacket *a_pkt )
{
    boost::lock_guard<boost::mutex> lock(m_strlist_mutex);

    if ( m_stream_listeners.size() )
    {
        try
        {
            switch( a_pkt->pkt_type )
            {
            case DeviceActive:
                {
                    string name = getDeviceName( a_pkt->device_id );
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->deviceActive( a_pkt->time, a_pkt->device_id, name );
                }
                break;

            case DeviceInactive:
                {
                    string name = getDeviceName( a_pkt->device_id );
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->deviceInactive( a_pkt->time, a_pkt->device_id, name );
                }
                break;

            case VarActive:
                for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                    (*il)->pvActive( a_pkt->time, *(a_pkt->pv_info) );
                break;

            case VarInactive:
                for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                    (*il)->pvInactive( a_pkt->time, *(a_pkt->pv_info) );
                break;

            case VarUpdate:
                switch ( a_pkt->pv_info->m_type )
                {
                case PV_INT:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->ival );
                    break;

                case PV_ENUM:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->ival, a_pkt->pv_info->m_enum );
                    break;

                case PV_UINT:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->uval );
                    break;

                case PV_DOUBLE:
                    for ( vector<IPVStreamListener*>::iterator il = m_stream_listeners.begin(); il != m_stream_listeners.end(); ++il )
                        (*il)->pvValueUpdated( a_pkt->time, *(a_pkt->pv_info), a_pkt->dval );
                    break;
                }
                break;
            default:
                break;
            }
        }
        catch(...)
        {
            // Don't really care if a client has problems - just don't want to
            // propagate exception beyond this method
        }
    }
}


}}
