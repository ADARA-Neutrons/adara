
AUTOMAKE_OPTIONS = -Wall foreign
ACLOCAL_AMFLAGS = ${ALOCAL_FLAGS}

AM_CFLAGS = -Wall -Werror
AM_CXXFLAGS = -Wall -Werror

EPICS_LIBS = -lcas -lca -lCom -lgdd
EPICS_LDFLAGS = @EPICS_LDFLAGS@
EPICS_CPPFLAGS = @EPICS_CPPFLAGS@

bin_PROGRAMS = smsd
noinst_PROGRAMS = storage-test

smsd_SOURCES = smsd.cc StorageManager.cc StorageContainer.cc StorageFile.cc \
		DataSource.cc ADARAParser.cc LiveClient.cc LiveServer.cc \
		SMSControl.cc SMSControlPV.cc
smsd_CPPFLAGS = $(EPICS_CPPFLAGS) $(AM_CPPFLAGS)
smsd_LDFLAGS = $(EPICS_LDFLAGS) $(AM_LDFLAGS)
smsd_LDADD = $(EPICS_LIBS)

storage_test_SOURCES = storage-test.cc StorageManager.cc \
		StorageContainer.cc StorageFile.cc
