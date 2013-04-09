/**
 * \file LDAS_PVReaderAgent.h
 * \brief Header file for LDAS_PVReaderAgent class.
 * \author Dale V. Stansberry
 * \date June 6, 2012
 */

#ifndef LDAS_PVREADERAGENT
#define LDAS_PVREADERAGENT

#include <boost/thread/mutex.hpp>

#include "LegacyDAS.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"
#include "LDAS_PVReader.h"

namespace SNS { namespace PVS { namespace LDAS {

/**
 * \class LDAS_PVReaderAgent
 * \brief Provides per-process-variable input processing.
 *
 * The LDAS_PVReaderAgent class is responsible for reading a single process
 * variable from an associated NI DataSocket. This class reads process variable
 * values and status, and also monitors the DataSocket connection for errors
 * and/or unexpected disconnections. This information is sent to the owning
 * LDAS_IPVReaderAgentMgr instance for further processing and distribution.
 * LDAS_PVReaderAgent instances are recycled in an object pool by the manager
 * to reduce memory allocation and initialization time. The DataSocket interface
 * used for PV values and status uses a legacy DAS format extracted from the SNS
 * DAS ListenerLib code base.
 */
class LDAS_PVReaderAgent
{
public:
    LDAS_PVReaderAgent( LDAS_IPVReaderAgentMgr &a_mgr, IPVReaderServices &a_reader_services );
    ~LDAS_PVReaderAgent();

    void                    connect( PVInfo &a_pv_info );
    void                    disconnect();
    PVInfo *                getPV() const { return m_pv_info; }

private:
    template<class T> bool  testAndSet( T &a_val, T a_new_val );
    void                    socketRead( NI::CNiDataSocketData &data );
    void                    socketStatus( long status, long error, const CString& message );
    void                    resendLastValue();

    LDAS_IPVReaderAgentMgr &m_mgr;                  ///< Owning reader agent manager instance
    IPVReaderServices      &m_reader_services;      ///< PVStreamer reader services interface
    PVInfo                 *m_pv_info;              ///< Associated process variable info struct
    int                     m_error;                ///< Current DataSockets error code
    long                    m_array_idx;            ///< PV index for array types
    bool                    m_first_send;           ///< Flag to force pkt send for first pkt regardless of value diff
    bool                    m_cache_array_info;     ///< Flag to initiate caching of array attributes
    unsigned long           m_array_size;           ///< Array size fo array types
    NI::CNiDataSocket       m_socket;               ///< DataSocket use to read PV values & status
    boost::mutex            m_mutex;                ///< Mutex to protect public interface
};

}}}

#endif
