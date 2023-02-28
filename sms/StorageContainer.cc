
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.StorageContainer"));

#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <boost/filesystem.hpp>

#include "StorageContainer.h"
#include "StorageManager.h"
#include "StorageFile.h"
#include "SMSControl.h"
#include "ADARA.h"
#include "ADARAUtils.h"

namespace fs = boost::filesystem;

RateLimitedLogging::History RLLHistory_StorageContainer;

// Rate-Limited Logging IDs...
#define RLL_PAUSEMODE_SAWTOOTH        0

#define CONTAINER_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP)
#define MARKER_MODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)

const char *StorageContainer::m_completed_marker = "translation_completed";
const char *StorageContainer::m_manual_marker = "manual_processing_needed";

const char *StorageContainer::m_proposal_id_marker_prefix = "proposal-";

// Default "New Start" Time, for "Don't Care" Scenarios...
// (Set to 0 Seconds, 1 Nanoseconds, so --Nanoseconds leaves it at 0.0! :-)
struct timespec StorageContainer::m_default_start_time = { 0, 1 };

void StorageContainer::terminateFile(
		std::list<struct PauseMode>::iterator &it,
		bool do_terminate,
		const struct timespec &newStart ) // EPICS Time...!
{
	SMSControl *ctrl = SMSControl::getInstance();

	if ( !(it->m_file) )
	{
		std::stringstream ss;
		ss << "terminateFile(): No File to Terminate!"
			<< " newStart=" << newStart.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< newStart.tv_nsec << std::setw(0)
			<< " do_terminate=" << do_terminate
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements";
		throw std::logic_error( ss.str() );
	}

	DEBUG("terminateFile(): Terminating File "
		<< it->m_file->path()
		<< " newStart=" << newStart.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< newStart.tv_nsec << std::setw(0)
		<< " do_terminate=" << do_terminate
		<< " in PauseMode " << it->m_numModes
		<< " [" << it->m_minTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_minTime.tv_nsec << std::setw(0)
		<< ", " << it->m_maxTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
		<< " m_paused=" << it->m_paused
		<< " m_numModes=" << it->m_numModes
		<< " m_numFiles=" << it->m_numFiles
		<< " m_numPauseFiles=" << it->m_numPauseFiles
		<< " m_pendingFiles.size()="
			<< it->m_pendingFiles.size()
		<< " m_lastPrologueFile="
		<< ( ( it->m_lastPrologueFile ) ?
			it->m_lastPrologueFile->path() : "(null)" )
		<< " - Btw, the PauseMode Stack now has "
		<< m_pauseModeStack.size() << " elements");

	// Set Max Time for Current PauseMode to 1 Nanosecond
	// _Before_ TimeStamp of Next PauseMode Start Time...
	// (So We Maintain Distinct Time Ranges Per PauseMode...)
	struct timespec maxTime = newStart; // EPICS Time...!
	if ( maxTime.tv_nsec > 0 )
		maxTime.tv_nsec--;
	else {
		maxTime.tv_nsec = NANO_PER_SECOND_LL - 1;
		maxTime.tv_sec--;
	}
	it->m_maxTime = maxTime;

	if ( ctrl->verbose() > 0 ) {
		DEBUG("terminateFile():"
			<< " Setting Current PauseMode " << it->m_numModes
			<< " maxTime=" << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0));
	}

	// Do We Terminate This PauseMode Now, or Keep It Alive on the Stack?
	if ( do_terminate )
	{
		ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;
		if ( m_runNumber )
		{
			status = m_active ? ADARA::RunStatus::RUN_EOF :
				    	ADARA::RunStatus::END_RUN;
		}

		it->m_file->terminate( status );
		StorageManager::addBaseStorage( it->m_file->size() );
		it->m_file.reset();
	}

	// Keep Current PauseMode Alive on the Stack...
	else
	{
		// Get "Last" Prologue File (_Not_ Any SavePrologue Files!)
		// Before Pushing New PauseMode Onto Stack...
		getLastPrologueFiles( it, false );

		// Push New "Empty" Current PauseMode onto Stack...

		struct PauseMode pauseMode;

		pauseMode.m_minTime.tv_sec = 0;
		pauseMode.m_minTime.tv_nsec = 0;
		pauseMode.m_maxTime.tv_sec = 0;
		pauseMode.m_maxTime.tv_nsec = 0;

		// Carry Over Current Pause State and File/Pause Numbers
		pauseMode.m_numModes = it->m_numModes;
		pauseMode.m_numFiles = it->m_numFiles;
		pauseMode.m_numPauseFiles = it->m_numPauseFiles;
		pauseMode.m_paused = it->m_paused;

		m_pauseModeStack.push_front( pauseMode );

		it = m_pauseModeStack.begin();

		// Reset "Last Time Stamp" for getPauseModeByTime()...!
		// ("Just in Case" Changing PauseMode Stack Here
		// Invalidates Saved Iterator)
		m_last_ts.tv_sec = -1;
		m_last_ts.tv_nsec = -1;

		DEBUG("terminateFile():"
			<< " Pushed New Empty Current PauseMode onto Stack"
			<< " do_terminate=" << do_terminate
			<< ", Inherited m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}
}

void StorageContainer::newFile(
		std::list<struct PauseMode>::iterator &it,
		bool paused, const struct timespec &minTime ) // EPICS Time...!
{
	SMSControl *ctrl = SMSControl::getInstance();

	if ( it->m_file )
	{
		ERROR("newFile(): StorageFile Already Exists "
			<< it->m_file->path()
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< ", Requested minTime=" << minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< minTime.tv_nsec << std::setw(0)
			<< " paused=" << paused
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
		return;
	}

	if ( ctrl->verbose() > 0 ) {
		DEBUG("newFile(): Create New StorageFile"
			<< " minTime=" << minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< minTime.tv_nsec << std::setw(0)
			<< " paused=" << paused
			<< " from m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}

	it->m_paused = paused;

	it->m_minTime.tv_sec = minTime.tv_sec;
	it->m_minTime.tv_nsec = minTime.tv_nsec;

	ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;

	if ( m_runNumber ) {
		status = ADARA::RunStatus::NEW_RUN;
		if ( it->m_numModes )
			status = ADARA::RunStatus::RUN_BOF;
	}

	// Bump for First PauseMode Index...
	if ( it->m_numModes == 0 )
		it->m_numModes = 1;

	if ( it->m_paused )
	{
		it->m_file = StorageFile::newFile( m_weakThis, it->m_paused,
				it->m_numModes, it->m_numFiles, ++(it->m_numPauseFiles),
				status );
	}
	else
	{
		it->m_file = StorageFile::newFile( m_weakThis, it->m_paused,
				it->m_numModes, ++(it->m_numFiles),
				/* it->m_numPauseFiles = */ 0, status );
	}

	DEBUG("newFile(): New StorageFile Created "
		<< it->m_file->path()
		<< " in PauseMode " << it->m_numModes
		<< " [" << it->m_minTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_minTime.tv_nsec << std::setw(0)
		<< ", " << it->m_maxTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
		<< " m_paused=" << it->m_paused
		<< " m_numModes=" << it->m_numModes
		<< " m_numFiles=" << it->m_numFiles
		<< " m_numPauseFiles=" << it->m_numPauseFiles
		<< " m_pendingFiles.size()="
			<< it->m_pendingFiles.size()
		<< " m_lastPrologueFile="
		<< ( ( it->m_lastPrologueFile ) ?
			it->m_lastPrologueFile->path() : "(null)" )
		<< " - Btw, the PauseMode Stack now has "
		<< m_pauseModeStack.size() << " elements");

	// Stagger Adding File to Container File List
	// If This Isn't the Oldest PauseMode on the Stack...
	std::list<struct PauseMode>::iterator next = it; next++;
	if ( next == m_pauseModeStack.end() )
	{
		// If We Have Some Previously Deferred Pending Files on Our List,
		// Go Ahead and Push Them Onto the Container File List Now...
		if ( it->m_pendingFiles.size() > 0 )
		{
			std::list<StorageFile::SharedPtr>::iterator fit;
			for ( fit = it->m_pendingFiles.begin() ;
					fit != it->m_pendingFiles.end() ; ++fit )
			{
				DEBUG("newFile():"
					<< " Pushing Previously Pending File "
					<< (*fit)->path()
					<< " Onto Container File List"
					<< " (and Notifying FileAdded)"
					<< " from Oldest PauseMode " << it->m_numModes
					<< " in [" << it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " m_paused=" << it->m_paused
					<< " m_numModes=" << it->m_numModes
					<< " m_numFiles=" << it->m_numFiles
					<< " m_numPauseFiles=" << it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( it->m_lastPrologueFile ) ?
						it->m_lastPrologueFile->path() : "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");

				m_files.push_back( *fit );
				m_newFile( *fit );
			}

			it->m_pendingFiles.clear();
		}

		if ( ctrl->verbose() > 0 ) {
			DEBUG("newFile(): Adding File to Container File List "
				<< it->m_file->path());
		}

		m_files.push_back( it->m_file );

		// Note: FileAdded Notify Signaled for This New File Below... ;-D
	}
	else
	{
		// We're _Not_ at the "Bottom" of the PauseMode Stack,
		// So Push This New File Onto the _End_ of the Pending Files List
		it->m_pendingFiles.push_back( it->m_file );

		DEBUG("newFile(): Defer Adding File from New Stacked PauseMode"
			<< " to Container File List "
			<< it->m_file->path() << ","
			<< " Instead Add New File to Pending Files List"
			<< " (Size Now " << it->m_pendingFiles.size() << ")"
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}

	// Keep Track of the Number of Stream Files,
	// for Long-Non-Running Container Splitting...
	m_totFileCount++;

	// If We are an Old Container/PauseMode with a "Last" Prologue File,
	// Then Just Append That File Directly Now...
	if ( it->m_lastPrologueFile )
	{
		if ( ctrl->verbose() > 0 ) {
			DEBUG("newFile():"
				<< " Directly Appending Last Prologue File "
				<< it->m_lastPrologueFile->path()
				<< " for New File "
				<< it->m_file->path());
		}

		it->m_file->catFile( it->m_lastPrologueFile );
	}

	// Tell the storage manager about the new file so we can
	// add the prologue before anyone else sees it.
	else
	{
		if ( ctrl->verbose() > 0 ) {
			DEBUG("newFile(): Append Normal FileCreated File Prologue to "
				<< it->m_file->path());
		}

		StorageManager::fileCreated( it->m_file,
			false /* capture_last */ );
	}

	/* Notify Any Subscribers that we have a New File to process...
	 * (_Even_ in Paused mode, subscribers can use StorageFile::paused()
	 * to selectively ignore any Paused run files... :-)
	 */
	// Stagger FileAdded Notify for Container
	// If This Isn't the Oldest PauseMode on the Stack...
	if ( next == m_pauseModeStack.end() )
	{
		if ( ctrl->verbose() > 0 ) {
			DEBUG("newFile(): FileAdded Notify for "
				<< it->m_file->path());
		}

		// Note: New File Already Pushed Onto Container File List Above...

		m_newFile( it->m_file );
	}
	else
	{
		// REMOVEME
		DEBUG("newFile(): Defer FileAdded Notify"
			<< " for New Stacked PauseMode "
			<< it->m_file->path()
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}
}

void StorageContainer::getPauseModeByTime(
		std::list<struct PauseMode>::iterator &pm_it,
		bool ignore_pkt_timestamp,
		struct timespec &ts, // EPICS Time...!
		bool check_old_pausemodes )
{
	std::list<struct PauseMode>::iterator found_it =
		m_pauseModeStack.end();

	SMSControl *ctrl = SMSControl::getInstance();

	// *** Quick Cached Time Stamp Lookup Shortcut:
	// If *Everything* is the "Same" for This Time Stamp Lookup,
	// Then We Can Simply Return the Same "Last Iterator"...!
	// (Otherwise, We Need to Proceed Fully Through the Logic Here...)
	if ( ignore_pkt_timestamp == m_last_ignore_pkt_timestamp
			&& check_old_pausemodes == m_last_check_old_pausemodes
			&& ts.tv_sec == m_last_ts.tv_sec
			&& ts.tv_nsec == m_last_ts.tv_nsec )
	{
		bool use_cached_lookup = true;

		// But Wait, Also Need to Check for Case of the
		// "Oldest" Max Data Source Time Having Been Advanced
		// Since Our Last Lookup, "Just in case"... ;-D
		if ( check_old_pausemodes
				&& m_last_found_it != m_pauseModeStack.end() )
		{
			// Get "Oldest" Max DataSource Time,
			// to Use as Furthest "Safe" Time Advancement for Comparison...
			struct timespec old_ts =
				ctrl->oldestMaxDataSourceTime(); // EPICS Time...!

			// Something Changed Underfoot...!
			// Go Through Usual Checking of Old PauseModes...
			if ( old_ts.tv_sec != m_last_old_ts.tv_sec
					|| old_ts.tv_nsec != m_last_old_ts.tv_nsec )
			{
				if ( ctrl->verbose() > 1 )
				{
					DEBUG("getPauseModeByTime():"
						<< " *** CACHE HIT!"
						<< " Yet Oldest Max Data Source Time Changed: "
						<< " old_ts=" << old_ts.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< old_ts.tv_nsec << std::setw(0)
						<< " != Cached"
						<< " m_last_old_ts=" << old_ts.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< m_last_old_ts.tv_nsec << std::setw(0) << ","
						<< " SAME Packet TimeStamp, Re-Use Last PauseMode "
							<< m_last_found_it->m_numModes
						<< " for ts=" << ts.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< ts.tv_nsec << std::setw(0)
						<< " in [" << m_last_found_it->m_minTime.tv_sec
							<< "."
						<< std::setfill('0') << std::setw(9)
						<< m_last_found_it->m_minTime.tv_nsec
							<< std::setw(0)
						<< ", " << m_last_found_it->m_maxTime.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< m_last_found_it->m_maxTime.tv_nsec
							<< std::setw(0) << "]"
						<< " ignore_pkt_timestamp=" << ignore_pkt_timestamp
						<< " check_old_pausemodes=" << check_old_pausemodes
						<< " m_paused=" << m_last_found_it->m_paused
						<< " m_numModes=" << m_last_found_it->m_numModes
						<< " m_numFiles=" << m_last_found_it->m_numFiles
						<< " m_numPauseFiles="
							<< m_last_found_it->m_numPauseFiles
						<< " m_pendingFiles.size()="
							<< m_last_found_it->m_pendingFiles.size()
						<< " m_lastPrologueFile="
						<< ( ( m_last_found_it->m_lastPrologueFile ) ?
							m_last_found_it->m_lastPrologueFile->path()
							: "(null)" )
						<< " - Btw, the PauseMode Stack has "
						<< m_pauseModeStack.size() << " elements");
				}

				// Don't Just Return Cached Lookup,
				// Go Through Usual Logic...
				use_cached_lookup = false;

				// We can still use the Cached PauseMode Iterator...
				// (Save _Some_ of the Lookup Overhead...)
				found_it = m_last_found_it;
			}
		}

		if ( use_cached_lookup )
		{
			if ( ctrl->verbose() > 1 )
			{
				DEBUG("getPauseModeByTime():"
					<< " *** CACHE HIT!"
					<< " SAME Packet TimeStamp, Re-Use Last PauseMode "
						<< m_last_found_it->m_numModes
					<< " for ts=" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0)
					<< " in [" << m_last_found_it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< m_last_found_it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << m_last_found_it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< m_last_found_it->m_maxTime.tv_nsec
						<< std::setw(0) << "]"
					<< " ignore_pkt_timestamp=" << ignore_pkt_timestamp
					<< " check_old_pausemodes=" << check_old_pausemodes
					<< " m_paused=" << m_last_found_it->m_paused
					<< " m_numModes=" << m_last_found_it->m_numModes
					<< " m_numFiles=" << m_last_found_it->m_numFiles
					<< " m_numPauseFiles="
						<< m_last_found_it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< m_last_found_it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( m_last_found_it->m_lastPrologueFile ) ?
						m_last_found_it->m_lastPrologueFile->path()
						: "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");
			}

			pm_it = m_last_found_it;

			return;
		}
	}

	std::list<struct PauseMode>::iterator it =
		m_pauseModeStack.begin();

	// If Ignoring Packet TimeStamp, Just Write Into Current PauseMode
	if ( ignore_pkt_timestamp && found_it == m_pauseModeStack.end() )
	{
		found_it = m_pauseModeStack.begin();
		if ( found_it != m_pauseModeStack.end() )
		{
			if ( ctrl->verbose() > 1 )
			{
				DEBUG("getPauseModeByTime():"
					<< " Ignore Packet TimeStamp,"
					<< " Use Current PauseMode " << it->m_numModes
					<< " for ts=" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0)
					<< " in [" << found_it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< found_it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << found_it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< found_it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " check_old_pausemodes=" << check_old_pausemodes
					<< " m_paused=" << found_it->m_paused
					<< " m_numModes=" << found_it->m_numModes
					<< " m_numFiles=" << found_it->m_numFiles
					<< " m_numPauseFiles=" << found_it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< found_it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( found_it->m_lastPrologueFile ) ?
						found_it->m_lastPrologueFile->path() : "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");
			}

			// Step Past Current PauseMode,
			// No Need to Check Its Expiration Yet...
			it = found_it;
			it++;
		}
	}

	// First, Make Sure We've Found the PauseMode with Time Range Match
	// - Check Back Through PauseModes for a Time Range Match...
	// (Unless We've Already Found the Matching PauseMode...)

	for ( ; found_it == m_pauseModeStack.end()
			&& it != m_pauseModeStack.end(); ++it )
	{
		// This PauseMode Encapsulates This EPICS TimeStamp...
		if ( compareTimeStamps( ts, it->m_minTime ) >= 0
			&& ( ( it->m_maxTime.tv_sec == 0
					&& it->m_maxTime.tv_nsec == 0 )
				|| compareTimeStamps( it->m_maxTime, ts ) >= 0 ) )
		{
			if ( ctrl->verbose() > 1 )
			{
				DEBUG("getPauseModeByTime():"
					<< " Found PauseMode " << it->m_numModes
					<< " for ts=" << ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< ts.tv_nsec << std::setw(0)
					<< " in [" << it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " check_old_pausemodes=" << check_old_pausemodes
					<< " m_paused=" << it->m_paused
					<< " m_numModes=" << it->m_numModes
					<< " m_numFiles=" << it->m_numFiles
					<< " m_numPauseFiles=" << it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( it->m_lastPrologueFile ) ?
						it->m_lastPrologueFile->path() : "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");
			}

			// Found It! :-D
			found_it = it;
		}
	}

	// Now We've (Hopefully) Found the Matching PauseMode;
	// If So, Then Check for Any "Older" PauseModes that have
	// Now Reached the Cleanup Timeout Threshold and Expired...

	if ( check_old_pausemodes && found_it != m_pauseModeStack.end() )
	{
		// Use Same Container Timeout for PauseModes Too...
		struct timespec container_cleanup_timeout =
			StorageManager::getContainerCleanupTimeout();

		// Get "Oldest" Max DataSource Time,
		// to Use as Furthest "Safe" Time Advancement for Comparison...
		struct timespec old_ts =
			ctrl->oldestMaxDataSourceTime(); // EPICS Time...!

		// Start at "End" of the PauseMode List, and Work Backwards,
		// Up to Just Before the Present/Found PauseMode...
		it = m_pauseModeStack.end();
		if ( it != m_pauseModeStack.begin() )
			it--;

		for ( ; it != found_it && it != m_pauseModeStack.begin() ; --it )
		{
			// Compute the PauseMode's EPICS Expiration Time...
			struct timespec pausemode_expire = it->m_maxTime;

			// Skip Old PauseMode Expiration Check If No Max Time...
			if ( pausemode_expire.tv_sec == 0
					&& pausemode_expire.tv_nsec == 0 )
			{
				ERROR("getPauseModeByTime():"
					<< " No Max Time for PauseMode " << it->m_numModes
					<< " in [" << it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " - Don't Check for PauseMode Expiration");
				continue;
			}

			// Add Timeout Threshold to PauseMode Max Time...
			pausemode_expire.tv_sec += container_cleanup_timeout.tv_sec;
			pausemode_expire.tv_nsec +=
				container_cleanup_timeout.tv_nsec;
			if ( pausemode_expire.tv_nsec >= NANO_PER_SECOND_LL )
			{
				pausemode_expire.tv_nsec -= NANO_PER_SECOND_LL;
				pausemode_expire.tv_sec++;
			}

			if ( ctrl->verbose() > 2 )
			{
				DEBUG("getPauseModeByTime():"
					<< " Check Expire PauseMode " << it->m_numModes
					<< " in [" << it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " has Expiration Time = "
					<< pausemode_expire.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< pausemode_expire.tv_nsec << std::setw(0)
					<< ", old_ts=" << old_ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< old_ts.tv_nsec << std::setw(0)
					<< " m_paused=" << it->m_paused
					<< " m_numModes=" << it->m_numModes
					<< " m_numFiles=" << it->m_numFiles
					<< " m_numPauseFiles=" << it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( it->m_lastPrologueFile ) ?
						it->m_lastPrologueFile->path() : "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");
			}

			// Is It Time to Close Down This PauseMode?
			// Note: old_ts = 0.0 When Uninitialized...
			if ( compareTimeStamps( old_ts, pausemode_expire ) >= 0 )
			{
				DEBUG("getPauseModeByTime():"
					<< " PauseMode " << it->m_numModes
					<< " has EXPIRED"
					<< " in [" << it->m_minTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_minTime.tv_nsec << std::setw(0)
					<< ", " << it->m_maxTime.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
					<< " old_ts=" << old_ts.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< old_ts.tv_nsec << std::setw(0)
					<< " >= Expiration Time "
					<< pausemode_expire.tv_sec << "."
					<< std::setfill('0') << std::setw(9)
					<< pausemode_expire.tv_nsec << std::setw(0)
					<< ", Terminating Expired PauseMode"
					<< " m_paused=" << it->m_paused
					<< " m_numModes=" << it->m_numModes
					<< " m_numFiles=" << it->m_numFiles
					<< " m_numPauseFiles=" << it->m_numPauseFiles
					<< " m_pendingFiles.size()="
						<< it->m_pendingFiles.size()
					<< " m_lastPrologueFile="
					<< ( ( it->m_lastPrologueFile ) ?
						it->m_lastPrologueFile->path() : "(null)" )
					<< " - Btw, the PauseMode Stack has "
					<< m_pauseModeStack.size() << " elements");

				// _First_ Add Any Deferred Files to Container List,
				// And Kick Out All Associated FileAdded Notifications...
				// (To Make Sure These Files are Processed By Listeners!)

				std::list<StorageFile::SharedPtr>::iterator fit;
				for ( fit = it->m_pendingFiles.begin() ;
						fit != it->m_pendingFiles.end() ; ++fit )
				{
					DEBUG("getPauseModeByTime():"
						<< " Pushing Pending File "
						<< (*fit)->path()
						<< " Onto Container File List"
						<< " (and Notifying FileAdded)"
						<< " from Expired PauseMode " << it->m_numModes
						<< " in [" << it->m_minTime.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< it->m_minTime.tv_nsec << std::setw(0)
						<< ", " << it->m_maxTime.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
						<< " m_paused=" << it->m_paused
						<< " m_numModes=" << it->m_numModes
						<< " m_numFiles=" << it->m_numFiles
						<< " m_numPauseFiles=" << it->m_numPauseFiles
						<< " m_pendingFiles.size()="
							<< it->m_pendingFiles.size()
						<< " m_lastPrologueFile="
						<< ( ( it->m_lastPrologueFile ) ?
							it->m_lastPrologueFile->path() : "(null)" )
						<< " - Btw, the PauseMode Stack has "
						<< m_pauseModeStack.size() << " elements");

					m_files.push_back( *fit );
					m_newFile( *fit );
				}

				it->m_pendingFiles.clear();

				// Finally Close Down This PauseMode & The Final File...

				if ( it->m_file )
				{
					ADARA::RunStatus::Enum status =
							ADARA::RunStatus::NO_RUN;
					if ( m_runNumber )
					{
						status = m_active ? ADARA::RunStatus::RUN_EOF :
									ADARA::RunStatus::END_RUN;
					}

					it->m_file->terminate( status );
					StorageManager::addBaseStorage( it->m_file->size() );
					it->m_file.reset();
				}

				// Remove PauseMode from Stack
				// Note: erase() Leaves Iterator Pointing at _Next_ Entry,
				// Which is Fine Because We're Iterating "Backwards"...!
				it = m_pauseModeStack.erase( it );

				// Note: No Need to Reset "Last Time Stamp" Here,
				// As We're About to Reset the Last Saved Iterator Anyway.
			}
		}

		// Also Check the Oldest PauseMode on the Stack
		// For Any Previously Deferred Pending Files...
		// We Can Always Go Ahead and Push Them
		// Onto the Container File List Now...

		// Get "Latest" Oldest PauseMode on the Stack...
		// (We May Have Just Closed Some Expired PauseModes Above...)
		it = m_pauseModeStack.end();
		if ( it != m_pauseModeStack.begin() )
			it--;

		// Note: Btw, It's Ok If This "Latest" Oldest PauseMode
		// is Actually the "Found" One, Because We Need to
		// Get/Keep That PauseMode Up-to-Date with the
		// FileAdded Notifies, Too...!
		// (a.k.a. the SMS Version 1.7.2 DASMON Delay/Timeout Bug... ;-b)

		// Make Sure We Actually Have a PauseMode Left Here... ;-D
		if ( it != m_pauseModeStack.end() )
		{
			// If We Have Some Previously Deferred Pending Files
			// on Our List, Go Ahead and Push Them Onto the
			// Container File List Now...
			if ( it->m_pendingFiles.size() > 0 )
			{
				std::list<StorageFile::SharedPtr>::iterator fit;
				for ( fit = it->m_pendingFiles.begin() ;
						fit != it->m_pendingFiles.end() ; ++fit )
				{
					DEBUG("getPauseModeByTime():"
						<< " Pushing Previously Pending File "
						<< (*fit)->path()
						<< " Onto Container File List"
						<< " (and Notifying FileAdded)"
						<< " from Oldest PauseMode " << it->m_numModes
						<< " in [" << it->m_minTime.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< it->m_minTime.tv_nsec << std::setw(0)
						<< ", " << it->m_maxTime.tv_sec << "."
						<< std::setfill('0') << std::setw(9)
						<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
						<< " m_paused=" << it->m_paused
						<< " m_numModes=" << it->m_numModes
						<< " m_numFiles=" << it->m_numFiles
						<< " m_numPauseFiles=" << it->m_numPauseFiles
						<< " m_pendingFiles.size()="
						<< it->m_pendingFiles.size()
						<< " m_lastPrologueFile="
						<< ( ( it->m_lastPrologueFile ) ?
							it->m_lastPrologueFile->path() : "(null)" )
						<< " - Btw, the PauseMode Stack has "
						<< m_pauseModeStack.size() << " elements");

					m_files.push_back( *fit );
					m_newFile( *fit );
				}

				it->m_pendingFiles.clear();
			}

			// Note: Any Probably-Active "Current" Open File
			// for This PauseMode Should Have Already Been Added
			// to Container File List and Signaled a FileAdded Notify
			// As Part of the Pending File List Above... ;-D
		}

		// Cache this "Oldest" Max DataSource Time,
		// To Use in Conjunction with the Next Cached Lookup...
		m_last_old_ts = old_ts;
	}

	// Didn't Find PauseMode for That Time...! ;-O
	if ( found_it == m_pauseModeStack.end() )
	{
		// Check TimeStamp Versus Oldest Stacked PauseMode Min Time...
		// (To Capture Case of Bogus SAWTOOTH TimeStamps, That Are
		// So Far Out of Order that We Already Closed Their PauseMode!
		// Or More Likely Simply a Bogus TimeStamp from Somewhere... ;-b)

		// Snag "Oldest" PauseMode...
		it = m_pauseModeStack.end();
		if ( it != m_pauseModeStack.begin() )
		{
			it--;
			if ( it->m_file )
			{
				// Does EPICS TimeStamp Occur _Before_ Oldest PauseMode...?
				if ( compareTimeStamps( ts, it->m_minTime ) < 0 )
				{
					// Use "Current PauseMode" for All Such
					// Bogus TimeStamps... ;-Q
					it = m_pauseModeStack.begin();

					// Rate-Limited Logging PauseMode SAWTOOTH...
					std::string log_info;
					if ( RateLimitedLogging::checkLog(
							RLLHistory_StorageContainer,
							RLL_PAUSEMODE_SAWTOOTH, "none",
							2, 10, 1000, log_info ) ) {
						ERROR(log_info
							<< "getPauseModeByTime():"
							<< " PauseMode SAWTOOTH for ts="
							<< ts.tv_sec << "."
							<< std::setfill('0') << std::setw(9)
								<< ts.tv_nsec
							<< " -> Using Current PauseMode "
								<< it->m_numModes
							<< " in [" << it->m_minTime.tv_sec << "."
							<< std::setfill('0') << std::setw(9)
							<< it->m_minTime.tv_nsec << std::setw(0)
							<< ", " << it->m_maxTime.tv_sec << "."
							<< std::setfill('0') << std::setw(9)
							<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
							<< " check_old_pausemodes="
								<< check_old_pausemodes
							<< " m_paused=" << it->m_paused
							<< " m_numModes=" << it->m_numModes
							<< " m_numFiles=" << it->m_numFiles
							<< " m_numPauseFiles=" << it->m_numPauseFiles
							<< " m_pendingFiles.size()="
								<< it->m_pendingFiles.size()
							<< " m_lastPrologueFile="
							<< ( ( it->m_lastPrologueFile ) ?
								it->m_lastPrologueFile->path() : "(null)" )
							<< " - Btw, the PauseMode Stack has "
							<< m_pauseModeStack.size() << " elements");
					}

					pm_it = it;

					// Initialize Last Lookup for Caching...
					m_last_found_it = it;
					m_last_ts.tv_sec = ts.tv_sec; // EPICS Time...!
					m_last_ts.tv_nsec = ts.tv_nsec;
					m_last_ignore_pkt_timestamp = ignore_pkt_timestamp;
					m_last_check_old_pausemodes = check_old_pausemodes;

					return;
				}
			}
		}

		// No PauseModes to Compare Times...?!
		ERROR("getPauseModeByTime(): No PauseMode Found"
			<< " for ts=" << ts.tv_sec << "."
			<< std::setfill('0') << std::setw(9) << ts.tv_nsec
			<< " check_old_pausemodes=" << check_old_pausemodes
			<< " - Btw, the PauseMode Stack has "
			<< m_pauseModeStack.size() << " elements");
	}

	pm_it = found_it;

	// Initialize Last Lookup for Caching...
	m_last_found_it = found_it;
	m_last_ts.tv_sec = ts.tv_sec; // EPICS Time...!
	m_last_ts.tv_nsec = ts.tv_nsec;
	m_last_ignore_pkt_timestamp = ignore_pkt_timestamp;
	m_last_check_old_pausemodes = check_old_pausemodes;

	return;
}

bool StorageContainer::write(
		std::list<struct PauseMode>::iterator &it,
		IoVector &iovec, uint32_t len, bool notify,
		uint32_t *written)
{
	static uint32_t retry_count = 0;

	/* We don't immediately close a file when we exceed the size limit
	 * in order to avoid creating a new file just for the end-of-run
	 * marker. Instead, we wait for the run to start or the next packet
	 * to be written (and clients notified).
	 * (Note: this is encapsulated in the StorageFile::oversize() method,
	 * which only triggers when "notify" is true... A-Ha! ;-D)
	 */

	// Preserve/Inherit Min & Max Time for Any PauseMode
	// Oversize Roll-Overs Here...
	// (Pre-Increment Max Time for Decrement in terminateFile()...)
	struct timespec maxTime = it->m_maxTime;
	maxTime.tv_nsec++;
	if ( maxTime.tv_nsec >= NANO_PER_SECOND_LL ) {
		maxTime.tv_nsec -= NANO_PER_SECOND_LL;
		maxTime.tv_sec++;
	}
	struct timespec minTime = it->m_minTime;
	bool paused = it->m_paused;

	if ( it->m_file && it->m_file->oversize() )
		terminateFile( it, /* do_terminate */ true, maxTime );

	if ( !it->m_file )
		newFile( it, paused, minTime );

	// On File Write Error, Try to Close the Current Data File
	// and Open a New One Here, Just in Case This Helps...
	// Use an Error Counter to prevent File Thrashing...

	bool doRetry;
	bool writeOk;

	do
	{
		writeOk = it->m_file->write(iovec, len, notify, written);

		// File Write Failed...!
		if ( !writeOk ) {
			// Rotate Current File and Retry...
			if ( retry_count++ < 5 ) {
				ERROR("Container Write Failed!"
					<< " Try Rotating File & Retrying Write"
					<< " it->m_file="
						<< ( it->m_file ? it->m_file->path() : "(null)" )
					<< " retry_count=" << retry_count);
				terminateFile( it, /* do_terminate */ true, maxTime );
				newFile( it, paused, minTime );
				doRetry = true;
			}
			// Retry Count Exceeded, Fail Hard...!
			else {
				ERROR("Container Write Failed!"
					<< " Retry Count Exceeded, HARD FAIL on Write...!"
					<< " it->m_file="
						<< ( it->m_file ? it->m_file->path() : "(null)" )
					<< " retry_count=" << retry_count);
				doRetry = false;
			}
		}

		// File Write Succeeded. :-D
		else {
			// We Were Buggered, But Now We've Recovered... Whew! ;-D
			if ( retry_count ) {
				ERROR("Container Recovered - Write Succeeded!"
					<< " it->m_file="
						<< ( it->m_file ? it->m_file->path() : "(null)" )
					<< " Resetting retry_count="
					<< retry_count << " -> 0");
				// Reset Retry Count, We Got Through...
				retry_count = 0;
			}
			// Note: No need to reset doRetry=false here.
			// We will fall thru loop anyway, as writeOk=true... ;-D
		}
	}
	while ( !writeOk && doRetry );

	return( writeOk );
}

void StorageContainer::terminate(void)
{
	DEBUG("terminate(): Terminating Container " << m_name
		<< " m_active=" << m_active
		<< " m_runNumber=" << m_runNumber);

	// Iterate Thru All PauseMode Structs In Turn...

	std::list<struct PauseMode>::iterator it;

	// Clean Up & Close Latest Data File for Each PauseMode,
	// Starting from "Oldest" Up to Current...

	// Still need to use a Forward Iterator for terminateFile() API,
	// so just Start from the End and Work Backwards... ;-D
	it = m_pauseModeStack.end();
	if ( it != m_pauseModeStack.begin() )
		it--;

	for ( ; it != m_pauseModeStack.begin(); --it )
	{
		DEBUG("terminate(): Terminating Container " << m_name
			<< " - Closing PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");

		// _First_ Add Any Deferred Files to Container List,
		// And Kick Out All Associated FileAdded Notifications...
		// (To Make Sure These Files are Processed By Listeners!)

		std::list<StorageFile::SharedPtr>::iterator fit;
		for ( fit = it->m_pendingFiles.begin() ;
				fit != it->m_pendingFiles.end() ; ++fit )
		{
			DEBUG("terminate(): Terminating Container " << m_name
				<< " - Pushing Pending File " << (*fit)->path()
				<< " onto Container File List"
				<< " and Notifying FileAdded"
				<< " for Closing PauseMode " << it->m_numModes
				<< " [" << it->m_minTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< it->m_minTime.tv_nsec << std::setw(0)
				<< ", " << it->m_maxTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
				<< " m_paused=" << it->m_paused
				<< " m_numModes=" << it->m_numModes
				<< " m_numFiles=" << it->m_numFiles
				<< " m_numPauseFiles=" << it->m_numPauseFiles
				<< " m_pendingFiles.size()="
					<< it->m_pendingFiles.size()
				<< " m_lastPrologueFile="
				<< ( ( it->m_lastPrologueFile ) ?
					it->m_lastPrologueFile->path() : "(null)" )
				<< " - Btw, the PauseMode Stack now has "
				<< m_pauseModeStack.size() << " elements");

			m_files.push_back( *fit );
			m_newFile( *fit );
		}

		it->m_pendingFiles.clear();

		// Finally Close Down This PauseMode & The Final File...

		if ( it->m_file )
		{
			ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;
			if ( m_runNumber )
			{
				status = m_active ? ADARA::RunStatus::RUN_EOF :
							ADARA::RunStatus::END_RUN;
			}

			it->m_file->terminate( status );
			StorageManager::addBaseStorage( it->m_file->size() );
			it->m_file.reset();
		}

		// Remove PauseMode from Stack
		// Note: erase() Leaves Iterator Pointing at _Next_ Entry,
		// Which is Fine Because We're Iterating "Backwards"...!
		it = m_pauseModeStack.erase( it );

		// Reset "Last Time Stamp" for getPauseModeByTime()...!
		// ("Just in Case" Changing PauseMode Stack Here
		// Invalidates Saved Iterator)
		m_last_ts.tv_sec = -1;
		m_last_ts.tv_nsec = -1;
	}

	// Now Close "Current" PauseMode, Set Container Inactive
	// (To Trigger "End of Run" Status Packet...)

	m_active = false;

	if ( it != m_pauseModeStack.end() )
	{
		DEBUG("terminate(): Terminating Container " << m_name
			<< " m_active=" << m_active
			<< " - Closing Current PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");

		// _First_ Add Any Deferred Files to Container List,
		// And Kick Out All Associated FileAdded Notifications...
		// (To Make Sure These Files are Processed By Listeners!)

		std::list<StorageFile::SharedPtr>::iterator fit;
		for ( fit = it->m_pendingFiles.begin() ;
				fit != it->m_pendingFiles.end() ; ++fit )
		{
			DEBUG("terminate(): Terminating Container " << m_name
				<< " - Pushing Pending File " << (*fit)->path()
				<< " onto Container File List"
				<< " and Notifying FileAdded"
				<< " for Closing Current PauseMode " << it->m_numModes
				<< " [" << it->m_minTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< it->m_minTime.tv_nsec << std::setw(0)
				<< ", " << it->m_maxTime.tv_sec << "."
				<< std::setfill('0') << std::setw(9)
				<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
				<< " m_paused=" << it->m_paused
				<< " m_numModes=" << it->m_numModes
				<< " m_numFiles=" << it->m_numFiles
				<< " m_numPauseFiles=" << it->m_numPauseFiles
				<< " m_pendingFiles.size()="
					<< it->m_pendingFiles.size()
				<< " m_lastPrologueFile="
				<< ( ( it->m_lastPrologueFile ) ?
					it->m_lastPrologueFile->path() : "(null)" )
				<< " - Btw, the PauseMode Stack now has "
				<< m_pauseModeStack.size() << " elements");

			m_files.push_back( *fit );
			m_newFile( *fit );
		}

		it->m_pendingFiles.clear();

		// Finally Close Down This PauseMode & The Final File...

		if ( it->m_file )
		{
			ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;
			if ( m_runNumber )
			{
				status = m_active ? ADARA::RunStatus::RUN_EOF :
							ADARA::RunStatus::END_RUN;
			}

			it->m_file->terminate( status );
			StorageManager::addBaseStorage( it->m_file->size() );
			it->m_file.reset();
		}

		// Remove Final/Current PauseMode from Stack
		m_pauseModeStack.erase( it );

		// Reset "Last Time Stamp" for getPauseModeByTime()...!
		// ("Just in Case" Changing PauseMode Stack Here
		// Invalidates Saved Iterator)
		m_last_ts.tv_sec = -1;
		m_last_ts.tv_nsec = -1;
	}

	// Clean Up & Close DataSource Saved Input Stream Files, Too!
	for ( uint32_t i = 0 ; i < m_ds_input_files.size() ; i++ )
	{
		// Terminate Saved Input Stream File for this Data Source...
		if ( m_ds_input_files[i] )
		{
			DEBUG("terminate(): Terminating Container " << m_name
				<< " m_active=" << m_active
				<< " - Closing Saved Data File "
				<< m_ds_input_files[i]->path()
				<< " for Data Source ID " << i);

			m_ds_input_files[i]->terminateSave();
			StorageManager::addBaseStorage( m_ds_input_files[i]->size() );
			m_ds_input_files[i].reset();
		}
	}
}

void StorageContainer::notify(void)
{
	std::list<struct PauseMode>::iterator it;
    it = m_pauseModeStack.begin();

	if ( it->m_file )
		it->m_file->notify();
}

bool StorageContainer::save(IoVector &iovec, uint32_t len,
		uint32_t dataSourceId, bool notify, uint32_t *written)
{
	// Verify the Saved Input Stream File for this Data Source,
	// Create it as needed...
	if ( dataSourceId >= m_ds_input_files.size() )
	{
		for ( uint32_t i = m_ds_input_files.size() ;
				i <= dataSourceId ; i++ )
		{
			// Initialize the Saved Input Stream File Number
			// for this Data Source...
			m_ds_input_num_files.push_back( 0 );

			// Create an Entry for the Saved Input Stream File
			// for this Data Source...
			m_ds_input_files.push_back( StorageFile::SharedPtr() );
		}
	}
 
	/* This Saved Input Stream file exceeds the size limit;
	 * close it so we can make a new one now...
	 * (Note: we do a manual "notify" trigger for Saved Input Stream
	 * file writes, so StorageFile::oversize() won't immediately be set
	 * for Saved File Prologue writes... ;-D)
	 */
	if ( notify && m_ds_input_files[dataSourceId]
			&& m_ds_input_files[dataSourceId]->oversize() )
	{
		// Terminate Saved Input Stream File
		// for this Data Source...
		m_ds_input_files[dataSourceId]->terminateSave();
		StorageManager::addBaseStorage(
			m_ds_input_files[dataSourceId]->size());
		m_ds_input_files[dataSourceId].reset();
	}

	// Make a new Saved Input Stream file...?
	if ( !m_ds_input_files[dataSourceId] )
	{
		// Create the Saved Input Stream File
		// for this Data Source...
		m_ds_input_files[dataSourceId] =
			StorageFile::saveFile( m_weakThis, dataSourceId,
					++(m_ds_input_num_files[dataSourceId]) );

		// Keep Track of the Number of Saved Input Stream Files,
		// for Long-Non-Running Container Splitting...
		m_totFileCount++;

		// If We are an Old Container with a "Last" Save Prologue File,
		// Just Append That File Directly Now...
		if ( dataSourceId < m_lastSavePrologueFiles.size()
				&& m_lastSavePrologueFiles[dataSourceId] )
		{
			DEBUG("StorageContainer::save():"
				<< " Directly Appending Last Save Prologue File "
				<< m_lastSavePrologueFiles[dataSourceId]->path()
				<< " for New Save File "
				<< m_ds_input_files[dataSourceId]->path());

			m_ds_input_files[dataSourceId]->catFile(
				m_lastSavePrologueFiles[dataSourceId] );
		}

		// Tell the storage manager about the new Saved Input Stream file
		// so we can add the prologue before anyone else sees it.
		else
		{
			StorageManager::saveCreated( dataSourceId,
				false /* capture_last */ );
		}
	}

	// TODO On Error, Should We Try to Close the Current
	// Saved Stream File and Open a New One Here...?
	// (Maybe with a Error Counter to prevent File Thrashing...?)
	// Nawww... We don't want to pound on a troubled filesystem
	// just to try and save our input stream data... Just let it go! ;-D
	return m_ds_input_files[dataSourceId]->save(iovec, len, written);
}

void StorageContainer::pause( struct timespec &pauseTime ) // EPICS Time
{
	std::list<struct PauseMode>::iterator it;
    it = m_pauseModeStack.begin();

	DEBUG("Pausing StorageContainer " << m_name
		<< " m_active=" << m_active
		<< " m_runNumber=" << m_runNumber
		<< " pauseTime=" << pauseTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< pauseTime.tv_nsec << std::setw(0)
		<< " it->m_file="
			<< ( it->m_file ? it->m_file->path() : "(null)" ) );

	// Close Any Non-Paused Run File...
	if ( it->m_file && !it->m_file->paused() )
	{
		terminateFile( it, /* do_terminate */ false, pauseTime );
		it->m_numModes++; // Increment for Next PauseMode...
		it->m_numFiles = 1; // Preset File Number Counter (No Incr Paused)
	}

	// Create New Paused Run File...
	if ( !it->m_file )
		newFile( it, /* paused */ true, pauseTime );
}

void StorageContainer::resume( struct timespec &resumeTime ) // EPICS Time
{
	std::list<struct PauseMode>::iterator it;
    it = m_pauseModeStack.begin();

	DEBUG("Resuming StorageContainer " << m_name
		<< " m_active=" << m_active
		<< " m_runNumber=" << m_runNumber
		<< " resumeTime=" << resumeTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< resumeTime.tv_nsec << std::setw(0)
		<< " it->m_file="
			<< ( it->m_file ? it->m_file->path() : "(null)" ) );

	// Close Any Paused Run File...
	if ( it->m_file && it->m_file->paused() )
	{
		terminateFile( it, /* do_terminate */ false, resumeTime );
		it->m_numModes++; // Increment for Next PauseMode...
		it->m_numFiles = 0; // Reset File Number Counter (Pre-Increments)
	}

	// Just to Be Sure... :-D
	it->m_numPauseFiles = 0;

	// Create New Non-Paused Run File to Resume Normal Data Collection
	if ( !it->m_file )
		newFile( it, /* paused */ false, resumeTime );
}

void StorageContainer::getLastPrologueFiles(
		std::list<struct PauseMode>::iterator &it,
		bool do_save_prologues )
{
	DEBUG("getLastPrologueFiles(): Capture Prologue Headers"
		<< " for Stacked Container " << m_name
		<< " in PauseMode " << it->m_numModes
		<< " [" << it->m_minTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_minTime.tv_nsec << std::setw(0)
		<< ", " << it->m_maxTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
		<< " m_paused=" << it->m_paused
		<< " m_numModes=" << it->m_numModes
		<< " m_numFiles=" << it->m_numFiles
		<< " m_numPauseFiles=" << it->m_numPauseFiles
		<< " m_pendingFiles.size()="
			<< it->m_pendingFiles.size()
		<< " m_lastPrologueFile="
		<< ( ( it->m_lastPrologueFile ) ?
			it->m_lastPrologueFile->path() : "(null)" )
		<< " - Btw, the PauseMode Stack now has "
		<< m_pauseModeStack.size() << " elements");

	// Make Sure We Don't Already Have A "Last" Prologue Header File...
	if ( !(it->m_lastPrologueFile) )
	{
		// Capture "Last" Prologue Header File for This Container
		it->m_lastPrologueFile = StorageFile::newFile( m_weakThis,
			it->m_paused, it->m_numModes, 0, 0,
			ADARA::RunStatus::PROLOGUE );

		// Tell the storage manager about the new file so we can
		// add the prologue header
		StorageManager::fileCreated( it->m_lastPrologueFile,
			true /* capture_last */, false /* addStateToIndex */ );

		// Close the Prologue Header File...
		it->m_lastPrologueFile->put_fd();

		// This is _Not_ an "Active" File, Just a Prologue Header...!
		it->m_lastPrologueFile->setInactive();

		DEBUG("getLastPrologueFiles():"
			<< " Created Raw Data Prologue Header File "
			<< it->m_lastPrologueFile->path()
			<< " for Stacked Container " << m_name
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}

	// Already Have A "Last" Prologue Header File...
	else
	{
		DEBUG("getLastPrologueFiles():"
			<< " _Already_ Have Raw Data Prologue Header File "
			<< it->m_lastPrologueFile->path()
			<< " for Stacked Container " << m_name
			<< " in PauseMode " << it->m_numModes
			<< " [" << it->m_minTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_minTime.tv_nsec << std::setw(0)
			<< ", " << it->m_maxTime.tv_sec << "."
			<< std::setfill('0') << std::setw(9)
			<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
			<< " m_paused=" << it->m_paused
			<< " m_numModes=" << it->m_numModes
			<< " m_numFiles=" << it->m_numFiles
			<< " m_numPauseFiles=" << it->m_numPauseFiles
			<< " m_pendingFiles.size()="
				<< it->m_pendingFiles.size()
			<< " m_lastPrologueFile="
			<< ( ( it->m_lastPrologueFile ) ?
				it->m_lastPrologueFile->path() : "(null)" )
			<< " - Btw, the PauseMode Stack now has "
			<< m_pauseModeStack.size() << " elements");
	}

	// As Requested, Get Last Save Prologue Files Too...
	if ( do_save_prologues )
	{
		// Allocate Last Save Prologue File Vector to
		// Match DataSource Input Files Vector Size...
		if ( m_ds_input_files.size() > m_lastSavePrologueFiles.size() )
		{
			for ( uint32_t i = m_lastSavePrologueFiles.size() ;
					i < m_ds_input_files.size() ; i++ )
			{
				// Create an Entry for the Saved Input Stream Prologue File
				// for this Data Source...
				m_lastSavePrologueFiles.push_back(
					StorageFile::SharedPtr() );
			}
		}

		// Capture a "Last" Save Prologue Header File for This Container,
		// For Each Valid Data Source ID... :-D
		for ( uint32_t i = 0 ; i < m_ds_input_files.size() ; i++ )
		{
			if ( m_ds_input_files[i] )
			{
				// Make Sure We Don't Already Have A "Last" Save Prologue
				// Header File for This DataSource...
				if ( !(m_lastSavePrologueFiles[i]) )
				{
					m_lastSavePrologueFiles[i] =
						StorageFile::newFile( m_weakThis,
							/* paused */ false, 0, 1, i,
							ADARA::RunStatus::PROLOGUE );

					// Tell the storage manager about the new file
					// so we can add the save prologue header
					StorageFile::SharedPtr tmp = m_ds_input_files[i];
					m_ds_input_files[i] = m_lastSavePrologueFiles[i];
					StorageManager::saveCreated( i,
						true /* capture_last */ );
					m_ds_input_files[i] = tmp;

					// Close the Save Prologue Header File...
					m_lastSavePrologueFiles[i]->put_fd();

					// This is _Not_ an "Active" File,
					// Just a Prologue Header...!
					m_lastSavePrologueFiles[i]->setInactive();

					DEBUG("getLastPrologueFiles():"
						<< " Created Saved Data Prologue Header File "
						<< m_lastSavePrologueFiles[i]->path()
						<< " for Data Source ID " << i
						<< " for Stacked Container " << m_name);
				}

				// Already Have A "Last" Prologue Header File...
				else
				{
					DEBUG("getLastPrologueFiles():"
						<< " _Already_ Have A"
						<< " Saved Data Prologue Header File "
						<< m_lastSavePrologueFiles[i]->path()
						<< " for Data Source ID " << i
						<< " for Stacked Container " << m_name);
				}
			}
		}
	}
}

void StorageContainer::copyLastPrologueFiles(
		std::list<struct PauseMode>::iterator &it,
		std::string &name, StorageFile::SharedPtr &lastPrologueFile,
		std::vector<StorageFile::SharedPtr> &lastSavePrologueFiles )
{
	DEBUG("copyLastPrologueFiles(): Copy Over Prologue Headers"
		<< " from Last Stacked Container " << name
		<< " for New Stacked Container " << m_name);

	if ( lastPrologueFile )
	{
		// Make New "Last" Prologue Header File for This Container
		// Note: This is Only Called for Long-Non-Running Container Splits,
		// Prior to Creating 1st "New File", so NumModes Needs an Incr...
		it->m_lastPrologueFile = StorageFile::newFile( m_weakThis,
			it->m_paused, it->m_numModes + 1, 0, 0,
			ADARA::RunStatus::PROLOGUE );

		// Copy "Last" Prologue Header File from Previous Container
		DEBUG("copyLastPrologueFiles():"
			<< " Appending Previous Last Prologue File "
			<< lastPrologueFile->path()
			<< " to New Last Prologue File "
			<< it->m_lastPrologueFile->path());
		it->m_lastPrologueFile->catFile( lastPrologueFile );

		// Close the Prologue Header File...
		it->m_lastPrologueFile->put_fd();

		// This is _Not_ an "Active" File, Just a Prologue Header...!
		it->m_lastPrologueFile->setInactive();

		DEBUG("copyLastPrologueFiles():"
			<< " Created Raw Data Prologue Header File "
			<< it->m_lastPrologueFile->path()
			<< " for Stacked Container " << m_name);
	}
	else
	{
		DEBUG("copyLastPrologueFiles():"
			<< " No Raw Data Prologue Header File to Copy"
			<< " for Stacked Container " << m_name);
	}

	// Allocate Last Save Prologue File Vector to
	// Match DataSource Input Files Vector Size...
	if ( lastSavePrologueFiles.size() > m_lastSavePrologueFiles.size() )
	{
		for ( uint32_t i = m_lastSavePrologueFiles.size() ;
				i < lastSavePrologueFiles.size() ; i++ )
		{
			// Create an Entry for the Saved Input Stream Prologue File
			// for this Data Source...
			m_lastSavePrologueFiles.push_back( StorageFile::SharedPtr() );
		}
	}

	// Copy "Last" Save Prologue Header File from Previous Container
	// for This Container, For Each Valid Data Source ID... :-D
	for ( uint32_t i = 0 ; i < lastSavePrologueFiles.size() ; i++ )
	{
		if ( lastSavePrologueFiles[i] )
		{
			m_lastSavePrologueFiles[i] = StorageFile::newFile( m_weakThis,
				/* paused */ false, 0, 1, i, ADARA::RunStatus::PROLOGUE );

			// Copy "Last" Prologue Header File from Previous Container
			DEBUG("copyLastPrologueFiles():"
				<< " Appending Previous Last Save Prologue File "
				<< lastSavePrologueFiles[i]->path()
				<< " to New Last Save Prologue File "
				<< m_lastSavePrologueFiles[i]->path()
				<< " for Data Source ID " << i
				<< " for Stacked Container " << m_name);
			m_lastSavePrologueFiles[i]->catFile(
				lastSavePrologueFiles[i] );

			// Close the Save Prologue Header File...
			m_lastSavePrologueFiles[i]->put_fd();

			// This is _Not_ an "Active" File, Just a Prologue Header...!
			m_lastSavePrologueFiles[i]->setInactive();

			DEBUG("copyLastPrologueFiles():"
				<< " Created Saved Data Prologue Header File "
				<< m_lastSavePrologueFiles[i]->path()
				<< " for Data Source ID " << i
				<< " for Stacked Container " << m_name);
		}
	}
}

void StorageContainer::getFiles(std::list<StorageFile::SharedPtr> &list)
{
	if (m_active || !m_files.empty()) {
		/* We've already loaded the list of files from disk, or
		 * we're currently active, so we can just copy our list
		 * into the caller's.
		 */
		list = m_files;
		return;
	}

	/* TODO load files from disk */
	throw std::runtime_error("not implemented");
}

bool StorageContainer::createMarker(const char *file)
{
	std::string path(m_name);
	int fd;

	path += "/";
	path += file;

	fd = openat(StorageManager::base_fd(), path.c_str(),
		    O_WRONLY|O_CREAT, MARKER_MODE);
	if (fd < 0) {
		int e = errno;
		ERROR("Unable to creat('" << path << "'): " << strerror(e));
	} else
		close(fd);

	return fd < 0;
}

void StorageContainer::markTranslated(void)
{
	/* Mark this container as completed so that we don't resend it to
	 * STC if we restart before it is purged.
	 */
	INFO("Marking Run " << m_runNumber << " as Successfully Translated");
	if (createMarker(m_completed_marker))
		ERROR("Run " << m_runNumber << " will be resent if SMS "
		      "restarts. ");
	m_translated = true;
}

void StorageContainer::markManual(void)
{
	/* Mark this container as needing manual processing.
	 */
	ERROR("Marking Run " << m_runNumber
		<< " as Requiring Manual Processing!");
	if (createMarker(m_manual_marker))
		ERROR("Run " << m_runNumber << " will be resent if SMS restarts.");
	m_manual = true;
}

StorageContainer::StorageContainer(
		const struct timespec &start, // Wallclock Time...!
		const struct timespec &minTime, // EPICS Time...!
		uint32_t run, std::string &propId) :
	m_startTime(start), // Wallclock Time...!
	m_minTime(minTime), // EPICS Time...!
	m_runNumber(run), m_propId(propId), m_totFileCount(0),
	m_active(true), m_translated(false), m_manual(false),
	m_requeueCount(0), m_saved_size(0)
{
	m_maxTime.tv_sec = 0; // EPICS Time...!
	m_maxTime.tv_nsec = 0;

	// Initialize Last Lookup for getPauseModeByTime() Caching...
	m_last_found_it = m_pauseModeStack.end();
	m_last_ts.tv_sec = -1; // EPICS Time...!
	m_last_ts.tv_nsec = -1;
	m_last_old_ts.tv_sec = -1; // EPICS Time...!
	m_last_old_ts.tv_nsec = -1;
	m_last_ignore_pkt_timestamp = true;
	m_last_check_old_pausemodes = false;
}

StorageContainer::StorageContainer(const std::string &name) :
	m_runNumber(0), m_propId("UNKNOWN"), m_totFileCount(0),
	m_name(name), m_active(false), m_translated(false), m_manual(false),
	m_requeueCount(0), m_saved_size(0)
{
	m_startTime.tv_sec = 0; // Wallclock Time...!
	m_startTime.tv_nsec = 0;

	m_minTime.tv_sec = 0; // EPICS Time...!
	m_minTime.tv_nsec = 0;

	m_maxTime.tv_sec = 0; // EPICS Time...!
	m_maxTime.tv_nsec = 0;

	// Initialize Last Lookup for getPauseModeByTime() Caching...
	m_last_found_it = m_pauseModeStack.end();
	m_last_ts.tv_sec = -1; // EPICS Time...!
	m_last_ts.tv_nsec = -1;
	m_last_old_ts.tv_sec = -1; // EPICS Time...!
	m_last_old_ts.tv_nsec = -1;
	m_last_ignore_pkt_timestamp = true;
	m_last_check_old_pausemodes = false;
}

StorageContainer::SharedPtr StorageContainer::create(
		const struct timespec &start, // Wallclock Time...!
		const struct timespec &minTime, // EPICS Time...!
		uint32_t run, std::string &propId)
{
	char path[64];
	struct tm tm;

	if (!gmtime_r(&start.tv_sec, &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " gmtime_r failed");

	if (!strftime(path, sizeof(path), "%Y%m%d", &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " base strftime failed");

	if (mkdirat(StorageManager::base_fd(), path, CONTAINER_MODE)) {
		// Don't "Pollute" Errno with Redundant Directory Creation...
		if (errno == EEXIST) {
			errno = 0;
		}
		else {
			int err = errno;
			std::string msg("StorageContainer::StorageContainer(): "
					"base mkdirat error: ");
			msg += strerror(err);
			throw std::runtime_error(msg);
		}
	}

	if (!strftime(path, sizeof(path), "%Y%m%d/%Y%m%d-%H%M%S", &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " path strftime failed");

	StorageContainer::SharedPtr c(
		new StorageContainer(start, minTime, run, propId));
	c->m_weakThis = c;
	c->m_name = path;

	snprintf(path, sizeof(path), ".%09lu",
		start.tv_nsec); // Wallclock Time...!
	c->m_name += path;

	if (run) {
		snprintf(path, sizeof(path), "-run-%u", run);
		c->m_name += path;
	}

	if (mkdirat(StorageManager::base_fd(), c->m_name.c_str(),
							CONTAINER_MODE)) {
		int err = errno;
		std::string msg("StorageContainer::StorageContainer(): "
				"container mkdirat error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	if ( !propId.empty() && propId != "UNKNOWN" ) {
		std::string path = m_proposal_id_marker_prefix + propId;
		c->createMarker(path.c_str());
	}

	// Create Initial Empty PauseMode Struct on Stack...

	struct PauseMode pauseMode;

	pauseMode.m_minTime.tv_sec = c->m_minTime.tv_sec;
	pauseMode.m_minTime.tv_nsec = c->m_minTime.tv_nsec;

	pauseMode.m_maxTime.tv_sec = c->m_maxTime.tv_sec;
	pauseMode.m_maxTime.tv_nsec = c->m_maxTime.tv_nsec;

	pauseMode.m_numModes = 0;
	pauseMode.m_numFiles = 0;
	pauseMode.m_numPauseFiles = 0;

	pauseMode.m_paused = false; // always initialize to Not Paused mode...

	c->m_pauseModeStack.push_front( pauseMode );

	std::list<struct PauseMode>::iterator it = c->m_pauseModeStack.begin();

	DEBUG("create():"
		<< " Pushed New Empty Current PauseMode onto Stack"
		<< " [" << it->m_minTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_minTime.tv_nsec << std::setw(0)
		<< ", " << it->m_maxTime.tv_sec << "."
		<< std::setfill('0') << std::setw(9)
		<< it->m_maxTime.tv_nsec << std::setw(0) << "]"
		<< " m_paused=" << it->m_paused
		<< " m_numModes=" << it->m_numModes
		<< " m_numFiles=" << it->m_numFiles
		<< " m_numPauseFiles=" << it->m_numPauseFiles
		<< " m_pendingFiles.size()="
			<< it->m_pendingFiles.size()
		<< " m_lastPrologueFile="
		<< ( ( it->m_lastPrologueFile ) ?
			it->m_lastPrologueFile->path() : "(null)" )
		<< " - Btw, the PauseMode Stack now has "
		<< c->m_pauseModeStack.size() << " elements");

	return c;
}

uint64_t StorageContainer::blocks(void) const
{
	std::list<StorageFile::SharedPtr>::const_iterator it, end;
	uint64_t total, blocks;

	end = m_files.end();
	for (total = 0, it = m_files.begin(); it != end; ++it) {
		blocks = (*it)->size() + StorageManager::m_block_size - 1;
		blocks /= StorageManager::m_block_size;
		total += blocks;
	}

	// Include the Blocks from Any Imported Saved Data Source Stream Files!
	blocks = m_saved_size + StorageManager::m_block_size - 1;
	blocks /= StorageManager::m_block_size;
	total += blocks;

	return total;
}

bool StorageContainer::validate(void)
{
	if ( m_files.empty() ) {
		WARN("Container " << m_name << " has no data files");
		return true;
	}

	std::list<StorageFile::SharedPtr>::iterator it, end = m_files.end();

	uint32_t addendumExpected = 0;
	uint32_t pauseExpected = 0;
	uint32_t fileExpected = 0;
	uint32_t modeExpected = 0;

	bool use_mode_index = false;  // default to Pre-1.7.0 naming convention

	bool last_paused = true;   // funny logic...! ;-D

	for ( it = m_files.begin() ; it != end ; ++it ) {

		// DEBUG("validate(): " << (*it)->path());

		// File Names Contain Distinct Mode Index and File Index
		// (SMS After 1.7.0)
		uint32_t modeNum = (*it)->modeNumber();
		uint32_t fileNum = (*it)->fileNumber();

		// Check Which File Naming Convention We Have...
		if ( modeNum > 0 )
		{
			// Did We Switch Modes Mid-Stream...?!
			if ( !use_mode_index && it != m_files.begin() )
			{
				WARN("validate(): Container " << m_name
					<< " Switched to Mode Index Bookkeeping Mid-Stream!"
					<< " [" << (*it)->path() << "]");
				return true;
			}

			use_mode_index = true;
		}
		else
		{
			// Did We Switch Modes Mid-Stream...?!
			if ( use_mode_index && it != m_files.begin() )
			{
				WARN("validate(): Container " << m_name
					<< " Switched to File Index Bookkeeping Mid-Stream!"
					<< " [" << (*it)->path() << "]");
				return true;
			}

			use_mode_index = false;
		}

		// REMOVEME
		// DEBUG("validate(): Container " << m_name
			// << " Mode Index " << modeNum
			// << " File Index " << fileNum
			// << " Pause Index " << (*it)->pauseFileNumber()
			// << " Addendum Index " << (*it)->addendumFileNumber()
			// << " [" << (*it)->path() << "]");

		// Paused Run File...
		if ( (*it)->paused() ) {
			// Reset Expected Addendum File Number for Any Paused File...
			addendumExpected = 0;
			// Changed Paused State...?
			if ( !last_paused ) {
				// New Mode Index File Naming Convention Case...
				if ( use_mode_index ) {
					modeExpected++;
					fileExpected = 1;
				}
				// Former Pre-1.7.0 File Naming Convention Case...
				else {
					fileExpected--;
				}
			}
			if ( modeNum != modeExpected
					|| fileNum != fileExpected
					|| (*it)->addendumFileNumber() != addendumExpected
					|| (*it)->pauseFileNumber() != ++pauseExpected ) {
				WARN("validate(): Container " << m_name
					<< " Missing Paused Run File:"
					<< " Expected Mode Index " << modeExpected
					<< ", got " << modeNum << ";"
					<< " Expected File Index " << fileExpected
					<< ", got " << fileNum << ";"
					<< " Expected Pause Index " << pauseExpected
					<< ", got " << (*it)->pauseFileNumber() << ";"
					<< " Expected Addendum Index " << addendumExpected
					<< ", got " << (*it)->addendumFileNumber() << ";"
					<< " [" << (*it)->path() << "]");
				return true;
			}
			last_paused = true;
		}

		// Non-Paused Run File...
		else {
			// Reset Expected Paused File Number for Any Non-Pause File...
			pauseExpected = 0;
			// Changed Paused State...?
			if ( last_paused ) {
				// New Mode Index File Naming Convention Case...
				if ( use_mode_index ) {
					modeExpected++;
					fileExpected = 1; // just to be sure...
				}
				// Former Pre-1.7.0 File Naming Convention Case...
				else {
					fileExpected++;
				}
			}
			if ( (*it)->addendum() ) {
				// Log Here, In Sorted Order, Rather than in importFile()
				DEBUG("validate(): Including ADARA Run Addendum file: "
					<< (*it)->path());
				addendumExpected++;
				fileExpected--;
			} else {
				addendumExpected = 0;
			}
			if ( modeNum != modeExpected
					|| fileNum != fileExpected
					|| (*it)->addendumFileNumber() != addendumExpected
					|| (*it)->pauseFileNumber() != pauseExpected ) {
				WARN("validate(): Container " << m_name
					<< " Missing Run File:"
					<< " Expected Mode Index " << modeExpected
					<< ", got " << modeNum << ";"
					<< " Expected File Index " << fileExpected
					<< ", got " << fileNum << ";"
					<< " Expected Pause Index " << pauseExpected
					<< ", got " << (*it)->pauseFileNumber() << ";"
					<< " Expected Addendum Index " << addendumExpected
					<< ", got " << (*it)->addendumFileNumber() << ";"
					<< " [" << (*it)->path() << "]");
				return true;
			}
			last_paused = false;
			fileExpected++;
		}
	}

	/* TODO validate that the last file has a proper EOF status */
	return false;
}

static bool order_by_filenumber(StorageFile::SharedPtr &a,
				StorageFile::SharedPtr &b)
{
	// Since ADARA/SMS Version 1.7.0:
	// - Sort First by Mode Index...
	// - then by File Index...
	// - then Any Paused Files, by Pause File Number...
	// - then Any Addendum Files by Addendum File Number...
	// (so the Addendum Files can "Resume" Any Preceding Paused State...!)

	// Note: These 3 File Groups are _Not_ Hierarchical,
	// the Paused and Addendum Files are *Siblings*...! ;-D
	// So, when Pause File Numbers are changing, All Addendum Files are 0
	// (and vice versa). Therefore, to get Paused Files listed *First*,
	// we need to give First Priority to the Addendum Files...! :-o
	// (which will All be 0 for the full Paused File sequence... ;-D)
	// Whew! Clear as mud... ;-Q

	// File Names Contain Distinct Mode Index and File Index
	// (SMS After 1.7.0)

	uint32_t modeNumA = a->modeNumber();
	uint32_t fileNumA = a->fileNumber();

	uint32_t modeNumB = b->modeNumber();
	uint32_t fileNumB = b->fileNumber();

	return( ( modeNumA == modeNumB ) ?
		( ( fileNumA == fileNumB ) ?
			( ( a->addendumFileNumber() || b->addendumFileNumber() ) ?
				( a->addendumFileNumber() < b->addendumFileNumber() )
				: ( a->pauseFileNumber() < b->pauseFileNumber() ) )
			: ( fileNumA < fileNumB ) ) :
		( modeNumA < modeNumB ) );
}

bool StorageContainer::validatePath(const std::string &in_path,
				    std::string &out_path, struct timespec &ts,
				    uint32_t &run)
{
	struct tm tm = { 0 };
	const char *p;
	uint32_t ns;
	fs::path fullpath(in_path);
	fs::path cname(fullpath.filename());
	fs::path cpath(fullpath.parent_path().filename());
	cpath /= cname;

	p = strptime(cname.string().c_str(), "%Y%m%d-%H%M%S", &tm);
	if (p && *p == '.') {
		char tmp[16];
		strftime(tmp, sizeof(tmp), "%Y%m%d-%H%M%S", &tm);
		if (strncmp(cname.string().c_str(), tmp, 15)) {
			DEBUG("validatePath():"
				<< " Error Parsing Run/Data Directory Time/Date Stamp: ["
				<< cname.string() << "](0-14) != [" << tmp << "]");
			p = NULL;
		}
	}

	/* If p is not NULL, it points to the period; now get the
	 * nanoseconds and run number, if any.
	 */
	run = 0;
	if (p && !sscanf(p, ".%9u-run-%u", &ns, &run)) {
		DEBUG("validatePath():"
			<< " Error Parsing Nanoseconds & Run Number: [" << p << "]");
		p = NULL;
	}

	/* We've been able to pull out everything so far, now try to
	 * build the expected name and make sure it matches.
	 */
	if (p) {
		char expected[64];
		strftime(expected, sizeof(expected), "%Y%m%d-%H%M%S", &tm);
		snprintf(expected + 15, sizeof(expected) - 15, ".%09u", ns);
		if (run) {
			snprintf(expected + 25, sizeof(expected) - 25,
				 "-run-%u", run);
		}
		if (strcmp(cname.string().c_str(), expected)) {
			DEBUG("validatePath():"
				<< " Error Validating Parsed Run/Data Directory Name: ["
				<< cname.string() << "] != [" << expected << "]");
			p = NULL;
		}
	}

	/* We use the UTC time in the container name, so we cannot use
	 * mktime() without temporarily setting TZ to UTC. Fortunately,
	 * we can use timegm() to deal with that.
	 */
	ts.tv_sec = timegm(&tm);
	ts.tv_nsec = ns;

	out_path = cpath.string();

	return p;
}

StorageContainer::SharedPtr StorageContainer::scan(const std::string &path,
		bool force)
{
	std::string cpath;
	struct timespec ts; // Wallclock Time...!
	uint32_t run;

	if (!validatePath(path, cpath, ts, run)) { // Wallclock Time...!
		WARN("scan(): Invalid storage container at '" << path << "'");
		return StorageContainer::SharedPtr();
	}

	/* If this container was created after we started the scan, it
	 * will be accounted for via normal operations and should be skipped
	 * here. Unless of course we're trying to Re-Scan a run directory,
	 * in which case we should ignore this criteria...! ;-D
	 */
	const timespec &start =
		StorageManager::scanStart(); // Wallclock Time...!
	if ( !force
			&& ( ts.tv_sec > start.tv_sec
				|| ( ts.tv_sec == start.tv_sec
					&& ts.tv_nsec >= start.tv_nsec ) ) ) {
		INFO("scan(): Storage Container at '" << path << "'"
			<< " Created After Scan Start - Ignore...");
		return StorageContainer::SharedPtr();
	}

	StorageContainer::SharedPtr c(new StorageContainer(cpath));
	c->m_weakThis = c;
	c->m_runNumber = run;
	c->m_startTime = ts; // Wallclock Time...!

	fs::directory_iterator end, it(path);
	StorageFile::SharedPtr f;
	bool had_errors = false;

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		if (status.type() != fs::regular_file) {
			WARN("scan(): Ignoring non-file '" << it->path() << "'");
			continue;
		}

		/* Check for flag files that indicate we've been translated
		 * or require manual processing.
		 */
		if (file == m_completed_marker) {
			c->m_translated = true;
			continue;
		}

		if (file == m_manual_marker) {
			c->m_manual = true;
			continue;
		}

		/* Check for ProposalId Marker File, Parse ProposalId if Found! */
		size_t prop_ck = file.string().find(m_proposal_id_marker_prefix);
		if (prop_ck == 0) {
			c->m_propId = file.string().substr(prop_ck
				+ sizeof(m_proposal_id_marker_prefix) + 1);
			DEBUG("scan(): Found ProposalId Marker"
				<< " for Run " << c->m_runNumber << ","
				<< " Set ProposalId for Container to: " << c->m_propId);
			continue;
		}

		if (file.extension() != ".adara") {
			WARN("scan(): Ignoring non-ADARA file '" << it->path() << "'");
			continue;
		}

		/* file holds the full path, but StorageFile wants the path
		 * to be relative to the base directory; we know we have
		 * a structure $BASE/daily/container/file here, so use
		 * the iterators to work our way back.
		 */
		fs::path::iterator rit = it->path().end();
		fs::path rel_path;
		--rit; --rit; --rit;
		rel_path = *rit++;
		rel_path /= *rit++;
		rel_path /= *rit;

		bool saved_file = false;
		uint64_t saved_size = 0;
		f = StorageFile::importFile( c, rel_path.string(),
			saved_file, saved_size );
		if ( f )
			c->m_files.push_back(f);
		else if ( saved_file )
			c->m_saved_size += saved_size;
		else
			had_errors = true;
	}

	c->m_files.sort( order_by_filenumber );

	/* We're a run pending translation -- verify we have all of our
	 * data.
	 */
	if (c->m_runNumber && !c->m_translated)
		had_errors |= c->validate();

	/* We only need to mark this container for manual processing if
	 * it is an untranslated run.
	 */
	if (had_errors && c->m_runNumber && !c->m_translated && !c->m_manual) {
		StorageManager::sendComBus(c->m_runNumber, c->m_propId,
			std::string("Needs Manual Translation"));
		c->markManual();
	}

	return c;
}

uint64_t StorageContainer::purge(const std::string &path, uint64_t goal,
		std::string &propId, bool &path_deleted )
{
	SMSControl *ctrl = SMSControl::getInstance();

	std::string cpath;
	struct timespec ts;
	uint32_t run;

	/* If we're not a valid container, then there's nothing to purge. */
	if (!validatePath(path, cpath, ts, run)) {
		ERROR("Tried to purge invalid storage container at '"
			<< path << "'");
		return 0;
	}

	fs::directory_iterator end, it(path);
	std::list<fs::path> files;
	bool translated = false;
	bool manual = false;

	propId.clear();

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		if (status.type() != fs::regular_file)
			continue;

		/* Check for flag files that indicate we've been translated
		 * or require manual processing.
		 */
		if (file == m_completed_marker) {
			translated = true;
			continue;
		}

		if (file == m_manual_marker) {
			manual = true;
			continue;
		}

		/* Check for ProposalId Marker File, Parse ProposalId if Found! */
		size_t prop_ck = file.string().find(m_proposal_id_marker_prefix);
		if (prop_ck == 0) {
			propId = file.string().substr(prop_ck
				+ sizeof(m_proposal_id_marker_prefix) + 1);
			if ( ctrl->verbose() > 2 ) {
				DEBUG("purge(): Found ProposalId Marker, "
					<< "Set ProposalId for Container to: " << propId);
			}
			continue;
		}

		if (file.extension() != ".adara")
			continue;

		files.push_back(it->path());
	}

	if (manual) {
		if ( ctrl->verbose() > 2 ) {
			DEBUG("Skipping purge of container '" << path << "' (manual)");
		}
		return 0;
	}

	if (run && !translated) {
		if ( ctrl->verbose() > 2 ) {
			DEBUG("Skipping purge of container '" << path
		      	<< "' (untranslated)");
		}
		return 0;
	}

	/* Filenames are f%05u[-run-%u], and are sortable via the default
	 * string sort. The run number portion doesn't change, so file number
	 * will be the key.
	 */
	files.sort();

	std::list<fs::path>::iterator fit, fend = files.end();
	uint64_t size, purged = 0;

	for (fit = files.begin(); purged < goal && fit != fend; ) {
		try {
			size = fs::file_size(*fit);
			remove(*fit);

			size += StorageManager::m_block_size - 1;
			size /= StorageManager::m_block_size;
			purged += size;

			fit = files.erase(fit);
		} catch (fs::filesystem_error err) {
			WARN("Error purging container: " << err.what());
			++fit;
			continue;
		}
	}

	DEBUG("Purged " << purged << " Blocks from Container " << path);

	/* If we removed all of the ADARA files, then also remove the
	 * translation complete marker, the proposal id marker,
	 * any *.BACKUP-pid or *.NEW-pid files from "ADARA Fix" Scripts,
	 * and finally the container directory itself.
	 */
	if (files.empty()) {
		fs::path base(path), completed(path), proposal_id(path);
		completed /= m_completed_marker;
		proposal_id /= m_proposal_id_marker_prefix + propId;

		path_deleted = true;
		try {
			// Remove Run Completed Marker
			if (run)
				remove(completed);
			// Remove Proposal ID Marker
			if (!propId.empty()) {
				remove(proposal_id);
			}
			// Remove Any Raw Data File "BACKUP" Artifacts from:
			// - adara_add_run_end
			// - adara_fix_ipts
			purgeBackups(path);
			// Remove Overall Container Directory
			remove(base);

			if ( ctrl->verbose() > 2 ) {
				DEBUG("Removed container " << base);
			}
		} catch(fs::filesystem_error err) {
			WARN("Error removing container: " << err.what());
			path_deleted = false;
		}
	}
	// Not Done Yet with This Container Path...
	else {
		path_deleted = false;
	}

	return purged;
}

void StorageContainer::purgeBackups(const std::string &path)
{
	SMSControl *ctrl = SMSControl::getInstance();

	fs::directory_iterator end, it(path);
	StorageFile::SharedPtr f;

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		if (status.type() != fs::regular_file) {
			WARN("purgeBackups(): Ignoring non-file '"
				<< it->path() << "'");
			continue;
		}

		/* Check for Raw Data "BACKUP" Files, "${datafile}.BACKUP-$$" */
		size_t backup_ck = file.string().find(".BACKUP-");
		if (backup_ck != std::string::npos) {
			if ( ctrl->verbose() > 2 ) {
				DEBUG("purgeBackups(): Found Raw Data BACKUP File - "
					<< it->path());
			}
			remove(it->path());
			continue;
		}

		/* Check for Raw Data "NEW" Files, "${datafile}.NEW-$$" */
		size_t new_ck = file.string().find(".NEW-");
		if (new_ck != std::string::npos) {
			if ( ctrl->verbose() > 2 ) {
				DEBUG("purgeBackups(): Found Raw Data NEW File - "
					<< it->path());
			}
			remove(it->path());
			continue;
		}
	}
}

uint64_t StorageContainer::openSize(void)
{
	std::list<struct PauseMode>::iterator it;

	uint64_t openSize = 0;

	// Iterate Thru Whole PauseMode Stack...
	// Capture Size of *All* Open Files...
	for ( it = m_pauseModeStack.begin() ;
			it != m_pauseModeStack.end() ; ++it )
	{
		if ( it->m_file )
			openSize += it->m_file->size();
	}

	for ( uint32_t i = 0 ; i < m_ds_input_files.size() ; i++ )
	{
		if ( m_ds_input_files[i] )
			openSize += m_ds_input_files[i]->size();
	}

	return( openSize );
}

