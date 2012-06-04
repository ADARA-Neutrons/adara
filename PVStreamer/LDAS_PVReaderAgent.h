#ifndef LDAS_PVREADERAGENT
#define LDAS_PVREADERAGENT

#include <boost/thread/mutex.hpp>

#include "PVStreamerSupport.h"
#include "NiCommonComponent.h"
#include "NiDataSocketComponent.h"
#include "LDAS_PVReader.h"

namespace SNS { namespace PVS { namespace LDAS {

class LDAS_PVReaderAgent
{
public:
    LDAS_PVReaderAgent( LDAS_IPVReaderAgentMgr &a_mgr, IPVReaderServices *a_reader_services, PVInfo *a_pv_info );
    ~LDAS_PVReaderAgent();

    void    connect( PVInfo &a_pv_info );
    void    disconnect();
    PVInfo *getPV() const { return m_pv_info; }

private:
    template<class T> bool testAndSet( T &a_val, T a_new_val );

    void    socketRead( NI::CNiDataSocketData &data );
    void    socketStatus( long status, long error, const CString& message );

    LDAS_IPVReaderAgentMgr &m_mgr;
    IPVReaderServices      *m_reader_services;
    PVInfo                 *m_pv_info;
    long                    m_array_idx;
    bool                    m_cache_array_info;
    unsigned long           m_array_size;
    //bool                    m_array_using_time;
    NI::CNiDataSocket       m_socket;
    boost::mutex            m_sock_mutex;
};

}}}

#endif
