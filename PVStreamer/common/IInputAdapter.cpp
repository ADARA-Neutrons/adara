#include "IInputAdapter.h"
#include "StreamService.h"

namespace PVS {

/** \brief Constructor for IInputAdapter class.
  * \param a_stream_serv - StreamService instance that will own/manage this adapter
  */
IInputAdapter::IInputAdapter( StreamService &a_stream_serv )
    : m_srteam_api(0)
{
    m_srteam_api = a_stream_serv.attach(*this);
}


/** \brief Destructor for IInputAdapter class.
  */
IInputAdapter::~IInputAdapter()
{
}


}
