
if BUILD_STS
bin_PROGRAMS += sts/sts
endif

sts_sts_SOURCES = sts/main.cpp sts/NxGen.cpp sts/StreamParser.cpp \
    sts/h5nx.cpp sts/ComBusTransMon.cpp combus/ComBus.cpp $(POSIX_PARSER)

sts_sts_CPPFLAGS = -Ists $(libxml_CPPFLAGS) $(activemq_CPPFLAGS) \
		$(apr_CPPFLAGS) $(COMMON_CPPFLAGS) $(HDF5_CPPFLAGS) \
		$(AM_CPPFLAGS)

# TODO clean this up to compile with these warnings enabled
sts_sts_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
sts_sts_LDADD = -lboost_system -lboost_filesystem \
	        -lboost_program_options -lboost_thread-mt \
		$(activemq_LIBS) $(apr_LIBS) $(libxml_LIBS) \
		$(HDF5_LDFLAGS)
