
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
#include <string.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "PixelMap.h"
#include "SMSControl.h"
#include "StorageManager.h"

std::auto_ptr<PixelMap::TempMap> PixelMap::readMap(const std::string &path)
{
	std::auto_ptr<TempMap> map(new TempMap);

	std::set<uint32_t> output_pixels;

	std::ifstream f(path.c_str());

	std::string line;

	uint32_t logical_start, logical_stop;
	int32_t logical_step;

	uint32_t physical_start, physical_stop;
	int32_t physical_step;

	uint32_t physical, logical, bank;

	int lineno = 0;

	char physical_buf[255];
	char logical_buf[255];
	char trash[2] = "";

	size_t pos;

	if (f.fail()) {
		std::string msg("Unable to Open Pixel Map File ");
		msg += path;
		throw std::runtime_error(msg);
	}

	SMSControl *ctrl = SMSControl::getInstance();

	INFO("readMap(): Opened Pixel Map File...");

	for (;;) {

		lineno++;
		getline(f, line);
		if (f.fail())
			break;

		// Truncate at the comment character
		pos = line.find_first_of("#");
		if (pos != std::string::npos)
			line.resize(pos);

		// Skip blank lines
		pos = line.find_first_not_of(" \t");
		if (pos == std::string::npos)
			continue;

		// Parse Line Into Physical and Logical Specifications, Plus Bank
		// (and Any "Trash" Characters at the End of the Line...)
		if (sscanf(line.c_str(), "%s %s %u %1s\n",
				physical_buf, logical_buf, &bank, trash) != 3) {
			std::string msg("Bad Entry in Pixel Map File, line ");
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}

		// Parse Any Closed-Form Physical PixelId Specification
		if ( strstr(physical_buf, ":") != NULL ) {
			if (sscanf(physical_buf, "%u:%u:%d",
					&physical_start, &physical_stop, &physical_step) != 3) {
				std::string msg("Bad Physical PixelId Entry Specification");
				msg += " in Pixel Map File, line ";
				msg += boost::lexical_cast<std::string>(lineno);
				msg += " [";
				msg += physical_buf;
				msg += "]";
				throw std::runtime_error(msg);
			}
			if ( ctrl->verbose() > 0 ) {
				DEBUG("readMap(): Parsed"
					<< " physical_start=" << physical_start
					<< " physical_stop=" << physical_stop
					<< " physical_step=" << physical_step);
			}
			// Adjust Physical PixelId Stop to "One Past" Last PixelId...
			// (For More Intuitive Loop Termination... ;-D)
			physical_stop += physical_step;
		}

		// Just a Plain Numerical Entry...
		else {
			if (sscanf(physical_buf, "%u", &physical) != 1) {
				std::string msg("Bad Physical PixelId Entry Specification");
				msg += " in Pixel Map File, line ";
				msg += boost::lexical_cast<std::string>(lineno);
				msg += " [";
				msg += physical_buf;
				msg += "]";
				throw std::runtime_error(msg);
			}
			// Set Start/Stop/Step to Single Physical PixelId Step...
			physical_start = physical;
			physical_step = 1;
			physical_stop = physical_start + physical_step;
		}

		// Parse Any Closed-Form Logical PixelId Specification
		if ( strstr(logical_buf, ":") != NULL ) {
			if (sscanf(logical_buf, "%u:%u:%d",
					&logical_start, &logical_stop, &logical_step) != 3) {
				std::string msg("Bad Logical PixelId Entry Specification");
				msg += " in Pixel Map File, line ";
				msg += boost::lexical_cast<std::string>(lineno);
				msg += " [";
				msg += logical_buf;
				msg += "]";
				throw std::runtime_error(msg);
			}
			if ( ctrl->verbose() > 0 ) {
				DEBUG("readMap(): Parsed"
					<< " logical_start=" << logical_start
					<< " logical_stop=" << logical_stop
					<< " logical_step=" << logical_step);
			}
			// Adjust Logical PixelId Stop to "One Past" Last PixelId...
			// (For More Intuitive Loop Termination... ;-D)
			logical_stop += logical_step;
		}

		// Just a Plain Numerical Entry...
		else {
			if (sscanf(logical_buf, "%u", &logical) != 1) {
				std::string msg("Bad Logical PixelId Entry Specification");
				msg += " in Pixel Map File, line ";
				msg += boost::lexical_cast<std::string>(lineno);
				msg += " [";
				msg += logical_buf;
				msg += "]";
				throw std::runtime_error(msg);
			}
			// Set Start/Stop/Step to Single Logical PixelId Step...
			logical_start = logical;
			logical_step = 1;
			logical_stop = logical_start + logical_step;
		}

		// Check/Process Each Physical-to-Logical PixelId In Turn...

		for ( physical=physical_start, logical=logical_start ;
				physical != physical_stop && logical != logical_stop ;
				physical += physical_step, logical += logical_step ) {

			if (physical & 0x80000000) {
				std::string msg("Physical PixelId has Error Bit Set "
					"in Pixel Map File, line ");
				msg += boost::lexical_cast<std::string>(lineno);
				throw std::runtime_error(msg);
			}

			if (map->count(physical)) {
				std::string msg("Duplicate Physical PixelId ");
				msg += boost::lexical_cast<std::string>(physical);
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

			// Unused "Gap" in PixelId Space, Mark as Being "Unmapped" Bank
			if (bank == ((uint32_t) -1)) {
				bank = PixelMap::UNMAPPED_BANK;
			}

			else if (bank >= 0x10000) {
				std::string msg(
					"Out-of-Range Bank in Pixel Map File, line ");
				msg += boost::lexical_cast<std::string>(lineno);
				throw std::runtime_error(msg);
			}

			output_pixels.insert(logical);

			map->insert(make_pair(physical, std::make_pair(logical, bank)));
		}

		// Make Sure the Physical and Logical Shorthand Sequence Aligned...
		// (i.e. Everything Stopped Together... ;-D)
		if ( physical != physical_stop || logical != logical_stop ) {
			std::string msg("Misalignment Error in Pixel Map Shorthand");
			msg += " - Physical and Logical Sequences";
			msg += " Did Not End Together:";
			msg += " physical_start="
				+ boost::lexical_cast<std::string>( physical_start );
			msg += " physical_stop="
				+ boost::lexical_cast<std::string>( physical_stop );
			msg += " physical_step="
				+ boost::lexical_cast<std::string>( physical_step );
			msg += " logical_start="
				+ boost::lexical_cast<std::string>( logical_start );
			msg += " logical_stop="
				+ boost::lexical_cast<std::string>( logical_stop );
			msg += " logical_step="
				+ boost::lexical_cast<std::string>( logical_step );
			msg += ", line ";
			msg += boost::lexical_cast<std::string>(lineno);
			throw std::runtime_error(msg);
		}
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
	std::queue<uint32_t> section_words;

	TempMap::iterator it, end;

	uint32_t physical_start = -1, physical_stop = -1;
	int32_t physical_step = 0;

	uint32_t logical_start = -1, logical_stop = -1;
	int32_t logical_step = 0;

	uint32_t last_physical = -1, last_logical = -1;
	uint32_t next_physical = -1, next_logical = -1;

	uint32_t physical, logical;

	uint32_t tot_pixelid_count;

	uint32_t phys_and_log_step;
	uint32_t bank_and_count;

	uint32_t payload;

	struct timespec now;

	uint32_t *u32;

	uint16_t max_section_pixelid_count = 0x7fff; // *15* bit section count
	uint16_t entries, bank, last_bank = -1;

	SMSControl *ctrl = SMSControl::getInstance();

	DEBUG("genAltPacket() Entry");

	// We now support Non-One-to-One Pixel Mappings, therefore
	// we must also avoid using any "Inverse" Mappings, as these
	// are practically impossible for some beamlines (e.g. HFIR WAND).
	// Just use physical->logical map "as is" for generating the
	// pixel map packet.

	// NOTE: This "Alternate" Pixel Mapping Table Packet is effectively
	// a "Mirror Image" of the Original Pixel Mapping Table Packet,
	// with "Logical" PixelIds now Swapped with "Physical" PixelIds
	// Everywhere, in a "Inverted" (ha ha) organizational structure. ;-D

	// We also pack in One Handy Extra Value _Before_ the Mapping Data,
	// to Avoid Future Issues - explicitly include the "Number of Banks"!

	// ALSO NOTE: This Version 1 (or Greater) Packet Type now also supports
	// an Efficient "Shorthand" Section Format, which like the new
	// "Start:Stop:Step" notation in the Pixel Mapping Table File Format,
	// can be used to compactly represent *HUGE* Pixel Maps that would
	// otherwise be prohibitive in terms of Packet Size. ;-D

	tot_pixelid_count = 0;
	payload = 0;

	// We're always going to have at least one section, with the first
	// physical pixel in it.

	it = map->begin();

	physical = it->first;
	logical = it->second.first;
	bank = it->second.second;

	// First PixelId Specification...

	physical_start = physical;
	physical_stop = physical;

	logical_start = logical;
	logical_stop = logical;

	last_physical = physical;
	last_logical = logical;
	last_bank = bank;

	entries = 1;

	for ( ++it, end = map->end(); it != end; ++it ) {

		physical = it->first;
		logical = it->second.first;
		bank = it->second.second;

		// If we've found a discontinuity in the physical pixels,
		// or we changed banks, OR we have _Filled Up_ this section
		// with the Max Section PixelId Count (*15* bits, 0x7fff = 32767),
		// then we have to start a new section.
		// -> Capture the "Last Bank" Section... :-D
		if ( bank != last_bank
				|| ( physical_step != 0 && physical != next_physical )
				|| ( logical_step != 0 && logical != next_logical )
				|| entries >= max_section_pixelid_count ) {

			// Skip Unmapped Banks...! Don't Count/Send Unmapped PixelIds...
			// No Need to Send These Over the Wire!
			if ( last_bank != (uint16_t) -1 ) {

				// Valid Shorthand Sequence Captured,
				// Use Shorthand Section Format...
				if ( physical_start != physical_stop
							&& physical_step != 0
						&& logical_start != logical_stop
							&& logical_step != 0 ) {

					// Base/Starting Physical PixelId
					section_words.push(physical_start);

					// Bank ID, "1", Count
					bank_and_count = (uint32_t) last_bank << 16;
					bank_and_count |= 0x1 << 15;
					bank_and_count |= entries;
					section_words.push(bank_and_count);

					// Stopping Physical PixelId
					section_words.push(physical_stop);

					// Base/Starting Logical PixelId
					section_words.push(logical_start);

					// Physical Step, Logical Step
					phys_and_log_step = physical_step << 16;
					if ( ctrl->verbose() > 1 ) {
						DEBUG("genAltPacket():" << std::hex
							<< " physical_step=0x" << physical_step
							<< " phys_and_log_step=0x" << phys_and_log_step
							<< std::dec);
					}
					phys_and_log_step |= logical_step & 0xFFFF;
					if ( ctrl->verbose() > 1 ) {
						DEBUG("genAltPacket():" << std::hex
							<< " logical_step=0x" << logical_step
							<< " phys_and_log_step=0x" << phys_and_log_step
							<< std::dec);
					}
					section_words.push(phys_and_log_step);

					// Stopping Logical PixelId
					section_words.push(logical_stop);

					if ( ctrl->verbose() > 0 ) {
						DEBUG("genAltPacket(): Shorthand Section"
							<< " bank=" << last_bank
							<< " count=" << entries
							<< " bank_and_count=0x"
							<< std::hex << bank_and_count << std::dec
							<< " physical="
							<< physical_start << ":"
							<< physical_stop << ":"
							<< physical_step
							<< " phys_and_log_step=0x"
							<< std::hex << phys_and_log_step << std::dec
							<< " logical="
							<< logical_start << ":"
							<< logical_stop << ":"
							<< logical_step);
					}
				}

				// Invalid Shorthand Sequence,
				// Just Dump Direct Pixelid Section Format...
				else {

					// Base Physical PixelId
					section_words.push(physical_start);

					// Bank ID, "0", Count
					bank_and_count = (uint32_t) last_bank << 16;
					bank_and_count |= 0x0 << 15; // NOOP... :-D
					bank_and_count |= entries; // Count, Always == 1...
					section_words.push(bank_and_count);

					// Logical PixelId, The "Only One"
					// (Or Else We Would Have Used the "Shorthand" Format!)
					section_words.push(logical_start);

					if ( ctrl->verbose() > 0 ) {
						DEBUG("genAltPacket(): Direct Section"
							<< " bank=" << last_bank
							<< " count=" << entries
							<< " bank_and_count=0x"
							<< std::hex << bank_and_count << std::dec
							<< " physical="
							<< physical_start
							<< " logical="
							<< logical_start);
					}
				}

				tot_pixelid_count += entries;
			}

			// Reset Section Bookkeeping...

			physical_start = -1;
			physical_stop = -1;
			physical_step = 0;

			logical_start = -1;
			logical_stop = -1;
			logical_step = 0;

			last_physical = -1;
			last_logical = -1;
			last_bank = -1;

			next_physical = -1;
			next_logical = -1;

			entries = 0;

			// Start New Shorthand Section...

			physical_start = physical;

			logical_start = logical;

			last_bank = bank;
		}

		// Otherwise, Check for New Sequence Step Sizes...

		else if ( physical_step == 0 && logical_step == 0 ) {

			physical_step = physical - last_physical;

			logical_step = logical - last_logical;
		}

		// Extend the End of the Physical/Logical PixelId Sequence...

		physical_stop = physical;

		logical_stop = logical;

		// Capture Last Physical/Logical PixelIds for Next Line...

		last_physical = physical;

		last_logical = logical;

		// Compute Expected Next Physical & Logical PixelIds...
		
		if ( physical_step != 0 && logical_step != 0 ) {

			// Prevent Physical or Logical Step "Overflow"...!
			// (Greater/Less Than 16-bit Integer Space in Packet...)
			//    - 0x7FFF is Max 16-bit Integer or 32767
			//    - Hence 0xFFFF is Max negative Integer or -32767

			if ( physical_step > 32767 || physical_step < -32767
					|| logical_step > 32767 || logical_step < -32767 ) {

				ERROR("genAltPacket(): PixelId Step Overflow!"
					<< " (> +/-32767=0x7FFF 16-bit Signed Integer)"
					<< " physical_step=" << physical_step
					<< " (0x" << std::hex << physical_step
						<< std::dec << ")"
					<< " logical_step=" << logical_step
					<< " (0x" << std::hex << logical_step
						<< std::dec << ")"
					<< " - Dump as Direct Pixel Map Section."
					<< " ("
					<< " bank=" << last_bank
					<< " count=" << entries
					<< " physical_start=" << physical_start
					<< " physical=" << physical
					<< " logical_start=" << logical_start
					<< " logical=" << logical
					<< " )");

				// Dump Direct Pixelid Section Format...

				// Base Physical PixelId
				section_words.push(physical_start);

				// Bank ID, "0", Count
				bank_and_count = (uint32_t) last_bank << 16;
				bank_and_count |= 0x0 << 15; // NOOP... :-D
				bank_and_count |= entries; // Count, Always == 1...
				section_words.push(bank_and_count);

				// Logical PixelId, The "Only One"
				// (Or Else We Would Have Used the "Shorthand" Format!)
				section_words.push(logical_start);

				if ( ctrl->verbose() > 0 ) {
					DEBUG("genAltPacket(): Direct Section"
						<< " bank=" << last_bank
						<< " count=" << entries
						<< " bank_and_count=0x"
						<< std::hex << bank_and_count << std::dec
						<< " physical="
						<< physical_start
						<< " logical="
						<< logical_start);
				}

				// Adjust Section Bookkeeping to Move Past 1st Map
				// (And Continue On... ;-D)

				physical_start = physical;
				physical_stop = physical;
				physical_step = 0;

				logical_start = logical;
				logical_stop = logical;
				logical_step = 0;

				last_physical = physical;
				last_logical = logical;
				last_bank = bank;

				next_physical = -1;
				next_logical = -1;

				entries = 0;
			}

			// Just Do Normal Step Increments...
			else {

				next_physical = physical + physical_step;

				next_logical = logical + logical_step;
			}
		}

		// Increment the Current Number of Entries Count...

		entries++;
	}

	// Push the last section we were working on.

	// Skip Unmapped Banks...! Don't Count/Send Unmapped PixelIds...
	// No Need to Send These Over the Wire!
	if ( last_bank != (uint16_t) -1 ) {

		// Valid Shorthand Sequence Captured,
		// Use Shorthand Section Format...
		if ( physical_start != physical_stop && physical_step != 0
				&& logical_start != logical_stop && logical_step != 0 ) {

			// Base/Starting Physical PixelId
			section_words.push(physical_start);

			// Bank ID, "1", Count
			bank_and_count = (uint32_t) last_bank << 16;
			bank_and_count |= 0x1 << 15;
			bank_and_count |= entries;
			section_words.push(bank_and_count);

			// Stopping Physical PixelId
			section_words.push(physical_stop);

			// Base/Starting Logical PixelId
			section_words.push(logical_start);

			// Physical Step, Logical Step
			phys_and_log_step = physical_step << 16;
			if ( ctrl->verbose() > 1 ) {
				DEBUG("genAltPacket():" << std::hex
					<< " physical_step=0x" << physical_step
					<< " phys_and_log_step=0x" << phys_and_log_step
					<< std::dec);
			}
			phys_and_log_step |= logical_step & 0xFFFF;
			if ( ctrl->verbose() > 1 ) {
				DEBUG("genAltPacket():" << std::hex
					<< " logical_step=0x" << logical_step
					<< " phys_and_log_step=0x" << phys_and_log_step
					<< std::dec);
			}
			section_words.push(phys_and_log_step);

			// Stopping Logical PixelId
			section_words.push(logical_stop);

			if ( ctrl->verbose() > 0 ) {
				DEBUG("genAltPacket(): Shorthand Section"
					<< " bank=" << last_bank
					<< " count=" << entries
					<< " bank_and_count=0x"
					<< std::hex << bank_and_count << std::dec
					<< " physical="
					<< physical_start << ":"
					<< physical_stop << ":"
					<< physical_step
					<< " phys_and_log_step=0x"
					<< std::hex << phys_and_log_step << std::dec
					<< " logical="
					<< logical_start << ":"
					<< logical_stop << ":"
					<< logical_step);
			}
		}

		// Invalid Shorthand Sequence,
		// Just Dump Direct Pixelid Section Format...
		else {

			// Base Physical PixelId
			section_words.push(physical_start);

			// Bank ID, "0", Count
			bank_and_count = (uint32_t) last_bank << 16;
			bank_and_count |= 0x0 << 15; // NOOP... :-D
			bank_and_count |= entries; // Count, Always == 1...
			section_words.push(bank_and_count);

			// Logical PixelId, The "Only One"
			// (Or Else We Would Have Used the "Shorthand" Format!)
			section_words.push(logical_start);

			if ( ctrl->verbose() > 0 ) {
				DEBUG("genAltPacket(): Direct Section"
					<< " bank=" << last_bank
					<< " count=" << entries
					<< " bank_and_count=0x"
					<< std::hex << bank_and_count << std::dec
					<< " physical="
					<< physical_start
					<< " logical="
					<< logical_start);
			}
		}

		tot_pixelid_count += entries;
	}

	DEBUG("genAltPacket(): section_words.size()=" << section_words.size());
	DEBUG("genAltPacket(): tot_pixelid_count=" << tot_pixelid_count);
	DEBUG("genAltPacket(): map->size()=" << map->size());

	// Now, build the packet; we have enough information to calculate
	// its size.

	payload = section_words.size() * sizeof(uint32_t);
	DEBUG("genAltPacket(): payload(mapping data)=" << payload);

	payload += sizeof(uint32_t); // for Explicit "Number of Banks"! ;-D
	DEBUG("genAltPacket(): payload(numbanks)=" << payload);

	packetSize = payload + sizeof(ADARA::Header);
	DEBUG("genAltPacket(): packetSize=" << packetSize);

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

	// Just Copy All the Section Words into the Packet :-D

	while ( !section_words.empty() ) {

		*u32++ = section_words.front();

		section_words.pop();
	}

	//DEBUG("Resulting Size: u32=" << std::hex << u32
		//<< " Starting Payload: pkt.get()=" << ((uint32_t *) pkt.get())
		//<< " Effective Size: (4 * (u32 - pkt.get()))="
		//<< std::dec << ( 4 * ( u32 - ((uint32_t *) pkt.get()) ) ) );

	DEBUG("genAltPacket(): Done");

	return pkt;
}

// Note: The Original genPacket() Method Below Will Still "Work"
// with the Latest Pixel Map Handling, With One Exception. Because
// an "Inverted" Map is Used Here to Generate the PixelMappingPkt
// Packet, the Ordering of the Pixel Map is by *Logical* Rather than
// by Physical PixelIds... ;-D  So if you're using "SavePixelMap"
// to capture the Pixel Map Datasets into the NeXus via the STC,
// Any _Negative Step_ Sequences will be in *Reverse* Order...! ;-D
boost::shared_array<uint8_t> PixelMap::genPacket(TempMap *map,
					      uint32_t &packetSize)
{
	std::queue<uint16_t> sections;

	TempMap inverted;
	TempMap::iterator it, end;

	uint32_t physical, logical;
	uint32_t expected;

	uint32_t bank_and_count;
	uint32_t payload;

	struct timespec now;

	uint32_t *u32;

	uint16_t max_section_pixelid_count = 0xffff; // 16 bit section count
	uint16_t i, entries, bank;

	DEBUG("genPacket() Entry");

	// A physical->logical map is better for parsing and for building
	// the lookup table used for normal operations, but going logical
	// to physical is better for generating the pixel map packet.
	// Since we guarantee a one-to-one mapping during load, we don't
	// have to worry about duplicate keys here.

	// NOTE: _Can_ Break for "m_allowNonOneToOnePixelMapping == true" case!!
	// -> Ordering of Banks in Map can "Cover Up" some Bank Numbers...!
	// -> Where Possible going forward, Use "PixelMappingAltPkt"...! ;-D
	// -> See Trac Ticket #1043, for HFIR WAND/HB2C...
	for (it = map->begin(), end = map->end(); it != end; ++it) {
		physical = it->first;
		logical = it->second.first;
		bank = it->second.second;
		inverted.insert(make_pair(logical, std::make_pair(physical, bank)));
	}

	// We're always going to have at least one section, with the first
	// logical pixel in it.
	it = inverted.begin();
	expected = it->first + 1; // expected next Logical PixelId...
	entries = 1;
	bank = it->second.second;

	for (++it, end = inverted.end(); it != end; ++it) {
		// If we've found a discontinuity in the logical pixels,
		// or we changed banks, OR we have _Filled Up_ this section
		// with the Max Section PixelId Count (16 bits, 0xffff = 65535),
		// then we have to start a new section.
		if (it->first != expected || it->second.second != bank
				|| entries >= max_section_pixelid_count) {
			sections.push(entries);
			entries = 0;
			bank = it->second.second;
		}
		entries++;
		expected = it->first + 1; // expected next Logical PixelId...
	}

	// Push the last section we were working on.
	sections.push(entries);

	DEBUG("genPacket(): sections.size()=" << sections.size());

	// Now, build the packet; we have enough information to calculate
	// its size.
	payload = sections.size() * (sizeof(uint32_t) + 2 * sizeof(uint16_t));
	payload += inverted.size() * sizeof(uint32_t);
	DEBUG("genPacket(): payload=" << payload);
	packetSize = payload + sizeof(ADARA::Header);
	DEBUG("genPacket(): packetSize=" << packetSize);

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

		// First the header (base logical ID, bankid, count)
		bank_and_count = (uint32_t) it->second.second << 16;
		bank_and_count |= entries;

		*u32++ = it->first;
		*u32++ = bank_and_count;

		// Then the entries (physical id)
		for (i = 0; i < entries; ++it, ++i)
			*u32++ = it->second.first;
	}

	DEBUG("genPacket(): Done");

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
	uint32_t max_physical = 0;
	uint32_t i;

	INFO("Entry PixelMap(): m_allowNonOneToOnePixelMapping="
		<< m_allowNonOneToOnePixelMapping);

	map = readMap(path);

	end = map->end();
	for (it = map->begin(); it != end; ++it) {
		if (it->first > max_physical)
			max_physical = it->first;
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
		<< " max_physical=" << max_physical
		<< " max_logical=" << max_logical
		<< " m_numBanks=" << m_numBanks);

	// Count from zero to the largest bank, that's how many slots will
	// be needed.
	m_numBanks++;

	// It's easier to parse into a std::map, but a vector is 100x faster
	// for lookups in the tests I've performed. No real surprise there...

	// First we populate with the "unmapped pixel" mapping, then fill
	// in the valid entries.
	m_table.reserve(max_physical + 1);
	for (i = 0; i <= max_physical; ++i) {
		m_table.push_back(
			std::make_pair( i | 0x80000000,
				(uint16_t) PixelMap::UNMAPPED_BANK ) );
	}

	// While we're at it, _Also_ create a "Logical-to-Bank" lookup vector,
	// for the case where a Data Source has _Already_ mapped the PixelId...
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
