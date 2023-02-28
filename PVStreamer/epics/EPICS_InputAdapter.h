#ifndef EPICS_INPUTADAPTER_H
#define EPICS_INPUTADAPTER_H

#include <vector>
#include <set>
#include <map>
#include <list>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <libxml/tree.h>

#include "CoreDefs.h"
#include "IInputAdapter.h"
#include "StreamService.h"

#define EPICS_PROTOCOL 2

namespace PVS {

class ConfigManager;

namespace EPICS {

class DeviceAgent;


/** \brief EPICS stream input adapter
  *
  * The EPICS::InputAdapter class manages the activities related to the
  * configuration and streaming of EPICS process variables into a
  * StreamService instance. This class monitors the specified configuration
  * file for updates and creates EPICS::DeviceAgent instances to handle
  * configured devices and process variables. A garbage collection thread is
  * used to allow de-configured DeviceAgent instances to clean-up any live
  * connection prior to be deleted.
  */
class InputAdapter : public IInputAdapter
{
public:
    InputAdapter( StreamService &a_stream_serv,
        const std::string &a_config_file, bool a_track_logged = false,
        time_t a_device_init_timeout = 60 );
    ~InputAdapter();

    uint32_t        numActiveDevices();
    uint32_t        numInactiveDevices();

    void            getDevicesStatus(
                        uint32_t &a_partialDeviceCount,
                        uint32_t &a_hungDeviceCount,
                        uint32_t &a_inactiveDeviceCount,
                        uint32_t &a_readyPVCount,
                        uint32_t &a_totalPVCount );
private:
    void            configFileMonitorThread();
    void            startDevice( DeviceDescriptor *a_device );
    void            stopDevice( const std::string &a_dev_name );
    void            stopAllDevices();
    void            gcThread();
    bool            parseConfigBuffer( const char* a_buffer,
                        int a_buffer_size,
                        std::vector<DeviceDescriptor*> &a_devices,
                        std::vector<Identifier> &a_inactive_device_ids );
    xmlNode*        xmlFind( const char *a_tag,
                        xmlNode *a_parent_node ) const;
    void            xmlGetValue( xmlNode *a_node,
                        std::string &a_value ) const;
    bool            xmlGetAttribute( xmlNode *a_node,
                        const char *a_attrib, std::string &a_value ) const;

    bool                                m_active;           ///< Instance active flag (reset on destruction)
    std::string                         m_config_file;      ///< Configuration file to read/monitor
    bool                                m_track_logged;     ///< Track logged PVs only (default is all)
    boost::thread                      *m_cfg_mon_thread;   ///< Configuration file monitoring thread handle
    std::vector<char>                   m_config_buffer;    ///< Configuration file buffer
    std::string                         m_source;           ///< Source "host" (not really used for EPICS adapter)
    std::set<std::string>               m_cur_device_names; ///< Currently configured devices (by name)
    std::vector<Identifier>             m_inactive_device_ids; ///< Inactive Device Ids
    std::map<std::string,DeviceAgent*>  m_dev_agents;       ///< Active DeviceAgent instances (by name)
    std::list<DeviceAgent*>             m_garbage;          ///< Decommissioned DeviceAgent instanced
    boost::thread                      *m_gc_thread;        ///< Garbage collection thread handle
    boost::recursive_mutex              m_mutex;            ///< Synch mutex
    struct ca_client_context           *m_epics_context;    ///< Current EPICS thread context
    time_t                              m_device_init_timeout; ///< Device Initialization Timeout (seconds)
    uint32_t                            m_offset;           ///< Device ID Offset

};

}}

#endif // EPICS_INPUTADAPTER_H

// vim: expandtab

