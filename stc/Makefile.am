
if BUILD_STC
bin_PROGRAMS += stc/stc
endif

bin_PROGRAMS += stc/retrieveUnmapped

stc_stc_SOURCES = stc/main.cpp stc/NxGen.cpp stc/StreamParser.cpp \
    stc/h5nx.cpp stc/ComBusTransMon.cpp combus/ComBus.cpp \
	stc/UserIdLdap.cpp $(POSIX_PARSER)

# Adding -fPIE Everywhere to Enable Building on Rhel9 & GCC 11.3.1
# Which, Along with HDF5 and its Compilation Flag:
#    -specs=/usr/lib/rpm/redhat/redhat-hardened-ld
# Has Apparently Forced Us to Build Everything with PIE Format... ;-Q
stc_stc_CPPFLAGS = -fPIE \
		-Istc -Istc/nexus \
		$(libxml_CPPFLAGS) $(activemq_CPPFLAGS) \
		$(apr_CPPFLAGS) $(COMMON_CPPFLAGS) $(HDF5_CPPFLAGS) \
		$(AM_CPPFLAGS) -DSYSLOG_LOGGING

# TODO clean this up to compile with these warnings enabled
stc_stc_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
stc_stc_LDADD = -lboost_system -lboost_filesystem \
		-lboost_program_options -lboost_thread -lboost_regex-mt \
		$(activemq_LIBS) $(apr_LIBS) $(libxml_LIBS) \
		$(HDF5_LDFLAGS) -lldap -llber

stc_retrieveUnmapped_SOURCES = stc/retrieveUnmapped.cpp stc/h5nx.cpp

# Adding -fPIE Everywhere to Enable Building on Rhel9 & GCC 11.3.1
# Which, Along with HDF5 and its Compilation Flag:
#    -specs=/usr/lib/rpm/redhat/redhat-hardened-ld
# Has Apparently Forced Us to Build Everything with PIE Format... ;-Q
stc_retrieveUnmapped_CPPFLAGS = -fPIE \
		-Istc -Istc/nexus \
		$(COMMON_CPPFLAGS) $(HDF5_CPPFLAGS) \
		$(AM_CPPFLAGS)

stc_retrieveUnmapped_CXXFLAGS = $(AM_CXXFLAGS)

# TODO pick up most of these from autoconf
stc_retrieveUnmapped_LDADD = -lboost_system -lboost_filesystem \
	        -lboost_program_options $(HDF5_LDFLAGS)

