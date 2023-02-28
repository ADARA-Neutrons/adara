AC_INIT([ADARA], [0.1], [adarabugs@ornl.gov], [ADARA])
AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([1.11 -Wall no-define foreign silent-rules subdir-objects])
AM_SILENT_RULES([yes])

m4_include([m4/ax_lib_hdf5.m4])

dnl AC_PROG_CC
dnl AC_LANG_C

AC_PROG_CXX
AC_LANG_CPLUSPLUS

dnl AM_PROG_CC_C_O

AC_SEARCH_LIBS([clock_gettime], [rt])
AC_SEARCH_LIBS([getaddrinfo_a], [anl], )
PKG_PROG_PKG_CONFIG()

AC_ARG_ENABLE([sms],
    AS_HELP_STRING([--disable-sms], [Disable building SMS]))
AC_ARG_ENABLE([stc],
    AS_HELP_STRING([--disable-stc], [Disable building STC]))
AC_ARG_ENABLE([dasmon_server],
    AS_HELP_STRING([--disable-dasmon-server], [Disable building DASMON server]))
AC_ARG_ENABLE([adara_gen],
    AS_HELP_STRING([--disable-adara-gen], [Disable building adara-gen]))
AC_ARG_ENABLE([adara_pvgen],
    AS_HELP_STRING([--disable-adara-pvgen], [Disable building adara-pvgen]))

AS_IF([test "x$enable_sms" != "xno"], [
	PKG_CHECK_EXISTS([liblog4cxx >= 0.10], [], [
		enable_sms=no
		AC_WARN(Disabling SMS due to missing dependencies)
	])
])

AS_IF([test "x$enable_stc" != "xno" -o "x$enable_dasmon_server" != "xno"], [
	PKG_CHECK_EXISTS([activemq-cpp >= 3.4.0 apr-1 >= 1.3.9
			  libxml-2.0 >= 2.7.6], [], [
		dnl Missing package(s), so disable those that need it
		AS_IF([test "x$enable_stc" != "xno"], [
			enable_stc=no;
			AC_WARN(Disabling STC due to missing dependencies)
		])
		AS_IF([test "x$enable_dasmon_server" != "xno"], [
			enable_dasmon_server=no;
			AC_WARN(Disabling DASMON server due to missing dependencies)
		])
	])
])

epics_disabled=yes
AS_IF([test "x$enable_sms" != "xno"], [epics_disabled=no])
AS_IF([test "x$enable_adara_gen" != "xno"], [epics_disabled=no])
AS_IF([test "x$enable_adara_pvgen" != "xno"], [epics_disabled=no])

tryepicsdir=""
AC_ARG_WITH(epics,
       [  --with-epics=PATH Specify path to EPICS installation ],
       [
                if test "x$withval" != "xno" ; then
                        tryepicsdir=$withval
                fi
       ]
)

EPICS_URL="http://www.aps.anl.gov/epics/index.php"

AS_IF([test "x$epics_disabled" = "xno"], [
	dnl AC_LANG_PUSH(C++)
	AC_CACHE_CHECK([for EPICS], ac_cv_epics_dir, [
		saved_LIBS="$LIBS"
		saved_LDFLAGS="$LDFLAGS"
		saved_CPPFLAGS="$CPPFLAGS"
		le_found=no
		for ledir in $tryepicsdir "" $prefix /usr/local /opt/epics; do
			LDFLAGS="$saved_LDFLAGS"
			LIBS="-lcas -lca -lCom -lgdd $saved_LIBS"

			if test ! -z "$ledir" -a ! -d "$ledir"; then
				continue;
			fi
			if test ! -z "$ledir" ; then
				if test -d "$ledir/lib/linux-x86_64" ; then
					LDFLAGS="-L$ledir/lib/linux-x86_64 $LDFLAGS"
				else
					LDFLAGS="-L$ledir $LDFLAGS"
				fi
				if test -d "$ledir/include" ; then
					CPPFLAGS="-I$ledir/include -I$ledir/include/os/Linux $CPPFLAGS"
				else
					CPPFLAGS="-I$ledir $CPPFLAGS"
				fi
			fi
			# Can I compile and link it?
			AC_TRY_LINK([#include "casdef.h"],
				[ pvAttachReturn *p = new pvAttachReturn(); ],
				[ libepics_linked=yes ], [ libepics_linked=no ])
			if test $libepics_linked = yes; then
				if test ! -z "$ledir" ; then
					ac_cv_epics_dir=$ledir
					saved_LDFLAGS="$saved_LDFLAGS -Wl,-rpath,$ledir/lib/linux-x86_64"
				else
					ac_cv_epics_dir="(system)"
				fi
				le_found=yes
				break
			fi
		done
		LIBS="$saved_LIBS"
		LDFLAGS="$saved_LDFLAGS"
		CPPFLAGS="$saved_CPPFLAGS"
		if test $le_found = no ; then
			epics_disabled=yes
			AC_WARN([EPICS is required to build this configuration.
	You can get it from $EPICS_URL
	If it's already installed, specify its path using --with-epics=PATH])
		fi
	])

	dnl #' fix highlighting

	if test "$ac_cv_epics_dir" != "(system)"; then
		if test -d "$ac_cv_epics_dir/lib/linux-x86_64" ; then
			EPICS_LDFLAGS="-L$ac_cv_epics_dir/lib/linux-x86_64"
			le_libdir="$ac_cv_epics_dir/lib/linux-x86_64"
		else
			EPICS_LDFLAGS="-L$ac_cv_epics_dir"
			le_libdir="$ac_cv_epics_dir"
		fi
		if test -d "$ac_cv_epics_dir/include" ; then
			EPICS_CPPFLAGS="-I$ac_cv_epics_dir/include/os/Linux"
			EPICS_CPPFLAGS="-I$ac_cv_epics_dir/include $EPICS_CPPFLAGS"
		else
			EPICS_CPPFLAGS="-I$ac_cv_epics_dir"
		fi
	fi
	dnl AC_LANG_POP(C++)
])

AC_SUBST(EPICS_LDFLAGS)
AC_SUBST(EPICS_CPPFLAGS)

AS_IF([test "x$epics_disabled" != "xno"], [
	dnl No EPICs found
	AS_IF([test "x$enable_sms" != "xno"], [
		enable_sms=no;
		AC_WARN(Disabling SMS due to missing EPICS)
	])
	AS_IF([test "x$enable_adara_gen" != "xno"], [
		enable_adara_gen=no;
		AC_WARN(Disabling adara-gen due to missing EPICS)
	])
	AS_IF([test "x$enable_adara_pvgen" != "xno"], [
		enable_adara_pvgen=no;
		AC_WARN(Disabling adara-pvgen due to missing EPICS)
	])
])

AS_IF([test "x$enable_stc" != "xno"], [
	AX_LIB_HDF5([serial])
	AS_IF([test "x$with_hdf5" = "xno"], [
		enable_stc=no
		AC_WARN(Disabling STC due to missing HDF5)
	])
])

AS_IF([test "x$enable_stc" != "xno" -o "x$enable_dasmon_server" != "xno"], [
	PKG_CHECK_MODULES([activemq], [activemq-cpp >= 3.4.0])
	PKG_CHECK_MODULES([apr], [apr-1 >= 1.3.9])
	PKG_CHECK_MODULES([libxml], [libxml-2.0 >= 2.7.6])
])

AS_IF([test "x$enable_sms" != "xno"], [
	PKG_CHECK_MODULES([liblog4cxx], [liblog4cxx >= 0.10])
])

AM_CONDITIONAL([BUILD_SMS], [test "x$enable_sms" != "xno"])
AM_CONDITIONAL([BUILD_STC], [test "x$enable_stc" != "xno"])
AM_CONDITIONAL([BUILD_DASMON_SERVER], [test "x$enable_dasmon_server" != "xno"])
AM_CONDITIONAL([BUILD_ADARA_GEN], [test "x$enable_adara_gen" != "xno"])
AM_CONDITIONAL([BUILD_ADARA_PVGEN], [test "x$enable_adara_pvgen" != "xno"])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
