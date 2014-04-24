#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <map>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "POSIXParser.h"

struct pevent {
	uint32_t	tof;
	uint32_t	pixel;
};

static int compare_event(const void *_a, const void *_b)
{
	const struct pevent *a = (const struct pevent *) _a;
	const struct pevent *b = (const struct pevent *) _b;
	if (a->tof < b->tof)
		return -1;
	if (a->tof > b->tof)
		return 1;

	/* tof is equal, sort by pixel */
	if (a->pixel < b->pixel)
		return -1;
	return a->pixel != b->pixel;
}

static void dump_events(struct pevent *ev, uint32_t num_events,
			uint32_t cycle)
{
	/* Get a consistent ordering for comparing to legacy */
	qsort(ev, num_events, sizeof(struct pevent), compare_event);

	while (num_events--) {
		if (ev->pixel >> 28 != 7) {
			printf("cyc %-3u: %08x %08x\n",
			       cycle, ev->tof, ev->pixel);
		}
		ev++;
	}
}

class Parser : public ADARA::POSIXParser {
public:
        Parser() {
		m_hadMonitors = false;
		m_ev = new pevent[10 * 1000 * 1000];
	}

        bool rxPacket(const ADARA::BankedEventPkt &pkt);
        bool rxPacket(const ADARA::BeamMonitorPkt &pkt);
        bool rxPacket(const ADARA::PixelMappingPkt &pkt);

        using ADARA::POSIXParser::rxPacket;

private:
	bool		m_hadMonitors;
	uint32_t	m_nEvents;
	uint32_t	m_cycle;
	struct pevent	*m_ev;
	std::map<uint32_t, uint32_t> m_unmap;
};

bool Parser::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
	struct pevent *ev = m_ev;
	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();
       	uint32_t nEvents, id;

	m_hadMonitors = true;
	m_nEvents = 0;
	m_cycle = 998;

	p+= 4;
	len -= 4 * sizeof(uint32_t);

	while (len) {
		nEvents = p[0] & ((1 << 22) - 1);
		id = (p[0] >> 22) << 16;
		p += 3;
		len -= 12;

		for (uint32_t i = 0; i < nEvents; ev++, p++, i++) {
			ev->tof = *p & ~(1U << 31);
			ev->pixel = 0x40000001 | id;
			m_cycle = ev->tof >> 21;
		}

		m_nEvents += nEvents;
		len -= nEvents * sizeof(uint32_t);
	}

	return false;
}

bool Parser::rxPacket(const ADARA::BankedEventPkt &pkt)
{
	if (!m_hadMonitors) {
		fprintf(stderr, "Missing monitors...\n");
		m_nEvents = 0;
		m_cycle = 999;
	}

	struct pevent *ev = m_ev + m_nEvents;
	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();
	uint32_t nBanks, nEvents, bank;

	p += 4;
	len -= 4 * sizeof(uint32_t);

	while (len) {
		nBanks = p[3];
		p += 4;
		len -= 16;

		for (uint32_t i = 0; i < nBanks; i++) {
			bank = p[0];
			nEvents = p[1];
			p += 2;
			len -= 8;

			for (uint32_t j = 0; j < nEvents; ev++, j++) {
				ev->tof = p[0];
				if (bank == 0xffffffff) {
					/* Wasn't mapped */
					ev->pixel = p[1] & ~(1U << 31);
				} else if (bank == 0xfffffffe) {
					/* Had an error */
					ev->pixel = 0x80000000 | p[1];
				} else {
					/* Need to undo the mapping applied */
					ev->pixel = m_unmap[p[1]];
				}
				p += 2;
				len -= 8;
			}

			m_nEvents += nEvents;
		}
	}

	dump_events(m_ev, m_nEvents, m_cycle);

	return false;
}

bool Parser::rxPacket(const ADARA::PixelMappingPkt &pkt)
{
	if (!m_unmap.empty())
		return false;

	uint32_t len = pkt.payload_length();
	uint32_t *p = (uint32_t *) pkt.payload();

	while (len) {
		uint32_t logical = p[0];
		uint32_t count = p[1] & 0xffff;

		p += 2;
		len -= 8;

		while (count--) {
			m_unmap[logical++] = *p++;
			len -= 4;
		}
	}

	return false;
}

int main(int, char **)
{
	Parser parser;
	parser.read(0);

	return 0;
}
