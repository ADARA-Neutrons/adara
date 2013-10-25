#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H


#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <set>
#include "DeviceDescriptor.h"


namespace PVS {

typedef boost::shared_ptr<DeviceDescriptor>    DeviceRecordPtr;

#if 0
class IConfigListener
{
public:
    void            deviceDefined( DeviceRecordPtr a_device );
    void            deviceRedefined( DeviceRecordPtr a_device );
    void            deviceUndefined( DeviceRecordPtr a_device );
};
#endif

class ConfigManager
{
public:
    ConfigManager();
    ~ConfigManager();

    DeviceRecordPtr getDeviceConfig( const std::string &a_device_name, const std::string &a_source, Protocol a_protocol );
    DeviceRecordPtr defineDevice( DeviceDescriptor &a_descriptor );
    //void            undefineDevice( const std::string &a_source, Protocol a_protocol );
    void            undefineDevice( const std::string &a_device_name, const std::string &a_source, Protocol a_protocol );
    //void            attachListener( IConfigListener &a_listener );
    //void            detachListener( IConfigListener &a_listener );

private:
    std::string     makeDeviceKey( const std::string &a_name, const std::string &a_source, Protocol a_protocol ) const;
    Identifier      getNextDeviceID() const;

    std::map<std::string,DeviceRecordPtr>   m_devices;
    boost::mutex                            m_mutex;
//    std::vector<IConfigListener *>          m_listeners;

#ifdef USE_GC
    bool                                    m_running;
    void            gcThread();
    std::set<DeviceRecordPtr>               m_garbage;
    boost::thread                          *m_gc_thread;
#endif
};

}

#endif // CONFIGRECORD_H
