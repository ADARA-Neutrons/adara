
AUTOMAKE_OPTIONS = -Wall foreign
ACLOCAL_AMFLAGS = ${ALOCAL_FLAGS}

AM_CPPFLAGS =
AM_CFLAGS = -Wall -Werror
AM_CXXFLAGS = -Wall -Wextra -Werror -Winit-self -Wpointer-arith -Wcast-align \
        -Woverloaded-virtual -Wno-missing-field-initializers \
        -Wunused-but-set-variable -Wunused-but-set-parameter

# These flags reduce warnings, which conflicts with finding problems
# -Wno-write-strings, -Wno-deprecated, -Wno-unused-result
# -Wno-unused-parameter
#
EPICS_LIBS = -lcas -lca -lCom -lgdd
EPICS_LDFLAGS = @EPICS_LDFLAGS@
EPICS_CPPFLAGS = @EPICS_CPPFLAGS@

# ActiveMQ, APR, and libxml2 (on Fedora 19 at least) put their include path in
# CFLAGS instead of CPPFLAGS, so fix that up
#
activemq_CPPFLAGS ?= $(activemq_CFLAGS)
apr_CPPFLAGS ?= $(apr_CFLAGS)
libxml_CPPFLAGS ?= $(libxml_CFLAGS)

bin_PROGRAMS =
noinst_PROGRAMS =
EXTRA_PROGRAMS =

include common/Makefile.am
include tools/Makefile.am
include sms/Makefile.am
include stc/Makefile.am
include dasmon/server/Makefile.am
include PVStreamer/pvsd/Makefile.am

test: $(EXTRA_PROGRAMS)

