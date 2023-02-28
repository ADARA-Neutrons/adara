#ifndef __PIXEL_MAP_H
#define __PIXEL_MAP_H

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/signals2.hpp>
#include <stdint.h>
#include <utility>
#include <vector>
#include <string>

#include <stdint.h>

class PixelMap : boost::noncopyable {
public:
	typedef std::pair<uint32_t, uint16_t> Entry; // ( logical, bank )
	typedef std::vector<Entry> Table;
	typedef std::map<uint32_t, PixelMap::Entry> TempMap;
		// ( physical, Entry=( logical, bank ) )

	PixelMap(const std::string &path,
		bool allowNonOneToOnePixelMapping,
		bool useOrigPixelMappingPkt);
	~PixelMap();

	uint32_t numBanks(void) const { return m_numBanks; }

	// "Special" Bank Indices...
	enum { UNMAPPED_BANK = 0xffff, ERROR_BANK = 0xfffe };

	// In Transit, We Use Bank Indices 0 and 1 to Store, Respectively:
	//    - Error Bank (-2 = 0xfffe)
	//    - Unknown Mapping Bank (-1 = 0xffff)
	// So account for the Bank Index where the Real Detector Banks start...
	// (This better equal the number of "Special" Bank Indices...!)
	enum { REAL_BANK_OFFSET = 2 };

	bool mapEvent(uint32_t physical, uint32_t &logical, uint16_t &bank) {
		if (physical < m_table.size()) {
			logical = m_table[physical].first;
			bank = m_table[physical].second;
		} else {
			logical = physical | 0x80000000;
			bank = UNMAPPED_BANK;
		}

		/* Return true if this pixel wasn't mapped */
		return( bank == UNMAPPED_BANK );
	}

	bool mapEventBank(uint32_t logical, uint16_t &bank) {
		if (logical < m_banks.size()) {
			bank = m_banks[logical];
		} else {
			bank = UNMAPPED_BANK;
		}

		/* Return true if this pixel wasn't mapped */
		return( bank == UNMAPPED_BANK );
	}

private:
	std::auto_ptr<TempMap> readMap(const std::string &path);

	boost::shared_array<uint8_t> genAltPacket(TempMap *map,
		uint32_t &packetSize);

	boost::shared_array<uint8_t> genPacket(TempMap *map,
		uint32_t &packetSize);

	Table m_table;
	std::vector<uint16_t> m_banks;
	boost::shared_array<uint8_t> m_packet;
	uint32_t m_packetSize;
	bool m_allowNonOneToOnePixelMapping;
	bool m_useOrigPixelMappingPkt;
	uint32_t m_numBanks;
	boost::signals2::connection m_connection;

	void onPrologue( bool capture_last );
};

#endif /* __PIXEL_MAP_H */
