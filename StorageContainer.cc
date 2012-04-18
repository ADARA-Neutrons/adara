#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <stdexcept>

#include "StorageContainer.h"
#include "StorageManager.h"
#include "StorageFile.h"
#include "ADARA.h"

#define CONTAINER_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP)

StorageContainer::StorageContainer(const struct timespec &start,
				   uint32_t run) :
	m_startTime(start), m_runNumber(run), m_numFiles(0), m_active(true)
{
	char path[64];
	struct tm tm;

	if (!gmtime_r(&m_startTime.tv_sec, &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " gmtime_r failed");

	if (!strftime(path, sizeof(path), "%Y%m%d", &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " base strftime failed");

	if (mkdirat(StorageManager::base_fd(), path, CONTAINER_MODE) &&
							errno != EEXIST) {
		int err = errno;
		std::string msg("StorageContainer::StorageContainer(): "
				"base mkdirat error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}

	if (!strftime(path, sizeof(path), "%Y%m%d/%Y%m%d-%H%M%S", &tm))
		throw std::runtime_error("StorageContainer::StorageContainer()"
					 " path strftime failed");

	m_name = path;

	snprintf(path, sizeof(path), ".%09lu", m_startTime.tv_nsec);
	m_name += path;

	if (m_runNumber) {
		snprintf(path, sizeof(path), "-run-%u", m_runNumber);
		m_name += path;
	}

	if (mkdirat(StorageManager::base_fd(), m_name.c_str(),
							CONTAINER_MODE)) {
		int err = errno;
		std::string msg("StorageContainer::StorageContainer(): "
				"container mkdirat error: ");
		msg += strerror(err);
		throw std::runtime_error(msg);
	}
}

StorageContainer::StorageContainer(const std::string &name)
{
	/* TODO Parse name string to start time/run number; open the
	 * directory and count the number of files in it.
	 */
	throw std::runtime_error("not implemented");
}

void StorageContainer::terminateFile(void)
{
	m_cur_file->terminate(m_runNumber ?  ADARA::RunStatus::END_RUN :
					     ADARA::RunStatus::NO_RUN);
	StorageManager::addBaseStorage(m_cur_file->size());
	m_cur_file.reset();
}

off_t StorageContainer::write(const void *data, uint32_t count, bool notify)
{
	/* We don't immediately close a file when we exceed the size limit
	 * in order to avoid creating a new file just for the end-of-run
	 * marker. Instead, we wait for the run to start or the next packet
	 * to be written (and clients notified).
	 */
	if (m_cur_file && m_cur_file->oversize())
		terminateFile();

	if (!m_cur_file) {
		ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;

		if (m_runNumber) {
			status = ADARA::RunStatus::NEW_RUN;
			if (m_numFiles)
				status = ADARA::RunStatus::RUN_BOF;
		}

		m_cur_file.reset(new StorageFile(*this, ++m_numFiles,
						 true, status));
		m_files.push_back(m_cur_file);
		m_newFile(m_cur_file);
	}

	return m_cur_file->write(data, count, notify);
}

void StorageContainer::terminate(void)
{
	m_active = false;
	if (m_cur_file)
		terminateFile();
}

void StorageContainer::getFiles(std::list<FileSharedPtr> &list)
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
