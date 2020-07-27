#ifndef __DETECTORBANKSET_H
#define __DETECTORBANKSET_H

#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>

#include <boost/smart_ptr.hpp>

#include <stdint.h>

#include <string>
#include <vector>

class DetectorBankSetInfo;
class smsUint32PV;

class DetectorBankSet : boost::noncopyable {
public:
	DetectorBankSet(const boost::property_tree::ptree & conf);
	~DetectorBankSet();

	bool truncateString( std::string & str, size_t sz,
		std::string caller, std::string desc, bool is_error );

	void resetPacketTime(void);

private:
	std::vector<DetectorBankSetInfo *> detBankSetInfos;

	uint32_t m_numDetBankSets;

	uint32_t m_payloadSize;
	uint32_t m_packetSize;

	uint8_t *m_packet;

	boost::shared_ptr<smsUint32PV> m_pvNumDetBankSets;

	boost::signals2::connection m_connection;

	void onPrologue( bool capture_last );
};

#endif /* __DETECTORBANKSET_H */
