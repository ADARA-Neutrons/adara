#ifndef EPICS_INPUTADAPTER_H
#define EPICS_INPUTADAPTER_H

#include <vector>
#include <set>
#include <string>
#include <libxml/tree.h>

#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/mutex.hpp>

#include "CoreDefs.h"
#include "IInputAdapter.h"
#include "StreamService.h"

#define EPICS_PROTOCOL 2

namespace PVS {

class ConfigManager;

namespace EPICS {

class DeviceAgent;

class InputAdapter : public IInputAdapter
{
public:
    InputAdapter( const std::string &a_config_file );
    ~InputAdapter();

private:
    void            configFileMonitorThread();
    void            startDevice( DeviceDescriptor *a_device );
    void            stopDevice( const std::string &a_dev_name );
    void            stopAllDevices();
    void            gcThread();
    bool            parseConfigBuffer( const char* a_buffer, int a_buffer_size, std::vector<DeviceDescriptor*> &a_devices );
    xmlNode*        xmlFind( const char *a_tag, xmlNode *a_parent_node ) const;
    void            xmlGetValue( xmlNode *a_node, std::string &a_value ) const;
    bool            xmlGetAttribute( xmlNode *a_node, const char *a_attrib, std::string &a_value ) const;

    bool                                m_active;
    std::string                         m_config_file;
    boost::thread                      *m_config_file_monitor_thread;
    std::vector<char>                   m_config_buffer;
    std::set<std::string>               m_cur_devices;
    std::string                         m_source;
    std::map<std::string,DeviceAgent*>  m_dev_agents;
    std::list<DeviceAgent*>             m_garbage;
    boost::thread                      *m_gc_thread;
    boost::recursive_mutex              m_mutex;
    struct ca_client_context           *m_epics_context;

};

}}

#endif // EPICS_INPUTADAPTER_H
