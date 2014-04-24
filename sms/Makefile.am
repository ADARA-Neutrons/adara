
if BUILD_SMS
bin_PROGRAMS += sms/smsd
noinst_PROGRAMS += sms/test/storage-test
endif

sms_smsd_SOURCES = sms/smsd.cc sms/StorageManager.cc sms/StorageContainer.cc \
		sms/StorageFile.cc sms/DataSource.cc sms/LiveClient.cc \
		sms/LiveServer.cc sms/SMSControl.cc sms/SMSControlPV.cc \
		sms/SignalEvents.cc sms/STSClient.cc sms/STSClientMgr.cc \
		sms/RunInfo.cc sms/Geometry.cc sms/PixelMap.cc \
		sms/BeamlineInfo.cc sms/MetaDataMgr.cc sms/FastMeta.cc \
		sms/Markers.cc sms/utils.cc $(POSIX_PARSER)
sms_smsd_CPPFLAGS = $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS)
sms_smsd_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) $(AM_LDFLAGS)
sms_smsd_LDADD = $(EPICS_LIBS) $(liblog4cxx_LIBS) -lboost_signals \
		-lboost_program_options -lboost_filesystem -lboost_system \
		-lboost_thread-mt -lpthread

sms_test_storage_test_SOURCES = sms/test/storage-test.cc \
		sms/StorageManager.cc sms/StorageContainer.cc \
		sms/StorageFile.cc sms/STSClientMgr.cc sms/STSClient.cc \
		sms/SignalEvents.cc sms/utils.cc $(POSIX_PARSER)
sms_test_storage_test_CPPFLAGS = -Isms $(COMMON_CPPFLAGS) \
		$(EPICS_CPPFLAGS) $(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS)
sms_test_storage_test_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)
sms_test_storage_test_LDADD = $(EPICS_LIBS) $(liblog4cxx_LIBS)-lboost_signals \
		-lboost_filesystem -lboost_system -lboost_thread-mt \
		-lpthread
