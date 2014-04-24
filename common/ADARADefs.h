#ifndef ADARADEFS_H
#define ADARADEFS_H

namespace ADARA {

/**
 * The Level enum is used to indicate severity of signals and log statements.
 * The values of the Level enum are identical to log4cxx log levels to simplify
 * integration.
 */
enum Level
{
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};


}

#endif // ADARADEFS_H
