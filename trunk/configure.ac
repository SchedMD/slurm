# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl
AC_INIT
AC_PREREQ(2.52)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR(auxdir)
AC_CANONICAL_TARGET([])

# determine project/version 
# XXX generate VERSION information automatically at some point
SLURM_MAJOR=0
SLURM_MINOR=1
SLURM_VERSION="$SLURM_MAJOR.$SLURM_MINOR"

VERSION=$SLURM_VERSION
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
AC_SUBST(VERSION)

AC_DEFINE_UNQUOTED(SLURM_VERSION, "$SLURM_VERSION", [slurm version number])
AC_SUBST(SLURM_VERSION)
AC_DEFINE_UNQUOTED(SLURM_MAJOR, "$SLURM_MAJOR", [slurm major version number])
AC_SUBST(SLURM_MAJOR)
AC_DEFINE_UNQUOTED(SLURM_MINOR, "$SLURM_MINOR", [slurm minor version number])
AC_SUBST(SLURM_MINOR)


dnl Initialize Automake
dnl
AM_INIT_AUTOMAKE(slurm, $VERSION)
AM_CONFIG_HEADER(config.h)


dnl Checks for programs.
dnl
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL


dnl Checks for libraries
dnl


dnl Checks for header files.
dnl
AC_CHECK_HEADERS(popt.h mcheck.h values.h socket.h sys/socket.h stdbool.h)
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_STDC


dnl Checks for structures.
dnl

dnl check to see if glibc's program_invocation_short_name is available:
dnl
AC_MSG_CHECKING([for program_invocation_short_name])
AC_TRY_LINK([extern char *program_invocation_short_name;], 
  [char *p; p = program_invocation_short_name; printf("%s\n", p);],
  [got_program_invocation_short_name=yes],
  []
)
AC_MSG_RESULT(${got_program_invocation_short_name=no})
if test "x$got_program_invocation_short_name" = "xyes"; then
  AC_DEFINE(HAVE_PROGRAM_INVOCATION_SHORT_NAME, 1, 
            [Define if libc sets program_invocation_short_name]
	   )
fi


dnl Checks for types.
dnl


dnl Checks for compiler characteristics.
dnl
AC_PROG_GCC_TRADITIONAL([])


dnl checks for library functions.
dnl
AC_FUNC_MALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS(strerror mtrace)
AC_SEARCH_LIBS([pthread_create], [pthread c_r cr], [], 
               [AC_MSG_ERROR([Error: Cannot find the pthread library])])

AC_CHECK_LIB(popt, poptGetNextOpt, [got_popt=yes], [got_popt=no])
AM_CONDITIONAL(USE_INCLUDED_POPT, test $got_popt = no)
if test $got_popt = no; then
    AC_MSG_RESULT([using included popt package...])
    POPT_LIBS="\$(top_srcdir)/src/popt/libpopt.a"
    POPT_INCLUDES="-I\$(top_srcdir)/src/popt"
else
    POPT_LIBS="-lpopt"
fi
AC_SUBST(POPT_LIBS)
AC_SUBST(POPT_INCLUDES)

AC_CHECK_LIB(ssl, EVP_SignInit, [got_ssl=yes], [got_ssl=no])
SSL_LIBS="-lssl"
dnl SSL_INCLUDES=""
AC_SUBST(SSL_LIBS)
AC_SUBST(SSL_INCLUDES)
dnl checks for system services.
dnl


dnl checks for system-specific stuff.
dnl

dnl check for debug compilation
dnl
AC_MSG_CHECKING(whether debug compilation of modules is enabled)
AC_ARG_ENABLE(debug, 
  AC_HELP_STRING([--enable-debug],[Enable debugging of individual modules]),
  [ case "${enableval}" in
     yes) with_slurm_debug=true ;;
     no)  with_slurm_debug=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
    esac
  ]
)

AC_MSG_RESULT(${with_slurm_debug=no})
if test "x$with_slurm_debug" = "xtrue"; then 
  CFLAGS="-g -Wall -Wpointer-arith"
  AC_DEFINE(DEBUG_SYSTEM, 1, [Define for extra debug messages.])
else
  CFLAGS="-g -Wall -Wpointer-arith"
fi
AM_CONDITIONAL(DEBUG_MODULES, test "x$with_slurm_debug" = "xtrue")


dnl check for slurmd and slurmctld default ports
dnl
AC_MSG_CHECKING(for slurmctld default port)
AC_ARG_WITH(slurmctld-port,
  AC_HELP_STRING([--with-slurmctld-port=N],
      [set slurmctld default port [[6817]]]),
    [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
        slurmctldport="$withval"
      fi
    ]
)
AC_MSG_RESULT(${slurmctldport=6817})
AC_DEFINE_UNQUOTED(SLURMCTLD_PORT, "$slurmctldport",
                   [Define the default port number for slurmctld])
AC_SUBST(SLURMCTLD_PORT)

AC_MSG_CHECKING(for slurmd default port)
AC_ARG_WITH(slurmd-port,
  AC_HELP_STRING([--with-slurmd-port=N],
      [set slurmd default port [[6818]]]),
    [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
        slurmdport="$withval"
      fi
    ]
)
AC_MSG_RESULT(${slurmdport=6818})
AC_DEFINE_UNQUOTED(SLURMD_PORT, "$slurmdport",
                   [Define the default port number for slurmd])
AC_SUBST(SLURMD_PORT)


dnl Set SLURM_CONFIG_FILE in config.h
dnl
AC_MSG_CHECKING(for slurm config file location)
AC_ARG_WITH(slurm-conf,
  AC_HELP_STRING([--with-slurm-conf=FILE],
      [set slurm config file location]),
    [ if test -n "$withval" -a "$withval" != yes -a "$withval" != no; then
        slurmconf="$withval"
      fi
    ]
)
topsrcdir=`cd $srcdir && pwd`
AC_MSG_RESULT(${slurmconf=${topsrcdir}/etc/slurm.conf})
AC_DEFINE_UNQUOTED(SLURM_CONFIG_FILE, "$slurmconf", 
                   [Define Default slurm config file location])



dnl check for whether to include Elan support
dnl
AC_MSG_CHECKING(whether to include Elan support)
AC_ARG_WITH(elan,
  AC_HELP_STRING([--with-elan],[compile with Elan support]),
  [ case "${withval}" in
      yes) elan=yes ;;
      no)  elan=no ;;
      *) AC_MSG_ERROR([bad value ${enableval} for --with-elan]) ;;
    esac
  ]
)
AC_MSG_RESULT(${elan=no})
AM_CONDITIONAL(WITH_ELAN, test "x$with_elan" = "xyes")
if test $with_elan; then
  AC_CHECK_LIB(elan3, elan3_create,
    [], 
    [AC_MSG_ERROR([unable to find the Elan3 library needed for Elan support.])], 
  )
  AC_CHECK_LIB(rmscall, rms_prgcreate, 
    [],
    [AC_MSG_ERROR([unable to find the RMS library needed for Elan support])],
  )
fi


dnl OpenSSL location (taken from openssh configure.ac)
dnl
AC_SUBST(SSL_LDFLAGS)
AC_SUBST(SSL_LIBS)
AC_SUBST(SSL_CPPFLAGS)
SSL_LIBS="-lcrypto"
AC_ARG_WITH(ssl-dir,
	[  --with-ssl-dir=PATH     Specify path to OpenSSL installation ],
	[
		if test "x$withval" != "xno" ; then
			tryssldir=$withval
		fi
	]
)

saved_LIBS="$LIBS"
saved_LDFLAGS="$LDFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
if test "x$prefix" != "xNONE" ; then
	tryssldir="$tryssldir $prefix"
fi
AC_CACHE_CHECK([for OpenSSL directory], ac_cv_openssldir, [
	for ssldir in $tryssldir "" /usr/local/openssl /usr/lib/openssl /usr/local/ssl /usr/lib/ssl /usr/local /usr/pkg /opt /opt/openssl ; do
		CPPFLAGS="$saved_CPPFLAGS"
		LDFLAGS="$saved_LDFLAGS"
		LIBS="$saved_LIBS $SSL_LIBS"
		
		# Skip directories if they don't exist
		if test ! -z "$ssldir" -a ! -d "$ssldir" ; then
			continue;
		fi
		if test ! -z "$ssldir" -a "x$ssldir" != "x/usr"; then
			# Try to use $ssldir/lib if it exists, otherwise 
			# $ssldir
			if test -d "$ssldir/lib" ; then
				LDFLAGS="-L$ssldir/lib $saved_LDFLAGS"
				if test ! -z "$need_dash_r" ; then
					LDFLAGS="-R$ssldir/lib $LDFLAGS"
				fi
			else
				LDFLAGS="-L$ssldir $saved_LDFLAGS"
				if test ! -z "$need_dash_r" ; then
					LDFLAGS="-R$ssldir $LDFLAGS"
				fi
			fi
			# Try to use $ssldir/include if it exists, otherwise 
			# $ssldir
			if test -d "$ssldir/include" ; then
				CPPFLAGS="-I$ssldir/include $saved_CPPFLAGS"
			else
				CPPFLAGS="-I$ssldir $saved_CPPFLAGS"
			fi
		fi

		# Basic test to check for compatible version and correct linking
		AC_TRY_RUN(
			[
#include <string.h>
#include <openssl/rand.h>
int main(void) 
{
	char a[2048];
	memset(a, 0, sizeof(a));
	RAND_add(a, sizeof(a), sizeof(a));
	return(RAND_status() <= 0);
}
			],
			[
				found_crypto=1
				break;
			], []
		)

		if test ! -z "$found_crypto" ; then
			break;
		fi
	done

	if test -z "$found_crypto" ; then
		AC_MSG_ERROR([Could not find working OpenSSL library, please install or check config.log])	
	fi
	if test -z "$ssldir" ; then
		ssldir="(system)"
	fi

	ac_cv_openssldir=$ssldir
])

if (test ! -z "$ac_cv_openssldir" && test "x$ac_cv_openssldir" != "x(system)") ; then
	dnl Need to recover ssldir - test above runs in subshell
	ssldir=$ac_cv_openssldir
	if test ! -z "$ssldir" -a "x$ssldir" != "x/usr"; then
		# Try to use $ssldir/lib if it exists, otherwise 
		# $ssldir
		if test -d "$ssldir/lib" ; then
			SSL_LDFLAGS="-L$ssldir/lib"
		else
			SSL_LDFLAGS="-L$ssldir"
		fi
		# Try to use $ssldir/include if it exists, otherwise 
		# $ssldir
		if test -d "$ssldir/include" ; then
			SSL_CPPFLAGS="-I$ssldir/include"
		else
			SSL_CPPFLAGS="-I$ssldir"
		fi
	fi
fi
LIBS="$saved_LIBS"
CPPFLAGS="$saved_CPPFLAGS"
LDFLAGS="$saved_LDFLAGS"


dnl All slurm Makefiles:
dnl need to add squeue and scancel when they're there

AC_CONFIG_FILES([Makefile 
                 src/Makefile 
		 src/api/Makefile 
		 src/common/Makefile 
		 src/popt/Makefile 
		 src/srun/Makefile 
		 src/slurmd/Makefile 
		 src/slurmctld/Makefile 
		 src/scontrol/Makefile
		 src/scancel/Makefile
		 src/squeue/Makefile
		 src/test/Makefile
		 testsuite/Makefile
		 testsuite/slurm_unit/Makefile
		 testsuite/slurm_unit/common/Makefile
		 testsuite/slurm_unit/slurmctld/Makefile
		 testsuite/slurm_unit/slurmd/Makefile
		 testsuite/slurm_unit/api/Makefile
		 testsuite/slurm_unit/api/manual/Makefile
		]
)

AC_OUTPUT

