
if BUILD_STC
bin_PROGRAMS += stc/stc
endif

stc_stc_SOURCES = stc/main.cpp stc/NxGen.cpp stc/StreamParser.cpp \
    stc/h5nx.cpp stc/ComBusTransMon.cpp combus/ComBus.cpp \
	stc/UserIdLdap.cpp $(POSIX_PARSER)

stc_stc_CPPFLAGS = -Istc $(libxml_CPPFLAGS) $(activemq_CPPFLAGS) \
		$(apr_CPPFLAGS) $(COMMON_CPPFLAGS) $(HDF5_CPPFLAGS) \
		$(AM_CPPFLAGS) -DSYSLOG_LOGGING

# TODO clean this up to compile with these warnings enabled
stc_stc_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
stc_stc_LDADD = -lboost_system -lboost_filesystem \
	        -lboost_program_options -lboost_thread-mt -lboost_regex-mt \
		$(activemq_LIBS) $(apr_LIBS) $(libxml_LIBS) \
		$(HDF5_LDFLAGS) -lldap -llber

