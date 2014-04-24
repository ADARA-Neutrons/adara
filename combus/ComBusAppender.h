#ifndef COMBUSAPPENDER_H
#define COMBUSAPPENDER_H

#include "ComBus.h"
#include <log4cxx/appenderskeleton.h>
#include <log4cxx/spi/loggingevent.h>


namespace ADARA {
namespace ComBus {

using namespace log4cxx;

class Log4cxxAppender : public log4cxx::AppenderSkeleton
{
public:
    class ClazzLog4cxxAppender : public helpers::Class
    {
    public:
            ClazzLog4cxxAppender() : helpers::Class() {}
            virtual ~ClazzLog4cxxAppender() {}
            virtual log4cxx::LogString getName() const { return LOG4CXX_STR("Log4cxxAppender"); }
            virtual helpers::ObjectPtr newInstance() const
            {
                    return new Log4cxxAppender();
            }
    };
    virtual const helpers::Class& getClass() const;
    static const helpers::Class& getStaticClass();
    static const log4cxx::helpers::ClassRegistration& registerClass();

    BEGIN_LOG4CXX_CAST_MAP()
        LOG4CXX_CAST_ENTRY( Log4cxxAppender)
        LOG4CXX_CAST_ENTRY_CHAIN( AppenderSkeleton )
    END_LOG4CXX_CAST_MAP()

    Log4cxxAppender()
        : m_connection(Connection::getInst())
    {
    }

    ~Log4cxxAppender()
    {
    }

    void append( const log4cxx::spi::LoggingEventPtr& event, log4cxx::helpers::Pool& p )
    {
        const log4cxx::spi::LocationInfo &loc = event->getLocationInformation();
        m_connection.sendLog( event->getMessage(), getLogLevel( event->getLevel()),
                              loc.getFileName(), loc.getLineNumber() );
    }

    void close()
    {
        closed = true;
    }

    bool isClosed() const
    {
        return closed;
    }

    bool requiresLayout() const
    {
        return false;
    }

private:
    static LogLevel getLogLevel( const log4cxx::LevelPtr &a_level )
    {
        switch ( a_level->toInt() )
        {
        case log4cxx::Level::FATAL_INT:
            return ADARA::ComBus::LOG_FATAL;
        case log4cxx::Level::ERROR_INT:
            return ADARA::ComBus::LOG_ERROR;
        case log4cxx::Level::WARN_INT:
            return ADARA::ComBus::LOG_WARN;
        case log4cxx::Level::INFO_INT:
            return ADARA::ComBus::LOG_INFO;
        case log4cxx::Level::DEBUG_INT:
            return ADARA::ComBus::LOG_DEBUG;
        case log4cxx::Level::TRACE_INT:
            return ADARA::ComBus::LOG_TRACE;
        }
    }

    Connection      &m_connection;
};

typedef helpers::ObjectPtrT<Log4cxxAppender> Log4cxxAppenderPtr;

}}

#endif // COMBUSAPPENDER_H
