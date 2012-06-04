
#include "stdafx.h"
#include "PVStreamLogger.h"


using namespace std;

namespace SNS { namespace PVS {


PVStreamLogger::PVStreamLogger( const std::string & a_filename )
{
    m_out.open( a_filename.c_str(), ios_base::app );
    m_out << timeString() << " PVStreamer started" << endl;
}


PVStreamLogger::~PVStreamLogger()
{
    m_out << timeString() << " PVStreamer stopped" << endl;
    m_out.close();
}


string
PVStreamLogger::timeString( Timestamp *ts )
{
    char buf[50];
    time_t t;

    if (ts)
        t = ts->sec;
    else
        t = time(0);

    strftime(buf,50,"%Y.%m.%d %H:%M.%S", localtime(&t));
    return string(buf);
}


// ---------- IPVConfigListener Methods -----------------------------------

void
PVStreamLogger::configurationLoaded( Protocol a_protocol, const string &a_source )
{
    m_out << timeString() << " Host Configured: " << a_source << " (protocol=" << a_protocol << ")" << endl;
}

// ---------- IPVStreamListener Methods -----------------------------------

void
PVStreamLogger::deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    m_out << timeString() << " Device Active: " << a_name << " (ID " << a_dev_id << ")" << endl;
}

void
PVStreamLogger::deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    m_out << timeString() << " Device Inactive: " << a_name << " (ID " << a_dev_id << ")" << endl;
}

void
PVStreamLogger::pvActive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    m_out << timeString() << " PV Active: " << a_pv_info.m_name <<  " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ")" << endl;
}

void
PVStreamLogger::pvInactive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    m_out << timeString() << " PV Inactive: " << a_pv_info.m_name <<  " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ")" << endl;
}

void
PVStreamLogger::pvStatusUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned short a_alarms )
{
}

void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value )
{
    m_out << timeString() << " PV Value: " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum )
{
    m_out << timeString() << " PV Value: " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value )
{
    m_out << timeString() << " PV Value: " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value )
{
    m_out << timeString() << " PV Value: " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}



}}

