#ifndef __LOGGING_H
#define __LOGGING_H

#include <log4cxx/logger.h>

using namespace log4cxx;

#define DEBUG(...)	do { LOG4CXX_DEBUG(logger, __VA_ARGS__); } while(0)
#define INFO(...)	do { LOG4CXX_INFO(logger, __VA_ARGS__); } while(0)
#define WARN(...)	do { LOG4CXX_WARN(logger, __VA_ARGS__); } while(0)
#define ERROR(...)	do { LOG4CXX_ERROR(logger, __VA_ARGS__); } while(0)

#endif /* __LOGGING_H */
