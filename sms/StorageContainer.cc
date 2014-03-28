#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <stdexcept>

#include <boost/filesystem.hpp>

#include "StorageContainer.h"
#include "StorageManager.h"
#include "StorageFile.h"
#include "ADARA.h"

#include "Logging.h"

namespace fs = boost::filesystem;

static LoggerPtr logger(Logger::getLogger("SMS.StorageContainer"));

#define CONTAINER_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP)
#define MARKER_MODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)

const char *StorageContainer::m_completed_marker = "translation_completed";
const char *StorageContainer::m_manual_marker = "manual_processing_needed";


void StorageContainer::terminateFile(void)
{
	ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;
	if (m_runNumber) {
		status = m_active ? ADARA::RunStatus::RUN_EOF :
				    ADARA::RunStatus::END_RUN;
	}
	m_cur_file->terminate(status);
	StorageManager::addBaseStorage(m_cur_file->size());
	m_cur_file.reset();
}

void StorageContainer::newFile(void)
{
	if (m_cur_file)
		return;

	ADARA::RunStatus::Enum status = ADARA::RunStatus::NO_RUN;

	if (m_runNumber) {
		status = ADARA::RunStatus::NEW_RUN;
		if (m_numFiles)
			status = ADARA::RunStatus::RUN_BOF;
	}

	m_cur_file = StorageFile::newFile(m_weakThis, ++m_numFiles, status);
	m_files.push_back(m_cur_file);

	/* Tell the storage manager about the new file so we can
	 * add the prologue before anyone else sees it.
	 */
	StorageManager::fileCreated(m_cur_file);
	m_newFile(m_cur_file);
}

off_t StorageContainer::write(IoVector &iovec, uint32_t len, bool notify)
{
	/* We don't immediately close a file when we exceed the size limit
	 * in order to avoid creating a new file just for the end-of-run
	 * marker. Instead, we wait for the run to start or the next packet
	 * to be written (and clients notified).
	 */
	if (m_cur_file && m_cur_file->oversize())
		terminateFile();

	if (!m_cur_file)
		newFile();

	return m_cur_file->write(iovec, len, notify);
}

void StorageContainer::terminate(void)
{
	m_active = false;
	if (m_cur_file)
		terminateFile();
}

void StorageContainer::notify(void)
{
	if (m_cur_file)
		m_cur_file->notify();
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
	 * STS if we restart before it is purged.
	 */
	if (createMarker(m_completed_marker))
		ERROR("Run " << m_runNumber << " will be resent if SMS "
		      "restarts. ");
	m_translated = true;
}

void StorageContainer::markManual(void)
{
	/* Mark this container as needing manual processing.
	 */
	if (createMarker(m_manual_marker))
		ERROR("Run " << m_runNumber << " will be resent if SMS "
		      "restarts. ");
	m_manual = true;
}

StorageContainer::StorageContainer(const struct timespec &start,
				   uint32_t run) :
	m_startTime(start), m_runNumber(run), m_numFiles(0), m_active(true),
	m_translated(false), m_manual(false)
{
}

StorageContainer::StorageContainer(const std::string &name) :
	m_runNumber(0), m_numFiles(0), m_name(name), m_active(false),
	m_translated(false), m_manual(false)
{
}

StorageContainer::SharedPtr StorageContainer::create(
				const struct timespec &start, uint32_t run)
{
	char path[64];
	struct tm tm;

	if (!gmtime_r(&start.tv_sec, &tm))
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

	StorageContainer::SharedPtr c(new StorageContainer(start, run));
	c->m_weakThis = c;
	c->m_name = path;

	snprintf(path, sizeof(path), ".%09lu", start.tv_nsec);
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

	return total;
}

bool StorageContainer::validate(void)
{
	if (m_files.empty()) {
		WARN("Container " << m_name << " has no data files");
		return true;
	}

	std::list<StorageFile::SharedPtr>::iterator it, end;
	it = m_files.begin();
	end = m_files.end();
	for (uint32_t expected = 1; it != end; ++expected, ++it) {
		if ((*it)->fileNumber() != expected) {
			WARN("Container " << m_name << " missing file number "
			     << expected);
			return true;
		}
	}

	/* TODO validate that the last file has a proper EOF status */
	return false;
}

static bool order_by_filenumber(StorageFile::SharedPtr &a,
				StorageFile::SharedPtr &b)
{
	return a->fileNumber() < b->fileNumber();
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
		if (strncmp(cname.string().c_str(), tmp, 15))
			p = NULL;
	}

	/* If p is not NULL, it points to the period; now get the
	 * nanoseconds and run number, if any.
	 */
	run = 0;
	if (p && !sscanf(p, ".%9u-run-%u", &ns, &run))
		p = NULL;

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
		if (strcmp(cname.string().c_str(), expected))
			p = NULL;
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

StorageContainer::SharedPtr StorageContainer::scan(const std::string &path)
{
	std::string cpath;
	struct timespec ts;
	uint32_t run;

	if (!validatePath(path, cpath, ts, run)) {
		WARN("Invalid storage container at '" << path << "'");
		return StorageContainer::SharedPtr();
	}

	/* If this container was created after we started the scan, it
	 * will be accounted for via normal operations and should be skipped
	 * here.
	 */
	const timespec &start = StorageManager::scanStart();
	if (ts.tv_sec > start.tv_sec || (ts.tv_sec == start.tv_sec &&
						ts.tv_nsec >= start.tv_nsec))
		return StorageContainer::SharedPtr();

	StorageContainer::SharedPtr c(new StorageContainer(cpath));
	c->m_weakThis = c;
	c->m_runNumber = run;
	c->m_startTime = ts;

	fs::directory_iterator end, it(path);
	StorageFile::SharedPtr f;
	bool had_errors = false;;

	for (; it != end; ++it) {
		fs::path file(it->path().filename());
		fs::file_status status = it->status();

		if (status.type() != fs::regular_file) {
			WARN("Ignoring non-file '" << it->path() << "'");
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

		if (file.extension() != ".adara") {
			WARN("Ignoring non-ADARA file '" << it->path() << "'");
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

		f = StorageFile::importFile(c, rel_path.string());
		if (f)
			c->m_files.push_back(f);
		else
			had_errors = true;
	}

	c->m_files.sort(order_by_filenumber);

	/* We're a run pending translation -- verify we have all of our
	 * data.
	 */
	if (c->m_runNumber && !c->m_translated)
		had_errors |= c->validate();

	/* We only need to mark this container for manual processing if
	 * it is an untranslated run.
	 */
	if (had_errors && c->m_runNumber && !c->m_translated && !c->m_manual)
		c->markManual();

	return c;
}

uint64_t StorageContainer::purge(const std::string &path, uint64_t goal,
				 bool keep)
{
	std::string cpath;
	struct timespec ts;
	uint32_t run;

	/* If we're not a valid container, then there's nothing to purge. */
	if (!validatePath(path, cpath, ts, run))
		return 0;

	fs::directory_iterator end, it(path);
	std::list<fs::path> files;
	bool translated = false;
	bool manual = false;

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

		if (file.extension() != ".adara")
			continue;

		files.push_back(it->path());
	}

	if (manual) {
		DEBUG("Skipping purge of container '" << path << "' (manual)");
		return 0;
	}

	if (run && !translated) {
		DEBUG("Skipping purge of container '" << path
		      << "' (untranslated)");
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

	DEBUG("Purged " << purged << " blocks from container " << path);

	/* If we removed all of the ADARA files, and are not being asked to
	 * keep the container around, remove the translation complete marker
	 * and the container directory.
	 */
	if (!keep && files.empty()) {
		fs::path base(path), completed(path);
		completed /= m_completed_marker;

		try {
			if (run)
				remove(completed);
			remove(base);

			DEBUG("Removed container " << base);
		} catch(fs::filesystem_error err) {
			WARN("Error removing container: " << err.what());
		}
	}

	return purged;
}
