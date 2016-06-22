#include "IInputAdapter.h"
#include "StreamService.h"

namespace PVS {

/** \brief Constructor for IInputAdapter class.
  * \param a_stream_serv - StreamService instance that will own/manage this adapter
  */
IInputAdapter::IInputAdapter( StreamService &a_stream_serv )
    : m_stream_serv(a_stream_serv), m_stream_api(0)
{
    m_stream_api = m_stream_serv.attach( *this );
}

/** \brief Destructor for IInputAdapter class.
  */
IInputAdapter::~IInputAdapter()
{
    m_stream_serv.detach( *this );
}

}

// vim: expandtab

