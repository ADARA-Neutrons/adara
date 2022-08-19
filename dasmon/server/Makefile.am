
if BUILD_DASMON_SERVER
noinst_PROGRAMS += dasmon/server/dasmond
endif

dasmon_server_dasmond_SOURCES = dasmon/server/main.cpp dasmon/server/StreamAnalyzer.cpp \
        dasmon/server/StreamMonitor.cpp dasmon/server/ComBusRouter.cpp \
        combus/ComBus.cpp dasmon/server/engine/RuleEngine.cpp \
        dasmon/server/engine/muParser.cpp dasmon/server/engine/muParserTokenReader.cpp \
        dasmon/server/engine/muParserError.cpp dasmon/server/engine/muParserCallback.cpp \
        dasmon/server/engine/muParserBytecode.cpp dasmon/server/engine/muParserBase.cpp \
        $(POSIX_PARSER)

dasmon_server_dasmond_CPPFLAGS = -Idasmon/common -Icombus \
			-Idasmon/server/engine $(libxml_CPPFLAGS) \
			-I/usr/pgsql-14/include \
			$(activemq_CPPFLAGS) $(apr_CPPFLAGS) \
			$(COMMON_CPPFLAGS) $(AM_CPPFLAGS) -DSYSLOG_LOGGING

# TODO clean this up to compile with these warnings enabled
dasmon_server_dasmond_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
dasmon_server_dasmond_LDADD = -lboost_program_options -lboost_thread-mt \
			-L/usr/pgsql-14/lib/ \
			-lboost_filesystem -lpq $(activemq_LIBS) $(apr_LIBS) \
			$(libxml_LIBS)
