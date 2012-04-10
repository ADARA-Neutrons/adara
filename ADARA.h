#ifndef __ADARA_H
#define __ADARA_H

#include <string>

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

enum RunStatus {
	ADARA_RUN_STATUS_NO_RUN		= 0,
	ADARA_RUN_STATUS_NEW_RUN	= 1,
	ADARA_RUN_STATUS_RUN_EOF	= 2,
	ADARA_RUN_STATUS_RUN_BOF	= 3,
	ADARA_RUN_STATUS_END_RUN	= 4,
};

class Exception {
public:
	Exception(int err, const char *msg) : m_error(err), m_msg(msg) {};
	Exception(int err, const std::string &msg) :
			m_error(err), m_msg(msg) {};
	Exception(int err);

	int error(void) const { return m_error; }
	const std::string &message(void) const { return m_msg; }

private:
	int		m_error;
	std::string	m_msg;
};

enum {
	EPICS_EPOCH_OFFSET = 631152000
};

} /* namespace ADARA */

#endif /* __ADARA_H */
