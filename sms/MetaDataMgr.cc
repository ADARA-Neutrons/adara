
#include "Logging.h"

static LoggerPtr logger(Logger::getLogger("SMS.MetaDataMgr"));

#include <sstream>
#include <string>
#include <map>

#include <stdint.h>
#include <string.h>

#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

#include "ADARA.h"
#include "ADARAPackets.h"
#include "ADARAUtils.h"
#include "MetaDataMgr.h"
#include "StorageManager.h"
#include "SMSControl.h"

RateLimitedLogging::History RLLHistory_MetaDataMgr;

// Rate-Limited Logging IDs...
#define RLL_DROP_DEVICES_FOR_TAG   0
#define RLL_NO_DEVICES_TO_DROP     1
#define RLL_UPDATE_DESCRIPTOR      2
#define RLL_ADD_FAST_META_DDP      3
#define RLL_DESC_INCORRECT_TAG     4
#define RLL_ADD_EXISTING_DEVICE    5
#define RLL_UNABLE_REMAP_U32_VAR   6
#define RLL_UNABLE_REMAP_DBL_VAR   7
#define RLL_UNABLE_REMAP_STR_VAR   8
#define RLL_VAR_UPDATE_NO_DESC     9
#define RLL_VAR_UPDATE_BAD_TAG    10

MetaDataMgr::MetaDataMgr() : m_nextMappedDevId(1)
{
	m_connection = StorageManager::onPrologue(
				boost::bind( &MetaDataMgr::onPrologue, this, _1 ) );
}

MetaDataMgr::~MetaDataMgr()
{
	m_connection.disconnect();
}

void MetaDataMgr::upstreamDisconnected(
		MetaDataMgr::VariablePktMap &varPkts )
{
	/* For each variable, modify the packet to indicate that we
	 * lost the upstream connection and feed it into the stream.
	 */
	VariablePktMap::iterator vit, vend = varPkts.end();
	uint32_t len, pktSize = 0;
	uint32_t *fields = NULL;
	uint8_t *pkt = NULL;

	std::stringstream var_log_ss;

	for ( vit = varPkts.begin(); vit != vend; vit++ ) {

		ADARA::PacketSharedPtr orig = vit->second;

		len = orig->packet_length();
		if ( len > pktSize ) {
			delete[] pkt;
			pktSize = len;
			pkt = new uint8_t[pktSize];
			fields = (uint32_t *) ( pkt +
					ADARA::PacketHeader::header_length() );
		}

		memcpy( pkt, orig->packet(), len );
		fields[2] = ADARA::VariableStatus::UPSTREAM_DISCONNECTED;
		fields[2] <<= 16;
		fields[2] |= ADARA::VariableSeverity::INVALID;

		/* Don't notify storage clients about the file size change;
		 * there is no reason to start a new file in the middle of
		 * these drops -- they should be small, and we should not
		 * dump the state of the variables we're dropping.
		 *
		 * We'll push the whole batch out at once when we are done.
		 */
		StorageManager::addPacket( pkt, len,
			true /* ignore_pkt_timestamp */,
			false /* check_old_containers */,
			false /* notify */ );

		// Add Variable Id to Log List...
		if ( var_log_ss.str().empty() )
			var_log_ss << vit->first;
		else
			var_log_ss << ", " << vit->first;
	}

	if ( !var_log_ss.str().empty() ) {
		DEBUG("Sending Upstream Disconnected for"
			<< " varIds = [ " << var_log_ss.str() << " ]");
	}

	delete[] pkt;
}

void MetaDataMgr::dropSourceTag( uint32_t srcTag )
{
	/* Rate-limited log that we got disconnected from a DataSource (srcTag)
	 * and are Dropping All Associated Devices...
	 */
	std::string log_info;
	std::stringstream ss;
	ss << srcTag;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_DROP_DEVICES_FOR_TAG, ss.str(),
			60, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG( log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "dropSourceTag():"
			<< " Dropping Devices for Data Source, srcTag=" << srcTag );
	}

	DeviceMap::iterator dit, dend = m_devices.end();
	bool dropped = false;

	for ( dit = m_devices.begin(); dit != dend; ) {
		DeviceVariables &dev = dit->second;

		if ( dev.m_srcTag == srcTag ) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropSourceTag(): Sending Upstream Disconnected for"
				<< " srcTag=" << dev.m_srcTag
				<< " devId=" << dev.m_devId
				<< " mapped_dev=" << dit->first);
			upstreamDisconnected(dev.m_variablePkts);
			// Move Device to "Old Devices" List for Possible Re-Connect...
			uint64_t key = ((uint64_t) srcTag << 32) | dev.m_devId;
			m_oldDevIdMap[key] = dit->first;
			// Copy DeviceVariables Struct Elements... ;-b
			m_oldDevices[dit->first].m_devId =
				m_devices[dit->first].m_devId;
			m_oldDevices[dit->first].m_srcTag =
				m_devices[dit->first].m_srcTag;
			m_oldDevices[dit->first].m_descriptorPkt =
				m_devices[dit->first].m_descriptorPkt;
			// *Don't* Save Variable Value Packets on a Disconnect...
			// - Go Ahead and Clear Out Any Previous Values, We'll Get
			// _New_ Values on a Re-Connect... ;-D
			// (cleaner this way, plus doesn't break "continuous" tests!)
			m_oldDevices[dit->first].m_variablePkts.clear();
			m_devices.erase(dit++);
			dropped = true;
		} else {
			++dit;
		}
	}

	if ( dropped ) {
		StorageManager::notify();
	}
	else {
		/* Rate-limited log that there were No Associated Devices to Drop
		 * from the disconnected DataSource (srcTag)...
		 */
		// re-use "ss" from above...
		log_info.clear();
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_NO_DEVICES_TO_DROP, ss.str(),
				60, 3, 10, log_info ) ) {
			DEBUG( log_info
				<< "dropSourceTag():"
				<< " Warning No Devices Found! srcTag=" << srcTag );
		}
	}

	/* Remove the mapped device ids for this srcTag */
	std::map<uint64_t, uint32_t>::iterator diit, diend;
	diend = m_devIdMap.end();
	for ( diit = m_devIdMap.begin(); diit != diend; ) {
		if ( diit->first >> 32 == srcTag ) {
			SMSControl *ctrl = SMSControl::getInstance();
			DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "dropSourceTag(): Removing Mapped Device"
				<< " srcTag=" << ( diit->first >> 32 )
				<< " devId=" << ( diit->first & 0xffff )
				<< " mapped_dev=" << diit->second);
			m_activeDevId.erase(diit->second);
			m_devIdMap.erase(diit++);
		} else {
			++diit;
		}
	}
}

uint32_t MetaDataMgr::lookupMappedDeviceId( uint32_t dev, uint32_t srcTag )
{
	uint64_t key = ((uint64_t) srcTag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator diit;

	diit = m_devIdMap.find(key);
	if ( diit == m_devIdMap.end() )
		return 0;

	return diit->second;
}

// Note: Always Check lookupMappedDeviceId() First,
// To Ensure "Mapped" Device ID is _Not_ Actively In Use...!
uint32_t MetaDataMgr::lookupOldMappedDeviceId(
		uint32_t dev, uint32_t srcTag, bool &reconnected )
{
	uint64_t key = ((uint64_t) srcTag << 32) | dev;
	std::map<uint64_t, uint32_t>::iterator diit;

	diit = m_oldDevIdMap.find(key);
	if ( diit == m_oldDevIdMap.end() )
		return 0;

	// Old Device Reconnected...! Reinstate It... :-D

	uint32_t mapped_dev = diit->second;

	m_oldDevIdMap.erase(diit);

	DeviceMap::iterator odit = m_oldDevices.find(mapped_dev);
	if ( odit != m_oldDevices.end() ) {

		// We should have Already Checked for "mapped_dev" in m_devIdMap[],
		// So we Know we're Not Overwriting an Existing Device here...! ;-b
		m_devices[mapped_dev].m_devId =
			odit->second.m_devId;
		m_devices[mapped_dev].m_srcTag =
			odit->second.m_srcTag;
		m_devices[mapped_dev].m_descriptorPkt =
			odit->second.m_descriptorPkt;
		m_devices[mapped_dev].m_variablePkts =
			odit->second.m_variablePkts;   // (cleared out on disconnect)

		m_oldDevices.erase(odit);

		m_activeDevId.insert( mapped_dev );

		m_devIdMap[key] = mapped_dev;

		reconnected = true;

		return mapped_dev;
	}

	// Wasn't Found, Let it Go... ;-D
	return 0;
}

uint32_t MetaDataMgr::allocDev( uint32_t dev, uint32_t srcTag,
		bool do_log, bool &reconnected )
{
	uint32_t mapped_dev;

	// Already an Active Device...?
	if ( (mapped_dev = lookupMappedDeviceId( dev, srcTag )) ) {
		if ( do_log ) {
			DEBUG("Device Lookup Returned mapped_dev=" << mapped_dev);
		}
		return mapped_dev;
	}

	// Reconnecting an Old Device...?
	if ( (mapped_dev = lookupOldMappedDeviceId( dev, srcTag,
			reconnected )) ) {
		if ( do_log ) {
			DEBUG("Old Device Lookup Reconnect mapped_dev=" << mapped_dev);
		}
		return mapped_dev;
	}

	// Must be a *New* Device...! :-D

	// Handle Wrap (Lol... ;-) and SMS Internal (Mapped) Device IDs...!
	// (We Currently Reserve Device IDs 0x80000000 for SMS Internal Use,
	// e.g. Chopper IDs... :-D)
	if ( !m_nextMappedDevId || m_nextMappedDevId >= 0x80000000 )
		m_nextMappedDevId = 1;

	while ( m_activeDevId.count( m_nextMappedDevId ) )
		m_nextMappedDevId++;

	mapped_dev = m_nextMappedDevId++;

	m_activeDevId.insert( mapped_dev );

	uint64_t key = ((uint64_t) srcTag << 32) | dev;
	m_devIdMap[key] = mapped_dev;

	if ( do_log ) {
		DEBUG("New Input Device"
			<< " srcTag=" << srcTag
			<< " devId=" << dev
			<< " Mapped to SMS Output Device"
			<< " mapped_dev=" << mapped_dev);
	}

	return mapped_dev;
}

void MetaDataMgr::updateDescriptor(
		const ADARA::DeviceDescriptorPkt &inPkt,
		uint32_t srcTag )
{
	/* Rate-limited log that we received a DeviceDescriptorPkt. */
	std::string log_info;
	std::stringstream ss;
	ss << inPkt.devId() << "/" << srcTag;
	bool do_log = false;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_UPDATE_DESCRIPTOR, ss.str(),
			60, 3, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG( log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "Update Descriptor"
			<< " srcTag=" << srcTag
			<< " devId=" << inPkt.devId() );
		do_log = true; // link this rate-limited log to other related logs
	}

	bool reconnected = false;

	uint32_t mapped_dev = allocDev( inPkt.devId(), srcTag,
		do_log, reconnected );

	DeviceMap::iterator dit = m_devices.find(mapped_dev);

	/* Device exists already, ignore it if it didn't change. */
	if ( dit != m_devices.end() ) {

		DeviceVariables &dev = dit->second;
		ADARA::DeviceDescriptorPkt *devPkt =
			dynamic_cast<ADARA::DeviceDescriptorPkt *>(
				dev.m_descriptorPkt.get() );

		if ( dit->second.m_srcTag != srcTag ) {
			/* Rate-limited log that we got a Descriptor from
			 * an Incorrect Source Tag (i.e., wrong/unexpected Data Source)
			 * [*** Can this ever actually happen? The Lookup Key Includes
			 * the Source Tag!]
			 */
			std::string log_info;
			std::stringstream ss;
			ss << srcTag;
			if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
					RLL_DESC_INCORRECT_TAG, ss.str(),
					10, 3, 50, log_info ) ) {
				SMSControl *ctrl = SMSControl::getInstance();
				DEBUG( log_info
					<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
					<< "Got Descriptor from Incorrect Source Tag: "
					<< dit->second.m_srcTag << " != " << srcTag
					<< " (devId=" << inPkt.devId() << ")" );
			}
			return;
		}

		if ( devPkt->packet_length() == inPkt.packet_length()
				&& devPkt->description().size()
					== inPkt.description().size()
				&& !devPkt->description().compare(
					inPkt.description() ) ) {
			if ( do_log ) {
				DEBUG("Inbound Descriptor is Identical");
			}

			/* *IF* Device is an Old Device Re-Connected, then we need to
			 * Add the descriptor to the stream before we squirrel it away;
			 * this keeps us from writing it twice in close proximity if we
			 * start a new file with it.
			 */
			if ( reconnected ) {
				StorageManager::addPacket(
					dev.m_descriptorPkt->packet(),
					dev.m_descriptorPkt->packet_length(),
					true /* ignore_pkt_timestamp */,
					false /* check_old_containers */ );
			}

			return;
		}

		/* XXX It changed on us; need to flush out old variables
		 * (don't notify, we'll be pushing the new descriptor in a
		 * moment).
		 */
		if ( do_log ) {
			DEBUG("Updating Existing Descriptor");
		}
		m_devices.erase(dit);

		// Empty Descriptor XML, "Undefine" Device...!
		if ( inPkt.description().empty() )
		{
			if ( do_log ) {
				DEBUG( "Empty Descriptor XML, Undefining Device devId="
					<< inPkt.devId() );
			}
			return;
		}
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::DeviceDescriptorPkt> ddp;
	ddp = boost::make_shared<ADARA::DeviceDescriptorPkt>(inPkt);
	ddp->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(ddp);

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 */
	StorageManager::addPacket( pkt->packet(), pkt->packet_length(),
		true /* ignore_pkt_timestamp */,
		false /* check_old_containers */ );
	m_devices[mapped_dev].m_descriptorPkt = pkt;
	m_devices[mapped_dev].m_devId = inPkt.devId();
	m_devices[mapped_dev].m_srcTag = srcTag;
}

void MetaDataMgr::addFastMetaDDP( const timespec &ts, uint32_t mapped_dev,
		const std::string &ddp )
{
	/* Rate-limited log that we Added a New Fast Meta-Data DDP (Descriptor)
	 */
	std::string log_info;
	if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
			RLL_ADD_FAST_META_DDP, "none",
			60, 10, 10, log_info ) ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG( log_info
			<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "addFastMetaDDP(): Add New Device mapped_dev=" << mapped_dev
			<< " (srcTag=0 devId=-1)" );
	}

	DeviceMap::iterator dit = m_devices.find(mapped_dev);
	if ( dit != m_devices.end() ) {
		/* Rate-limited logging of adding existing device? */
		std::string log_info;
		std::stringstream ss;
		ss << mapped_dev;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_ADD_EXISTING_DEVICE, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "addFastMetaDDP():"
				<< " Tried to Add Existing (Mapped) Device"
				<< " mapped_dev=" << mapped_dev );
		}
		return;
	}

	uint32_t size = (ddp.size() + 3) & ~3;
	size += 2 * sizeof(uint32_t) + sizeof(ADARA::Header);

	/* Build the base DDP packet */
	uint32_t pkt[size / sizeof(uint32_t)];
	memset( pkt, 0, sizeof(pkt) );
	pkt[0] = size - sizeof(ADARA::Header);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::DEVICE_DESC_TYPE,
		ADARA::PacketType::DEVICE_DESC_VERSION );
	pkt[2] = ts.tv_sec - ADARA::EPICS_EPOCH_OFFSET;
	pkt[3] = ts.tv_nsec;
	pkt[4] = mapped_dev;
	pkt[5] = ddp.size();
	memcpy( pkt + 6, ddp.data(), ddp.size() );

	/* Wrap our buffered packet in an ADARA packet object so
	 * we can make a copy to store in our device map.
	 */
	ADARA::Packet wrapped( (uint8_t *) pkt, size );

	/* Add the descriptor to the stream before we squirrel it away; this
	 * keeps us from writing it twice in close proximity if we start
	 * a new file with it.
	 *
	 * We may get called before the StorageManager is fully initialized;
	 * if we are not currently streaming data, then just store it in
	 * our tracking structures; we'll put it in the stream when it gets
	 * started.
	 */
	if ( StorageManager::streaming() ) {
		SMSControl *ctrl = SMSControl::getInstance();
		DEBUG( ( ctrl->getRecording() ? "[RECORDING] " : "" )
			<< "addFastMetaDDP(): Already Streaming,"
			<< " So Add New Device Descriptor Packet Now"
			<< " mapped_dev=" << mapped_dev
			<< " (srcTag=0 devId=-1)" );
		StorageManager::addPacket( pkt, size,
			true /* ignore_pkt_timestamp */,
			false /* check_old_containers */ );
	}
	m_devices[mapped_dev].m_descriptorPkt =
		boost::make_shared<ADARA::Packet>(wrapped);
	m_devices[mapped_dev].m_devId = -1; // FastMetaDDP...!
	m_devices[mapped_dev].m_srcTag = 0;
}

void MetaDataMgr::updateValue( const ADARA::VariableU32Pkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32): Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32Pkt> vup;
	vup = boost::make_shared<ADARA::VariableU32Pkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::VariableDoublePkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoublePkt> vup;
	vup = boost::make_shared<ADARA::VariableDoublePkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::VariableStringPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_STR_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(String):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableStringPkt> vup;
	vup = boost::make_shared<ADARA::VariableStringPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::VariableU32ArrayPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(U32 Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableU32ArrayPkt> vup;
	vup = boost::make_shared<ADARA::VariableU32ArrayPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::VariableDoubleArrayPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Double Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::VariableDoubleArrayPkt> vup;
	vup = boost::make_shared<ADARA::VariableDoubleArrayPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::MultVariableU32Pkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Mult U32):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::MultVariableU32Pkt> vup;
	vup = boost::make_shared<ADARA::MultVariableU32Pkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::MultVariableDoublePkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Mult Double):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::MultVariableDoublePkt> vup;
	vup = boost::make_shared<ADARA::MultVariableDoublePkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::MultVariableStringPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_STR_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Mult String):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::MultVariableStringPkt> vup;
	vup = boost::make_shared<ADARA::MultVariableStringPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue( const ADARA::MultVariableU32ArrayPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_U32_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Mult U32 Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::MultVariableU32ArrayPkt> vup;
	vup = boost::make_shared<ADARA::MultVariableU32ArrayPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::updateValue(
		const ADARA::MultVariableDoubleArrayPkt &inPkt,
		uint32_t srcTag )
{
	uint32_t mapped_dev = lookupMappedDeviceId( inPkt.devId(), srcTag );

	if ( !mapped_dev ) {
		/* Rate-limited logging of Device/Source Tag Lookup failed...? */
		std::string log_info;
		std::stringstream ss;
		ss << inPkt.devId() << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_UNABLE_REMAP_DBL_VAR, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateValue(Mult Double Array):"
				<< " Device Lookup Failed for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << inPkt.devId()
				<< " varId=" << inPkt.varId() );
		}
		return;
	}

	/* Fix the device id in the packet before further processing... */
	boost::shared_ptr<ADARA::MultVariableDoubleArrayPkt> vup;
	vup = boost::make_shared<ADARA::MultVariableDoubleArrayPkt>(inPkt);
	vup->remapDeviceId(mapped_dev);
	ADARA::PacketSharedPtr pkt(vup);

	updateVariable( mapped_dev, inPkt.varId(), pkt, srcTag );
}

void MetaDataMgr::extractLastValue( ADARA::MultVariableU32Pkt inPkt,
		ADARA::PacketSharedPtr &outPkt )
{
	// Snag Last Value & TOF from Multiple Variable Value Packet...

	std::vector<uint32_t> vals = inPkt.values();
	std::vector<uint32_t> tofs = inPkt.tofs();

	uint32_t numVals = inPkt.numValues();

	uint32_t val = vals[ numVals - 1 ];

	uint32_t tof = tofs[ numVals - 1 ];

	// Create New Single Variable Value Packet...

	uint32_t pkt[4 + (sizeof(ADARA::Header) / sizeof(uint32_t))];

	pkt[0] = 4 * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_U32_TYPE,
		ADARA::PacketType::VAR_VALUE_U32_VERSION );

	// Convert Timestamp Back into EPICS Time & Add TOF Value...

	struct timespec ts = inPkt.timestamp();

	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	ts.tv_nsec += tof;

	while ( ts.tv_nsec >= NANO_PER_SECOND_LL ) {
		ts.tv_nsec -= NANO_PER_SECOND_LL;
		ts.tv_sec++;
	}

	pkt[2] = ts.tv_sec;
	pkt[3] = ts.tv_nsec;

	// Populate Variable Value Packet with Last Value from Multiple...

	pkt[4] = inPkt.devId();
	pkt[5] = inPkt.varId();

	pkt[6] = inPkt.status() << 16;
	pkt[6] |= inPkt.severity();

	pkt[7] = val;

	ADARA::VariableU32Pkt vvu( (const uint8_t *) pkt, sizeof(pkt) );

	// Return New Packet Shared Pointer...
	boost::shared_ptr<ADARA::VariableU32Pkt> vup;
	vup = boost::make_shared<ADARA::VariableU32Pkt>(vvu);
	outPkt = vup;

	// DEBUG("extractLastValue(MultVariableU32Pkt):"
		// << " Extracted Last Value for Prologue = " << val
		// << " at " << ts.tv_sec
		// << "." << std::setfill('0') << std::setw(9)
		// << ts.tv_nsec << std::setw(0));
}

void MetaDataMgr::extractLastValue( ADARA::MultVariableDoublePkt inPkt,
		ADARA::PacketSharedPtr &outPkt )
{
	// Snag Last Value & TOF from Multiple Variable Value Packet...

	std::vector<double> vals = inPkt.values();
	std::vector<uint32_t> tofs = inPkt.tofs();

	uint32_t numVals = inPkt.numValues();

	double val = vals[ numVals - 1 ];

	uint32_t tof = tofs[ numVals - 1 ];

	// Create New Single Variable Value Packet...

	uint32_t size = 3 + ( sizeof(ADARA::Header) / sizeof(uint32_t) )
		+ ( sizeof(double) / sizeof(uint32_t) );

	uint32_t pkt[ size ];

	pkt[0] = sizeof(double) + ( 3 * sizeof(uint32_t) );
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_DOUBLE_TYPE,
		ADARA::PacketType::VAR_VALUE_DOUBLE_VERSION );

	// Convert Timestamp Back into EPICS Time & Add TOF Value...

	struct timespec ts = inPkt.timestamp();

	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	ts.tv_nsec += tof;

	while ( ts.tv_nsec >= NANO_PER_SECOND_LL ) {
		ts.tv_nsec -= NANO_PER_SECOND_LL;
		ts.tv_sec++;
	}

	pkt[2] = ts.tv_sec;
	pkt[3] = ts.tv_nsec;

	// Populate Variable Value Packet with Last Value from Multiple...

	pkt[4] = inPkt.devId();
	pkt[5] = inPkt.varId();

	pkt[6] = inPkt.status() << 16;
	pkt[6] |= inPkt.severity();

	double *ptr = (double *) &(pkt[7]);
	*ptr = val;

	ADARA::VariableDoublePkt vvu( (const uint8_t *) pkt, sizeof(pkt) );

	// Return New Packet Shared Pointer...
	boost::shared_ptr<ADARA::VariableDoublePkt> vup;
	vup = boost::make_shared<ADARA::VariableDoublePkt>(vvu);
	outPkt = vup;

	// DEBUG("extractLastValue(MultVariableDoublePkt):"
		// << " Extracted Last Value for Prologue = " << val
		// << " at " << ts.tv_sec
		// << "." << std::setfill('0') << std::setw(9)
		// << ts.tv_nsec << std::setw(0));
}

void MetaDataMgr::extractLastValue( ADARA::MultVariableStringPkt inPkt,
		ADARA::PacketSharedPtr &outPkt )
{
	// Snag Last Value & TOF from Multiple Variable Value Packet...

	std::vector<std::string> vals = inPkt.values();
	std::vector<uint32_t> tofs = inPkt.tofs();

	uint32_t numVals = inPkt.numValues();

	std::string val = vals[ numVals - 1 ];

	uint32_t tof = tofs[ numVals - 1 ];

	// Create New Single Variable Value Packet...

	uint32_t roundup = ( val.size() + sizeof(uint32_t) - 1 )
		/ sizeof(uint32_t);

	uint32_t size = 4 + ( sizeof(ADARA::Header) / sizeof(uint32_t) )
		+ roundup;

	uint32_t pkt[ size ];

	pkt[0] = ( roundup + 4 ) * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_STRING_TYPE,
		ADARA::PacketType::VAR_VALUE_STRING_VERSION );

	// Convert Timestamp Back into EPICS Time & Add TOF Value...

	struct timespec ts = inPkt.timestamp();

	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	ts.tv_nsec += tof;

	while ( ts.tv_nsec >= NANO_PER_SECOND_LL ) {
		ts.tv_nsec -= NANO_PER_SECOND_LL;
		ts.tv_sec++;
	}

	pkt[2] = ts.tv_sec;
	pkt[3] = ts.tv_nsec;

	// Populate Variable Value Packet with Last Value from Multiple...

	pkt[4] = inPkt.devId();
	pkt[5] = inPkt.varId();

	pkt[6] = inPkt.status() << 16;
	pkt[6] |= inPkt.severity();

	pkt[7] = val.size();

	memcpy( pkt + 8, val.data(), val.size() );

	ADARA::VariableStringPkt vvu( (const uint8_t *) pkt, sizeof(pkt) );

	// Return New Packet Shared Pointer...
	boost::shared_ptr<ADARA::VariableStringPkt> vup;
	vup = boost::make_shared<ADARA::VariableStringPkt>(vvu);
	outPkt = vup;

	// DEBUG("extractLastValue(MultVariableStringPkt):"
		// << " Extracted Last Value for Prologue = " << val
		// << " at " << ts.tv_sec
		// << "." << std::setfill('0') << std::setw(9)
		// << ts.tv_nsec << std::setw(0));
}

void MetaDataMgr::extractLastValue( ADARA::MultVariableU32ArrayPkt inPkt,
		ADARA::PacketSharedPtr &outPkt )
{
	// Snag Last Value & TOF from Multiple Variable Value Packet...

	std::vector< std::vector<uint32_t> > vals = inPkt.values();
	std::vector<uint32_t> tofs = inPkt.tofs();

	uint32_t numVals = inPkt.numValues();

	std::vector<uint32_t> val = vals[ numVals - 1 ];

	uint32_t tof = tofs[ numVals - 1 ];

	// Create New Single Variable Value Packet...

	uint32_t size = 4 + ( sizeof(ADARA::Header) / sizeof(uint32_t) )
		+ val.size();

	uint32_t pkt[ size ];

	pkt[0] = ( val.size() + 4 ) * sizeof(uint32_t);
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_U32_ARRAY_TYPE,
		ADARA::PacketType::VAR_VALUE_U32_ARRAY_VERSION );

	// Convert Timestamp Back into EPICS Time & Add TOF Value...

	struct timespec ts = inPkt.timestamp();

	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	ts.tv_nsec += tof;

	while ( ts.tv_nsec >= NANO_PER_SECOND_LL ) {
		ts.tv_nsec -= NANO_PER_SECOND_LL;
		ts.tv_sec++;
	}

	pkt[2] = ts.tv_sec;
	pkt[3] = ts.tv_nsec;

	// Populate Variable Value Packet with Last Value from Multiple...

	pkt[4] = inPkt.devId();
	pkt[5] = inPkt.varId();

	pkt[6] = inPkt.status() << 16;
	pkt[6] |= inPkt.severity();

	pkt[7] = val.size();

	memcpy( pkt + 8, val.data(), val.size() * sizeof(uint32_t) );

	ADARA::VariableU32ArrayPkt vvu( (const uint8_t *) pkt, sizeof(pkt) );

	// Return New Packet Shared Pointer...
	boost::shared_ptr<ADARA::VariableU32ArrayPkt> vup;
	vup = boost::make_shared<ADARA::VariableU32ArrayPkt>(vvu);
	outPkt = vup;

	// DEBUG("extractLastValue(MultVariableU32ArrayPkt):"
		// << " Extracted Last Value for Prologue, size=" << val.size()
		// << " at " << ts.tv_sec
		// << "." << std::setfill('0') << std::setw(9)
		// << ts.tv_nsec << std::setw(0));
}

void MetaDataMgr::extractLastValue(
		ADARA::MultVariableDoubleArrayPkt inPkt,
		ADARA::PacketSharedPtr &outPkt )
{
	// Snag Last Value & TOF from Multiple Variable Value Packet...

	std::vector< std::vector<double> > vals = inPkt.values();
	std::vector<uint32_t> tofs = inPkt.tofs();

	uint32_t numVals = inPkt.numValues();

	std::vector<double> val = vals[ numVals - 1 ];

	uint32_t tof = tofs[ numVals - 1 ];

	// Create New Single Variable Value Packet...

	uint32_t size = 4 + ( sizeof(ADARA::Header) / sizeof(uint32_t) )
		+ ( val.size() * sizeof(double) / sizeof(uint32_t) );

	uint32_t pkt[ size ];

	pkt[0] = ( val.size() * sizeof(double) ) + ( 4 * sizeof(uint32_t) );
	pkt[1] = ADARA_PKT_TYPE(
		ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_TYPE,
		ADARA::PacketType::VAR_VALUE_DOUBLE_ARRAY_VERSION );

	// Convert Timestamp Back into EPICS Time & Add TOF Value...

	struct timespec ts = inPkt.timestamp();

	ts.tv_sec -= ADARA::EPICS_EPOCH_OFFSET;

	ts.tv_nsec += tof;

	while ( ts.tv_nsec >= NANO_PER_SECOND_LL ) {
		ts.tv_nsec -= NANO_PER_SECOND_LL;
		ts.tv_sec++;
	}

	pkt[2] = ts.tv_sec;
	pkt[3] = ts.tv_nsec;

	// Populate Variable Value Packet with Last Value from Multiple...

	pkt[4] = inPkt.devId();
	pkt[5] = inPkt.varId();

	pkt[6] = inPkt.status() << 16;
	pkt[6] |= inPkt.severity();

	pkt[7] = val.size();

	memcpy( pkt + 8, val.data(), val.size() * sizeof(double) );

	ADARA::VariableDoubleArrayPkt vvu(
		(const uint8_t *) pkt, sizeof(pkt) );

	// Return New Packet Shared Pointer...
	boost::shared_ptr<ADARA::VariableDoubleArrayPkt> vup;
	vup = boost::make_shared<ADARA::VariableDoubleArrayPkt>(vvu);
	outPkt = vup;

	// DEBUG("extractLastValue(MultVariableDoubleArrayPkt):"
		// << " Extracted Last Value for Prologue, size=" << val.size()
		// << " at " << ts.tv_sec
		// << "." << std::setfill('0') << std::setw(9)
		// << ts.tv_nsec << std::setw(0));
}

void MetaDataMgr::updateMappedVariable(
		uint32_t mapped_dev, uint32_t varId,
		const uint8_t *data, uint32_t size )
{
	/* We need to make a copy of the packet, but can only copy from
	 * another Packet object, so create a wrapper object then copy it.
	 */
	ADARA::Packet pkt( data, sizeof(size) );
	ADARA::PacketSharedPtr copy(new ADARA::Packet(pkt));
	updateVariable( mapped_dev, varId, copy, 0 );
}

void MetaDataMgr::updateVariable( uint32_t dev, uint32_t varId,
		ADARA::PacketSharedPtr &inPkt, uint32_t srcTag )
{
	DeviceMap::iterator dit = m_devices.find(dev);

	if ( dit == m_devices.end() ) {
		/* Rate-limited log that we got a variable update without
		 * the corresponding device descriptor.
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_NO_DESC, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable(): Got Variable Without a Descriptor!"
				<< " srcTag=" << srcTag
				<< " devId=" << dev
				<< " varId=" << varId );
		}
		return;
	}

	if ( dit->second.m_srcTag != srcTag ) {
		/* Rate-limited log that we got a Variable Update with
		 * an Incorrect srcTag (ie, Wrong Data Source)
		 */
		std::string log_info;
		std::stringstream ss;
		ss << dev << "/" << srcTag;
		if ( RateLimitedLogging::checkLog( RLLHistory_MetaDataMgr,
				RLL_VAR_UPDATE_BAD_TAG, ss.str(),
				60, 3, 10, log_info ) ) {
			SMSControl *ctrl = SMSControl::getInstance();
			ERROR( log_info
				<< ( ctrl->getRecording() ? "[RECORDING] " : "" )
				<< "updateVariable():"
				<< " Device Source Tag Mismatch for Variable!"
				<< " srcTag=" << srcTag
				<< " devId=" << dev
				<< " varId=" << varId
				<< ", Expected srcTag=" << dit->second.m_srcTag );
		}
		return;
	}

	/* Remove the old variable value from our map before we add the
	 * update to the stream; this keeps us from writing out the old
	 * value in a prologue if we start a new file on this update.
	 */
	VariablePktMap &varPktMap = dit->second.m_variablePkts;
	VariablePktMap::iterator vit = varPktMap.find(varId);

	if ( vit != varPktMap.end() )
		varPktMap.erase(vit);

	// Since PV Value Update TimeStamps can be "Old" Relative to
	// the Current Run Time (because they haven't changed in a while),
	// We Need to "Ignore the Packet TimeStamp" when Writing Them;
	// They Need to Just Go into the "Current" Storage Container.
	StorageManager::addPacket( inPkt->packet(), inPkt->packet_length(),
		true /* ignore_pkt_timestamp */ );

	varPktMap[varId] = inPkt;
}

void MetaDataMgr::onPrologue( bool UNUSED(capture_last) )
{
	DeviceMap::iterator dit, dend = m_devices.end();

	for ( dit = m_devices.begin(); dit != dend; ++dit ) {

		DeviceVariables &dev = dit->second;
		ADARA::Packet *dev_pkt = dev.m_descriptorPkt.get();

		/* Push out the device descriptor before the variable values */
		StorageManager::addPrologue( dev_pkt->packet(),
			dev_pkt->packet_length() );

		VariablePktMap &varPkts = dev.m_variablePkts;
		VariablePktMap::iterator vit, vend = varPkts.end();

		for ( vit = varPkts.begin(); vit != vend; ++vit ) {

			ADARA::Packet *var_pkt = vit->second.get();

			// Handle Multiple Variable Value Packets!
			// (Extract "Last" Variable Value and Create
			// New Single Variable Value Packet for Prologue...)

			ADARA::PacketSharedPtr newPkt;
			bool is_mult = false;

			if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_U32_TYPE )
			{
				ADARA::MultVariableU32Pkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_TYPE )
			{
				ADARA::MultVariableDoublePkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_STRING_TYPE )
			{
				ADARA::MultVariableStringPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_U32_ARRAY_TYPE )
			{
				ADARA::MultVariableU32ArrayPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}
			else if ( var_pkt->base_type()
					== ADARA::PacketType::MULT_VAR_VALUE_DOUBLE_ARRAY_TYPE)
			{
				ADARA::MultVariableDoubleArrayPkt mult_var_pkt(
					var_pkt->packet(), var_pkt->packet_length() );
				extractLastValue( mult_var_pkt, newPkt );
				is_mult = true;
			}

			if ( is_mult )
			{
				// Replace Multiple Variable Value Packet for
				// This Device PV with New Single Variable Value Packet
				vit->second.swap( newPkt );

				// Use New Packet in Prologue...
				var_pkt = vit->second.get();
			}

			StorageManager::addPrologue( var_pkt->packet(),
				var_pkt->packet_length() );
		}
	}
}

