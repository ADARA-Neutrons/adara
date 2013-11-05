#include "IOutputAdapter.h"
#include "StreamService.h"

namespace PVS {


/** \brief Constructor for IOutputAdapter class.
  * \param a_stream_serv - StreamService instance that will own/manage this adapter
  */
IOutputAdapter::IOutputAdapter()
    : m_stream_serv(0), m_srteam_api(0)
{}


/** \brief Destructor for IOutputAdapter class.
  */
IOutputAdapter::~IOutputAdapter()
{}


}

