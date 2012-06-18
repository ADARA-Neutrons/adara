
AUTOMAKE_OPTIONS = -Wall foreign
ACLOCAL_AMFLAGS = ${ALOCAL_FLAGS}

AM_CFLAGS = -Wall -Werror
AM_CXXFLAGS = -Wall -Wextra -Werror -Winit-self -Wpointer-arith -Wcast-align \
		-Woverloaded-virtual -Wno-missing-field-initializers \
		-Wno-unused-parameter

# These reduces warnings, which we don't want
# -Wno-write-strings, -Wno-deprecated, -Wno-unused-result

EPICS_LIBS = -lcas -lca -lCom -lgdd
EPICS_LDFLAGS = @EPICS_LDFLAGS@
EPICS_CPPFLAGS = @EPICS_CPPFLAGS@

bin_PROGRAMS = smsd
noinst_PROGRAMS = storage-test parser-test

smsd_SOURCES = smsd.cc StorageManager.cc StorageContainer.cc StorageFile.cc \
		DataSource.cc ADARAPackets.cc ADARAParser.cc \
		LiveClient.cc LiveServer.cc \
		SMSControl.cc SMSControlPV.cc SignalEvents.cc \
		STSClient.cc STSClientMgr.cc RunInfo.cc \
		Geometry.cc PixelMap.cc BeamlineInfo.cc \
		MetaDataMgr.cc
smsd_CPPFLAGS = $(EPICS_CPPFLAGS) $(AM_CPPFLAGS)
smsd_LDFLAGS = $(EPICS_LDFLAGS) $(AM_LDFLAGS)
smsd_LDADD = $(EPICS_LIBS) -lboost_signals -lpthread

# The EPICS headers spew these warnings, but we'd like to keep the
# ADARA parser clean for sharing with Mantid, so build a throw-away
# to get a pass with increaed warnings
#
parser_test_SOURCES = parser-test.cc ADARAPackets.cc ADARAParser.cc
parser_test_CXXFLAGS = $(AM_CXXFLAGS) -Wunused-parameter \
				-Wcast-qual -Wconversion -std=c++0x

storage_test_SOURCES = storage-test.cc StorageManager.cc \
		StorageContainer.cc StorageFile.cc
storage_test_LDADD = -lboost_signals
