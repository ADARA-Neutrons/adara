
AUTOMAKE_OPTIONS = -Wall foreign
ACLOCAL_AMFLAGS = ${ALOCAL_FLAGS}

AM_CFLAGS = -Wall -Werror
AM_CXXFLAGS = -Wall -Werror

#LDADD = $(libevent2_LIBS)

EPICS_LIBS = -lcas -lca -lCom -lgdd
EPICS_LDFLAGS = @EPICS_LDFLAGS@
EPICS_CPPFLAGS = @EPICS_CPPFLAGS@

bin_PROGRAMS = smsd
noinst_PROGRAMS = storage-test
#noinst_PROGRAMS = storage-test adara-client adara-gen adara-sts

smsd_SOURCES = smsd.cc StorageManager.cc StorageContainer.cc StorageFile.cc \
		DataSource.cc ADARAParser.cc
#smsd_SOURCES = smsd.c sources.c live.c storage.c ts-client.c epics.cc smsCAS.cc smsPV.cc
smsd_CPPFLAGS = $(EPICS_CPPFLAGS) $(AM_CPPFLAGS)
smsd_LDFLAGS = $(EPICS_LDFLAGS) $(AM_LDFLAGS)
#smsd_LDADD = $(EPICS_LIBS) $(libevent2_LIBS) -lpthread
smsd_LDADD = $(EPICS_LIBS)

storage_test_SOURCES = storage-test.cc StorageManager.cc \
		StorageContainer.cc StorageFile.cc
