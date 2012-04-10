#ifndef __STORAGE_CONTAINER_H
#define __STORAGE_CONTAINER_H

#include <time.h>
#include <stdint.h>
#include <string>

class StorageContainer {
public:
	const struct timespec &startTime(void) const { return m_startTime; }
	uint64_t runNumber (void) const { return m_runNumber; }
	uint32_t numFiles (void) const { return m_numFiles; }
	const std::string &name(void) const { return m_name; }

private:
	struct timespec m_startTime;
	uint32_t m_runNumber;
	uint32_t m_numFiles;
	std::string m_name;

	StorageContainer(const struct timespec &start, uint32_t run);
	StorageContainer(const std::string &name);

	friend class StorageManager;
};

#endif /* __STORAGE_CONTAINER_H */
