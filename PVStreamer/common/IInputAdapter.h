#ifndef IINPUTADAPTER_H
#define IINPUTADAPTER_H

#include "CoreDefs.h"

namespace PVS {

class StreamService;
class IInputAdapterAPI;


/** The IInputAdapter class is the base class for all protocol-specific pv input
  * adapters. IInputAdapter are protocol-specific and there can be mupltiple
  * instances of IInputAdapter supporting multiple protocols. The IInputAdapter
  * base class provides basic glue logic to automatically bind an adapter
  * subclass to the owning StreamService instance.
  */
class IInputAdapter
{
public:
    IInputAdapter( StreamService &a_stream_serv );
    virtual ~IInputAdapter();

protected:
    StreamService      &m_stream_serv;  ///< StreamService instance that owns this adapter
    IInputAdapterAPI   *m_stream_api;   ///< Stream services interface acquired from owning StreamService
};

}

#endif // IINPUTADAPTER_H

// vim: expandtab

