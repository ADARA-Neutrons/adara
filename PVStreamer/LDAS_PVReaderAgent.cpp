// LDAS_SocketReader.cpp

#include "stdafx.h"

#include <boost/thread/locks.hpp>

#include "LDAS_PVReaderAgent.h"

using namespace std;
using namespace NI;

//#define EPSILON 1e-18

namespace SNS { namespace PVS { namespace LDAS {

LDAS_PVReaderAgent::LDAS_PVReaderAgent( LDAS_IPVReaderAgentMgr &a_mgr, IPVReaderServices *a_reader_services, PVInfo *a_pv_info )
:m_mgr(a_mgr), m_reader_services(a_reader_services), m_pv_info(0), m_array_idx(-1), m_cache_array_info(false), m_array_size(0) //, m_array_using_time(false)

{
    if ( a_pv_info )
        connect( *a_pv_info);
}

LDAS_PVReaderAgent::~LDAS_PVReaderAgent()
{
    boost::lock_guard<boost::mutex> lock(m_sock_mutex);
    if ( m_pv_info )
    {
        // Do NOT call disconnect as it triggers synchronous callbacks
        m_socket.RemoveEventHandler( CNiDataSocket::StatusUpdatedEvent );
        m_socket.RemoveEventHandler( CNiDataSocket::DataUpdatedEvent );
        m_pv_info = 0;
    }
}

void
LDAS_PVReaderAgent::connect( PVInfo &a_pv_info )
{
    boost::lock_guard<boost::mutex> lock(m_sock_mutex);

    if ( m_pv_info )
        throw -1;

    m_pv_info = &a_pv_info;

    // If DataSocket connection URI ends with [xxx], then this is an array-based PV
    // and we need to extract the array index
    size_t i = m_pv_info->m_connection.find_last_of("[");
    if ( i != string::npos )
    {
      size_t j = m_pv_info->m_connection.find_last_of("]");
      if ( j != string::npos && i < j )
      {
          string idx_str = m_pv_info->m_connection.substr( i + 1, j-i-1 );
          // TODO Should add error checking here (why isn't this already in the lang???)
          m_array_idx = atol( idx_str.c_str());
          m_cache_array_info = true;
      }
    }

    m_socket.InstallEventHandler( *this, &LDAS_PVReaderAgent::socketRead );
    m_socket.InstallEventHandler( *this, &LDAS_PVReaderAgent::socketStatus );
    m_socket.Connect( a_pv_info.m_connection.c_str(), CNiDataSocket::ReadAutoUpdate ); // will trigger callbacks
}

void
LDAS_PVReaderAgent::disconnect()
{
    boost::lock_guard<boost::mutex> lock(m_sock_mutex);

    if ( m_pv_info )
    {
        m_socket.Disconnect(); // Will trigger callbacks
        m_socket.RemoveEventHandler( CNiDataSocket::StatusUpdatedEvent );
        m_socket.RemoveEventHandler( CNiDataSocket::DataUpdatedEvent );
        m_pv_info = 0;
        m_array_idx = -1;
        m_cache_array_info = false;
        m_array_size = 0;
        //m_array_using_time = false;
    }
}

void
LDAS_PVReaderAgent::socketRead( NI::CNiDataSocketData &a_data )
{
    // Just in case an odd cb comes-in when we're not officially connected
    if ( !m_pv_info )
        return;

    bool send_pkt = false;

    if ( m_cache_array_info )
    {
        // TODO Handle errors (missing attributes)
        if ( a_data.HasAttribute("ArraySize"))
            m_array_size = a_data.GetAttribute("ArraySize").Value.ulVal;

        // Don't care about timestamps on individual array elements - just compare the value
        // and if it's different from te last, send it

        //if ( a_data.HasAttribute("UsingTimes"))
        //    m_array_using_time = a_data.GetAttribute("UsingTimes").Value.lVal == 1;

        m_cache_array_info = false;
    }

    // The following code will update the "current" alarm and value fields on
    // the global PVInfo instance associated with the given PV. These values
    // are needed in some cases to fill-in writer-protocol-specific fields that
    // are not available from a PVStreamPacket.

    if ( m_array_idx > -1 )
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
    else
    {
        // a_data.Value attribute is a CNiVariant type - must be cast appropriately
        switch ( m_pv_info->m_type )
        {
        case PV_ENUM:
        case PV_INT:
            send_pkt = testAndSet<long>( m_pv_info->m_ival, a_data.Value.lVal );
            break;

        case PV_UINT:
            send_pkt = testAndSet<unsigned long>( m_pv_info->m_uval, a_data.Value.ulVal );
            break;

        case PV_DOUBLE:
            send_pkt = testAndSet<double>( m_pv_info->m_dval, a_data.Value.dblVal );
            break;
        }
    }

    if ( send_pkt )
    {
        PVStreamPacket *pkt = m_reader_services->getFreePacket();

        pkt->time.sec = 0;
        pkt->time.nsec = 0;

        // Is it an error if no timestamp is present?
	    if ( a_data.HasAttribute("SocketTimeStamp"))
            pkt->time.sec = a_data.GetAttribute("SocketTimeStamp").Value.lVal;
        else
        {
            // This must be the initial update from the Data Socket Server after we connected
            // Use current time for timestamp
            pkt->time.sec = (unsigned long)time(0);
        }

        pkt->pkt_type = VarValueUpdate;
        pkt->device_id = m_pv_info->m_device_id;
        pkt->pv_info = m_pv_info;
        pkt->alarms = m_pv_info->m_alarms;

        switch ( m_pv_info->m_type )
        {
        case PV_ENUM:
        case PV_INT:
            pkt->ival = m_pv_info->m_ival;
            break;

        case PV_UINT:
            pkt->uval = m_pv_info->m_uval;
            break;

        case PV_DOUBLE:
            pkt->dval = m_pv_info->m_dval;
            break;
        }

        m_reader_services->putFilledPacket(pkt);
    }
}



template<class T>
bool
LDAS_PVReaderAgent::testAndSet(  T &a_val, T a_new_val )
{
    bool differs;

    if ( typeid(a_new_val) == typeid(double))
        differs = (fabs((double)(a_new_val - a_val)) > DBL_EPSILON );
    else
        differs = (a_new_val != a_val);

    if (differs)
    {
        //m_pv_info->m_dval = a_new_val;
        a_val = a_new_val;

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

void
LDAS_PVReaderAgent::socketStatus( long status, long error, const CString& message )
{
    // Just in case an odd cb comes-in when we're not officially connected
    if ( !m_pv_info )
        return;

    if ( status == CNiDataSocket::ConnectionActive )
    {
        m_pv_info->m_active = true;
        m_mgr.socketConnected( *this );
    }
    else if ( status == CNiDataSocket::Unconnected )
    {
        m_pv_info->m_active = false;
        m_mgr.socketDisconnected( *this );
    }
    else if ( status == CNiDataSocket::ConnectionError )
    {
        m_mgr.socketConnectionError( *this, error );
    }
    else if ( error != 0 )
    {
        m_mgr.socketError( *this, error );
    }
}

}}}

