
EXTRA_PROGRAMS += common/test/parser-test

# Adding -fPIE Everywhere to Enable Building on Rhel9 & GCC 11.3.1
# Which, Along with HDF5 and its Compilation Flag:
#    -specs=/usr/lib/rpm/redhat/redhat-hardened-ld
# Has Apparently Forced Us to Build Everything with PIE Format... ;-Q
COMMON_CPPFLAGS = -fPIE -Icommon

COMMON_PARSER = common/ADARAPackets.cc common/ADARAParser.cc

POSIX_PARSER = common/POSIXParser.cc $(COMMON_PARSER)

# The EPICS headers spew these warnings, but we'd like to keep the
# ADARA parser clean for sharing with Mantid, so build a throw-away
# tester to get a pass with increased warnings
#
common_test_parser_test_SOURCES = common/test/parser-test.cc $(POSIX_PARSER)
common_test_parser_test_CPPFLAGS = $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)
common_test_parser_test_CXXFLAGS = \
		$(AM_CXXFLAGS) -Wunused-parameter \
		-Wcast-qual -Wconversion -std=c++0x
