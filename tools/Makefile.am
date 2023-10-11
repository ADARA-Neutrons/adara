
noinst_PROGRAMS += tools/adara-parser tools/adara-dump tools/adara-munge

if BUILD_ADARA_GEN
noinst_PROGRAMS += tools/adara-gen
endif
if BUILD_ADARA_PVGEN
noinst_PROGRAMS += tools/adara-pvgen
endif

# Adding -fPIE Everywhere to Enable Building on Rhel9 & GCC 11.3.1
# Which, Along with HDF5 and its Compilation Flag:
#    -specs=/usr/lib/rpm/redhat/redhat-hardened-ld
# Has Apparently Forced Us to Build Everything with PIE Format... ;-Q
# ADARA Parser

tools_adara_parser_SOURCES = tools/adara-parser.cc $(POSIX_PARSER)

tools_adara_parser_CPPFLAGS = -fPIE \
		$(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

tools_adara_parser_LDADD = -lboost_program_options

# ADARA Gen

tools_adara_gen_SOURCES = tools/adara-gen.cc

tools_adara_gen_CPPFLAGS = -fPIE \
		$(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(log4cpp_CPPFLAGS) $(liblog4cxx_CPPFLAGS) \
		$(AM_CPPFLAGS) -DLOG4C_LOGGING

tools_adara_gen_LDFLAGS = $(EPICS_LDFLAGS) \
		$(log4cpp_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)

tools_adara_gen_LDADD = $(EPICS_LIBS) \
		$(log4cpp_LIBS) $(liblog4cxx_LIBS) \
		-lboost_program_options -lpthread

# ADARA PVGen

tools_adara_pvgen_SOURCES = tools/adara-pvgen.cc

tools_adara_pvgen_CPPFLAGS = -fPIE \
		$(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(log4cpp_CPPFLAGS) $(liblog4cxx_CPPFLAGS) \
		$(AM_CPPFLAGS) -DLOG4C_LOGGING

tools_adara_pvgen_LDFLAGS = $(EPICS_LDFLAGS) \
		$(log4cpp_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)

tools_adara_pvgen_LDADD = $(EPICS_LIBS) \
		$(log4cpp_LIBS) $(liblog4cxx_LIBS) \
		-lboost_program_options -lpthread

# ADARA Dump

tools_adara_dump_SOURCES = tools/adara-dump.cc $(POSIX_PARSER)

tools_adara_dump_CPPFLAGS = -fPIE \
		$(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

# ADARA Munge

tools_adara_munge_SOURCES = tools/adara-munge.cc $(POSIX_PARSER)

tools_adara_munge_CPPFLAGS = -fPIE \
		$(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

tools_adara_munge_LDADD = -lboost_program_options

