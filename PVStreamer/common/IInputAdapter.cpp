#include "IInputAdapter.h"
#include "StreamService.h"

namespace PVS {

/** \brief Constructor for IInputAdapter class.
  * \param a_stream_serv - StreamService instance that will own/manage this adapter
  */
IInputAdapter::IInputAdapter()
    : m_srteam_api(0)
{}


/** \brief Destructor for IInputAdapter class.
  */
IInputAdapter::~IInputAdapter()
{}


}
