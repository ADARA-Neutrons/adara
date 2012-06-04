// PVStreamer.h

#ifndef PVSTREAMER
#define PVSTREAMER

#include <map>
#include <vector>
#include <string>

#include <boost/thread/mutex.hpp>

#include "PVStreamerSupport.h"
#include "SyncDeque.h"


namespace SNS { namespace PVS {

class PVConfig;
class PVReader;
class PVWriter;

// ---------- Interfaces used by PVStreamer and clients -----------------------

/**
 * The IPVStreamListener interface is used by non-critical stream listeners
 * to access the PV event stream (prior to protocol serialization). Under heavy
 * loading it is possible for packets to be dropped.
 */
class IPVStreamListener
{
public:
    virtual void                deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name ) = 0;
    virtual void                deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name ) = 0;
    virtual void                pvActive( Timestamp &a_time, const PVInfo &a_pv_info ) = 0;
    virtual void                pvInactive( Timestamp &a_time, const PVInfo &a_pv_info ) = 0;
    virtual void                pvStatusUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned short a_alarms ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value ) = 0;
    virtual void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value ) = 0;
    //virtual void                pvValueUpdated( const PVInfo &a_pv_info, std::string a_value ) = 0;
};

class IPVConfigListener
{
public:
    virtual void                configurationLoaded( Protocol a_protocol, const std::string &a_source ) = 0;

    //virtual void                deviceRunning( unsigned long a_dev_id, unsigned long a_protocol_id ) = 0;
    //virtual void                deviceStopped( unsigned long a_dev_id, unsigned long a_protocol_id ) = 0;

    // TODO Need API for reconnecting or configuration changes? (CfgMgr can just stop and restart all running devices)
};

/**
 * The IPVConfigServices interface is used by PVConfig instances to add and
 * remove pvs and enums to/from the central pv repository. Only PVConfig
 * instances have the ability to define and undefine pvs.
 */
class IPVConfigServices
{
public:
    
    virtual void                defineApp( Protocol a_protocol, Identifier a_app_id, const std::string &a_source ) = 0;
    virtual void                undefineApp( Identifier a_app_id ) = 0;
    virtual void                defineDevice( Protocol a_protocol, Identifier a_dev_id, const std::string &a_name, const std::string &a_source,  Identifier a_app_id = 0 ) = 0;
    virtual void                undefineDevice( Identifier a_dev_id ) = 0;
    virtual void                undefineDeviceIfNoPVs( Identifier a_dev_id ) = 0;
    virtual void                definePV( PVInfo & info ) = 0;
    virtual void                undefinePV( Identifier a_dev_id, Identifier a_pv_id ) = 0;
    virtual const Enum*         getEnum( Identifier a_id ) const = 0;
    virtual const Enum*         defineEnum( const std::map<int,std::string> &a_values ) = 0;
    virtual void                configurationLoaded( Protocol a_protocol, const std::string &a_source ) = 0;
    virtual PVInfo*             getWriteablePV( const std::string & a_name ) const = 0;
};

/**
 * The IPVReaderServices interface is used to grant access to reader-specific
 * services (buffers and writeable PVInfo objects).
 */
class IPVReaderServices
{
public:
    virtual PVStreamPacket*     getFreePacket() = 0;
    virtual void                putFilledPacket( PVStreamPacket *a_pkt ) = 0;
    virtual PVInfo*             getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const = 0;
    virtual std::vector<PVInfo*> &  getWriteableDevicePVs( Identifier a_dev_id ) const = 0;
    virtual void                getSourceInfo( Protocol a_protocol, const std::string &a_source, std::map<Identifier,std::vector<PVInfo*> > &a_info ) const = 0;
};

/**
 * The IPVWriterServices interface is used to grant access to writer-specific
 * services (buffers). Only one writer may be attached to the streamer at a
 * given time. (a future revision may provide multiple writers, but will require
 * substantial changes to the writer interface.)
 */
class IPVWriterServices
{
public:
    virtual PVStreamPacket*     getFilledPacket() = 0;
    virtual void                putFreePacket( PVStreamPacket *a_pkt ) = 0;
};


// ---------- PVStreamer class ------------------------------------------------

class PVStreamer : private IPVConfigServices, private IPVReaderServices, private IPVWriterServices
{
public:
    PVStreamer( size_t a_pkt_buffer_size = 100, size_t a_max_notify_pkts = 0 );
    ~PVStreamer();

//    void                                getActivePVs( std::map<Identifier,std::vector<const PVInfo*> > &a_pvs ) const;
    bool                                isPVDefined( Identifier a_dev_id, Identifier a_pv_id ) const;
    const PVInfo*                       getPV( Identifier a_dev_id, Identifier a_pv_id ) const;
    const std::map<Identifier,const Enum*> &getEnums() const;
    const std::vector<const PVInfo*>&   getDevicePVs( Identifier a_dev_id ) const;
    void                                getActiveDevices( std::vector<Identifier> &a_devs ) const;
    bool                                isDeviceDefined( Identifier a_dev_id ) const;
    std::string                         getDeviceName( Identifier a_dev_id ) const;
    bool                                isAppDefined( Identifier a_app_id ) const;
    const std::vector<Identifier>&      getAppDevices( Identifier a_app_id ) const;

    void                        attachConfigListener( IPVConfigListener &a_listener );
    void                        detachConfigListener( IPVConfigListener &a_listener );

    void                        attachStreamListener( IPVStreamListener &a_listener );
    void                        detachStreamListener( IPVStreamListener &a_listener );

    IPVConfigServices*          attach( PVConfig &a_config );
    IPVReaderServices*          attach( PVReader &a_reader );
    IPVWriterServices*          attach( PVWriter &a_writer );

    //TODO Need to think about reader/writer/config detachment - is it even needed?

private:
    typedef std::pair<Identifier,Identifier> PVKey;

    struct AppInfo
    {
        Identifier                  app_id;
        Protocol                    protocol;
        std::string                 source;
        std::vector<Identifier>     devices;
    };

    struct DeviceInfo
    {
        Identifier              device_id;
        Identifier              app_id;
        std::string             name;
        Protocol                protocol;
        std::string             source;
        std::vector<PVInfo*>    pvs;
    };

    // ---------- IPVConfigServices methods ----------

    void                        defineApp( Protocol a_protocol, Identifier a_app_id, const std::string &a_source );
    void                        undefineApp( Identifier a_app_id );
    void                        defineDevice( Protocol a_protocol, Identifier a_dev_id, const std::string &a_name, const std::string &a_source, Identifier a_app_id = 0 );
    void                        undefineDevice( Identifier a_dev_id );
    void                        undefineDeviceIfNoPVs( Identifier a_dev_id );
    void                        definePV( PVInfo & info );
    void                        undefinePV( Identifier a_dev_id, Identifier a_pv_id );
    const Enum*                 getEnum( Identifier a_id ) const;
    const Enum*                 defineEnum( const std::map<int,std::string> &a_values );
    void                        configurationLoaded( Protocol a_protocol, const std::string &a_source );
    PVInfo*                     getWriteablePV( const std::string & a_name ) const;

    // ---------- IPVReaderServices methods ----------

    PVStreamPacket*             getFreePacket();
    void                        putFilledPacket( PVStreamPacket *a_pkt );
    PVInfo*                     getWriteablePV( Identifier a_dev_id, Identifier a_pv_id ) const;
    std::vector<PVInfo*> &      getWriteableDevicePVs( Identifier a_dev_id ) const;
    void                        getSourceInfo( Protocol a_protocol, const std::string &a_source, std::map<Identifier,std::vector<PVInfo*> > &a_info ) const;

    // ---------- IPVWriterServices methods ----------

    PVStreamPacket*             getFilledPacket();
    void                        putFreePacket( PVStreamPacket *a_pkt );

    // ---------- IPVStreamListener support methods ----------

    void                        streamListenersNotifyThreadFunc();
    void                        notifyStreamListeners( PVStreamPacket *a_pkt );

    // ---------- Private Attributes ----------
 
    size_t                                      m_pkt_buffer_size;
    size_t                                      m_max_notify_pkts;

    std::map<Protocol,PVConfig*>                m_config;
    std::map<Protocol,PVReader*>                m_readers;
    PVWriter*                                   m_writer;

    SyncDeque<PVStreamPacket*>                  m_free_que;
    SyncDeque<PVStreamPacket*>                  m_fill_que;
    SyncDeque<PVStreamPacket*>                  m_notify_que;

    mutable boost::mutex                        m_api_mutex;
    mutable boost::mutex                        m_strlist_mutex;    //< Used to protect stream listener container
    mutable boost::mutex                        m_cfglist_mutex;    //< Used to protect config listener container
    mutable boost::mutex                        m_cfg_mutex;        //< Used for access to pv and device containers

    boost::thread*                              m_stream_listeners_thread;
    std::vector<IPVStreamListener*>             m_stream_listeners;
    std::vector<IPVConfigListener*>             m_config_listeners;

    std::map<PVKey,PVInfo*>                     m_pv_info;
    std::map<Identifier,DeviceInfo*>            m_devices;
    std::map<Identifier,AppInfo*>               m_apps;
    std::map<Identifier,Enum*>                  m_enums;

    static Identifier m_next_enum_id;
};

}}

#endif
