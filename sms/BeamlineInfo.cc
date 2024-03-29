#include <boost/bind.hpp>
#include <stdint.h>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "BeamlineInfo.h"
#include "StorageManager.h"

BeamlineInfo::BeamlineInfo(const uint32_t targetStationNumber,
		const std::string &id,
		const std::string &shortName,
		const std::string &longName)
{
	struct timespec ts;
	std::string data;
	uint32_t *fields;
	int payload;

	if (!id.length())
		throw std::runtime_error("Beamline id has no content");
	if (!shortName.length())
		throw std::runtime_error("Beamline short name has no content");
	if (!longName.length())
		throw std::runtime_error("Beamline long name has no content");

	if (targetStationNumber > 255)
	{
		throw std::runtime_error(
			"Target Station Number is too large (>255)");
	}
	if (id.length() > 255)
		throw std::runtime_error("Beamline id is too long");
	if (shortName.length() > 255)
		throw std::runtime_error("Beamline short name is too long");
	if (longName.length() > 255)
		throw std::runtime_error("Beamline long name is too long");

	/* Concatenate the beamline data and round its length up to a
	 * multiple of four; string::resize() will pad with 0's
	 */
	data = id + shortName + longName;
	data.resize((data.length() + 3) & ~3);

	payload = data.length();
	payload += sizeof(uint32_t);

	m_packetSize = payload + sizeof(ADARA::Header);
	m_packet = new uint8_t[m_packetSize];

	clock_gettime(CLOCK_REALTIME, &ts);

	fields = (uint32_t *) m_packet;
	fields[0] = payload;
	fields[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::BEAMLINE_INFO_TYPE,
		ADARA::PacketType::BEAMLINE_INFO_VERSION );
	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
	fields[4] = longName.length();
	fields[4] |= shortName.length() << 8;
	fields[4] |= id.length() << 16;
	fields[4] |= targetStationNumber << 24;
	memcpy(fields + 5, data.data(), data.size());

	m_connection = StorageManager::onPrologue(
				boost::bind(&BeamlineInfo::onPrologue, this, _1));
}

BeamlineInfo::~BeamlineInfo()
{
	delete [] m_packet;
	m_connection.disconnect();
}

void BeamlineInfo::onPrologue( bool UNUSED(capture_last) )
{
	StorageManager::addPrologue(m_packet, m_packetSize);
}
