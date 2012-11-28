
noinst_PROGRAMS += tools/adara-parser tools/adara-gen

tools_adara_parser_SOURCES = tools/adara-parser.cc $(COMMON_PARSER)
tools_adara_parser_CPPFLAGS = $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)
tools_adara_parser_LDADD = -lboost_program_options

tools_adara_gen_SOURCES = tools/adara-gen.cc
tools_adara_gen_CPPFLAGS = $(COMMON_CPPFLAGS) $(EPICS_CPPFLAGS) $(AM_CPPFLAGS)
tools_adara_gen_LDFLAGS = $(EPICS_LDFLAGS) $(AM_LDFLAGS)
tools_adara_gen_LDADD = $(EPICS_LIBS) -lboost_program_options -lpthread
