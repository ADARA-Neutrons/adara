#ifndef STREAMSERVICE_H
#define STREAMSERVICE_H

#include <vector>
#include <string>
#include <string.h>
#include <syslog.h>

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
    VariableUpdate
};


/// Timestamp associated with device activity and variable values
struct Timestamp
{
    Timestamp() : sec(0), nsec(0) {}

    uint32_t    sec;
    uint32_t    nsec;
};

/// Holds last-known value and alarm state/severity for a process variable
struct PVState
{
    PVState()
        : m_uint_val(0),
        m_short_array(NULL), m_long_array(NULL),
        m_float_array(NULL), m_double_array(NULL),
        m_elem_count(0),
        m_status(0), m_severity(0)
    {}

    PVState( int16_t a_status, int16_t a_severity )
        : m_uint_val(0),
        m_short_array(NULL), m_long_array(NULL),
        m_float_array(NULL), m_double_array(NULL),
        m_elem_count(0),
        m_status(a_status), m_severity(a_severity)
    {}

    PVState( const PVState & a_state )
        : m_double_val(a_state.m_double_val), // Copy *Double*, Covers Union
        m_str_val(a_state.m_str_val),
        m_short_array(NULL), m_long_array(NULL),
        m_float_array(NULL), m_double_array(NULL),
        m_elem_count(a_state.m_elem_count), m_time(a_state.m_time),
        m_status(a_state.m_status), m_severity(a_state.m_severity)
    {
        // Don't Allocate Anything if there are No Elements...
        // (Minimum Array Size is 2! :-)
        if ( m_elem_count > 1 )
        {
            if ( a_state.m_short_array != NULL )
            {
                m_short_array = new int16_t[m_elem_count];
                memcpy( m_short_array,
                    a_state.m_short_array, m_elem_count * sizeof(int16_t) );
            }
            if ( a_state.m_long_array != NULL )
            {
                m_long_array = new int32_t[m_elem_count];
                memcpy( m_long_array,
                    a_state.m_long_array, m_elem_count * sizeof(int32_t) );
            }
            if ( a_state.m_float_array != NULL )
            {
                m_float_array = new float[m_elem_count];
                memcpy( m_float_array,
                    a_state.m_float_array, m_elem_count * sizeof(float) );
            }
            if ( a_state.m_double_array != NULL )
            {
                m_double_array = new double[m_elem_count];
                memcpy( m_double_array,
                    a_state.m_double_array, m_elem_count * sizeof(double) );
            }
        }
    }

    ~PVState()
    {
        delete[] m_short_array;
        delete[] m_long_array;
        delete[] m_float_array;
        delete[] m_double_array;
    }

    PVState& operator=( const PVState & a_state )
    {
        // Free & Null Out Any Existing Numerical Arrays...
        delete[] m_short_array;
        m_short_array = (int16_t *) NULL;
        delete[] m_long_array;
        m_long_array = (int32_t *) NULL;
        delete[] m_float_array;
        m_float_array = (float *) NULL;
        delete[] m_double_array;
        m_double_array = (double *) NULL;

        // Copy Scalar Fields...
        m_double_val = a_state.m_double_val; // Copy *Double*, Covers Union!
        m_str_val = a_state.m_str_val;
        m_elem_count = a_state.m_elem_count;
        m_time = a_state.m_time;
        m_status = a_state.m_status;
        m_severity = a_state.m_severity;

        // Don't Allocate Anything if there are No Elements...
        // (Minimum Array Size is 2! :-)
        if ( m_elem_count > 1 )
        {
            if ( a_state.m_short_array != NULL )
            {
                m_short_array = new int16_t[m_elem_count];
                memcpy( m_short_array,
                    a_state.m_short_array, m_elem_count * sizeof(int16_t) );
            }
            if ( a_state.m_long_array != NULL )
            {
                m_long_array = new int32_t[m_elem_count];
                memcpy( m_long_array,
                    a_state.m_long_array, m_elem_count * sizeof(int32_t) );
            }
            if ( a_state.m_float_array != NULL )
            {
                m_float_array = new float[m_elem_count];
                memcpy( m_float_array,
                    a_state.m_float_array, m_elem_count * sizeof(float) );
            }
            if ( a_state.m_double_array != NULL )
            {
                m_double_array = new double[m_elem_count];
                memcpy( m_double_array,
                    a_state.m_double_array, m_elem_count * sizeof(double) );
            }
        }
        return *this;
    }

    union
    {
        uint32_t        m_uint_val;   ///< Used for both uint and enum types
        int32_t         m_int_val;
        double          m_double_val;
    };
    std::string         m_str_val;

    int16_t            *m_short_array;
    int32_t            *m_long_array;
    float              *m_float_array;
    double             *m_double_array;

    uint32_t            m_elem_count;
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
    virtual ConfigManager&  getCfgMgr() = 0;
    virtual StreamPacket   *getFreePacket() = 0;
    virtual StreamPacket   *getFreePacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual bool            getFreeQueueActive(void) = 0;
    virtual size_t          getFreeQueueSize(void) = 0;
    virtual bool            getFilledQueueActive(void) = 0;
    virtual void            putFilledPacket( StreamPacket *a_pkt ) = 0;
    virtual size_t          getFilledQueueSize(void) = 0;
};


/**
 * \class IOutputAdapterAPI
 *
 * The IOutputAdapterAPI interface provides access to output-adapter-
 * specific services. Only one consumer may be attached to the streamer
 * at a given time.
 */
class IOutputAdapterAPI
{
public:
    virtual ConfigManager&  getCfgMgr() = 0;
    virtual StreamPacket   *getFilledPacket() = 0;
    virtual StreamPacket   *getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag ) = 0;
    virtual bool            getFilledQueueActive(void) = 0;
    virtual size_t          getFilledQueueSize(void) = 0;
    virtual bool            getFreeQueueActive(void) = 0;
    virtual void            putFreePacket( StreamPacket *a_pkt ) = 0;
    virtual size_t          getFreeQueueSize(void) = 0;
};


/**
 * @brief The StreamService class provide generalized internal streaming support
 *
 * The StreamService class provides a generalized streaming API to support both input- and output-
 * protocol adapters. Input Adapters inject packets into the internal stream; whereas Output
 * Adapters consume packets from the internal stream. Only one Output Adapter may be connected
 * to a StreamService instance at a time. A packet buffer is used to avoid memory allocation,
 * and SyncDeque instances are used to manage free- and filled-packet buffers. Ownership of connected
 * adapters is transferred to the StreamService instance and are destroyed when the StreamService
 * instance is destroyed. When destroyed, the StreamService instance shutsdown the SyncDeque objects
 * which is the signal for adapters to clean-up in preparation for deletion.
 */
class StreamService : private IInputAdapterAPI, private IOutputAdapterAPI
{
public:
    StreamService( size_t a_pkt_buffer_size, uint32_t a_offset = 0 );
    ~StreamService();

    IInputAdapterAPI*   attach( IInputAdapter &a_adapter );
    IOutputAdapterAPI*  attach( IOutputAdapter &a_adapter );
    void                detach( IInputAdapter &a_adapter );
    void                detach( IOutputAdapter &a_adapter );
    ConfigManager&      getCfgMgr() { return m_cfg_mgr; }

private:
    // ---------- IStreamProducer methods ----------

    StreamPacket   *getFreePacket();
    StreamPacket   *getFreePacket( unsigned long a_timeout, bool & a_timeout_flag );
    bool            getFreeQueueActive(void);
    size_t          getFreeQueueSize(void);
    void            putFilledPacket( StreamPacket *a_pkt );

    // ---------- IStreamConsumer methods ----------

    StreamPacket   *getFilledPacket();
    StreamPacket   *getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag );
    bool            getFilledQueueActive(void);
    size_t          getFilledQueueSize(void);
    void            putFreePacket( StreamPacket *a_pkt );

    ConfigManager               m_cfg_mgr;
    IOutputAdapter             *m_out_adapter;  ///< Active stream output adapter
    std::vector<IInputAdapter*> m_in_adapters;  ///< Active stream input adapters
    std::vector<StreamPacket*>  m_stream_pkts;  ///< Stream packets
    SyncDeque<StreamPacket*>    m_free_que;     ///< Free stream packet buffer
    SyncDeque<StreamPacket*>    m_fill_que;     ///< Filled stream packet buffer
    bool                        m_in_dtor;
};

}

#endif // STREAMSERVICE_H

// vim: expandtab

