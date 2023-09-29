#ifndef __ADARA_PACKETS_H
#define __ADARA_PACKETS_H

#define BOOST_BIND_GLOBAL_PLACEHOLDERS // Duh...
#include <boost/smart_ptr.hpp>

#include <stdint.h>
#include <string>
#include <sstream>
#include <string.h>
#include <vector>

#include "ADARA.h"

namespace ADARA {

class PacketHeader {
public:
	PacketHeader(const uint8_t *data) {
		const uint32_t *field = (const uint32_t *) data;

		m_payload_len = field[0];

		m_type = field[1];

		m_base_type = (PacketType::Type) ADARA_BASE_PKT_TYPE( m_type );

		m_version = (PacketType::Version) ADARA_PKT_VERSION( m_type );

		/* Convert EPICS epoch to Unix epoch,
		 * Jan 1, 1990 ==> Jan 1, 1970
		 */
		m_timestamp.tv_sec = field[2] + EPICS_EPOCH_OFFSET;
		m_timestamp.tv_nsec = field[3];

		m_pulseId = ((uint64_t) field[2]) << 32;
		m_pulseId |= field[3];
	}

	uint32_t type(void) const { return m_type; }
	PacketType::Type base_type(void) const { return m_base_type; }
	PacketType::Version version(void) const { return m_version; }
	uint32_t payload_length(void) const { return m_payload_len; }
	const struct timespec &timestamp(void) const { return m_timestamp; }
	uint64_t pulseId(void) const { return m_pulseId; }
	uint32_t packet_length(void) const { return m_payload_len + 16; }

	static uint32_t header_length(void) { return 16; }

protected:
	uint32_t m_payload_len;
	uint32_t m_type;
	PacketType::Type m_base_type;
	PacketType::Version m_version;
	struct timespec m_timestamp;
	uint64_t m_pulseId;

	/* Don't allow the default constructor */
	PacketHeader();
};

class Packet : public PacketHeader {
public:
	Packet(const uint8_t *data, uint32_t len);
	Packet(const Packet &pkt);

	virtual ~Packet();

	const uint8_t *packet(void) const { return m_data; }
	const uint8_t *payload(void) const {
		return m_data + header_length();
	}

	// ADARA Munge "Packet Editing" Methods... ;-b

	// Edit/Change the PulseId (or Packet "Timestamp")...
	void setPulseId(uint64_t pulseId)
	{
		uint32_t *field = (uint32_t *) m_data;
		field[2] = pulseId >> 32;
		field[3] = pulseId;
	}

	// Edit/Change the Packet Type Version...
	// (Yes, Believe It or Not, We Need This Capability... ;-b)
	void remapVersion( PacketType::Version version ) {
		m_version = version;
		m_type = ADARA_PKT_TYPE( m_base_type, m_version );
		uint32_t *field = (uint32_t *) m_data;
		field[1] = m_type;
	}

protected:
	const uint8_t *	m_data;
	uint32_t	m_len;
	bool		m_allocated;

private:
	/* Don't allow the default constructor or assignment operator */
	Packet();
	Packet &operator=(const Packet &pkt);
};

typedef boost::shared_ptr<ADARA::Packet> PacketSharedPtr;

class RawDataPkt : public Packet {
public:
	RawDataPkt(const RawDataPkt &pkt);

	uint32_t sourceID(void) const { return m_fields[0]; }
	bool endOfPulse(void) const { return !!(m_fields[1] & 0x80000000); }
	uint32_t pulseSeq(void) const { return (m_fields[1] >> 16) & 0x7fff; }
	uint32_t maxPulseSeq(void) const { return ( 0x7fff + 1 ); }
	uint32_t sourceSeq(void) const { return m_fields[1] & 0xffff; }
	uint32_t maxSourceSeq(void) const { return ( 0xffff + 1 ); }

	bool gotDataFlags(void) const { return m_version >= 0x01; }
	uint32_t dataFlags(void) const {
		if ( gotDataFlags() )
			return (m_fields[2] >> 27) & 0x1f;
		else return 0;
	}
	PulseFlavor::Enum flavor(void) const {
		return static_cast<PulseFlavor::Enum>
						((m_fields[2] >> 24) & 0x7);
	}
	uint32_t pulseCharge(void) const { return m_fields[2] & 0x00ffffff; }

	bool badVeto(void) const { return !!(m_fields[3] & 0x80000000); }
	bool badCycle(void) const { return !!(m_fields[3] & 0x40000000); }
	uint8_t timingStatus(void) const {
		return (uint8_t) (m_fields[3] >> 22);
	}
	uint16_t vetoFlags(void) const { return (m_fields[3] >> 10) & 0xfff; }
	uint16_t cycle(void) const { return m_fields[3] & 0x3ff; }

	uint32_t intraPulseTime(void) const { return m_fields[4]; }

	bool tofCorrected(void) const { return !!(m_fields[5] & 0x80000000); }
	uint32_t tofOffset(void) const { return m_fields[5] & 0x7fffffff; }
	uint32_t tofField(void) const { return m_fields[5]; }

	const Event *events(void) const { return (const Event *) &m_fields[6]; }
	uint32_t num_events(void) const {
		return (m_payload_len - 24) / (uint32_t) (2 * sizeof(uint32_t));
	}

private:
	const uint32_t *m_fields;

	RawDataPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
	friend class MappedDataPkt;
};

class MappedDataPkt : public RawDataPkt {
public:
	MappedDataPkt(const MappedDataPkt &pkt);
private:
	MappedDataPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RTDLPkt : public Packet {
public:
	RTDLPkt(const RTDLPkt &pkt);

	bool gotDataFlags(void) const { return m_version >= 0x01; }
	uint32_t dataFlags(void) const {
		if ( gotDataFlags() )
			return (m_fields[0] >> 27) & 0x1f;
		else return 0;
	}

	PulseFlavor::Enum flavor(void) const {
		return static_cast<PulseFlavor::Enum>
						((m_fields[0] >> 24) & 0x7);
	}

	uint32_t pulseCharge(void) const { return m_fields[0] & 0x00ffffff; }

	void setPulseCharge(uint32_t pulseCharge) {
		m_fields[0] &= 0xff000000;
		m_fields[0] |= pulseCharge & 0x00ffffff;
	}

	bool badVeto(void) const { return !!(m_fields[1] & 0x80000000); }
	bool badCycle(void) const { return !!(m_fields[1] & 0x40000000); }
	uint8_t timingStatus(void) const {
		return (uint8_t) (m_fields[1] >> 22);
	}

	uint16_t vetoFlags(void) const { return (m_fields[1] >> 10) & 0xfff; }

	void setVetoFlags(uint16_t vetoFlags) {
		m_fields[1] &= 0xffc003ff;
		m_fields[1] |= ( vetoFlags & 0xfff ) << 10;
	}

	uint16_t cycle(void) const { return m_fields[1] & 0x3ff; }
	uint32_t intraPulseTime(void) const { return m_fields[2]; }
	bool tofCorrected(void) const { return !!(m_fields[3] & 0x80000000); }
	uint32_t tofOffset(void) const { return m_fields[3] & 0x7fffffff; }
	uint32_t ringPeriod(void) const { return m_fields[4] & 0xffffff; }

	// accessor methods for optional FNA/Frame Data fields

	uint32_t FNA(uint32_t index) const
	{
		// If out of bounds, just return "0" for "Unused Frame"... ;-D
		if ( index > 24 )
			return( 0 );
		else
			return ( m_fields[ 5 + index ] >> 24 ) & 0xff;
	}

	uint32_t frameData(uint32_t index) const
	{
		// Out of bounds, return "-1" (0xffffff) for Bogus "Frame Data" ;-b
		if ( index > 24 )
			return( -1 );
		else
			return m_fields[ 5 + index ] & 0xffffff;
	}

private:
	// Note: RTDLPkt m_fields can't be "const", as we Modify Pulse Charge!
	uint32_t *m_fields;

	RTDLPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class SourceListPkt : public Packet {
public:
	SourceListPkt(const SourceListPkt &pkt);

	const uint32_t *ids(void) const { return (const uint32_t *) payload(); }
	uint32_t num_ids(void) const {
		return (uint32_t) payload_length() / (uint32_t) sizeof(uint32_t);
	}

private:
	SourceListPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

enum PulseFlags {
	ERROR_PIXELS        = 0x00001,
	PARTIAL_DATA        = 0x00002,
	PULSE_VETO          = 0x00004,
	MISSING_RTDL        = 0x00008,
	MAPPING_ERROR       = 0x00010,
	DUPLICATE_PULSE     = 0x00020,
	PCHARGE_UNCORRECTED = 0x00040,
	VETO_UNCORRECTED    = 0x00080,
	GOT_METADATA        = 0x00100,
	GOT_NEUTRONS        = 0x00200,
	HAS_STATES          = 0x00400,
};

class BankedEventPkt : public Packet {
public:
	BankedEventPkt(const BankedEventPkt &pkt);

	uint32_t pulseCharge(void) const { return m_fields[0]; }
	uint32_t pulseEnergy(void) const { return m_fields[1]; }
	uint32_t cycle(void) const { return m_fields[2]; }
	uint32_t vetoFlags(void) const { return (m_fields[3] >> 20) & 0xfff; }
	uint32_t flags(void) const { return m_fields[3] & 0xfffff; }

	// TODO implment bank/event accessors

private:
	const uint32_t *m_fields;

	BankedEventPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BankedEventStatePkt : public Packet {
public:
	BankedEventStatePkt(const BankedEventStatePkt &pkt);

	uint32_t pulseCharge(void) const { return m_fields[0]; }
	uint32_t pulseEnergy(void) const { return m_fields[1]; }
	uint32_t cycle(void) const { return m_fields[2]; }
	uint32_t vetoFlags(void) const { return (m_fields[3] >> 20) & 0xfff; }
	uint32_t flags(void) const { return m_fields[3] & 0xfffff; }

	// TODO implment bank/event accessors

private:
	const uint32_t *m_fields;

	BankedEventStatePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BeamMonitorPkt : public Packet {
public:
	BeamMonitorPkt(const BeamMonitorPkt &pkt);

	uint32_t pulseCharge(void) const { return m_fields[0]; }
	uint32_t pulseEnergy(void) const { return m_fields[1]; }
	uint32_t cycle(void) const { return m_fields[2]; }
	uint32_t vetoFlags(void) const { return (m_fields[3] >> 20) & 0xfff; }
	uint32_t flags(void) const { return m_fields[3] & 0xfffff; }

	// TODO implment monitor/event accessors

private:
	const uint32_t *m_fields;

	BeamMonitorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class PixelMappingPkt : public Packet {
public:
	PixelMappingPkt(const PixelMappingPkt &pkt);

	// TODO implement accessors for fields
	const uint8_t *mappingData(void) const {
		return( (const uint8_t *) &(m_fields[0]) );
	}

private:
	const uint32_t *m_fields;

	PixelMappingPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class PixelMappingAltPkt : public Packet {
public:
	PixelMappingAltPkt(const PixelMappingAltPkt &pkt);

	// TODO implement accessors for fields
	uint32_t numBanks(void) const { return m_fields[0]; }
	const uint8_t *mappingData(void) const {
		return( (const uint8_t *) &(m_fields[1]) );
	}

private:
	const uint32_t *m_fields;

	PixelMappingAltPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunStatusPkt : public Packet {
public:
	RunStatusPkt(const RunStatusPkt &pkt);

	uint32_t runNumber(void) const { return m_fields[0]; }
	uint32_t runStart(void) const { return m_fields[1]; }
	uint32_t fileNumber(void) const { return m_fields[2] & 0xffffff; }
	RunStatus::Enum status(void) const {
		return static_cast<RunStatus::Enum>(m_fields[2] >> 24);
	}

	void setRunStart(uint32_t runStart) // ;-b
	{
		uint32_t *field = (uint32_t *) m_fields;
		field[1] = runStart;
	}

	uint32_t pauseFileNumber(void) const {
		if ( m_version >= 0x01 ) {
			return m_fields[3] & 0xffffff;
		}
		else return( 0 );
	}
	uint32_t paused(void) const {
		if ( m_version >= 0x01 ) {
			return m_fields[3] >> 24;
		}
		else return( 0 );
	}
	uint32_t addendumFileNumber(void) const {
		if ( m_version >= 0x01 ) {
			return m_fields[4] & 0xffffff;
		}
		else return( 0 );
	}
	uint32_t addendum(void) const {
		if ( m_version >= 0x01 ) {
			return m_fields[4] >> 24;
		}
		else return( 0 );
	}

private:
	const uint32_t *m_fields;

	RunStatusPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class RunInfoPkt : public Packet {
public:
	RunInfoPkt(const RunInfoPkt &pkt);

	const std::string &info(void) const { return m_xml; }

private:
	std::string m_xml;

	RunInfoPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class TransCompletePkt : public Packet {
public:
	TransCompletePkt(const TransCompletePkt &pkt);

	uint16_t status(void) const { return m_status; }
	const std::string &reason(void) const { return m_reason; }

private:
	uint16_t m_status;
	std::string m_reason;

	TransCompletePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class ClientHelloPkt : public Packet {
public:
	ClientHelloPkt(const ClientHelloPkt &pkt);

	enum Flags {
		PAUSE_AGNOSTIC    = 0x0000,
		NO_PAUSE_DATA     = 0x0001,
		SEND_PAUSE_DATA   = 0x0002,
	};

	uint32_t requestedStartTime(void) const { return m_reqStart; }
	uint32_t clientFlags(void) const { return m_clientFlags; }

private:
	uint32_t m_reqStart;
	uint32_t m_clientFlags;

	ClientHelloPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class AnnotationPkt : public Packet {
public:
	AnnotationPkt(const AnnotationPkt &pkt);

	bool resetHint(void) const { return !!(m_fields[0] & 0x80000000); }
	MarkerType::Enum marker_type(void) const {
		uint16_t type = (m_fields[0] >> 16) & 0x7fff;
		return static_cast<MarkerType::Enum>(type);
	}
	uint32_t scanIndex(void) const { return m_fields[1]; }
	const std::string &comment(void) const {
		if (!m_comment.length() && (m_fields[0] & 0xffff)) {
			m_comment.assign((const char *) &m_fields[2],
					 m_fields[0] & 0xffff);
		}

		return m_comment;
	}

private:
	const uint32_t *m_fields;
	mutable std::string m_comment;

	AnnotationPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class SyncPkt : public Packet {
public:
	SyncPkt(const SyncPkt &pkt);

	const std::string signature(void) const { return m_signature; }
	uint64_t fileOffset(void) const { return m_offset; }
	const std::string comment(void) const { return m_comment; }

private:
	const uint32_t *m_fields;
	std::string m_signature;
	uint64_t m_offset;
	std::string m_comment;

	SyncPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class HeartbeatPkt : public Packet {
public:
	HeartbeatPkt(const HeartbeatPkt &pkt);

private:
	HeartbeatPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class GeometryPkt : public Packet {
public:
	GeometryPkt(const GeometryPkt &pkt);

	const std::string &info(void) const { return m_xml; }

private:
	std::string m_xml;

	GeometryPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class BeamlineInfoPkt : public Packet {
public:
	BeamlineInfoPkt(const BeamlineInfoPkt &pkt);

	const uint32_t &targetStationNumber(void) const
		{ return m_targetStationNumber; }

	const std::string &id(void) const { return m_id; }
	const std::string &shortName(void) const { return m_shortName; }
	const std::string &longName(void) const { return m_longName; }

private:
	uint32_t m_targetStationNumber;

	std::string m_id;
	std::string m_shortName;
	std::string m_longName;

	BeamlineInfoPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

enum DataFormat {
	EVENT_FORMAT    = 0x0001,
	HISTO_FORMAT    = 0x0002,
};

class BeamMonitorConfigPkt : public Packet {
public:
	BeamMonitorConfigPkt(const BeamMonitorConfigPkt &pkt);

	uint32_t beamMonCount(void) const { return m_fields[0]; }

	uint32_t bmonId(uint32_t index) const
	{
		if ( index < beamMonCount() ) {
			const uint32_t *section =
				(const uint32_t *) ( ((const char *) m_fields)
					+ sizeof(uint32_t) + ( index * m_sectionSize ) );
			return( section[0] );
		}
		else
			return( 0 );
	}

	uint32_t tofOffset(uint32_t index) const
	{
		if ( index < beamMonCount() ) {
			const uint32_t *section =
				(const uint32_t *) ( ((const char *) m_fields)
					+ sizeof(uint32_t) + ( index * m_sectionSize ) );
			return( section[1] );
		}
		else
			return( 0 );
	}

	uint32_t tofMax(uint32_t index) const
	{
		if ( index < beamMonCount() ) {
			const uint32_t *section =
				(const uint32_t *) ( ((const char *) m_fields)
					+ sizeof(uint32_t) + ( index * m_sectionSize ) );
			return( section[2] );
		}
		else
			return( 0 );
	}

	uint32_t tofBin(uint32_t index) const
	{
		if ( index < beamMonCount() ) {
			const uint32_t *section =
				(const uint32_t *) ( ((const char *) m_fields)
					+ sizeof(uint32_t) + ( index * m_sectionSize ) );
			return( section[3] );
		}
		else
			return( 0 );
	}

	double distance(uint32_t index) const
	{
		if ( index < beamMonCount() ) {
			const uint32_t *section =
				(const uint32_t *) ( ((const char *) m_fields)
					+ sizeof(uint32_t) + ( index * m_sectionSize ) );
			return( *( (const double *) &(section[4]) ) );
		}
		else
			return( 0.0 );
	}

	// Duh... All the Format Specifications Squirreled Away at the End...
	// - For Backwards-Compatible Wire Protocol... <sigh/> ;-)
	// ...It's Ok, Mantid Requires Them to All Be the Same Anyway...! ;-D
	uint32_t format(uint32_t index) const
	{
		if ( m_version >= 0x01 ) {
			if ( index < beamMonCount() ) {
				// Trailing "Format Appendix" Section... ;-b
				const uint32_t *format_section =
					(const uint32_t *) ( ((const char *) m_fields)
						+ sizeof(uint32_t)
						+ ( beamMonCount() * m_sectionSize ) );
				return( format_section[ index ] );
			}
			else
				return( HISTO_FORMAT );
		}
		// With Older Packet Protocol Versions (0x00), We _Only_ Sent the
		// BeamMonitorConfigPkt When We Were Histogramming the Beam Monitor.
		// The Default Case was Event Formatting, Where No Packet was Sent.
		else
			return( HISTO_FORMAT );
	}

	void countFormats(uint32_t &numEvent, uint32_t &numHisto) const
	{
		numEvent = 0;
		numHisto = 0;
		for (uint32_t i=0 ; i < beamMonCount() ; i++) {
			if ( format(i) == EVENT_FORMAT )
				numEvent++;
			else if ( format(i) == HISTO_FORMAT )
				numHisto++;
		}
	}

private:
	const uint32_t *m_fields;
	size_t m_sectionSize;

	BeamMonitorConfigPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class DetectorBankSetsPkt : public Packet {
public:
	DetectorBankSetsPkt(const DetectorBankSetsPkt &pkt);

	virtual ~DetectorBankSetsPkt();

	// Detector Bank Set Name, alphanumeric characters...
	static const size_t SET_NAME_SIZE = 16;

	// Throttle Suffix, alphanumeric, no spaces/punctuation...
	static const size_t THROTTLE_SUFFIX_SIZE = 16;

	uint32_t detBankSetCount(void) const { return m_fields[0]; }

	uint32_t sectionOffset(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return( m_sectionOffsets[index] );
		else
			return( 0 );   // Minimum Valid offset is always past Header...
	}

	std::string name(uint32_t index) const
	{
		if ( index < detBankSetCount() ) {
			char name_c[SET_NAME_SIZE + 1];   // give them an inch...
			memset( (void *) name_c, '\0', SET_NAME_SIZE + 1 );
			strncpy(name_c,
				(const char *) &(m_fields[ m_sectionOffsets[index] ]),
				SET_NAME_SIZE);
			return( std::string(name_c) );
		} else {
			return( "<Out Of Range!>" );
		}
	}

	uint32_t flags(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return m_fields[ m_sectionOffsets[index] + m_name_offset ];
		else
			return( 0 );
	}

	uint32_t bankCount(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return m_fields[ m_sectionOffsets[index] + m_name_offset + 1 ];
		else
			return( 0 );
	}

	const uint32_t *banklist(uint32_t index) const
	{
		if ( index < detBankSetCount() ) {
			return (const uint32_t *) &m_fields[ m_sectionOffsets[index]
				+ m_name_offset + 2 ];
		}
		else {
			// Shouldn't be asking for this if bankCount() returned 0...!
			return( (const uint32_t *) NULL );
		}
	}

	uint32_t tofOffset(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return m_fields[ m_after_banks_offset[index] ];
		else
			return( 0 );
	}

	uint32_t tofMax(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return m_fields[ m_after_banks_offset[index] + 1 ];
		else
			return( 0 );
	}

	uint32_t tofBin(uint32_t index) const
	{
		if ( index < detBankSetCount() )
			return m_fields[ m_after_banks_offset[index] + 2 ];
		else
			return( 0 );
	}

	double throttle(uint32_t index) const
	{
		if ( index < detBankSetCount() ) {
			return *(const double *) &m_fields[
				m_after_banks_offset[index] + 3 ];
		}
		else
			return( 0.0 );
	}

	std::string suffix(uint32_t index) const
	{
		if ( index < detBankSetCount() ) {
			char suffix_c[THROTTLE_SUFFIX_SIZE + 1];   // give them an inch
			memset( (void *) suffix_c, '\0', THROTTLE_SUFFIX_SIZE + 1 );
			strncpy(suffix_c,
				(const char *) &(m_fields[m_after_banks_offset[index] + 5]),
				THROTTLE_SUFFIX_SIZE);
			return( std::string(suffix_c) );
		} else {
			std::stringstream ss;
			ss << "out-of-range-";
			ss << index;
			return( ss.str() );
		}
	}

private:
	const uint32_t *m_fields;

	static const uint32_t m_name_offset =
		SET_NAME_SIZE / sizeof(uint32_t);

	static const uint32_t m_suffix_offset =
		THROTTLE_SUFFIX_SIZE / sizeof(uint32_t);

	uint32_t *m_sectionOffsets;

	uint32_t *m_after_banks_offset;

	DetectorBankSetsPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class DataDonePkt : public Packet {
public:
	DataDonePkt(const DataDonePkt &pkt);

private:
	DataDonePkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class DeviceDescriptorPkt : public Packet {
public:
	DeviceDescriptorPkt(const DeviceDescriptorPkt &pkt);

	uint32_t devId(void) const { return m_devId; }
	const std::string &description(void) const { return m_desc; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
	        fields[0] = dev;
		m_devId = dev;
	};

private:
	uint32_t m_devId;
	std::string m_desc;

	DeviceDescriptorPkt(const uint8_t *data, uint32_t len);

	friend class Parser;
};

class VariableU32Pkt : public Packet {
public:
	VariableU32Pkt(const VariableU32Pkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t value(void) const { return m_fields[3]; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	VariableU32Pkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;

	friend class Parser;
};

class VariableDoublePkt : public Packet {
public:
	VariableDoublePkt(const VariableDoublePkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	double value(void) const { return *((const double *) &m_fields[3]); }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	void updateValue(double value) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		*((double *) &fields[3]) = value;
	};

	VariableDoublePkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;

	friend class Parser;
};

class VariableStringPkt : public Packet {
public:
	VariableStringPkt(const VariableStringPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
						(m_fields[2] & 0xffff);
	}
	const std::string &value(void) const { return m_val; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	VariableStringPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::string m_val;

	friend class Parser;
};

class VariableU32ArrayPkt : public Packet {
public:
	VariableU32ArrayPkt(const VariableU32ArrayPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t elemCount(void) const { return m_fields[3]; }
	const std::vector<uint32_t> &value(void) const { return m_val; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	VariableU32ArrayPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<uint32_t> m_val;

	friend class Parser;
};

class VariableDoubleArrayPkt : public Packet {
public:
	VariableDoubleArrayPkt(const VariableDoubleArrayPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t elemCount(void) const { return m_fields[3]; }
	const std::vector<double> &value(void) const { return m_val; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	VariableDoubleArrayPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<double> m_val;

	friend class Parser;
};

class MultVariableU32Pkt : public Packet {
public:
	MultVariableU32Pkt(const MultVariableU32Pkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t numValues(void) const { return m_fields[3]; }
	const std::vector<uint32_t> &values(void) const { return m_vals; }
	const std::vector<uint32_t> &tofs(void) const { return m_tofs; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	MultVariableU32Pkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<uint32_t> m_vals;
	std::vector<uint32_t> m_tofs;

	friend class Parser;
};

class MultVariableDoublePkt : public Packet {
public:
	MultVariableDoublePkt(const MultVariableDoublePkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t numValues(void) const { return m_fields[3]; }
	const std::vector<double> &values(void) const { return m_vals; }
	const std::vector<uint32_t> &tofs(void) const { return m_tofs; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	void updateValue(double value) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		*((double *) &fields[3]) = value;
	};

	MultVariableDoublePkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<double> m_vals;
	std::vector<uint32_t> m_tofs;

	friend class Parser;
};

class MultVariableStringPkt : public Packet {
public:
	MultVariableStringPkt(const MultVariableStringPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
						(m_fields[2] & 0xffff);
	}
	uint32_t numValues(void) const { return m_fields[3]; }
	const std::vector<std::string> &values(void) const { return m_vals; }
	const std::vector<uint32_t> &tofs(void) const { return m_tofs; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	MultVariableStringPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<std::string> m_vals;
	std::vector<uint32_t> m_tofs;

	friend class Parser;
};

class MultVariableU32ArrayPkt : public Packet {
public:
	MultVariableU32ArrayPkt(const MultVariableU32ArrayPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t numValues(void) const { return m_fields[3]; }
	uint32_t elemCount(uint32_t index) const {
		return( ( index < numValues() ) ? m_vals[index].size() : 0 );
	}
	const std::vector<std::vector<uint32_t> > values(void) const {
		return m_vals;
	}
	const std::vector<uint32_t> &tofs(void) const { return m_tofs; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	MultVariableU32ArrayPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<std::vector<uint32_t> > m_vals;
	std::vector<uint32_t> m_tofs;

	friend class Parser;
};

class MultVariableDoubleArrayPkt : public Packet {
public:
	MultVariableDoubleArrayPkt(const MultVariableDoubleArrayPkt &pkt);

	uint32_t devId(void) const { return m_fields[0]; }
	uint32_t varId(void) const { return m_fields[1]; }
	VariableStatus::Enum status(void) const {
		return static_cast<VariableStatus::Enum> (m_fields[2] >> 16);
	}
	VariableSeverity::Enum severity(void) const {
		return static_cast<VariableSeverity::Enum>
							(m_fields[2] & 0xffff);
	}
	uint32_t numValues(void) const { return m_fields[3]; }
	uint32_t elemCount(uint32_t index) const {
		return( ( index < numValues() ) ? m_vals[index].size() : 0 );
	}
	const std::vector<std::vector<double> > values(void) const {
		return m_vals;
	}
	const std::vector<uint32_t> &tofs(void) const { return m_tofs; }

	void remapDeviceId(uint32_t dev) {
		uint32_t *fields = (uint32_t *)const_cast<uint8_t *>(payload());
		fields[0] = dev;
	};

	MultVariableDoubleArrayPkt(const uint8_t *data, uint32_t len);

private:
	const uint32_t *m_fields;
	std::vector<std::vector<double> > m_vals;
	std::vector<uint32_t> m_tofs;

	friend class Parser;
};

} /* namespacce ADARA */

#endif /* __ADARA_PACKETS_H */
