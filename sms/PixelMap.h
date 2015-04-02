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
	typedef std::pair<uint32_t, uint16_t> Entry;
	typedef std::vector<Entry> Table;

	PixelMap(const std::string &path);
	~PixelMap();

	uint32_t numBanks(void) const { return m_numBanks; }

	bool mapEvent(uint32_t phys, uint32_t &logical, uint16_t &bank) {
		if (phys < m_table.size()) {
			logical = m_table[phys].first;
			bank = m_table[phys].second;
		} else {
			logical = phys | 0x80000000;
			bank = 0xffff;
		}

		/* Return true if this pixel wasn't mapped */
		return bank == 0xffff;
	}

	bool mapEventBank(uint32_t logical, uint16_t &bank) {
		if (logical < m_banks.size()) {
			bank = m_banks[logical];
		} else {
			bank = 0xffff;
		}

		/* Return true if this pixel wasn't mapped */
		return bank == 0xffff;
	}

private:
	Table m_table;
	std::vector<uint16_t> m_banks;
	boost::shared_array<uint8_t> m_packet;
	uint32_t m_packetSize;
	uint32_t m_numBanks;
	boost::signals2::connection m_connection;

	void onPrologue(void);
};

#endif /* __PIXEL_MAP_H */
