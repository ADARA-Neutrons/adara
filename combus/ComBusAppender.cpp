#include "ComBusAppender.h"

//using namespace log4cxx;

//namespace ADARA {
//namespace ComBus {

#if 0
#define IMPLEMENT_LOG4CXX_OBJECT(object)\
const log4cxx::helpers::Class& object::getClass() const { return getStaticClass(); }\
const log4cxx::helpers::Class& object::getStaticClass() { \
   static Clazz##object theClass;                         \
   return theClass;                                       \
}                                                                      \
const log4cxx::helpers::ClassRegistration& object::registerClass() {   \
    static log4cxx::helpers::ClassRegistration classReg(object::getStaticClass); \
    return classReg; \
}\
namespace log4cxx { namespace classes { \
const log4cxx::helpers::ClassRegistration& object##Registration = object::registerClass(); \
} }
#endif

const log4cxx::helpers::Class&
ADARA::ComBus::Log4cxxAppender::getClass() const
{
    return getStaticClass();
}

const log4cxx::helpers::Class&
ADARA::ComBus::Log4cxxAppender::getStaticClass()
{
    static ClazzLog4cxxAppender theClass;
    return theClass;
}

const log4cxx::helpers::ClassRegistration&
ADARA::ComBus::Log4cxxAppender::registerClass()
{
    static log4cxx::helpers::ClassRegistration classReg(ADARA::ComBus::Log4cxxAppender::getStaticClass);
    return classReg;
}

namespace log4cxx {
namespace classes {

const log4cxx::helpers::ClassRegistration&
Log4cxxAppenderRegistration = ADARA::ComBus::Log4cxxAppender::registerClass();

}}



//}}

// vim: expandtab

