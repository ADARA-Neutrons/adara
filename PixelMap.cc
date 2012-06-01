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
	TempMap::iterator it, end = map->end();
	uint32_t payload, expected;
	struct timespec now;
	uint32_t *u32;
	uint16_t *u16, entries, i;

	/* We're always going to have at least one section, with the first
	 * physical pixel in it.
	 */
	it = map->begin();
	expected = it->first + 1;
	entries = 1;

	for (++it; it != end; ++it) {
		if (it->first != expected) {
			sections.push(entries);
			entries = 0;
		}
		entries++;
		expected = it->first + 1;
	}

	sections.push(entries);

	payload = sections.size() + map->size();
	payload *= sizeof(uint32_t) + sizeof(uint16_t);
	payload += 3;
	payload &= ~3;
	packetSize = payload + sizeof(ADARA::Header);

	boost::shared_array<uint8_t> pkt(new uint8_t[packetSize]);

	clock_gettime(CLOCK_REALTIME, &now);

	/* First, zero out the last 4 bytes of the packet to avoid leaving
	 * garbage if we're not a mulitple of 4, then build up the
	 * packet a section at a time.
	 */
	u32 = (uint32_t *) pkt.get();
	u32[(packetSize / 4) - 1] = 0;

	*u32++ = payload;
	*u32++ = ADARA::PacketType::PIXEL_MAPPING_V0;
	*u32++ = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	*u32++ = now.tv_nsec;

	it = map->begin();
	while (!sections.empty()) {
		entries = sections.front();
		sections.pop();

		/* First the six byte header (base phys ID + count) */
		*u32++ = it->first;
		u16 = (uint16_t *) u32;
		*u16++ = entries;

		/* Then the entries (logical id + bank id) */
		for (i = 0; i < entries; ++it, ++i) {
			u32 = (uint32_t *) u16;
			*u32++ = it->second.first;
			u16 = (uint16_t *) u32;
			*u16++ = it->second.second;
		}

		u32 = (uint32_t *) u16;
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
