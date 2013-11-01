
bin_PROGRAMS += PVStreamer/pvsd/pvsd

PVStreamer_pvsd_pvsd_SOURCES = PVStreamer/pvsd/main.cpp \
    PVStreamer/common/DeviceDescriptor.cpp PVStreamer/common/ConfigManager.cpp \
    PVStreamer/common/StreamService.cpp PVStreamer/common/IInputAdapter.cpp \
    PVStreamer/common/IOutputAdapter.cpp PVStreamer/adara/ADARA_OutputAdapter.cpp \
    PVStreamer/epics/EPICS_InputAdapter.cpp PVStreamer/epics/EPICS_DeviceAgent.cpp \
    combus/ComBus.cpp $(COMMON_PARSER)

# TODO pick up the libxml2 include from autoconf/pkgconfig
PVStreamer_pvsd_pvsd_CPPFLAGS = -IPVStreamer/common -Icombus \
    -IPVStreamer/adara -IPVStreamer/epics \
    -I/usr/include/libxml2 \
    -I/usr/include/apr-1 -I/usr/include/activemq-cpp-3.4.0 \
     $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS)

# TODO clean this up to compile with these warnings enabled
PVStreamer_pvsd_pvsd_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
PVStreamer_pvsd_pvsd_LDFLAGS = $(EPICS_LDFLAGS)

PVStreamer_pvsd_pvsd_LDADD = $(EPICS_LIBS) -lxml2 -lactivemq-cpp \
    -lboost_filesystem -lboost_system -lboost_program_options -lboost_thread-mt

