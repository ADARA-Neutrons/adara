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
	PKG_CHECK_EXISTS([log4cpp >= 1.1], [
			enable_log4cpp=yes
			enable_liblog4cxx=no
		], [
		PKG_CHECK_EXISTS([liblog4cxx >= 0.10], [
				enable_log4cpp=no
				enable_liblog4cxx=yes
			], [
			enable_sms=no
			AC_WARN(Disabling SMS due to missing Log4Cpp/Cxx dependencies)
		])
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
trypcasdir=""
epicsversion=""

AC_ARG_WITH(epics,
       [  --with-epics=PATH Specify path to EPICS 3 installation ],
       [
                if test "x$withval" != "xno" ; then
                        AC_MSG_NOTICE([Setting EPICS 3 Installation to:])
                        AC_MSG_NOTICE([   $withval])
                        tryepicsdir=$withval
                        # Use Same Channel Access Install for EPICS 3...
                        trypcasdir=$withval
                        epicsversion="3"
                fi
       ]
)

AC_ARG_WITH(epics7,
       [  --with-epics7=PATH Specify path to EPICS 7 installation ],
       [
                if test "x$withval" != "xno" ; then
                        if test "x$tryepicsdir" != "x" ; then
                                AC_WARN([Overriding EPICS 3 Installation as Specified in:])
                                AC_MSG_NOTICE([   $tryepicsdir])
                                AC_MSG_NOTICE([Instead with EPICS 7 Installation in:])
                                AC_MSG_NOTICE([   $withval])
                        else
                                AC_MSG_NOTICE([Setting EPICS 7 Installation to:])
                                AC_MSG_NOTICE([   $withval])
                        fi
                        tryepicsdir=$withval
                        epicsversion="7"
                        # Clear Channel Access Install for EPICS 7...
                        if test "x$trypcasdir" != "x" ; then
                                AC_WARN([Clearing Previous Channel Access Installation Setting in:])
                                AC_MSG_NOTICE([   $trypcasdir])
                                AC_MSG_NOTICE([After Setting EPICS 7 Installation.])
                                trypcasdir=""
                        fi
                fi
       ]
)

AC_ARG_WITH(pcas,
       [  --with-pcas=PATH Specify path to EPICS Channel Access installation ],
       [
                if test "x$withval" != "xno" ; then
                        if test "x$trypcasdir" != "x" ; then
                                AC_WARN([Overriding EPICS Channel Access Installation as Specified in:])
                                AC_MSG_NOTICE([   $trypcasdir])
                                AC_MSG_NOTICE([Instead with Specified Explicit Installation in:])
                                AC_MSG_NOTICE([   $withval.])
                        else
                                AC_MSG_NOTICE([Setting EPICS Channel Access Installation to:])
                                AC_MSG_NOTICE([   $withval])
                        fi
                        trypcasdir=$withval
                fi
       ]
)

EPICS_URL="http://www.aps.anl.gov/epics/index.php"

PCAS_URL="https://github.com/epics-modules/pcas"

AS_IF([test "x$epics_disabled" = "xno"], [

	dnl AC_LANG_PUSH(C++)

	AC_CACHE_CHECK([for EPICS], ac_cv_epics_dir, [
		saved_LIBS="$LIBS"
		saved_LDFLAGS="$LDFLAGS"
		saved_CPPFLAGS="$CPPFLAGS"
		le_found=no
		for ledir in $tryepicsdir "" $prefix /usr/local /opt/epics; do
			LDFLAGS="$saved_LDFLAGS"
			LIBS="-lca -lCom $saved_LIBS"
			if test ! -z "$ledir" -a ! -d "$ledir" ; then
				continue;
			fi
			if test ! -z "$ledir" ; then
				if test -d "$ledir/lib/linux-x86_64" ; then
					LDFLAGS="-L$ledir/lib/linux-x86_64 $LDFLAGS"
				else
					LDFLAGS="-L$ledir $LDFLAGS"
				fi
				if test -d "$ledir/include" ; then
					if test -d "$ledir/include/compiler/gcc" ; then
						CPPFLAGS="-I$ledir/include -I$ledir/include/os/Linux -I$ledir/include/compiler/gcc $CPPFLAGS"
					else
						CPPFLAGS="-I$ledir/include -I$ledir/include/os/Linux $CPPFLAGS"
					fi
				else
					CPPFLAGS="-I$ledir $CPPFLAGS"
				fi
			fi
			# Can I compile and link it?
			AC_TRY_LINK([#include "cadef.h"],
				[ int status = ca_task_initialize(); status = ca_task_exit(); ],
				[ libepics_linked=yes ], [ libepics_linked=no ])
			if test $libepics_linked = yes ; then
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
	If it is already installed, specify its path using --with-epics=PATH])
		fi
	])

	if test "$ac_cv_epics_dir" != "(system)" ; then
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
		# Extract Compiler Class for compilerSpecific.h in EPICS 7... ;-b
		# (If in doubt, just set EPICS_CMPLR_CLASS to "gcc", lol... ;-D)
		if test $epicsversion = 7 ; then
			# Yuk... ;-D
			EPICS_CMPLR_CLASS=`make -C $ac_cv_epics_dir -n -p |& grep "^CMPLR_CLASS = " | head -1 | awk '{print $3}'`
			if test "x$EPICS_CMPLR_CLASS" != "x" ; then
				AC_MSG_NOTICE([Found EPICS 7 Compiler Class: $EPICS_CMPLR_CLASS])
				EPICS_CPPFLAGS="-I$ac_cv_epics_dir/include/compiler/$EPICS_CMPLR_CLASS $EPICS_CPPFLAGS"
			fi
		fi
	fi

	AC_CACHE_CHECK([for PCAS], ac_cv_pcas_dir, [
		saved_LIBS="$LIBS"
		saved_LDFLAGS="$LDFLAGS"
		saved_CPPFLAGS="$CPPFLAGS"
		le_found=no
		for ledir in $trypcasdir "" $prefix /usr/local /opt/pcas; do
			CPPFLAGS="$EPICS_CPPFLAGS"
			LDFLAGS="$saved_LDFLAGS $EPICS_LDFLAGS"
			LIBS="-lcas -lgdd $saved_LIBS"

			if test ! -z "$ledir" -a ! -d "$ledir" ; then
				continue;
			fi
			if test ! -z "$ledir" ; then
				if test -d "$ledir/lib/linux-x86_64" ; then
					LDFLAGS="-L$ledir/lib/linux-x86_64 $LDFLAGS"
				else
					LDFLAGS="-L$ledir $LDFLAGS"
				fi
				if test -d "$ledir/include" ; then
					CPPFLAGS="-I$ledir/include $CPPFLAGS"
				else
					CPPFLAGS="-I$ledir $CPPFLAGS"
				fi
			fi
			# Can I compile and link it?
			AC_TRY_LINK([#include "casdef.h"],
				[ pvAttachReturn *p = new pvAttachReturn(); ],
				[ libpcas_linked=yes ], [ libpcas_linked=no ])
			if test $libpcas_linked = yes ; then
				if test ! -z "$ledir" ; then
					ac_cv_pcas_dir=$ledir
					saved_LDFLAGS="$saved_LDFLAGS -Wl,-rpath,$ledir/lib/linux-x86_64"
				else
					ac_cv_pcas_dir="(system)"
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
			AC_WARN([EPICS Channel Access is required to build this configuration.
	You can get it from $PCAS_URL
	If it is already installed, specify its path using --with-pcas=PATH])
		fi
	])

	if test "$ac_cv_pcas_dir" != "(system)" ; then
		if test -d "$ac_cv_pcas_dir/lib/linux-x86_64" ; then
			EPICS_LDFLAGS="-L$ac_cv_pcas_dir/lib/linux-x86_64 $EPICS_LDFLAGS"
		else
			EPICS_LDFLAGS="-L$ac_cv_pcas_dir $EPICS_LDFLAGS"
		fi
		if test -d "$ac_cv_pcas_dir/include" ; then
			EPICS_CPPFLAGS="-I$ac_cv_pcas_dir/include $EPICS_CPPFLAGS"
		else
			EPICS_CPPFLAGS="-I$ac_cv_pcas_dir $EPICS_CPPFLAGS"
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
	AS_IF([test "x$enable_log4cpp" != "xno"], [
		PKG_CHECK_MODULES([log4cpp], [log4cpp >= 1.1])
		log4cpp_CFLAGS="$log4cpp_CFLAGS -DUSE_LOG4CPP_LOGGING"
	])
	AS_IF([test "x$enable_liblog4cxx" != "xno"], [
		PKG_CHECK_MODULES([liblog4cxx], [liblog4cxx >= 0.10])
		liblog4cxx_CFLAGS="$liblog4cxx_CFLAGS -DUSE_LOG4CXX_LOGGING"
	])
])

AM_CONDITIONAL([BUILD_SMS], [test "x$enable_sms" != "xno"])
AM_CONDITIONAL([BUILD_STC], [test "x$enable_stc" != "xno"])
AM_CONDITIONAL([BUILD_DASMON_SERVER], [test "x$enable_dasmon_server" != "xno"])
AM_CONDITIONAL([BUILD_ADARA_GEN], [test "x$enable_adara_gen" != "xno"])
AM_CONDITIONAL([BUILD_ADARA_PVGEN], [test "x$enable_adara_pvgen" != "xno"])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
