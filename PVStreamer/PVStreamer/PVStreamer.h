/**
 * \file PVStreamer.h
 * \brief Header file for PVStreamer class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVSTREAMER
#define PVSTREAMER

#include <map>
#include <vector>
#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include "SyncDeque.h"
#include "PVStreamerSupport.h"


namespace SNS { namespace PVS {

class PVConfig;
class PVReader;
class PVWriter;

/**
 * \class PVStreamer
 * \brief Primary sream and configuration management class of the PVStreamer process.
 *
 * The PVStreamer class provides two primary services: 1) configuration management
 * and access, and 2) stream management and access. The PVStreamer class is the
 * central class for the PVStreamer application - instances of PVConfig, PVReader,
 * and PVWriter subclasses (that each handle specific protocols) are associated with
 * a PVStreamer instance to create an end-to-end stream translation process. Downstream
 * clients (i.e. the SMS process) connect to a particular PVWriter instance in a
 * protocol specific manner. Currently, PVStreamer only allows on PVWriter object to
 * be associated with it due to limitations of the inernal buffering scheme; however,
 * multiple PVReader and PVConfig may be attached as needed.
 *
 * Configuration management and access is provided by the IPVConfigServices and
 * IPVConfigListener interfaces as well as the public interface of PVSTreamer class itself.
 * PVStreamer does not acquire configuration data; rather instances of associated
 * PVConfig objects gather configuration data and use PVStreamer to store and
 * disseminate this data. PVStreamer also does not monitor which devices and/or
 * process variables are actually active (as opposed to simply defined) - this is
 * defered to PVReader classes as it require protocol-specific implementation.
 *
 * PVStreamer provides buffering and access to an internal protocol-independent process
 * variable stream. PVReader objects acquire, fill, then emit PVStreamPacket objects
 * into the internal stream. PVWriter objects receive internal stream packets, then
 * translate and emit them into protocol-specific extrernal streams. This stream path
 * is "high performance" and requires minimal latency due to buffering and translation.
 * Clients with less-critical and higher-latency processing (GUIs and loggers) can
 * access the internal stream via the IPVStreamListener interface. This interface
 * tracks the internal stream unless the buffering backs-up to a specified level, at
 * which point incoming stream packets bypass the stream listener queue.
 */
class PVStreamer : private IPVConfigServices, private IPVReaderServices, private IPVWriterServices
{
public:
    PVStreamer( size_t a_pkt_buffer_size = 100, size_t a_max_notify_pkts = 0 );
    ~PVStreamer();

    bool                            isPVDefined( Identifier a_dev_id, Identifier a_pv_id ) const;
    const PVInfo*                   getPV( Identifier a_dev_id, Identifier a_pv_id ) const;
    const std::map<Identifier,const Enum*> &getEnums() const;
    const std::vector<const PVInfo*>& getDevicePVs( Identifier a_dev_id ) const;
    void                            getActiveDevices( std::vector<Identifier> &a_devs ) const;
    bool                            isDeviceDefined( Identifier a_dev_id ) const;
    std::string                     getDeviceName( Identifier a_dev_id ) const;
    bool                            isAppDefined( Identifier a_app_id ) const;
    void                            appActive( Identifier a_app_id ) const;
    void                            appInactive( Identifier a_app_id ) const;
    bool                            isAppActive( Identifier a_app_id ) const;
    const std::vector<Identifier>&  getAppDevices( Identifier a_app_id ) const;

    void                            attachConfigListener( IPVConfigListener &a_listener );
    void                            detachConfigListener( IPVConfigListener &a_listener );
    void                            attachStreamListener( IPVStreamListener &a_listener );
    void                            detachStreamListener( IPVStreamListener &a_listener );
    void                            attachStatusListener( IPVStreamerStatusListener &a_listener );
    void                            detachStatusListener( IPVStreamerStatusListener &a_listener );
    IPVConfigServices*              attach( PVConfig &a_config );
    IPVReaderServices*              attach( PVReader &a_reader );
    IPVWriterServices*              attach( PVWriter &a_writer );

private:
    /// Defines a global process variable key by associating with device ID
    typedef std::pair<Identifier,Identifier> PVKey;

    /// IOC Application information structure
    struct AppInfo
    {
        Identifier                  app_id;     /// Application ID
        Protocol                    protocol;   /// Protocol
        std::string                 source;     /// Source (i.e. hostname)
        std::vector<Identifier>     devices;    /// Configured devices associated with app
        bool                        active;     /// Is active?
    };

    /// Device information structure
    struct DeviceInfo
    {
        Identifier                  device_id;  /// Device ID
        Identifier                  app_id;     /// Owning application ID
        std::string                 name;       /// Name of device
        Protocol                    protocol;   /// Protocol
        std::string                 source;     /// Source (i.e. hostname)
        std::vector<PVInfo*>        pvs;        /// Configured process variables associated with device
    };


    // ---------- IPVCommonServices methods ----------

    void                        unhandledException( TraceException &e );

    // ---------- IPVConfigServices methods ----------

    void                        defineDevice( Protocol a_protocol, const std::string &a_name, const std::string &a_source );
    void                        undefineDevice( Protocol a_protocol, const std::string &a_name );
    void                        undefineDeviceIfNoPVs( Identifier a_dev_id );
    void                        definePV( PVInfo & info );
    void                        undefinePV( Identifier a_dev_id, Identifier a_pv_id );
    const Enum*                 getEnum( Identifier a_id ) const;
    const Enum*                 defineEnum( const std::map<int,std::string> &a_values );
    void                        configurationLoaded( Protocol a_protocol, const std::string &a_source );
    void                        configurationInvalid( Protocol a_protocol, const std::string &a_source );
    PVInfo*                     getWriteablePV( const std::string & a_name ) const;
    Identifier                  getDeviceIdentifier( Protocol a_protocol, const std::string &a_device_name );
    void                        releaseDeviceIdentifier( Protocol a_protocol, const std::string &a_device_name );

    // ---------- IPVReaderServices methods ----------

    PVStreamPacket*             getFreePacket();
    PVStreamPacket*             getFreePacket( unsigned long a_timeout, bool & a_timeout_flag );
    void                        putFilledPacket( PVStreamPacket *a_pkt );
    PVInfo*                     getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const;
    std::vector<PVInfo*> &      getWriteableDevicePVs( Identifier a_dev_id ) const;

    // ---------- IPVWriterServices methods ----------

    PVStreamPacket*             getFilledPacket();
    PVStreamPacket*             getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag );
    void                        putFreePacket( PVStreamPacket *a_pkt );

    // ---------- IPVStreamListener support methods ----------

    void                        streamListenersNotifyThreadFunc();
    void                        notifyStreamListeners( PVStreamPacket *a_pkt );

    // ---------- Private Attributes ----------

    size_t                                      m_pkt_buffer_size;          ///< Stream packet buffer size
    size_t                                      m_max_notify_pkts;          ///< Max stream packets to queue to notify service
    std::map<Protocol,PVConfig*>                m_config;                   ///< Active configuration objects
    std::map<Protocol,PVReader*>                m_readers;                  ///< Active reader objects
    PVWriter*                                   m_writer;                   ///< Active writer
    std::vector<PVStreamPacket*>                m_stream_pkts;              ///< Stream packets
    SyncDeque<PVStreamPacket*>                  m_free_que;                 ///< Free stream packet buffer
    SyncDeque<PVStreamPacket*>                  m_fill_que;                 ///< Filled stream packet buffer
    SyncDeque<PVStreamPacket*>                  m_notify_que;               ///< Stream listener notification packet buffer
    mutable boost::mutex                        m_api_mutex;                ///< Synchronizes PVStreamer API
    mutable boost::mutex                        m_strlist_mutex;            ///< Protects stream listener container
    mutable boost::mutex                        m_cfglist_mutex;            ///< Protects config listener container
    mutable boost::mutex                        m_cfg_mutex;                ///< Protects access to pv and device containers
    mutable boost::mutex                        m_statlist_mutex;           ///< Protects status listener container
    boost::thread*                              m_stream_listeners_thread;  ///< Thread to send notifications to stream listeners
    std::vector<IPVStreamListener*>             m_stream_listeners;         ///< Registered stream listener container
    std::vector<IPVConfigListener*>             m_config_listeners;         ///< Registered config listener container
    std::vector<IPVStreamerStatusListener*>     m_status_listeners;         ///< Registered status listener container
    std::map<PVKey,PVInfo*>                     m_pv_info;                  ///< All defined (configured) process variables
    std::map<Identifier,DeviceInfo*>            m_devices;                  ///< All defined (configured) devices
    std::map<Identifier,Enum*>                  m_enums;                    ///< All defined (configured) enumerations
    std::map<std::string,Identifier>            m_dev_id;                   ///< Device name to dynamically assigned identifier
    std::map<Identifier,std::string>            m_id_dev;                   ///< Dynamically assigned identifier to device name
};

}}

#endif
