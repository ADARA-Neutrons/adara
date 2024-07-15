
#include "Logging.h"

LOGGER("QuickCounter");

#include <string>

#include <stdint.h>

#include "ADARA.h"
#include "FastMeta.h"
#include "QuickCounter.h"

QuickCounter::QuickCounter(struct FastMeta::Variable *var,
			uint32_t key):
		m_var(var), m_key(key)
{
	LOGGER_INIT();

	if ( var == NULL )
	{
		ERROR("QuickCounter(): NULL Fast Meta-Data Variable..."
			<< " Bailing...!!");
		return;
	}

	DEBUG("QuickCounter(): New Fast Meta-Data Counter Device"
		<< " [" << m_var->m_name << "]"
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec);

	m_ctrl = SMSControl::getInstance();
}

void QuickCounter::startCounting(void)
{
	DEBUG("startCounting(): Start Accumulating Statistics"
		<< " for Fast Meta-Data Counter Device"
		<< " [" << m_var->m_name << "]"
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec);
}

void QuickCounter::stopCounting(void)
{
	DEBUG("stopCounting(): Stop Accumulating Statistics"
		<< " for Fast Meta-Data Counter Device"
		<< " [" << m_var->m_name << "]"
		<< " PixelId Key " << std::hex << "0x" << m_key << std::dec);
}

