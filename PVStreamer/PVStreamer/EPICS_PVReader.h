/**
 * \file EPICS_PVReader.h
 * \brief Header file for EPICS_PVReader class.
 * \author Dale V. Stansberry
 * \date Sep 27, 2013
 */

#ifndef EPICS_PVREADER_H
#define EPICS_PVREADER_H


#include <map>
#include <vector>
#include <list>

#include "LegacyDAS.h"
#include "PVReader.h"


namespace SNS { namespace PVS { namespace EPICS {

        // Notify config service that this source is now loaded
//        m_cfg_service.configurationLoaded( LDAS_PROTOCOL, m_hostname );


/**
 * \class EPICS_PVReader
 * \brief Provides input processing of EPICS process variables.
 *
 * The EPICS_PVReader class reads, translates, and transmits EPICS process
 * variable and device data into an PVStreamer internal data stream for eventual
 * processing by other PVStreamer components (output protocol adapters).
 */
class EPICS_PVReader : public IPVConfigListener, public PVReader, protected LDAS_IPVReaderAgentMgr, protected LDAS_IDevMonitorMgr
{
public:
    EPICS_PVReader( PVStreamer &a_streamer );
    ~EPICS_PVReader();

private:

};

}}}


#endif // EPICS_PVREADER_H
