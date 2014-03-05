#include <stdexcept>
#include "StreamService.h"

using namespace std;

namespace PVS {

StreamService::StreamService( size_t a_pkt_buffer_size, uint32_t a_offset )
    : m_cfg_mgr(a_offset), m_out_adapter(0)
{
    // Make sure buffer sizes are sane
    if ( a_pkt_buffer_size < 2 )
        throw runtime_error( "Invalid buffer size parameter" );

    // Create stream packets and fill free queue;
    for ( size_t i = 0; i < a_pkt_buffer_size; ++i )
    {
        m_stream_pkts.push_back(new StreamPacket());
        m_free_que.put( m_stream_pkts.back() );
    }

    m_cfg_mgr.attach( this );
}


StreamService::~StreamService()
{
    // Deactivate all queues - will force waiting threads to wake and exit
    m_free_que.deactivate();
    m_fill_que.deactivate();

    for (vector<IInputAdapter*>::iterator i = m_in_adapters.begin(); i != m_in_adapters.end(); ++i )
        delete *i;

    if ( m_out_adapter )
        delete m_out_adapter;

    // Delete stream packets
    for ( vector<StreamPacket*>::iterator ip = m_stream_pkts.begin(); ip != m_stream_pkts.end(); ++ip )
        delete *ip;
}


IInputAdapterAPI*
StreamService::attach( IInputAdapter *a_adapter )
{
    if ( find( m_in_adapters.begin(), m_in_adapters.end(), a_adapter ) == m_in_adapters.end())
    {
        m_in_adapters.push_back( a_adapter );
    }
    return this;
}


IOutputAdapterAPI*
StreamService::attach( IOutputAdapter *a_adapter )
{
    if ( m_out_adapter )
        throw runtime_error( "Can not change output adapter once set." );

    m_out_adapter = a_adapter;

    return this;
}


// ---------- IStreamProducer methods ---------------------------------------


/**
 * \brief Gets a stream packet from the free queue (blocks until available)
 * \return PVStreamPacket pointer on success; null on failure
 */
StreamPacket*
StreamService::getFreePacket()
{
    StreamPacket* pkt = 0;

    m_free_que.get( pkt );

    return pkt;
}


/**
 * \brief Gets a filled stream packet (blocks until available)
 * \param a_timeout - Timeout period in msec
 * \param a_timeout_flag - (output) Indicates if a timeout occurred
 * \return PVStreamPacket pointer on success; null on failure
 */
StreamPacket*
StreamService::getFreePacket( unsigned long a_timeout, bool & a_timeout_flag )
{
    StreamPacket* pkt = 0;

    m_free_que.getTimed( pkt, a_timeout, a_timeout_flag );

    return pkt;
}


/**
 * \brief Puts a stream packet on the filled queue
 * \param a_pkt - PVStreamPacket object to put on queue
 */
void
StreamService::putFilledPacket( StreamPacket *a_pkt )
{
    m_fill_que.put(a_pkt);
}


// ---------- IStreamConsumer methods ---------------------------------------


/**
 * \brief Gets a filled stream packet (blocks until available)
 * \return PVStreamPacket pointer on success; null on failure
 */
StreamPacket*
StreamService::getFilledPacket()
{
    StreamPacket* pkt = 0;

    m_fill_que.get( pkt );

    return pkt;
}

/**
 * \brief Gets a filled stream packet (blocks until available)
 * \param a_timeout - Timeout period in msec
 * \param a_timeout_flag - (output) Indicates if a timeout occurred
 * \return PVStreamPacket pointer on success; null on failure
 */
StreamPacket*
StreamService::getFilledPacket( unsigned long a_timeout, bool & a_timeout_flag )
{
    StreamPacket* pkt = 0;

    m_fill_que.getTimed( pkt, a_timeout, a_timeout_flag );

    return pkt;
}


/** \brief Returns a processed packet to the packet pool
  * \param a_pkt - StreamPacket object to return
  *
  * This method returns a processed packet to the StreamService queues for
  * either re-use, or for notification to stream listeners. If stream listeners
  * are attached, and the notification queue is not full, the packet is placed
  * in the notification queue where it will be forwarded to stream listeners
  * by the notification thread. If the notify queue is full, the packet is
  * returned directly to the free queue.
  */
void
StreamService::putFreePacket( StreamPacket *a_pkt )
{

    // If the notify buffer is backed-up, bypass it. This will cause stream
    // listeners to miss packets under heavy load, but it will maintain the
    // output stream integrity.

    //if ( m_notify_que.size() < m_max_notify_pkts )
    //    m_notify_que.put(a_pkt);
    //else

    // Ensure that shared ptr to Device is released before returning to free queue
    a_pkt->device.reset();
    a_pkt->pv = 0;
    m_free_que.put(a_pkt);
}


// ---------- Private StreamService methods -----------------------------------


/** \brief Stream listener notification thread function.
  */
/*
void
PVStreamer::streamNotifyThread()
{
    StreamPacket* pkt;

    while(1)
    {
        if ( !m_notify_que.get( pkt ))
            break;

        notifyStreamListeners(pkt);

        // Ensure that shared ptr to Device is released before returning to free queue
        pkt->device.reset();
        pkt->pv = 0;
        m_free_que.put(pkt);
    }
}
*/


}
