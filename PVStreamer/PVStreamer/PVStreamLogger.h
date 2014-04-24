/**
 * \file PVStreamLogger.h
 * \brief Header file for PVStreamLogger class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef PVSTREAMLOGGER
#define PVSTREAMLOGGER

#include <string>
#include <fstream>

#include <boost/thread/mutex.hpp>

#include "PVStreamerSupport.h"
#include "ADARA_PVWriter.h"

namespace SNS { namespace PVS {

/**
 * \class PVStreamLogger
 * \brief Data logger class for PVStreamer application
 *
 * The PVStreamLogger class provides file-based data logging for three types of
 * events within the PVStreamer application: stream events, configuration events,
 * and general log events (info, warnings, and errors). The log entries created
 * by this class are human-readable text. This class is thread safe. Ad hoc
 * logging is provided through the ILogger LOG_XXX macros defined in
 * PVStreamSupport.h. (Note: Do not access te ILogger API directly as deadlock
 * can result.) Only one instance of this class is allowed due to the global
 * logging feature of ILogger.
 */
class PVStreamLogger : public IPVStreamListener, public IPVConfigListener, public SNS::PVS::ADARA::IADARAWriterListener, public ILogger
{
public:
    PVStreamLogger( const std::string & a_logfilepath );
    ~PVStreamLogger();

private:

    // ---------- IPVConfigListener Methods -----------------------------------

    void                configurationLoaded( Protocol a_protocol, const std::string &a_source );
    void                configurationInvalid( Protocol a_protocol, const std::string &a_source );

    // ---------- IADARAWriterListener Methods --------------------------------

    void                listening( const std::string & a_address, unsigned short a_port );
    void                connected( std::string &a_address );
    void                disconnected( std::string &a_address );

    // ---------- IPVStreamListener Methods -----------------------------------

    void                deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                pvActive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvInactive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value );

    // ---------- ILogger Methods ---------------------------------------------

    std::ostream &      write(ILogger::RecordType a_type);
    void                done();

    // ---------- Local Methods -----------------------------------------------

    std::string         timeString( Timestamp *ts = 0 );
    const char *        getText( RecordType a_type ) const;

    std::ofstream       m_out;      ///< File output stream object
    boost::mutex        m_mutex;    ///< Mutext to synchronize file output
};

}}

#endif