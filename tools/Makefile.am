
noinst_PROGRAMS += tools/adara-parser tools/adara-dump tools/adara-munge

if BUILD_ADARA_GEN
noinst_PROGRAMS += tools/adara-gen
endif
if BUILD_ADARA_PVGEN
noinst_PROGRAMS += tools/adara-pvgen
endif

tools_adara_parser_SOURCES = tools/adara-parser.cc $(POSIX_PARSER)
tools_adara_parser_CPPFLAGS = $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)
tools_adara_parser_LDADD = -lboost_program_options

tools_adara_gen_SOURCES = tools/adara-gen.cc
tools_adara_gen_CPPFLAGS = $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS) -DLOGCXX_LOGGING
tools_adara_gen_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)
tools_adara_gen_LDADD = $(EPICS_LIBS) $(liblog4cxx_LIBS) \
		-lboost_program_options -lpthread

tools_adara_pvgen_SOURCES = tools/adara-pvgen.cc
tools_adara_pvgen_CPPFLAGS = $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) \
		$(liblog4cxx_CPPFLAGS) $(AM_CPPFLAGS) -DLOGCXX_LOGGING
tools_adara_pvgen_LDFLAGS = $(EPICS_LDFLAGS) $(liblog4cxx_LDFLAGS) \
		$(AM_LDFLAGS)
tools_adara_pvgen_LDADD = $(EPICS_LIBS) $(liblog4cxx_LIBS) \
		-lboost_program_options -lpthread

tools_adara_dump_SOURCES = tools/adara-dump.cc $(POSIX_PARSER)
tools_adara_dump_CPPFLAGS = $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

tools_adara_munge_SOURCES = tools/adara-munge.cc $(POSIX_PARSER)
tools_adara_munge_CPPFLAGS = $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)
tools_adara_munge_LDADD = -lboost_program_options

