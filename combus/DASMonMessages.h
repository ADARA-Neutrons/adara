#ifndef DASMONMESSAGES_H
#define DASMONMESSAGES_H

#include <string>
#include <boost/lexical_cast.hpp>
#include "ComBusMessages.h"
#include "../dasmon/common/DASMonDefs.h"

namespace ADARA {
namespace ComBus {
namespace DASMON {

//////////////////////////////////////////////////////////////////////////////
// DASMon Commands

#if 0
class SubscribeProcessVariableCommand : public Command
{
public:
SubscribeProcessVariableCommand( std::string a_pv_name, bool a_subscribe, uint32_t a_update_interval )
    : m_pv_name(a_pv_name), m_subscribe(a_subscribe), m_update_interval(a_update_interval)
{}
SubscribeProcessVariableCommand( const cms::Message &a_msg )
    : Command( a_msg )
{  translateFrom( a_msg ); }

inline MessageType getMessageType() const
{ return MSG_CMD_CONFIG_LOGGING; }

private:
virtual void translateTo( cms::Message &a_msg )
{
    Command::translateTo( a_msg );

    a_msg.setBooleanProperty( "log_enabled", m_enabled );
    a_msg.setShortProperty( "log_level", (short)m_level );
}

void translateFrom( const cms::Message &a_msg )
{
    m_enabled = a_msg.getBooleanProperty( "log_enabled" );
    m_level = (Level)a_msg.getShortProperty( "log_level" );
}

std::string     m_pv_name;
bool            m_subscribe;
uint32_t        m_update_interval;
};
#endif

//////////////////////////////////////////////////////////////////////////////
// DASMon Application Messages

class ConnectionStatusMessage : public MessageBase
{
public:
    ConnectionStatusMessage( const cms::Message &a_msg )
        : MessageBase( a_msg )  { translateFrom( a_msg ); }

    ConnectionStatusMessage( bool m_connected, const std::string &a_host, unsigned short a_port )
        : m_connected(m_connected), m_host(a_host), m_port(a_port) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_SMS_CONN_STATUS; }
    bool                m_connected;
    std::string         m_host;
    unsigned short      m_port;

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "connected", m_connected );
                            a_msg.setStringProperty( "host", m_host );
                            a_msg.setShortProperty( "port", (short)m_port );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_connected = a_msg.getBooleanProperty( "connected" );
                            m_host = a_msg.getStringProperty( "host" );
                            m_port = (unsigned short)a_msg.getShortProperty( "port" );
                        }
};


class RunStatusMessage : public MessageBase
{
public:
    RunStatusMessage( const cms::Message &a_msg )
        : MessageBase( a_msg )  { translateFrom( a_msg ); }

    RunStatusMessage( bool a_recording, unsigned long a_run_number )
        : m_recording(a_recording), m_run_number(a_run_number) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_STATUS; }
    bool                m_recording;
    unsigned long       m_run_number;

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "recording", m_recording );
                            a_msg.setIntProperty( "run_number", (long)m_run_number );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_recording = a_msg.getBooleanProperty( "recording" );
                            m_run_number = (unsigned long)a_msg.getIntProperty( "run_number" );
                        }

};


class PauseStatusMessage : public MessageBase
{
public:
    PauseStatusMessage( const cms::Message &a_msg )
        : MessageBase( a_msg )  { translateFrom( a_msg ); }

    PauseStatusMessage( bool a_paused )
        : m_paused(a_paused) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_PAUSE_STATUS; }
    bool                m_paused;

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "paused", m_paused );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_paused = a_msg.getBooleanProperty( "paused" );
                        }

};


class ScanStatusMessage : public MessageBase
{
public:
    ScanStatusMessage( const cms::Message &a_msg )
        : MessageBase( a_msg )  { translateFrom( a_msg ); }

    ScanStatusMessage( bool a_scaning, unsigned long a_scan_index )
        : m_scaning(a_scaning), m_scan_index(a_scan_index) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_SCAN_STATUS; }
    bool                m_scaning;
    unsigned long       m_scan_index;

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setBooleanProperty( "scanning", m_scaning );
                            a_msg.setIntProperty( "scan_index", (long)m_scan_index );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_scaning = a_msg.getBooleanProperty( "scanning" );
                            m_scan_index = (unsigned long)a_msg.getIntProperty( "scan_index" );
                        }
};


class BeamInfoMessage : public MessageBase, public ADARA::DASMON::BeamInfo
{
public:
    BeamInfoMessage( const cms::Message &a_msg )
      : MessageBase( a_msg ) { translateFrom( a_msg ); }
    BeamInfoMessage() {}
    BeamInfoMessage( const ADARA::DASMON::BeamInfo &a_info )
      : ADARA::DASMON::BeamInfo( a_info ) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_BEAM_INFO; }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setStringProperty( "facility", m_facility );
                            a_msg.setStringProperty( "beam_id", m_beam_id );
                            a_msg.setStringProperty( "beam_sname", m_beam_sname );
                            a_msg.setStringProperty( "beam_lname", m_beam_lname );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_facility = a_msg.getStringProperty( "facility" );
                            m_beam_id = a_msg.getStringProperty( "beam_id" );
                            m_beam_sname = a_msg.getStringProperty( "beam_sname" );
                            m_beam_lname = a_msg.getStringProperty( "beam_lname" );
                        }
};


class RunInfoMessage : public MessageBase, public ADARA::DASMON::RunInfo
{
public:
    RunInfoMessage( const cms::Message &a_msg )
      : MessageBase( a_msg ) { translateFrom( a_msg ); }
    RunInfoMessage() {}
    RunInfoMessage( const ADARA::DASMON::RunInfo &a_info )
      : ADARA::DASMON::RunInfo( a_info ) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_INFO; }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setStringProperty( "proposal_id", m_proposal_id );
                            a_msg.setStringProperty( "run_title", m_run_title );
                            a_msg.setIntProperty( "run_num", (long)m_run_num );
                            a_msg.setStringProperty( "sample_id", m_sample_id );
                            a_msg.setStringProperty( "sample_name", m_sample_name );
                            a_msg.setStringProperty( "sample_environment", m_sample_environ );
                            a_msg.setStringProperty( "sample_formula", m_sample_formula );
                            a_msg.setStringProperty( "sample_nature", m_sample_nature );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_proposal_id = a_msg.getStringProperty( "proposal_id" );
                            m_run_title = a_msg.getStringProperty( "run_title" );
                            m_run_num = (unsigned long)a_msg.getIntProperty( "run_num" );
                            m_sample_id = a_msg.getStringProperty( "sample_id" );
                            m_sample_name = a_msg.getStringProperty( "sample_name" );
                            m_sample_environ = a_msg.getStringProperty( "sample_environment" );
                            m_sample_formula = a_msg.getStringProperty( "sample_formula" );
                            m_sample_nature = a_msg.getStringProperty( "sample_nature" );
                        }
};



class BeamMetricsMessage : public MessageBase, public ADARA::DASMON::BeamMetrics
{
public:
    BeamMetricsMessage( const cms::Message &a_msg )
        : MessageBase( a_msg ) { translateFrom( a_msg ); }
    BeamMetricsMessage() {}
    BeamMetricsMessage( const ADARA::DASMON::BeamMetrics &a_metrics )
      : ADARA::DASMON::BeamMetrics( a_metrics ) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_BEAM_METRICS; }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setDoubleProperty( "count_rate", m_count_rate );
                            a_msg.setDoubleProperty( "pulse_charge", m_pulse_charge );
                            a_msg.setDoubleProperty( "pulse_freq", m_pulse_freq );
                            a_msg.setDoubleProperty( "pixel_error_rate", m_pixel_error_rate );
                            a_msg.setIntProperty( "stream_bps", (long)m_stream_bps );
                            a_msg.setShortProperty( "num_monitors", m_num_monitors );
                            for ( unsigned short m = 0; m < m_num_monitors; ++m )
                                a_msg.setDoubleProperty( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m), m_monitor_count_rate[m] );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_count_rate = a_msg.getDoubleProperty( "count_rate" );
                            m_pulse_charge = a_msg.getDoubleProperty( "pulse_charge" );
                            m_pulse_freq = a_msg.getDoubleProperty( "pulse_freq" );
                            m_pixel_error_rate = a_msg.getDoubleProperty( "pixel_error_rate" );
                            m_stream_bps = (unsigned long)a_msg.getIntProperty( "stream_bps" );
                            m_num_monitors = (unsigned short)a_msg.getShortProperty( "num_monitors" );
                            for ( unsigned short m = 0; m < m_num_monitors; ++m )
                                m_monitor_count_rate[m] = a_msg.getDoubleProperty( std::string("monitor_count_rate_") + boost::lexical_cast<std::string>(m) );
                        }
};


class RunMetricsMessage : public MessageBase, public ADARA::DASMON::RunMetrics
{
public:
    RunMetricsMessage( const cms::Message &a_msg )
        : MessageBase( a_msg ) { translateFrom( a_msg ); }
    RunMetricsMessage() {}
    RunMetricsMessage( const ADARA::DASMON::RunMetrics &a_metrics )
      : ADARA::DASMON::RunMetrics( a_metrics ) {}

    inline MessageType  getMessageType() const { return MSG_DASMON_RUN_METRICS; }

protected:
    virtual void        translateTo( cms::Message &a_msg )
                        {
                            MessageBase::translateTo( a_msg );
                            a_msg.setIntProperty( "pulse_count", (long)m_pulse_count );
                            a_msg.setDoubleProperty( "pulse_charge", m_pulse_charge );
                            a_msg.setIntProperty( "pixel_error_count", (long)m_pixel_error_count );
                            a_msg.setIntProperty( "dup_pulse_count", (long)m_dup_pulse_count );
                            a_msg.setIntProperty( "cycle_error_count", (long)m_cycle_error_count );
                        }

    void                translateFrom( const cms::Message &a_msg )
                        {
                            m_pulse_count = (unsigned long) a_msg.getIntProperty( "pulse_count" );
                            m_pulse_charge = a_msg.getDoubleProperty( "pulse_charge" );
                            m_pixel_error_count = (unsigned long) a_msg.getIntProperty( "pixel_error_count" );
                            m_dup_pulse_count = (unsigned long) a_msg.getIntProperty( "dup_pulse_count" );
                            m_cycle_error_count = (unsigned long) a_msg.getIntProperty( "cycle_error_count" );
                        }
};

}}}


#endif // DASMONMESSAGES_H
