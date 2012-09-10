AC_INIT([ADARA], [0.1], [adarabugs@ornl.gov], [ADARA])
AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([1.11 -Wall no-define foreign silent-rules subdir-objects])
AM_SILENT_RULES([yes])

dnl AC_PROG_CC
dnl AC_LANG_C

AC_PROG_CXX
AC_LANG_CPLUSPLUS

dnl AM_PROG_CC_C_O

AC_SEARCH_LIBS([clock_gettime], [rt])
AC_SEARCH_LIBS([getaddrinfo_a], [anl], )
PKG_CHECK_MODULES([liblog4cxx], [liblog4cxx >= 0.10])

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
		AC_MSG_ERROR([EPICS is required. You can get it from $EPICS_URL

        If it's already installed, specify its path using --with-epics=PATH
])
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

AC_SUBST(EPICS_LDFLAGS)
AC_SUBST(EPICS_CPPFLAGS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
