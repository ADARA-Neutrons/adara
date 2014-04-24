#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "StorageManager.h"

struct adara_header {
	uint32_t payload_len;
	uint32_t pkt_format;
	uint32_t ts_sec;
	uint32_t ts_nsec;
};

int main(int argc, char **argv)
{
	unsigned char pkt[8192] = { 0, };
	struct adara_header *hdr = (struct adara_header *) pkt;
	struct timespec ts;
	int i;

	hdr->payload_len = sizeof(pkt) - sizeof(*hdr);
	hdr->pkt_format = ADARA::PacketType::RAW_EVENT_V0;

	boost::property_tree::ptree conf;
	conf.put("basedir", "/SNSlocal/sms");

	StorageManager::config(conf);
	StorageManager::init();

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt), false);
	}

	StorageManager::startRecording(12345);

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt), false);
	}

	StorageManager::stopRecording();

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt), false);
	}

	StorageManager::stop();
	return 0;
}
