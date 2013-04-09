/**
 * \file LDAS_PVReaderAgent.cpp
 * \brief Source file for LDAS_PVReaderAgent class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#include "stdafx.h"

#include <boost/thread/locks.hpp>

#include "LDAS_PVReaderAgent.h"

using namespace std;
using namespace NI;

namespace SNS { namespace PVS { namespace LDAS {

/**
 * \brief Constructor for LDAS_PVReaderAgent class.
 * \param a_mgr - Owning LDAS_IPVReaderAgentMgr instance
 * \param a_reader_services - PVStreamer reader services interface
 * \param a_pv_info - PVInfo instance for variable to be associated with this reader agent (optional)
 */
LDAS_PVReaderAgent::LDAS_PVReaderAgent( LDAS_IPVReaderAgentMgr &a_mgr, IPVReaderServices &a_reader_services )
: m_mgr(a_mgr), m_reader_services(a_reader_services), m_pv_info(0), m_error(0), m_array_idx(-1), m_first_send(false), m_cache_array_info(false), m_array_size(0)
{
}

/**
 * \brief Destructor for LDAS_PVReaderAgent class.
 */
LDAS_PVReaderAgent::~LDAS_PVReaderAgent()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    if ( m_pv_info )
    {
        // Do NOT call disconnect as it triggers synchronous callbacks
        m_socket.RemoveEventHandler( CNiDataSocket::StatusUpdatedEvent );
        m_socket.RemoveEventHandler( CNiDataSocket::DataUpdatedEvent );
        m_pv_info = 0;
    }
}

/**
 * \brief Attempts to connect this agent to a new process variable.
 * \param a_pv_info - PVInfo instance for variable to be associated with this reader agent
 */
void
LDAS_PVReaderAgent::connect( PVInfo &a_pv_info )
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_pv_info )
        EXC( EC_INVALID_OPERATION, "Already connected to a process variable" );

    m_pv_info = &a_pv_info;
    m_error = 0;

    // If DataSocket connection URI ends with [xxx], then this is an array-based PV
    // and we need to extract the array index
    size_t i = m_pv_info->m_connection.find_last_of("[");
    if ( i != string::npos )
    {
      size_t j = m_pv_info->m_connection.find_last_of("]");
      if ( j != string::npos && i < j )
      {
          string idx_str = m_pv_info->m_connection.substr( i + 1, j-i-1 );
          m_array_idx = atol( idx_str.c_str());
          m_cache_array_info = true;
          m_first_send = true;
      }
    }

    m_socket.InstallEventHandler( *this, &LDAS_PVReaderAgent::socketRead );
    m_socket.InstallEventHandler( *this, &LDAS_PVReaderAgent::socketStatus );
    m_socket.Connect( a_pv_info.m_connection.c_str(), CNiDataSocket::ReadAutoUpdate ); // will trigger callbacks
}

/**
 * \brief Attempts to disconnect this agent from the current process variable.
 */
void
LDAS_PVReaderAgent::disconnect()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if ( m_pv_info )
    {
        m_socket.Disconnect(); // Will trigger callbacks
        m_socket.RemoveEventHandler( CNiDataSocket::StatusUpdatedEvent );
        m_socket.RemoveEventHandler( CNiDataSocket::DataUpdatedEvent );
        m_pv_info = 0;
        m_error = 0;
        m_array_idx = -1;
        m_cache_array_info = false;
        m_array_size = 0;
    }
}

/**
 * \brief Processes incomming data on the NI DataSocket connection
 * \param a_data - The received DataSocket data object
 *
 * This method parses incomming process variable data that is in a legacy SNS DAS
 * format. The approach used here was extracted from the ListenerLib code base.
 * The legacy DAS protocol is not very efficient as it tends to use arrays of
 * process variables and transmits the entire array when only a single value in that
 * array has changed.
 */
void
LDAS_PVReaderAgent::socketRead( NI::CNiDataSocketData &a_data )
{
    // Just in case an odd cb comes-in when we're not officially connected
    if ( !m_pv_info )
        return;

    bool send_pkt = false;

    // Note: The code below was changed due to a bug in DataSockets. When a satellite
    // application is restarted, DataSockets sends garbage for the first value update.
    // Because of this, the code below was changed so that bad data will be ignored, -
    // resulting in no updates being streamed out until a variable value changes. This
    // is a semi-serious flaw and needs to be addressed 

    if ( m_cache_array_info && a_data.HasAttribute("ArraySize"))
    {
        m_array_size = a_data.GetAttribute("ArraySize").Value.ulVal;
        m_cache_array_info = false;
    }

    // The following code will update the "current" alarm and value fields on
    // the global PVInfo instance associated with the given PV. These values
    // are needed in some cases to fill-in writer-protocol-specific fields that
    // are not available from a PVStreamPacket.

    if ( m_array_idx > -1 )
    {
        // Data is from an array - only process update if array size attribute has been received
        if ( !m_cache_array_info )
        {
            switch ( m_pv_info->m_type )
            {
            case PV_ENUM:
            case PV_INT:
                {
                    CNiInt32Vector values(a_data);
                    send_pkt = testAndSet<long>( m_pv_info->m_ival, values[m_array_idx] );
                }
                break;

            case PV_UINT:
                {
                    CNiUInt32Vector values(a_data);
                    send_pkt = testAndSet<unsigned long>( m_pv_info->m_uval, values[m_array_idx] );
                }
                break;

            case PV_DOUBLE:
                {
                    CNiReal64Vector values(a_data);
                    send_pkt = testAndSet<double>( m_pv_info->m_dval, values[m_array_idx] );
                }
                break;
            }
        }
    }
    else
    {
        // a_data.Value attribute is a CNiVariant type - must be cast appropriately
        switch ( m_pv_info->m_type )
        {
        case PV_ENUM:
        case PV_INT:
            send_pkt = testAndSet<long>( m_pv_info->m_ival, (long)a_data.Value );
            break;

        case PV_UINT:
            send_pkt = testAndSet<unsigned long>( m_pv_info->m_uval, (long)a_data.Value );
            break;

        case PV_DOUBLE:
            send_pkt = testAndSet<double>( m_pv_info->m_dval, (double)a_data.Value );
            break;
        }
    }

    if ( send_pkt || m_first_send )
    {
        PVStreamPacket *pkt = m_reader_services.getFreePacket();
        if ( pkt )
        {
            pkt->time.sec = 0;
            pkt->time.nsec = 0;

            // Note: due to time synchronization issues with DataSockets based apps,
            // this code will ignore the timestamp on the incoming packet and simply use
            // local receive time.

            // Is it an error if no timestamp is present?
	        //if ( a_data.HasAttribute("SocketTimeStamp"))
            //    pkt->time.sec = a_data.GetAttribute("SocketTimeStamp").Value.lVal;
            //else
            //{
                // This must be the initial update from the Data Socket Server after we connected
                // Use current time for timestamp
                pkt->time.sec = (unsigned long)time(0);
            //}

            pkt->pkt_type = VarUpdate;
            pkt->device_id = m_pv_info->m_device_id;
            pkt->pv_info = m_pv_info;
            pkt->alarms = m_pv_info->m_alarms;

            switch ( m_pv_info->m_type )
            {
            case PV_ENUM:
            case PV_INT:
                pkt->ival = m_pv_info->m_ival;
                //m_val_history.push_back(m_pv_info->m_ival);
                break;

            case PV_UINT:
                pkt->uval = m_pv_info->m_uval;
                //m_val_history.push_back(m_pv_info->m_uval);
                break;

            case PV_DOUBLE:
                pkt->dval = m_pv_info->m_dval;
                //m_val_history.push_back(m_pv_info->m_dval);
                break;
            }

            m_reader_services.putFilledPacket(pkt);

            // reset initial send
            m_first_send = false;
        }
    }
}

void
LDAS_PVReaderAgent::resendLastValue()
{
    PVStreamPacket *pkt = m_reader_services.getFreePacket();
    if ( pkt )
    {
        pkt->time.sec = (unsigned long)time(0);
        pkt->time.nsec = 0;
        pkt->pkt_type = VarUpdate;
        pkt->device_id = m_pv_info->m_device_id;
        pkt->pv_info = m_pv_info;
        // Update Comm alarm (limits will be same since value hasn't changed)
        if ( m_error )
            m_pv_info->m_alarms |= PV_COMM_ALARM;
        else
            m_pv_info->m_alarms &= ~PV_COMM_ALARM;

        pkt->alarms = m_pv_info->m_alarms;

        m_reader_services.putFilledPacket(pkt);
    }
}

/**
 * \brief This is a generic method that tests PVs for changes and updates the PVInfo object if it has.
 * \param a_val - The current PV value (as a ref)
 * \param a_new_val - The new PV value
 */
template<class T>
bool
LDAS_PVReaderAgent::testAndSet(  T &a_val, T a_new_val )
{
    // If value is different or error state has changed, send update
    if ( a_new_val != a_val || (((m_pv_info->m_alarms & PV_COMM_ALARM) != 0 ) != ( m_error != 0 )))
    {
        a_val = a_new_val;

        if ( m_error )
            m_pv_info->m_alarms = PV_COMM_ALARM;
        else
            m_pv_info->m_alarms = PV_NO_ALARM;

        if ( m_pv_info->m_hw_limits.m_active )
        {
            if ( a_new_val > m_pv_info->m_hw_limits.m_max )
                m_pv_info->m_alarms |= PV_HW_LIMIT_HI;
            else if ( a_new_val < m_pv_info->m_hw_limits.m_min )
                m_pv_info->m_alarms |= PV_HW_LIMIT_LO;
        }

        if ( m_pv_info->m_hw_alarms.m_active )
        {
            if ( a_new_val > m_pv_info->m_hw_alarms.m_max )
                m_pv_info->m_alarms |= PV_HW_ALARM_HI;
            else if ( a_new_val < m_pv_info->m_hw_alarms.m_min )
                m_pv_info->m_alarms |= PV_HW_ALARM_LO;
        }

        // TODO SW limits & alarms don't seem to be used (not loaded from config files)

        return true;
    }

    return false;
}

/**
 * \brief Processes incomming status on the NI DataSocket connection
 * \param a_status - Status code
 * \param a_error - Error code
 * \param a_message - Not used
 *
 * There is insufficient documentation from NI concerning the exact meaning and
 * use of the error and message paramateres of this callback. The error code is
 * passed up to the reader agent manager where is is simply logged.
 */
void
LDAS_PVReaderAgent::socketStatus( long a_status, long a_error, const CString& a_message )
{
    // Just in case an odd cb comes-in when we're not officially connected
    if ( !m_pv_info )
        return;

    if ( a_status == CNiDataSocket::ConnectionActive )
    {
        m_pv_info->m_active = true;
        m_mgr.socketConnected( *this );
    }
    else if ( a_status == CNiDataSocket::Unconnected )
    {
        // This is an expected event triggered from a call to disconnect() (which is properly guarded)
        m_pv_info->m_active = false;
        m_mgr.socketDisconnected( *this );
    }
    else if ( a_status == CNiDataSocket::ConnectionError )
    {
        // Unexpectd event - failed to connect to specified PV URL
        LOG_WARNING( "Connect failed for PV: " << m_pv_info->m_name );

        m_pv_info->m_active = false;
        m_mgr.socketDisconnected( *this );
    }
    else if ( a_error != 0 && m_error != a_error )
    {
        // Unexpected event - set alarm state for PV and re-emit last known value
        LOG_WARNING( "Error code (" << a_error << ") set for PV: " << m_pv_info->m_name );
        m_error = a_error;
        resendLastValue();
    }
    else if ( a_error == 0 && m_error != 0 )
    {
        // Error code has cleared
        LOG_WARNING( "Error code cleared for PV: " << m_pv_info->m_name );
        m_error = a_error;
        resendLastValue();
    }
}

}}}

