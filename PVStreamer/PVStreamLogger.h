#ifndef PVSTREAMLOGGER
#define PVSTREAMLOGGER

#include <string>
#include <fstream>

#include "PVStreamer.h"

namespace SNS { namespace PVS {

class PVStreamLogger : public IPVStreamListener, public IPVConfigListener
{
public:
    PVStreamLogger( const std::string & a_filename );
    ~PVStreamLogger();

protected:

    // ---------- IPVConfigListener Methods -----------------------------------

    void                configurationLoaded( Protocol a_protocol, const std::string &a_source );

    // ---------- IPVStreamListener Methods -----------------------------------

    void                deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                pvActive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvInactive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvStatusUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned short a_alarms );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value );

    // ---------- Local Methods -----------------------------------------------

    std::string         timeString( Timestamp *ts = 0 );

private:
    std::ofstream       m_out;
};

}}

#endif