#include "IOutputAdapter.h"
#include "StreamService.h"

namespace PVS {


/** \brief Constructor for IOutputAdapter class.
  * \param a_stream_serv - StreamService instance that will own/manage this adapter
  */
IOutputAdapter::IOutputAdapter( StreamService &a_stream_serv )
    : m_stream_serv(a_stream_serv), m_srteam_api(0)
{
    m_srteam_api = m_stream_serv.attach(*this);
}


/** \brief Destructor for IOutputAdapter class.
  */
IOutputAdapter::~IOutputAdapter()
{
}


}

