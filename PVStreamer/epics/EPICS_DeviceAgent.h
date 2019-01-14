#ifndef EPICS_DEVICEAGENT_H
#define EPICS_DEVICEAGENT_H

#include <string>
#include <map>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include <cadef.h>

#include "ConfigManager.h"
#include "StreamService.h"


namespace PVS {

class IInputAdapterAPI;

namespace EPICS {


/** \brief EPICS DeviceAgent class
  *
  * The EPICS::DeviceAgent class manages the EPICS channel access
  * connections for a given device.
  */
class DeviceAgent
{
public:
    DeviceAgent( IInputAdapterAPI &a_stream_api,
        DeviceDescriptor *a_device,
        struct ca_client_context *a_epics_context,
        time_t a_device_init_timeout );
    ~DeviceAgent();

    DeviceDescriptor   *get_desc(void)
        { return m_dev_desc ? m_dev_desc : m_dev_record.get(); }

    void    update( DeviceDescriptor *a_device );
    void    stop(void);
    void    undefine(void);
    bool    stopped(void);

    void    deviceStatus( uint32_t &a_ready_pvs, uint32_t &a_total_pvs,
                bool &a_hung, bool &a_active );

private:
    enum ChanState
    {
        UNINITIALIZED = 0,
        INFO_NEEDED,
        INFO_PENDING,
        INFO_AVAILABLE,
        READY
    };

    struct ChanInfo
    {
        ChanInfo() : m_pv(0), m_chid(0), m_evid(0),
            m_chan_state(UNINITIALIZED), m_connected(false),
            m_subscribed(false)
        {}

        DeviceRecordPtr                 m_device;
        PVDescriptor                   *m_pv;
        chid                            m_chid;
        evid                            m_evid;
        ChanState                       m_chan_state;
        PVState                         m_pv_state;
        bool                            m_connected;
        bool                            m_subscribed;
        unsigned long                   m_ca_type;
        unsigned long                   m_ca_elem_count;
        std::string                     m_ca_units;
        std::map<int32_t,std::string>   m_ca_enum_vals;
    };


    void        metadataUpdated();
    void        connectPV( PVDescriptor *a_pv );
    void        disconnectPV( PVDescriptor *a_pv,
                    boost::unique_lock<boost::mutex> & lock );
    void        controlThread();
    void        monitorThread();
    void        sendCurrentValues();
    void        epicsConnectionHandler(
                    struct connection_handler_args a_args );
    void        epicsEventHandler( struct event_handler_args a_args );
    PVType      epicsToPVType( uint32_t a_rec_type, uint32_t a_elem_count );
    int32_t     epicsToTimeRecordType( uint32_t a_rec_type );
    int32_t     epicsToCtrlRecordType( uint32_t a_rec_type );
    bool        epicsIsTimeRecordType( uint32_t a_rec_type );
    bool        epicsIsCtrlRecordType( uint32_t a_rec_type );
    template<typename T>
    void        updateState( const void *a_src, PVState &a_state );

    static void epicsConnectionCallback(
                    struct connection_handler_args a_args );
    static void epicsEventCallback( struct event_handler_args a_args );

    IInputAdapterAPI           &m_stream_api;       ///< Streaming API provided by StreamService
    DeviceRecordPtr             m_dev_record;
    DeviceDescriptor           *m_dev_desc;
    bool                        m_defined;
    bool                        m_hung;
    std::map<chid,ChanInfo>     m_chan_info;        ///< PV channel ID to channel info map
    std::map<std::string,chid>  m_pv_index;         ///< PV connection to channel id map
    boost::thread              *m_ctrl_thread;
    boost::mutex                m_mutex;
    boost::condition_variable   m_state_cond;
    bool                        m_state_changed;
    bool                        m_agent_active;
    struct ca_client_context   *m_epics_context;
    time_t                      m_device_init_timeout;
    boost::thread              *m_monitor_thread;
};

}}

#endif // EPICS_DEVICEAGENT_H

// vim: expandtab

