#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "StorageManager.h"
#include "ADARA.h"
#include "ADARAUtils.h"

int main(int UNUSED(argc), char **UNUSED(argv))
{
	unsigned char pkt[8192] = { 0, };
	ADARA::Header *hdr = (ADARA::Header *) pkt;
	struct timespec ts;
	int i;

	hdr->payload_len = sizeof(pkt) - sizeof(*hdr);
	hdr->pkt_format = ADARA_PKT_TYPE(
		ADARA::PacketType::RAW_EVENT_TYPE,
		ADARA::PacketType::RAW_EVENT_VERSION );

	boost::property_tree::ptree conf;
	conf.put("basedir", "/SNSlocal/sms");

	StorageManager::config(conf);
	StorageManager::init();

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt),
			false /* ignore_pkt_timestamp */,
			true /* check_old_containers */,
			false /* notify */);
	}

	StorageManager::startRecording(12345, "IPTS-0000");

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt),
			false /* ignore_pkt_timestamp */,
			true /* check_old_containers */,
			false /* notify */);
	}

	StorageManager::stopRecording();

	for (i = 0; i < 10240; i++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		StorageManager::addPacket(pkt, sizeof(pkt),
			false /* ignore_pkt_timestamp */,
			true /* check_old_containers */,
			false /* notify */);
	}

	clock_gettime(CLOCK_REALTIME, &ts);
	StorageManager::stop(ts);
	return 0;
}
