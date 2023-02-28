/**
 * \file EPICS_PVConfigMgr.h
 * \brief Header file for EPICS_PVConfigMgr class.
 * \author Dale V. Stansberry
 * \date Sep 27, 2013
 */

#ifndef EPICS_PVCONFIGMGR_H
#define EPICS_PVCONFIGMGR_H

#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#include "EPICSDefs.h"
#include "PVConfig.h"

/*
EPICS input is much simpler than LDAS

EPICS_PVConfigMgr just needs to parse one file then emit a config loaded to PVStreamer.
EPICS_PVReader gets notification, scans config for all EPICS vars, then makes connections
All EPICS input will be through one set of callback on reader.

EPICS_PVConfigMgr will start a thread to monitor config file updates and reload as required.
EPICS_PVReader will need to determine when PVs go offline when config notices come in.
*/

namespace SNS { namespace PVS { namespace EPICS {

        // Create an EPICS DAS PV reader
//        EPICS_PVReader*     epics_reader = new EPICS_PVReader(*pvs);

/**
 * \class EPICS_PVConfigMgr
 * \brief Provides management of overall EPICS DAS configuration.
 *
 * The EPICS_PVConfigMgr class provides the entry-point for configuring PVStreamer
 * from the EPICS DAS configuration file.
 */
class EPICS_PVConfigMgr : public PVConfig
{
public:
    EPICS_PVConfigMgr( PVStreamer & a_streamer, const std::string &a_file );
    ~EPICS_PVConfigMgr();

private:
    void            readConfigFile();
    void            fileMonThread();

    const std::string               m_config_file;
    boost::thread                  *m_filemon_thread;
    boost::mutex                    m_mutex;
};

}}}


#endif // EPICS_PVCONFIGMGR_H
