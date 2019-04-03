
if BUILD_SMS
bin_PROGRAMS += sms/smsd
EXTRA_PROGRAMS += sms/test/storage-test
endif

sms_smsd_SOURCES = sms/smsd.cc \
		sms/StorageManager.cc sms/StorageContainer.cc \
		sms/StorageFile.cc sms/DataSource.cc sms/LiveClient.cc \
		sms/LiveServer.cc sms/SMSControl.cc sms/SMSControlPV.cc \
		sms/SignalEvents.cc sms/STCClient.cc sms/STCClientMgr.cc \
		sms/RunInfo.cc sms/Geometry.cc sms/PixelMap.cc \
		sms/BeamlineInfo.cc sms/MetaDataMgr.cc sms/FastMeta.cc \
		sms/Markers.cc sms/BeamMonitorConfig.cc sms/DetectorBankSet.cc \
		sms/ComBusSMSMon.cc combus/ComBus.cpp \
		sms/EventFd.cc sms/utils.cc $(POSIX_PARSER)
sms_smsd_CPPFLAGS = $(activemq_CPPFLAGS) $(apr_CPPFLAGS) \
		$(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS) \
		-I/home/controls/Accelerator -DLOGCXX_LOGGING
sms_smsd_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) $(AM_LDFLAGS)
sms_smsd_LDADD = $(EPICS_LIBS) $(activemq_LIBS) $(liblog4cxx_LIBS) \
		-lboost_signals \
		-lboost_program_options -lboost_filesystem -lboost_system \
		-lboost_thread-mt -lpthread

sms_test_storage_test_SOURCES = sms/test/storage-test.cc \
		sms/StorageManager.cc sms/StorageContainer.cc \
		sms/StorageFile.cc sms/STCClientMgr.cc sms/STCClient.cc \
		sms/SMSControl.cc sms/SMSControlPV.cc sms/RunInfo.cc \
		sms/Geometry.cc sms/Markers.cc sms/MetaDataMgr.cc sms/FastMeta.cc \
		sms/BeamlineInfo.cc sms/DataSource.cc sms/PixelMap.cc \
		sms/SignalEvents.cc sms/BeamMonitorConfig.cc \
		sms/DetectorBankSet.cc sms/ComBusSMSMon.cc combus/ComBus.cpp \
		sms/EventFd.cc sms/utils.cc $(POSIX_PARSER)
sms_test_storage_test_CPPFLAGS = -Isms $(activemq_CPPFLAGS) $(apr_CPPFLAGS)\
		$(COMMON_CPPFLAGS) \
		$(EPICS_CPPFLAGS) $(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS) \
		-I/home/controls/Accelerator
sms_test_storage_test_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)
sms_test_storage_test_LDADD = $(EPICS_LIBS) $(activemq_LIBS) \
		$(liblog4cxx_LIBS) -lboost_signals \
		-lboost_filesystem -lboost_system -lboost_thread-mt \
		-lpthread
