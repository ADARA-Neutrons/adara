#ifndef __LOGGING_H
#define __LOGGING_H

#ifdef USE_LOG4CXX_LOGGING

#include <log4cxx/logger.h>

using namespace log4cxx;

#define LOGGER(_name)	static LoggerPtr logger(Logger::getLogger(_name))

#define LOGGER_INIT()	{}

#define DEBUG(...)	do { LOG4CXX_DEBUG(logger, __VA_ARGS__); } while(0)
#define INFO(...)	do { LOG4CXX_INFO(logger, __VA_ARGS__); } while(0)
#define WARN(...)	do { LOG4CXX_WARN(logger, __VA_ARGS__); } while(0)
#define ERROR(...)	do { LOG4CXX_ERROR(logger, __VA_ARGS__); } while(0)

#elif USE_LOG4CPP_LOGGING

#include <string>

#include "log4cpp/Category.hh"
#include "log4cpp/CategoryStream.hh"

using namespace log4cpp;

#define LOGGER(_name) \
	static log4cpp::Category *logger; static const char *logger_name = _name;

#define LOGGER_INIT() \
	{ \
	    log4cpp::Category& log = \
			log4cpp::Category::getInstance(std::string(logger_name)); \
	    logger = &log; \
	}

#define DEBUG(_msg)	do { LOG4CPP_DEBUG_S((*logger)) << _msg; } while(0)
#define INFO(_msg)	do { LOG4CPP_INFO_S((*logger)) << _msg; } while(0)
#define WARN(_msg)	do { LOG4CPP_WARN_S((*logger)) << _msg; } while(0)
#define ERROR(_msg)	do { LOG4CPP_ERROR_S((*logger)) << _msg; } while(0)

#endif

#endif /* __LOGGING_H */
