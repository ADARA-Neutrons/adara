
bin_PROGRAMS += PVStreamer/pvsd/pvsd

PVStreamer_pvsd_pvsd_SOURCES = PVStreamer/pvsd/main.cpp \
    combus/ComBus.cpp $(COMMON_PARSER)

# TODO pick up the libxml2 include from autoconf/pkgconfig
PVStreamer_pvsd_pvsd_CPPFLAGS = -IPVStreamer/common -Icombus \
    -I/usr/include/libxml2 \
    -I/usr/include/apr-1 -I/usr/include/activemq-cpp-3.4.0 \
     $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

# TODO clean this up to compile with these warnings enabled
PVStreamer_pvsd_pvsd_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
PVStreamer_pvsd_pvsd_LDADD = -lxml2 -lboost_system -lboost_program_options \
    -lactivemq-cpp -lboost_thread-mt
