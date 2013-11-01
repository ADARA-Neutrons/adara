#ifndef STREAMSERVICE_H
#define STREAMSERVICE_H

#include <vector>
#include <string>

#include "CoreDefs.h"
#include "ConfigManager.h"
#include "SyncDeque.h"
#include "IInputAdapter.h"
#include "IOutputAdapter.h"

namespace PVS {


/// Used to define packet types of the (internal) PVStreamPacket.
enum StreamPktType
{
    DeviceDefined,      // A NEW device has been defined
    DeviceRedefined,    // An existing device has been redefined
    DeviceUndefined,    // An existing device has been undefined
    //DeviceActive,       // An existing device has become active
    //DeviceInactive,     // An existing device has become inactive
    //VariableActive,
    //VariableInactive,
    VariableUpdate
};


/// Timestamp associated with device activity and variable values
struct Timestamp
{
    Timestamp() : sec(0), nsec(0) {}

    uint32_t    sec;
    uint32_t    nsec;
};


struct PVState
{
    PVState() : m_uint_val(0), m_status(0), m_severity(0) {}

    union
    {
        int32_t         m_int_val;    ///< Used for both int and enum type
        uint32_t        m_uint_val;
        double          m_real_val;
    };
    std::string         m_str_val;
    Timestamp           m_time;
    int16_t             m_status;       ///< EPICS alarm code
    int16_t             m_severity;     ///< EPICS severity code
};


// TODO This is a union of all stream packets - need to make it a proper hierarchy
struct StreamPacket
{
    StreamPktType       type;
    DeviceRecordPtr     device;
    PVDescriptor       *pv;
    PVState             state;
    DeviceRecordPtr     old_device;
};


/**
 * \class IInputAdapterAPI
 *
 * The IInputAdapterAPI interface provides access to input-adapter-specific
 * services.
 */
class IInputAdapterAPI
{
public:
    virtual StreamPacket   *getFreePacket() = 0;
    virtual StreamPacket   *getFreePacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual void            putFilledPacket( StreamPacket *a_pkt ) = 0;
};


/**
 * \class IOutputAdapter
 *
 * The IOutputAdapter interface provides access to output-adapter-specific
 * services. Only one consumer may be attached to the streamer at a given time.
 */
class IOutputAdapterAPI
{
public:
    virtual StreamPacket   *getFilledPacket() = 0;
    virtual StreamPacket   *getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual void            putFreePacket( StreamPacket *a_pkt ) = 0;
};


class StreamService : private IInputAdapterAPI, private IOutputAdapterAPI
{
public:
    StreamService( ConfigManager &a_cfg_mgr, size_t a_pkt_buffer_size /*, size_t a_max_notify_pkts*/ );
    ~StreamService();

    IInputAdapterAPI*       attach( IInputAdapter &a_adapter );
    IOutputAdapterAPI*      attach( IOutputAdapter &a_adapter );

private:
    // ---------- IStreamProducer methods ----------

    StreamPacket   *getFreePacket();
    StreamPacket   *getFreePacket( unsigned long a_timeout, bool & a_timeout_flag );
    void            putFilledPacket( StreamPacket *a_pkt );

    // ---------- IStreamConsumer methods ----------

    StreamPacket   *getFilledPacket();
    StreamPacket   *getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag );
    void            putFreePacket( StreamPacket *a_pkt );

//    void            streamNotifyThread();

    ConfigManager              &m_cfg_mgr;
    IOutputAdapter             *m_out_adapter;  ///< Active stream output adapter
    std::vector<IInputAdapter*> m_in_adapters;  ///< Active stream input adapters
    std::vector<StreamPacket*>  m_stream_pkts;  ///< Stream packets
    SyncDeque<StreamPacket*>    m_free_que;     ///< Free stream packet buffer
    SyncDeque<StreamPacket*>    m_fill_que;     ///< Filled stream packet buffer
/*
    SyncDeque<StreamPacket*>        m_notify_que;   ///< Stream listener notification packet buffer
    uint32_t                        m_max_notify_pkts;
    std::vector<IStreamListener*>   m_stream_listeners;         ///< Registered stream listener container
    boost::thread                  *m_stream_notify_thread;  ///< Thread to send notifications to stream listeners
*/
};

}

#endif // STREAMSERVICE_H
