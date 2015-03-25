#ifndef __DETECTORBANKSET_H
#define __DETECTORBANKSET_H

#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals2.hpp>

#include <boost/smart_ptr.hpp>

#include "SMSControl.h"
#include "SMSControlPV.h"

class DetectorBankSetInfo;
class smsUint32PV;

class DetectorBankSet : boost::noncopyable {
public:
	DetectorBankSet(const boost::property_tree::ptree & conf);
	~DetectorBankSet();

	void resetPacketTime(void);

private:
	std::vector<DetectorBankSetInfo *> detBankSetInfos;

	uint32_t m_numDetBankSets;
	uint32_t m_numEvent;
	uint32_t m_numHisto;

	uint32_t m_sectionSize;
	uint32_t m_payloadSize;
	uint32_t m_packetSize;

	uint8_t *m_packet;

	boost::shared_ptr<smsUint32PV> m_pvNumDetBankSets;

	boost::signals2::connection m_connection;

	void onPrologue(void);
};

#endif /* __DETECTORBANKSET_H */
