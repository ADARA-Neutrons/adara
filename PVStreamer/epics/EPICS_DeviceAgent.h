#ifndef EPICS_DEVICEAGENT_H
#define EPICS_DEVICEAGENT_H

#include <string>
#include <map>
#include <cadef.h>

namespace PVS {

class IInputAdapterAPI;
class ConfigManager;
class DeviceDescriptor;
class PVDescriptor;

namespace EPICS {

class DeviceAgent
{
public:
    DeviceAgent( IInputAdapterAPI &a_stream_api, ConfigManager &a_cfg_mgr, DeviceDescriptor *a_device );
    ~DeviceAgent();

    void    update( DeviceDescriptor *a_device );
    void    stop();
    bool    stopped();

private:
    struct ChanInfo
    {
        ChanInfo() : m_chid(0), m_evid(0), m_connected(false), m_subscribed(false),
            m_pv_status(0), m_pv_type(0), m_int_val(0)
        {}

        chid            m_chid;
        evid            m_evid;
        bool            m_connected;
        bool            m_subscribed;
        std::string     m_pv_name;
        int             m_pv_status;
        long            m_pv_type;
        std::string     m_pv_units;
        union
        {
            int32_t     m_int_val;
            uint32_t    m_uint_val;
            double      m_real_val;
        };
    };

    void    resendDeviceDescriptor();
    void    connectPV( PVDescriptor *a_pv );
    void    disconnectPV( PVDescriptor *a_pv );
    void    epicsConnectionHandler( struct connection_handler_args a_args );
    void    epicsEventHandler( struct event_handler_args a_args );

    static void epicsConnectionCallback( struct connection_handler_args a_args );
    static void epicsEventCallback( struct event_handler_args a_args );

    IInputAdapterAPI           &m_stream_api;
    ConfigManager              &m_cfg_mgr;
    DeviceDescriptor           *m_device;
    std::map<chid,ChanInfo>     m_chan_info;
    std::map<std::string,chid>    m_pv_index;
};

}}

#endif // EPICS_DEVICEAGENT_H
