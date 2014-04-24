#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <stdexcept>
#include <queue>
#include <map>
#include <set>
#include <stdio.h>
#include <stdint.h>

#include "ADARA.h"
#include "PixelMap.h"
#include "StorageManager.h"

typedef std::map<uint32_t, PixelMap::Entry> TempMap;

static std::auto_ptr<TempMap> readMap(const std::string &path)
{
	std::auto_ptr<TempMap> map(new TempMap);
	std::set<uint32_t> output_pixels;
	std::ifstream f(path.c_str());
	std::string line;
	uint32_t phys, logical, bank;
	int lineno = 0;
	char trash[2];
	size_t pos;

	if (f.fail()) {
		std::string msg("Unable to open pixel map file ");
		msg += path;
		throw std::runtime_error(msg);
	}

	for (;;) {
		lineno++;
		getline(f, line);
		if (f.fail())
			break;

		/* Truncate at the comment character */
		pos = line.find_first_of("#");
		if (pos != std::string::npos)
			line.resize(pos);

		/* Skip blank lines */
		pos = line.find_first_not_of(" \t");
		if (pos == std::string::npos)
			continue;

		if (sscanf(line.c_str(), "%u %u %u %1s\n", &phys, &logical,
							&bank, trash) != 3) {
			std::string msg("Bad entry in pixel map, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (phys & 0x80000000) {
			std::string msg("Physical pixel has error bit set "
					"in pixel map, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (map->count(phys)) {
			std::string msg("Duplicate physical pixel ");
			msg += boost::lexical_cast<std::string>(phys);
			msg += " in pixel map, line ";
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (output_pixels.count(logical)) {
			std::string msg("Duplicate logical pixel ");
			msg += boost::lexical_cast<std::string>(logical);
			msg += " in pixel map, line ";
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (bank >= 0x10000) {
			std::string msg("Out-of-range bank in pixel map, "
					"line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (bank == 0xffff) {
			std::string msg("Reserved bank id (unmapped) in "
					"pixel map, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (bank == 0xfffe) {
			std::string msg("Reserved bank id (error bank) in "
					"pixel map, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		output_pixels.insert(logical);
		map->insert(make_pair(phys, std::make_pair(logical, bank)));
	}

	if (!f.eof()) {
		std::string msg("Read error in pixel map file ");
		msg += path;
		throw std::runtime_error(msg);
	}

	if (!map->size()) {
		std::string msg("No mapping in pixel map file ");
		msg += path;
		throw std::runtime_error(msg);
	}

	return map;
}

static boost::shared_array<uint8_t> genPacket(TempMap *map,
					      uint32_t &packetSize)
{
	std::queue<uint16_t> sections;
	TempMap inverted;
	TempMap::iterator it, end;
	uint32_t phys, logical, payload, expected, bank_count;
	struct timespec now;
	uint32_t *u32;
	uint16_t i, entries, bank;

	/* A physical->logical map is better for parsing and for building
	 * the lookup table used for normal operations, but going logical
	 * to physical is better for generating the pixel map packet.
	 * Since we guarantee a one-to-one mapping  during load, we don't
	 * have to worry about duplicate keys here.
	 */
	for (it = map->begin(), end = map->end(); it != end; ++it) {
		phys = it->first;
		logical = it->second.first;
		bank = it->second.second;
		inverted.insert(make_pair(logical, std::make_pair(phys, bank)));
	}

	/* We're always going to have at least one section, with the first
	 * logical pixel in it.
	 */
	it = inverted.begin();
	expected = it->first + 1;
	entries = 1;
	bank = it->second.second;

	for (++it, end = inverted.end(); it != end; ++it) {
		/* If we've found a discontinuity in the logical pixels,
		 * or we changed banks, then we have to start a new section.
		 */
		if (it->first != expected || it->second.second != bank) {
			sections.push(entries);
			entries = 0;
			bank = it->second.second;
		}
		entries++;
		expected = it->first + 1;
	}

	/* Push the last section we were working on. */
	sections.push(entries);

	/* Now, build the packet; we have enough information to calculate
	 * its size.
	 */
	payload = sections.size() * (sizeof(uint32_t) + 2 * sizeof(uint16_t));
	payload += inverted.size() * sizeof(uint32_t);
	packetSize = payload + sizeof(ADARA::Header);

	boost::shared_array<uint8_t> pkt(new uint8_t[packetSize]);

	clock_gettime(CLOCK_REALTIME, &now);

	u32 = (uint32_t *) pkt.get();
	*u32++ = payload;
	*u32++ = ADARA::PacketType::PIXEL_MAPPING_V0;
	*u32++ = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	*u32++ = now.tv_nsec;

	it = inverted.begin();
	while (!sections.empty()) {
		entries = sections.front();
		sections.pop();

		/* First the header (base logical ID, bankid, count) */
		bank_count = (uint32_t) it->second.second << 16;
		bank_count |= entries;

		*u32++ = it->first;
		*u32++ = bank_count;

		/* Then the entries (physical id) */
		for (i = 0; i < entries; ++it, ++i)
			*u32++ = it->second.first;
	}

	return pkt;
}

PixelMap::PixelMap(const std::string &path) : m_numBanks(0)
{
	std::auto_ptr<TempMap> map;
	TempMap::iterator it, end;
	std::set<uint32_t> banks;
	uint32_t i, max_phys = 0;

	map = readMap(path);

	end = map->end();
	for (it = map->begin(); it != end; ++it) {
		if (it->first > max_phys)
			max_phys = it->first;
		if (it->second.second > m_numBanks)
			m_numBanks = it->second.second;
	}

	/* Count from zero to the largest bank, that's how many slots will
	 * be needed.
	 */
	m_numBanks++;

	/* It's easier to parse into a std::map, but a vector is 100x faster
	 * for lookups in the tests I've performed. No real surprise there...
	 *
	 * First we populate with the "unmapped pixel" mapping, then fill
	 * in the valid entries.
	 */
	m_table.reserve(max_phys + 1);
	for (i = 0; i < max_phys; ++i) {
		m_table.push_back(std::make_pair(i | 0x80000000,
						 (uint16_t) ~0));
	}

	for (it = map->begin(); it != end; ++it)
		m_table[it->first] = it->second;

	m_packet = genPacket(map.get(), m_packetSize);

	m_connection = StorageManager::onPrologue(
				boost::bind(&PixelMap::onPrologue, this));

}

PixelMap::~PixelMap()
{
	m_connection.disconnect();
}

void PixelMap::onPrologue(void)
{
	StorageManager::addPrologue(m_packet.get(), m_packetSize);
}
