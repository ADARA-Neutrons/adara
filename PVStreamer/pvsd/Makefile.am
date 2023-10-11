
bin_PROGRAMS += PVStreamer/pvsd/pvsd

PVStreamer_pvsd_pvsd_SOURCES = PVStreamer/pvsd/main.cpp \
    PVStreamer/common/DeviceDescriptor.cpp \
    PVStreamer/common/ConfigManager.cpp \
    PVStreamer/common/StreamService.cpp \
    PVStreamer/common/IInputAdapter.cpp \
    PVStreamer/common/IOutputAdapter.cpp \
    PVStreamer/adara/ADARA_OutputAdapter.cpp \
    PVStreamer/epics/EPICS_InputAdapter.cpp \
    PVStreamer/epics/EPICS_DeviceAgent.cpp \
    combus/ComBus.cpp $(COMMON_PARSER)

# Adding -fPIE Everywhere to Enable Building on Rhel9 & GCC 11.3.1
# Which, Along with HDF5 and its Compilation Flag:
#    -specs=/usr/lib/rpm/redhat/redhat-hardened-ld
# Has Apparently Forced Us to Build Everything with PIE Format... ;-Q
PVStreamer_pvsd_pvsd_CPPFLAGS = -fPIE \
	-IPVStreamer/common -Icombus -Icommon \
    -IPVStreamer/adara -IPVStreamer/epics \
    $(libxml_CPPFLAGS) $(apr_CPPFLAGS) $(activemq_CPPFLAGS) \
    $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) -DSYSLOG_LOGGING
# TODO pick up the libxml2 include from autoconf/pkgconfig

# TODO clean this up to compile with these warnings enabled
PVStreamer_pvsd_pvsd_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
PVStreamer_pvsd_pvsd_LDFLAGS = $(EPICS_LDFLAGS)

PVStreamer_pvsd_pvsd_LDADD = $(EPICS_LIBS) $(activemq_LIBS) \
    $(apr_LIBS) $(libxml_LIBS) \
    -lboost_filesystem -lboost_system -lboost_program_options \
    -lboost_thread -lrt

