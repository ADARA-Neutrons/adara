
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.PixelMap"));

#include <fstream>
#include <utility>
#include <stdexcept>
#include <string>
#include <iterator>
#include <queue>
#include <map>
#include <set>

#include <stdio.h>
#include <stdint.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "PixelMap.h"
#include "StorageManager.h"

std::auto_ptr<PixelMap::TempMap> PixelMap::readMap(const std::string &path)
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
		std::string msg("Unable to Open Pixel Map File ");
		msg += path;
		throw std::runtime_error(msg);
	}

	INFO("readMap(): Opened Pixel Map File...");

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

		if (sscanf(line.c_str(), "%u %u %u %1s\n",
				&phys, &logical, &bank, trash) != 3) {
			std::string msg("Bad Entry in Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (phys & 0x80000000) {
			std::string msg("Physical PixelId has Error Bit Set "
				"in Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (map->count(phys)) {
			std::string msg("Duplicate Physical PixelId ");
			msg += boost::lexical_cast<std::string>(phys);
			msg += " in Pixel Map File, line ";
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (!m_allowNonOneToOnePixelMapping
				&& output_pixels.count(logical)) {
			std::string msg("Duplicate Logical PixelId ");
			msg += boost::lexical_cast<std::string>(logical);
			msg += " in Pixel Map File, line ";
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (bank == PixelMap::UNMAPPED_BANK) {
			std::string msg("Reserved Bank Id (Unmapped Bank) in "
				"Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		if (bank == PixelMap::ERROR_BANK) {
			std::string msg("Reserved Bank Id (Error Bank) in "
				"Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		// Unused "Gap" in PixelId Space, Mark as Being "Unmapped" Bank...
		if (bank == ((uint32_t) -1))
			bank = PixelMap::UNMAPPED_BANK;

		else if (bank >= 0x10000) {
			std::string msg("Out-of-Range Bank in Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		output_pixels.insert(logical);
		map->insert(make_pair(phys, std::make_pair(logical, bank)));
	}

	INFO("readMap(): Done Reading Pixel Map File."
		<< " [" << ( lineno - 1 ) << " Lines Read.]");

	if (!f.eof()) {
		std::string msg("Read Error in Pixel Map File ");
		msg += path;
		throw std::runtime_error(msg);
	}

	if (!map->size()) {
		std::string msg("No Mapping in Pixel Map File ");
		msg += path;
		throw std::runtime_error(msg);
	}

	return map;
}

boost::shared_array<uint8_t> PixelMap::genAltPacket(TempMap *map,
					      uint32_t &packetSize)
{
	std::queue<uint16_t> sections;
	TempMap::iterator it, end;
	uint32_t payload, expected, bank_count;
	uint32_t tot_pixelid_count;
	uint32_t section_count;
	struct timespec now;
	uint32_t *u32;
	uint16_t i, entries, bank;
	uint16_t max_section_pixelid_count = 0xffff; // 16 bit section count

	DEBUG("genAltPacket() Entry");

	/* We now support Non-One-to-One Pixel Mappings, therefore
	 * we must also avoid using any "Inverse" Mappings, as these
	 * are practically impossible for some beamlines (e.g. HFIR WAND).
	 * Just use physical->logical map "as is" for generating the
	 * pixel map packet.
	 */

	/* NOTE: This "Alternate" Pixel Mapping Table Packet is effectively
	 * a "Mirror Image" of the Original Pixel Mapping Table Packet,
	 * with "Logical" PixelIds now Swapped with "Physical" PixelIds
	 * Everywhere, in a "Inverted" (ha ha) organizational structure. ;-D
	 *
	 * We also pack in One Handy Extra Value _Before_ the Mapping Data,
	 * to Avoid Future Issues - explicitly include the "Number of Banks"!
	 */

	/* We're always going to have at least one section, with the first
	 * physical pixel in it.
	 */
	it = map->begin();
	expected = it->first + 1; // expected next Physical PixelId...
	entries = 1;
	bank = it->second.second;

	tot_pixelid_count = 0;
	section_count = 0;

	for (++it, end = map->end(); it != end; ++it) {
		/* If we've found a discontinuity in the physical pixels,
		 * or we changed banks, OR we have _Filled Up_ this section
		 * with the Max Section PixelId Count (16 bits, 0xffff = 65535),
		 * then we have to start a new section.
		 */
		if (it->first != expected || it->second.second != bank
				|| entries >= max_section_pixelid_count) {
			sections.push(entries);
			// Skip Unmapped Banks...! Don't Count/Send Unmapped PixelIds...
			// No Need to Send These Over the Wire!
			if ( bank != (uint16_t) -1 ) {
				tot_pixelid_count += entries;
				section_count++;
			}
			entries = 0;
			bank = it->second.second;
		}
		entries++;
		expected = it->first + 1; // expected next Physical PixelId...
	}

	/* Push the last section we were working on. */
	sections.push(entries);
	// Skip Unmapped Banks...! No Need to Send These Over the Wire!
	if ( bank != (uint16_t) -1 ) {
		tot_pixelid_count += entries;
		section_count++;
	}

	DEBUG("section_count=" << section_count);
	DEBUG("sections.size()=" << sections.size());
	DEBUG("tot_pixelid_count=" << tot_pixelid_count);
	DEBUG("map->size()=" << map->size());

	/* Now, build the packet; we have enough information to calculate
	 * its size.
	 */
	payload = section_count * ( sizeof(uint32_t) + (2 * sizeof(uint16_t)) );
	DEBUG("payload(sections)=" << payload);
	payload += tot_pixelid_count * sizeof(uint32_t);
	DEBUG("payload(tot_pixelid_count)=" << payload);
	payload += sizeof(uint32_t); // for Explicit "Number of Banks"! ;-D
	DEBUG("payload(numbanks)=" << payload);
	packetSize = payload + sizeof(ADARA::Header);
	DEBUG("packetSize=" << packetSize);

	boost::shared_array<uint8_t> pkt(new uint8_t[packetSize]);

	clock_gettime(CLOCK_REALTIME, &now);

	u32 = (uint32_t *) pkt.get();
	*u32++ = payload;
	*u32++ = ADARA_PKT_TYPE(
		ADARA::PacketType::PIXEL_MAPPING_ALT_TYPE,
		ADARA::PacketType::PIXEL_MAPPING_ALT_VERSION );
	*u32++ = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	*u32++ = now.tv_nsec;

	*u32++ = (m_numBanks - 1); // Note: Artificially Incremented By 1...!

	it = map->begin();
	while ( !sections.empty() ) {
		entries = sections.front();
		sections.pop();

		// Skip Unmapped Banks...! No Need to Send These Over the Wire!
		if ( it->second.second == (uint16_t) -1 ) {
			std::advance( it, entries );
			continue;
		}

		/* First the header (base physical ID, bankid, count) */
		bank_count = (uint32_t) it->second.second << 16;
		bank_count |= entries;

		*u32++ = it->first;
		*u32++ = bank_count;

		/* Then the entries (logical id) */
		for (i = 0; i < entries; ++it, ++i) {
			*u32++ = it->second.first;
		}
	}

	//DEBUG("Resulting Size: u32=" << std::hex << u32
		//<< " Starting Payload: pkt.get()=" << ((uint32_t *) pkt.get())
		//<< " Effective Size: (4 * (u32 - pkt.get()))="
		//<< std::dec << ( 4 * ( u32 - ((uint32_t *) pkt.get()) ) ) );

	DEBUG("Done");

	return pkt;
}

boost::shared_array<uint8_t> PixelMap::genPacket(TempMap *map,
					      uint32_t &packetSize)
{
	std::queue<uint16_t> sections;
	TempMap inverted;
	TempMap::iterator it, end;
	uint32_t phys, logical, payload, expected, bank_count;
	struct timespec now;
	uint32_t *u32;
	uint16_t i, entries, bank;
	uint16_t max_section_pixelid_count = 0xffff; // 16 bit section count

DEBUG("genPacket() Entry");

	/* A physical->logical map is better for parsing and for building
	 * the lookup table used for normal operations, but going logical
	 * to physical is better for generating the pixel map packet.
	 * Since we guarantee a one-to-one mapping during load, we don't
	 * have to worry about duplicate keys here.
	 */
	// NOTE: _Can_ Break for "m_allowNonOneToOnePixelMapping == true" case!!
	// -> Ordering of Banks in Map can "Cover Up" some Bank Numbers...!
	// -> Where Possible going forward, Use "PixelMappingAltPkt"...! ;-D
	// -> See Trac Ticket #1043, for HFIR WAND/HB2C...
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
	expected = it->first + 1; // expected next Logical PixelId...
	entries = 1;
	bank = it->second.second;

	for (++it, end = inverted.end(); it != end; ++it) {
		/* If we've found a discontinuity in the logical pixels,
		 * or we changed banks, OR we have _Filled Up_ this section
		 * with the Max Section PixelId Count (16 bits, 0xffff = 65535),
		 * then we have to start a new section.
		 */
		if (it->first != expected || it->second.second != bank
				|| entries >= max_section_pixelid_count) {
			sections.push(entries);
			entries = 0;
			bank = it->second.second;
		}
		entries++;
		expected = it->first + 1; // expected next Logical PixelId...
	}

	/* Push the last section we were working on. */
	sections.push(entries);

DEBUG("sections.size()=" << sections.size());

	/* Now, build the packet; we have enough information to calculate
	 * its size.
	 */
	payload = sections.size() * (sizeof(uint32_t) + 2 * sizeof(uint16_t));
	payload += inverted.size() * sizeof(uint32_t);
DEBUG("payload=" << payload);
	packetSize = payload + sizeof(ADARA::Header);
DEBUG("packetSize=" << packetSize);

	boost::shared_array<uint8_t> pkt(new uint8_t[packetSize]);

	clock_gettime(CLOCK_REALTIME, &now);

	u32 = (uint32_t *) pkt.get();
	*u32++ = payload;
	*u32++ = ADARA_PKT_TYPE(
		ADARA::PacketType::PIXEL_MAPPING_TYPE,
		ADARA::PacketType::PIXEL_MAPPING_VERSION );
	*u32++ = now.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	*u32++ = now.tv_nsec;

	it = inverted.begin();
	while ( !sections.empty() ) {
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

DEBUG("Done");

	return pkt;
}

PixelMap::PixelMap(const std::string &path,
		bool allowNonOneToOnePixelMapping, bool useOrigPixelMappingPkt)
	: m_allowNonOneToOnePixelMapping(allowNonOneToOnePixelMapping),
	m_useOrigPixelMappingPkt(useOrigPixelMappingPkt),
	m_numBanks(0)
{
	std::auto_ptr<TempMap> map;
	TempMap::iterator it, end;
	std::set<uint32_t> banks;
	uint32_t max_logical = 0;
	uint32_t max_phys = 0;
	uint32_t i;

	INFO("Entry PixelMap(): m_allowNonOneToOnePixelMapping="
		<< m_allowNonOneToOnePixelMapping);

	map = readMap(path);

	end = map->end();
	for (it = map->begin(); it != end; ++it) {
		if (it->first > max_phys)
			max_phys = it->first;
		if (it->second.first != ((uint32_t) -1)
				&& it->second.first > max_logical) {
			max_logical = it->second.first;
		}
		if (it->second.second != PixelMap::UNMAPPED_BANK
				&& it->second.second > m_numBanks) {
			m_numBanks = it->second.second;
		}
	}

	INFO("Pixel Map Stats:"
		<< " max_phys=" << max_phys
		<< " max_logical=" << max_logical
		<< " m_numBanks=" << m_numBanks);

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
	for (i = 0; i <= max_phys; ++i) {
		m_table.push_back(
			std::make_pair( i | 0x80000000,
				(uint16_t) PixelMap::UNMAPPED_BANK ) );
	}

	/* While we're at it, _Also_ create a "Logical-to-Bank" lookup vector,
	 * for the case where a Data Source has _Already_ mapped the PixelId...
	 */
	m_banks.reserve(max_logical + 1);
	for (i = 0; i <= max_logical; ++i) {
		m_banks.push_back( PixelMap::UNMAPPED_BANK );
	}

	for (it = map->begin(); it != end; ++it) {
		m_table[it->first] = it->second;
		if ( it->second.first != ((uint32_t) -1) ) {
			m_banks[it->second.first] = it->second.second;
		}
	}

	if ( m_useOrigPixelMappingPkt )
		m_packet = genPacket(map.get(), m_packetSize);
	else
		m_packet = genAltPacket(map.get(), m_packetSize);

	m_connection = StorageManager::onPrologue(
				boost::bind(&PixelMap::onPrologue, this, _1));

	INFO("Done with Pixel Map.");
}

PixelMap::~PixelMap()
{
	m_connection.disconnect();
}

void PixelMap::onPrologue( bool UNUSED(capture_last) )
{
	StorageManager::addPrologue(m_packet.get(), m_packetSize);
}
