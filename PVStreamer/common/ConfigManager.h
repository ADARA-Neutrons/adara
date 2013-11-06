#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H


#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <set>
#include "DeviceDescriptor.h"


namespace PVS {

class IInputAdapterAPI;

typedef boost::shared_ptr<DeviceDescriptor>    DeviceRecordPtr;


class ConfigManager
{
public:
    ConfigManager();
    ~ConfigManager();

    DeviceRecordPtr getDeviceConfig( const std::string &a_device_name, const std::string &a_source, Protocol a_protocol );
    DeviceRecordPtr defineDevice( DeviceDescriptor &a_descriptor );
    void            undefineDevice( DeviceRecordPtr &a_record );
    void            attach( IInputAdapterAPI *a_stream_api );

private:
    std::string     makeDeviceKey( const std::string &a_name, const std::string &a_source, Protocol a_protocol ) const;
    Identifier      getNextDeviceID() const;
    void            sendDeviceDefined( DeviceRecordPtr a_dev_desc );
    void            sendDeviceUndefined( DeviceRecordPtr a_dev_desc );
    void            sendDeviceRedefined( DeviceRecordPtr a_dev_desc, DeviceRecordPtr a_old_dev_desc );

    IInputAdapterAPI*                       m_stream_api;
    std::map<std::string,DeviceRecordPtr>   m_devices;
    boost::mutex                            m_mutex;
};

}

#endif // CONFIGRECORD_H
