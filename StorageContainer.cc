#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <stdexcept>

#include "StorageContainer.h"
#include "StorageManager.h"
#include "ADARA.h"

#define CONTAINER_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP)

StorageContainer::StorageContainer(const struct timespec &start,
				   uint32_t run) :
	m_startTime(start), m_runNumber(run)
{
	char path[64];
	struct tm tm;

	if (!gmtime_r(&m_startTime.tv_sec, &tm))
		throw ADARA::Exception(ERANGE, "StorageContainer gmtime_r");

	if (!strftime(path, sizeof(path), "%Y%m%d", &tm))
		throw ADARA::Exception(EINVAL, "StorageContainer base strftime");

	if (mkdirat(StorageManager::base_fd(), path, CONTAINER_MODE) &&
							errno != EEXIST)
		throw ADARA::Exception(errno, "StorageContainer base mkdirat");

	if (!strftime(path, sizeof(path), "%Y%m%d/%Y%m%d-%H%M%S", &tm))
		throw ADARA::Exception(EINVAL, "StorageContainer strftime");

	m_name = path;

	snprintf(path, sizeof(path), ".%09lu", m_startTime.tv_nsec);
	m_name += path;

	if (m_runNumber) {
		snprintf(path, sizeof(path), "-run-%u", m_runNumber);
		m_name += path;
	}

	if (mkdirat(StorageManager::base_fd(), m_name.c_str(), CONTAINER_MODE))
		throw ADARA::Exception(ERANGE, "StorageContainer mkdirat");
}

StorageContainer::StorageContainer(const std::string &name)
{
	/* TODO Parse name string to start time/run number; open the
	 * directory and count the number of files in it.
	 */
	throw std::runtime_error("not implemented");
}
