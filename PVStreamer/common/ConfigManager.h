#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H


#include <map>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <string>

#include "DeviceDescriptor.h"


namespace PVS {

class IInputAdapterAPI;

typedef boost::shared_ptr<DeviceDescriptor>    DeviceRecordPtr;

/**
 * @brief ConfigManager class manages device & process variable descriptors.
 *
 * The ConfigMaganger class manages and streams device and process variable
 * configuration data.
 * Streaming is implemented over the internal generic protocol and feeds
 * output protocol-adapters.
 * When devices are undefined or redefined, the ConfigManager automatically
 * emits corresponding process variable "disconnected" packets
 * into the internal stream.
 */
class ConfigManager
{
public:
    ConfigManager( uint32_t a_offset = 0 );
    ~ConfigManager();

    DeviceRecordPtr getDeviceConfig( const std::string &a_device_name,
                        const std::string &a_source, Protocol a_protocol );
    DeviceRecordPtr defineDevice( DeviceDescriptor &a_descriptor,
                        bool &a_device_changed );
    void            undefineDevice( DeviceRecordPtr &a_record,
                        bool a_delete_device = true );
    void            attach( IInputAdapterAPI *a_stream_api );
    uint32_t        getOffset(void) { return m_offset; }

private:
    std::string     makeDeviceKey( const std::string &a_name,
                        const std::string &a_source,
                        Protocol a_protocol ) const;
    void            makePvNamesUnique(  const std::string &a_key,
                        DeviceDescriptor &a_descriptor );
    void            sendDeviceDefined( DeviceRecordPtr a_dev_desc );
    void            sendDeviceUndefined( DeviceRecordPtr a_dev_desc );
    void            sendDeviceRedefined( DeviceRecordPtr a_dev_desc,
                        DeviceRecordPtr a_old_dev_desc );
    void            sendPvUndefined( DeviceRecordPtr a_dev_desc,
                        PVDescriptor *a_pv_desc );

    IInputAdapterAPI*                       m_stream_api;
    uint32_t                                m_offset;
    std::map<std::string,DeviceRecordPtr>   m_devices;
    boost::mutex                            m_mutex;
};

}

#endif // CONFIGRECORD_H

// vim: expandtab

