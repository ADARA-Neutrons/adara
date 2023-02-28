
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.Geometry"));

#include <boost/bind.hpp>
#include <fstream>
#include <stdint.h>

#include "ADARA.h"
#include "ADARAUtils.h"
#include "Geometry.h"
#include "StorageManager.h"

Geometry::Geometry(const std::string &path)
{
	uint32_t payloadSize, *fields;
	size_t contentsSize;
	struct timespec ts;
	std::ifstream f(path.c_str());

	if (f.fail()) {
		std::string msg("Unable to open geometry file ");
		msg += path;
		throw std::runtime_error(msg);
	}

	f.seekg(0, std::ios::end);
	contentsSize = f.tellg();

	DEBUG("Found Geometry File at "
		<< path << " - " << contentsSize << " Bytes");

	/* TODO error checking, how big is too big? */

	/* We need to add tailing bytes to make the payload a multiple of 4,
	 * and we need 4 bytes for the string length field.
	 */
	payloadSize = contentsSize + 4 + 3;
	payloadSize &= ~3;
	m_packetSize = payloadSize + sizeof(ADARA::Header);

	m_packet = new uint8_t[m_packetSize];
	fields = (uint32_t *) m_packet;

	clock_gettime(CLOCK_REALTIME, &ts);

	fields[0] = payloadSize;
	fields[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::GEOMETRY_TYPE,
		ADARA::PacketType::GEOMETRY_VERSION );
	fields[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	fields[3] = ts.tv_nsec;
	fields[4] = contentsSize;

        /* Zero fill the last word in the packet, then copy in the file */
        fields[3 + (payloadSize / 4)] = 0;

	f.seekg(0);
	f.read((char *)(fields + 5), contentsSize);

	if (f.fail()) {
		std::string msg("Unable to read geometry file ");
		msg += path;
		delete [] m_packet;
		throw std::runtime_error(msg);
	}

	m_connection = StorageManager::onPrologue(
				boost::bind(&Geometry::onPrologue, this, _1));
}

Geometry::~Geometry()
{
	delete [] m_packet;
	m_connection.disconnect();
}

void Geometry::onPrologue( bool UNUSED(capture_last) )
{
	StorageManager::addPrologue(m_packet, m_packetSize);
}
