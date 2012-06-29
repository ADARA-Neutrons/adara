#ifndef __LOGGING_H
#define __LOGGING_H

#include <log4cxx/logger.h>

using namespace log4cxx;

#define DEBUG(...)	LOG4CXX_DEBUG(logger, __VA_ARGS__)
#define INFO(...)	LOG4CXX_INFO(logger, __VA_ARGS__)
#define WARN(...)	LOG4CXX_WARN(logger, __VA_ARGS__)
#define ERROR(...)	LOG4CXX_ERROR(logger, __VA_ARGS__)

#endif /* __LOGGING_H */
