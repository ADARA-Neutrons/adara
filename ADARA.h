#ifndef __ADARA_H
#define __ADARA_H

#include <string>
#include <stdexcept>

namespace ADARA {

#define ADARA_PKT_TYPE(type, ver)  ((((uint32_t) type) << 8) | (ver))
enum PacketType {
	ADARA_PKT_RAW_EVENT_V0			= ADARA_PKT_TYPE(0x0000, 0),
	ADARA_PKT_RTDL_V0			= ADARA_PKT_TYPE(0x0001, 0),
	ADARA_PKT_BANKED_EVENT_V0		= ADARA_PKT_TYPE(0x4000, 0),
	ADARA_PKT_BEAM_MONITOR_EVENT_V0		= ADARA_PKT_TYPE(0x4001, 0),
	ADARA_PKT_PIXEL_MAPPING_V0		= ADARA_PKT_TYPE(0x4002, 0),
	ADARA_PKT_RUN_STATUS_V0			= ADARA_PKT_TYPE(0x4003, 0),
	ADARA_PKT_RUN_INFO_V0			= ADARA_PKT_TYPE(0x4004, 0),
	ADARA_PKT_TRANSLATION_COMPLETE_V0	= ADARA_PKT_TYPE(0x4005, 0),
	ADARA_PKT_CLIENT_HELLO_V0		= ADARA_PKT_TYPE(0x4006, 0),
	ADARA_PKT_STATS_RESET_V0		= ADARA_PKT_TYPE(0x4007, 0),
	ADARA_PKT_SYNC_V0			= ADARA_PKT_TYPE(0x4008, 0),
	ADARA_PKT_HEARTBEAT_V0			= ADARA_PKT_TYPE(0x4009, 0),
	ADARA_PKT_DEVICE_DESC_V0		= ADARA_PKT_TYPE(0x8000, 0),
	ADARA_PKT_VAR_VALUE_U32_V0		= ADARA_PKT_TYPE(0x8001, 0),
	ADARA_PKT_VAR_VALUE_DOUBLE_V0		= ADARA_PKT_TYPE(0x8002, 0),
	ADARA_PKT_VAR_VALUE_STRING_V0		= ADARA_PKT_TYPE(0x8003, 0),
};

/* These are defined in the SNS Timing Master Functional System Description,
 * section 1.3.4.
 */
namespace PulseFlavor {
	enum Enum {
		NO_BEAM		  = 0,
		NORMAL		  = 1,
		NORMAL_TGT_1	  = 1,
		NORMAL_TGT_2	  = 2,
		DIAG_10us	  = 3,
		DIAG_50us	  = 4,
		DIAG_100us	  = 5,
		SPECIAL_PHYSICS_1 = 6,
		SPECIAL_PHYSICS_2 = 7
	};
}

enum RunStatus {
	ADARA_RUN_STATUS_NO_RUN		= 0,
	ADARA_RUN_STATUS_NEW_RUN	= 1,
	ADARA_RUN_STATUS_RUN_EOF	= 2,
	ADARA_RUN_STATUS_RUN_BOF	= 3,
	ADARA_RUN_STATUS_END_RUN	= 4,
};

namespace VariableStatus {
	enum Enum {
		OK			= 0,	// EPICS: NO_ALARM
		READ_ERROR		= 1,
		WRITE_ERROR		= 2,
		HIHI_LIMIT		= 3,
		HIGH_LIMIT		= 4,
		LOLO_LIMIT		= 5,
		LOW_LIMIT		= 6,
		BAD_STATE		= 7,
		CHANGED_STATE		= 8,
		NO_COMMUNICATION	= 9,
		COMMUNICATION_TIMEOUT	= 10,
		HARDWARE_LIMIT		= 11,
		BAD_CALCULATION		= 12,
		INVALID_SCAN		= 13,
		LINK_FAILED		= 14,
		INVALID_STATE		= 15,
		BAD_SUBROUTINE		= 16,
		UNDEFINED_ALARM		= 17,
		DISABLED		= 18,
		SIMULATED		= 19,
		READ_PERMISSION		= 20,
		WRITE_PERMISSION	= 21,
		NOT_REPORTED		= 0xffff,
	};
}

namespace VariableSeverity {
	enum Enum {
		OK			= 0,	// EPICS: NO_ALARM
		MINOR_ALARM		= 1,
		MAJOR_ALARM		= 2,
		INVALID			= 3,
		NOT_REPORTED		= 0xffff,
	};
}

class invalid_packet : public std::runtime_error {
public:
	explicit invalid_packet(const std::string &msg) : runtime_error(msg) {}
};

enum {
	EPICS_EPOCH_OFFSET = 631152000
};

} /* namespace ADARA */

#endif /* __ADARA_H */
