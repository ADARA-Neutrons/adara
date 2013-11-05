#ifndef EPICS_DEVICEAGENT_H
#define EPICS_DEVICEAGENT_H

#include <string>
#include <map>
#include <stdint.h>

#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <cadef.h>

#include "ConfigManager.h"
#include "StreamService.h"


namespace PVS {

class IInputAdapterAPI;

namespace EPICS {

class DeviceAgent
{
public:
    DeviceAgent( IInputAdapterAPI &a_stream_api, DeviceDescriptor *a_device );
    ~DeviceAgent();

    void    update( DeviceDescriptor *a_device );
    void    stop();
    bool    stopped();

private:
    enum ChanState
    {
        UNINITIALIZED = 0,
        INFO_NEEDED,
        INFO_PENDING,
        READY
    };

    struct ChanInfo
    {
        ChanInfo() : m_pv(0), m_chid(0), m_evid(0), m_chan_state(UNINITIALIZED), m_connected(false), m_subscribed(false)
        {}

        DeviceRecordPtr     m_device;
        PVDescriptor       *m_pv;
        chid                m_chid;
        evid                m_evid;
        ChanState           m_chan_state;
        PVState             m_pv_state;
        bool                m_connected;
        bool                m_subscribed;
    };


    void        connectPV( PVDescriptor *a_pv );
    void        disconnectPV( PVDescriptor *a_pv );
    void        controlThread();
    void        sendLastValues();
    void        epicsConnectionHandler( struct connection_handler_args a_args );
    void        epicsEventHandler( struct event_handler_args a_args );
    PVType      epicsToPVType( uint32_t a_rec_type );
    int32_t     epicsToTimeRecordType( uint32_t a_rec_type );
    int32_t     epicsToCtrlRecordType( uint32_t a_rec_type );
    bool        epicsIsTimeRecordType( uint32_t a_rec_type );
    bool        epicsIsCtrlRecordType( uint32_t a_rec_type );

    static void epicsConnectionCallback( struct connection_handler_args a_args );
    static void epicsEventCallback( struct event_handler_args a_args );

    IInputAdapterAPI           &m_stream_api;
    DeviceRecordPtr             m_dev_record;
    DeviceDescriptor           *m_dev_desc;
    bool                        m_defined;
    std::map<chid,ChanInfo>     m_chan_info;
    std::map<std::string,chid>  m_pv_index;
    boost::thread              *m_ctrl_thread;
    //boost::reursive_mutex       m_mutex;
    //boost::mutex                m_state_mutex;
    boost::mutex                m_mutex;
    boost::condition_variable   m_state_cond;
    bool                        m_active;
};

}}

#endif // EPICS_DEVICEAGENT_H
