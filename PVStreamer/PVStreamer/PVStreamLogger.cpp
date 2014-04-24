/**
 * \file PVStreamLogger.cpp
 * \brief Source file for PVStreamLogger class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"
#include "PVStreamLogger.h"

#include <boost/thread/locks.hpp>

using namespace std;

namespace SNS { namespace PVS {

/// Initialize global ILogger instance pointer
ILogger * ILogger::g_inst = 0;

/**
 * \brief Constructor for PVStreamLogger class.
 * \param a_logfilepath - Path for logfile to create.
 */
PVStreamLogger::PVStreamLogger( const std::string & a_logfilepath )
{
    if ( ILogger::g_inst )
        EXC(EC_INVALID_OPERATION,"Global logger instance already defined");

    char buf[50];
    time_t t = time(0);
	struct tm loctime;
	localtime_s( &loctime, &t );
    strftime(buf,50,"%Y%m%d_%H%M%S", &loctime);

    string filename = a_logfilepath + "\\pvslog_" + buf + ".txt";

    m_out.open( filename.c_str(), ios_base::app );
    m_out << timeString() << " PVStreamer started" << endl;

    ILogger::g_inst = this;
}

/**
 * \brief Destructor for PVStreamLogger class.
 */
PVStreamLogger::~PVStreamLogger()
{
    m_out << timeString() << " PVStreamer stopped" << endl;
    m_out.close();
}


/**
 * \brief Converts a timestamp into human-readable text.
 * \param a_ts - Pointer to timestamp value to convert (if null, uses current time).
 */
string
PVStreamLogger::timeString( Timestamp *a_ts )
{
    char buf[50];
    time_t t;

    if (a_ts)
        t = a_ts->sec;
    else
        t = time(0);

	struct tm loctime;
	localtime_s( &loctime, &t );
    strftime(buf,50,"%Y.%m.%d %H:%M.%S", &loctime);

    return string(buf);
}


// ---------- IPVConfigListener Methods ---------------------------------------

/**
 * \brief Callback to receive and log configuration loaded events.
 * \param a_protocol - Protocol associated with configuration
 * \param a_source - Source (host) of configuration event
 */
void
PVStreamLogger::configurationLoaded( Protocol a_protocol, const string &a_source )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString() << " Host Configured: " << a_source << " (protocol=" << a_protocol << ")" << endl;
}

/**
 * \brief Callback to receive and log invalid configuration events.
 * \param a_protocol - Protocol associated with configuration
 * \param a_source - Source (host) of configuration event
 */
void
PVStreamLogger::configurationInvalid( Protocol a_protocol, const string &a_source )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString() << " Error: Host has INVALID configuration: " << a_source << " (protocol=" << a_protocol << ")" << endl;
}

// ---------- IADARAWriterListener Methods ------------------------------------

/**
 * \brief Callback to receive and log ADARA listening status events.
 * \param a_address - Tcp address
 * \param a_port - Tcp port
 */
void
PVStreamLogger::listening( const std::string & a_address, unsigned short a_port )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << "ADARA: Listening on " << a_address << ":" << a_port << endl;
}

/**
 * \brief Callback to receive and log ADARA client connection events.
 * \param a_address - Tcp address
 */
void
PVStreamLogger::connected( string &a_address )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << "ADARA: Client connected from " << a_address << endl;
}

/**
 * \brief Callback to receive and log ADARA client disconnection events.
 * \param a_address - Tcp address
 */
void
PVStreamLogger::disconnected( string &a_address )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << "ADARA: Client disconnected from " << a_address << endl;
}

// ---------- IPVStreamListener Methods -----------------------------------

/**
 * \brief  Callback to receive and log device activation events.
 * \param a_time - Time of event
 * \param a_dev_id - ID of device that has become active
 * \param a_name - Name of device that has become active
 */
void
PVStreamLogger::deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " Device " << a_name << " (ID " << a_dev_id << ") ACTIVE" << endl;
}

/**
 * \brief  Callback to receive and log device de-activation events.
 * \param a_time - Time of event
 * \param a_dev_id - ID of device that has become inactive
 * \param a_name - Name of device that has become inactive
 */
void
PVStreamLogger::deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " Device " << a_name << " (ID " << a_dev_id << ") INACTIVE" << endl;
}

/**
 * \brief  Callback to receive and log process variable activation events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 */
void
PVStreamLogger::pvActive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name <<  " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") ACTIVE" << endl;
}

/**
 * \brief  Callback to receive and log process variable de-activation events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 */
void
PVStreamLogger::pvInactive( Timestamp &a_time, const PVInfo &a_pv_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name <<  " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") INACTIVE" << endl;
}

/**
 * \brief  Callback to receive and log process variable update events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 * \param a_value - New process value (long)
 */
void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

/**
 * \brief  Callback to receive and log process variable update events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 * \param a_value - New process value (long/enum)
 * \param a_enum - Enum pointer
 */
void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = ";
    if ( a_enum )
        m_out << a_enum->getName(a_value) << " ";

    m_out << "{" << a_value << "}" << alarmText(a_pv_info.m_alarms) << endl;
}

/**
 * \brief  Callback to receive and log process variable update events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 * \param a_value - New process value (unsigned long)
 */
void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

/**
 * \brief  Callback to receive and log process variable update events.
 * \param a_time - Time of event
 * \param a_pv_info - Process variable information object
 * \param a_value - New process value (double)
 */
void
PVStreamLogger::pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_out << timeString(&a_time) << " PV " << a_pv_info.m_name << " (ID " << a_pv_info.m_device_id << "." << a_pv_info.m_id << ") VALUE = " << a_value << alarmText(a_pv_info.m_alarms) << endl;
}

// ---------- ILogger Methods ---------------------------------------------


/**
 * \brief Synchronizes and writes header of new log record.
 * \param a_type - Record type to write
 * \return Ref to file out stream
 */
ostream &
PVStreamLogger::write( ILogger::RecordType a_type )
{
    m_mutex.lock();
    m_out << timeString() << " " << getText( a_type ); 
    return m_out;
}

/**
 * \brief Synchronizes and ends new log record.
 */
void
PVStreamLogger::done()
{
    m_out << endl;
    m_mutex.unlock();
}

/**
 * \brief Converts a record type value to human-readable text
 * \param a_type - Record type to convert
 * \return Record type text
 */
const char *
PVStreamLogger::getText( RecordType a_type ) const
{
    switch ( a_type )
    {
    case RT_INFO: return "";
    case RT_WARNING: return "Warning: ";
    case RT_ERROR: return "ERROR: ";
    }

    return "";
}

}}

