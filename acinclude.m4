dnl##***************************************************************************
dnl## $Id$
dnl##***************************************************************************
dnl#  AUTHOR:
dnl#    Chris Dunlap <cdunlap@llnl.gov>
dnl#
dnl#  SYNOPSIS:
dnl#    AC_GPL_LICENSED
dnl#
dnl#  DESCRIPTION:
dnl#  Acknowledge being licensed under terms of the GNU General Public License.
dnl*****************************************************************************

AC_DEFUN([AC_GPL_LICENSED],
[
  AC_DEFINE([GPL_LICENSED], 1,
    [Define to 1 if licensed under terms of the GNU General Public License.]
  )
])


dnl
dnl Check for program_invocation_short_name
dnl
AC_DEFUN([AC_SLURM_PROGRAM_INVOCATION_SHORT_NAME],
[
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
])dnl AC_PROG_INVOCATION_NAME

dnl
dnl Check for Bigendian arch and set SLURM_BIGENDIAN acc'dngly
dnl
AC_DEFUN([AC_SLURM_BIGENDIAN],
[
  AC_C_BIGENDIAN
  if test "x$ac_cv_c_bigendian" = "xyes"; then
    AC_DEFINE(SLURM_BIGENDIAN,1,
             [Define if your architecture's byteorder is big endian.])
  fi
])dnl AC_SLURM_BIGENDIAN

dnl
dnl AC_SLURM_SEMAPHORE
dnl
AC_DEFUN([AC_SLURM_SEMAPHORE],
[
  SEMAPHORE_SOURCES=""
  SEMAPHORE_LIBS=""
  AC_CHECK_LIB(
    posix4,
    sem_open,
    [SEMAPHORE_LIBS="-lposix4";
     AC_DEFINE(HAVE_POSIX_SEMS, 1, [Define if you have Posix semaphores.])],
    [SEMAPHORE_SOURCES="semaphore.c"]
  )
  AC_SUBST(SEMAPHORE_SOURCES)
  AC_SUBST(SEMAPHORE_LIBS)
])dnl AC_SLURM_SEMAPHORE

dnl
dnl Perform checks related to setproctitle() emulation
dnl
AC_DEFUN([AC_SLURM_SETPROCTITLE],
[
#
case "$host" in
*-*-aix*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_CLOBBER_ARGV)
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0')
     ;;
*-*-hpux*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_PSTAT)
     ;;
*-*-linux*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_CLOBBER_ARGV)
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0')
     ;;
*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_NONE,
               [Define to the setproctitle() emulation type])
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0',
               [Define if you need setproctitle padding])
     ;;
esac
])

dnl
dnl
dnl
dnl Perform SLURM Project version setup
AC_DEFUN([AC_SLURM_VERSION],
[
#
# Determine project/version from META file.
#  These are substituted into the Makefile and config.h.
#
PROJECT="`perl -ne 'print,exit if s/^\s*NAME:\s*(\S*).*/\1/i' $srcdir/META`"
AC_DEFINE_UNQUOTED(PROJECT, "$PROJECT", [Define the project's name.])
AC_SUBST(PROJECT)

# Automake desires "PACKAGE" variable instead of PROJECT
PACKAGE=$PROJECT

# rpm make target needs Version in META, not major and minor version nubmers
VERSION="`perl -ne 'print,exit if s/^\s*VERSION:\s*(\S*).*/\1/i' $srcdir/META`"
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
AC_SUBST(VERSION)

MAJOR="`perl -ne 'print,exit if s/^\s*MAJOR:\s*(\S*).*/\1/i' $srcdir/META`"
MINOR="`perl -ne 'print,exit if s/^\s*MINOR:\s*(\S*).*/\1/i' $srcdir/META`"
MICRO="`perl -ne 'print,exit if s/^\s*MICRO:\s*(\S*).*/\1/i' $srcdir/META`"
RELEASE="`perl -ne 'print,exit if s/^\s*RELEASE:\s*(\S*).*/\1/i' $srcdir/META`"

# Check to see if we're on an unstable branch (no prereleases yet)
if echo "$RELEASE" | grep -e "pre0" -e "UNSTABLE"; then 
   if test "$RELEASE" = "UNSTABLE"; then
      DATE=`date +"%Y%m%d%H%M"`
   else
      DATE=`echo $RELEASE | cut -d. -f3`
   fi
   SLURM_RELEASE="unstable cvs build $DATE" 
   SLURM_VERSION="$MAJOR.$MINOR ($SLURM_RELEASE)"
else
   SLURM_RELEASE="`echo $RELEASE | sed 's/^.*\.//'`"
   SLURM_VERSION="$MAJOR.$MINOR.$MICRO"
   test $RELEASE = "1" || SLURM_VERSION="$SLURM_VERSION-$SLURM_RELEASE"
fi
AC_DEFINE_UNQUOTED(SLURM_MAJOR, "$MAJOR", 
                   [Define the project's major version.])
AC_DEFINE_UNQUOTED(SLURM_MINOR, "$MINOR",
                   [Define the project's minor version.])
AC_DEFINE_UNQUOTED(SLURM_MICRO, "$MICRO",
                   [Define the project's minor version.])
AC_DEFINE_UNQUOTED(RELEASE, "$RELEASE", [Define the project's release.])
AC_DEFINE_UNQUOTED(SLURM_VERSION, "$SLURM_VERSION",
                   [Define the project's version string.])
AC_SUBST(SLURM_MAJOR)
AC_SUBST(SLURM_MINOR)
AC_SUBST(SLURM_MICRO)
AC_SUBST(RELEASE)
AC_SUBST(SLURM_VERSION)

]) dnl AC_SLURM_VERSION
 
dnl @synopsis ACX_PTHREAD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
dnl
dnl This macro figures out how to build C programs using POSIX
dnl threads.  It sets the PTHREAD_LIBS output variable to the threads
dnl library and linker flags, and the PTHREAD_CFLAGS output variable
dnl to any special C compiler flags that are needed.  (The user can also
dnl force certain compiler flags/libs to be tested by setting these
dnl environment variables.)
dnl
dnl Also sets PTHREAD_CC to any special C compiler that is needed for
dnl multi-threaded programs (defaults to the value of CC otherwise).
dnl (This is necessary on AIX to use the special cc_r compiler alias.)
dnl
dnl If you are only building threads programs, you may wish to
dnl use these variables in your default LIBS, CFLAGS, and CC:
dnl
dnl        LIBS="$PTHREAD_LIBS $LIBS"
dnl        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
dnl        CC="$PTHREAD_CC"
dnl
dnl In addition, if the PTHREAD_CREATE_JOINABLE thread-attribute
dnl constant has a nonstandard name, defines PTHREAD_CREATE_JOINABLE
dnl to that name (e.g. PTHREAD_CREATE_UNDETACHED on AIX).
dnl
dnl ACTION-IF-FOUND is a list of shell commands to run if a threads
dnl library is found, and ACTION-IF-NOT-FOUND is a list of commands
dnl to run it if it is not found.  If ACTION-IF-FOUND is not specified,
dnl the default action will define HAVE_PTHREAD.
dnl
dnl Please let the authors know if this macro fails on any platform,
dnl or if you have any other suggestions or comments.  This macro was
dnl based on work by SGJ on autoconf scripts for FFTW (www.fftw.org)
dnl (with help from M. Frigo), as well as ac_pthread and hb_pthread
dnl macros posted by AFC to the autoconf macro repository.  We are also
dnl grateful for the helpful feedback of numerous users.
dnl
dnl @version $Id$
dnl @author Steven G. Johnson <stevenj@alum.mit.edu> and Alejandro Forero Cuervo <bachue@bachue.com>

AC_DEFUN([ACX_PTHREAD], [
AC_REQUIRE([AC_CANONICAL_HOST])
AC_LANG_SAVE
AC_LANG_C
acx_pthread_ok=no

# We used to check for pthread.h first, but this fails if pthread.h
# requires special compiler flags (e.g. on True64 or Sequent).
# It gets checked for in the link test anyway.

# First of all, check if the user has set any of the PTHREAD_LIBS,
# etcetera environment variables, and if threads linking works using
# them:
if test x"$PTHREAD_LIBS$PTHREAD_CFLAGS" != x; then
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        AC_MSG_CHECKING([for pthread_join in LIBS=$PTHREAD_LIBS with CFLAGS=$PTHREAD_CFLAGS])
        AC_TRY_LINK_FUNC(pthread_join, acx_pthread_ok=yes)
        AC_MSG_RESULT($acx_pthread_ok)
        if test x"$acx_pthread_ok" = xno; then
                PTHREAD_LIBS=""
                PTHREAD_CFLAGS=""
        fi
        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"
fi

# We must check for the threads library under a number of different
# names; the ordering is very important because some systems
# (e.g. DEC) have both -lpthread and -lpthreads, where one of the
# libraries is broken (non-POSIX).

# Create a list of thread flags to try.  Items starting with a "-" are
# C compiler flags, and other items are library names, except for "none"
# which indicates that we try without any flags at all.

acx_pthread_flags="pthreads none -Kthread -kthread lthread -pthread -pthreads -mthreads pthread --thread-safe -mt"

# The ordering *is* (sometimes) important.  Some notes on the
# individual items follow:

# pthreads: AIX (must check this before -lpthread)
# none: in case threads are in libc; should be tried before -Kthread and
#       other compiler flags to prevent continual compiler warnings
# -Kthread: Sequent (threads in libc, but -Kthread needed for pthread.h)
# -kthread: FreeBSD kernel threads (preferred to -pthread since SMP-able)
# lthread: LinuxThreads port on FreeBSD (also preferred to -pthread)
# -pthread: Linux/gcc (kernel threads), BSD/gcc (userland threads)
# -pthreads: Solaris/gcc
# -mthreads: Mingw32/gcc, Lynx/gcc
# -mt: Sun Workshop C (may only link SunOS threads [-lthread], but it
#      doesn't hurt to check since this sometimes defines pthreads too;
#      also defines -D_REENTRANT)
# pthread: Linux, etcetera
# --thread-safe: KAI C++

case "${host_cpu}-${host_os}" in
        *solaris*)

        # On Solaris (at least, for some versions), libc contains stubbed
        # (non-functional) versions of the pthreads routines, so link-based
        # tests will erroneously succeed.  (We need to link with -pthread or
        # -lpthread.)  (The stubs are missing pthread_cleanup_push, or rather
        # a function called by this macro, so we could check for that, but
        # who knows whether they'll stub that too in a future libc.)  So,
        # we'll just look for -pthreads and -lpthread first:

        acx_pthread_flags="-pthread -pthreads pthread -mt $acx_pthread_flags"
        ;;
esac

if test x"$acx_pthread_ok" = xno; then
for flag in $acx_pthread_flags; do

        case $flag in
                none)
                AC_MSG_CHECKING([whether pthreads work without any flags])
                ;;

                -*)
                AC_MSG_CHECKING([whether pthreads work with $flag])
                PTHREAD_CFLAGS="$flag"
                ;;

                *)
                AC_MSG_CHECKING([for the pthreads library -l$flag])
                PTHREAD_LIBS="-l$flag"
                ;;
        esac

        save_LIBS="$LIBS"
        save_CFLAGS="$CFLAGS"
        LIBS="$PTHREAD_LIBS $LIBS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Check for various functions.  We must include pthread.h,
        # since some functions may be macros.  (On the Sequent, we
        # need a special flag -Kthread to make this header compile.)
        # We check for pthread_join because it is in -lpthread on IRIX
        # while pthread_create is in libc.  We check for pthread_attr_init
        # due to DEC craziness with -lpthreads.  We check for
        # pthread_cleanup_push because it is one of the few pthread
        # functions on Solaris that doesn't have a non-functional libc stub.
        # We try pthread_create on general principles.
        AC_TRY_LINK([#include <pthread.h>],
                    [pthread_t th; pthread_join(th, 0);
                     pthread_attr_init(0); pthread_cleanup_push(0, 0);
                     pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
                    [acx_pthread_ok=yes])

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        AC_MSG_RESULT($acx_pthread_ok)
        if test "x$acx_pthread_ok" = xyes; then
                break;
        fi

        PTHREAD_LIBS=""
        PTHREAD_CFLAGS=""
done
fi

# Various other checks:
if test "x$acx_pthread_ok" = xyes; then
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Detect AIX lossage: threads are created detached by default
        # and the JOINABLE attribute has a nonstandard name (UNDETACHED).
        AC_MSG_CHECKING([for joinable pthread attribute])
        AC_TRY_LINK([#include <pthread.h>],
                    [int attr=PTHREAD_CREATE_JOINABLE;],
                    ok=PTHREAD_CREATE_JOINABLE, ok=unknown)
        if test x"$ok" = xunknown; then
                AC_TRY_LINK([#include <pthread.h>],
                            [int attr=PTHREAD_CREATE_UNDETACHED;],
                            ok=PTHREAD_CREATE_UNDETACHED, ok=unknown)
        fi
        if test x"$ok" != xPTHREAD_CREATE_JOINABLE; then
                AC_DEFINE(PTHREAD_CREATE_JOINABLE, $ok,
                          [Define to the necessary symbol if this constant
                           uses a non-standard name on your system.])
        fi
        AC_MSG_RESULT(${ok})
        if test x"$ok" = xunknown; then
                AC_MSG_WARN([we do not know how to create joinable pthreads])
        fi

        AC_MSG_CHECKING([if more special flags are required for pthreads])
        flag=no
        case "${host_cpu}-${host_os}" in
                *-aix* | *-freebsd*)     flag="-D_THREAD_SAFE";;
                *solaris* | alpha*-osf*) flag="-D_REENTRANT";;
        esac
        AC_MSG_RESULT(${flag})
        if test "x$flag" != xno; then
                PTHREAD_CFLAGS="$flag $PTHREAD_CFLAGS"
        fi

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        # More AIX lossage: must compile with cc_r
        AC_CHECK_PROG(PTHREAD_CC, cc_r, cc_r, ${CC})
else
        PTHREAD_CC="$CC"
fi

AC_SUBST(PTHREAD_LIBS)
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_CC)

# Finally, execute ACTION-IF-FOUND/ACTION-IF-NOT-FOUND:
if test x"$acx_pthread_ok" = xyes; then
        ifelse([$1],,AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.]),[$1])
        :
else
        acx_pthread_ok=no
        $2
fi
AC_LANG_RESTORE
])dnl ACX_PTHREAD


dnl
dnl AC_SLURM_WITH_SSL([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
dnl
AC_DEFUN([AC_SLURM_WITH_SSL], [

ac_slurm_with_ssl=no
ssl_default_dirs="/usr/local/openssl /usr/lib/openssl    \
                  /usr/local/ssl /usr/lib/ssl /usr/local \
		  /usr/pkg /opt /opt/openssl"

AC_SUBST(SSL_LDFLAGS)
AC_SUBST(SSL_LIBS)
AC_SUBST(SSL_CPPFLAGS)

SSL_LIBS="-lcrypto"

AC_ARG_WITH(ssl-dir,
  AC_HELP_STRING([--with-ssl=PATH],[Specify path to OpenSSL installation]),
  [ tryssldir=$withval ]
)

saved_LIBS="$LIBS"
saved_LDFLAGS="$LDFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
if test "x$prefix" != "xNONE" ; then
	tryssldir="$tryssldir $prefix"
fi

AC_CACHE_CHECK([for OpenSSL directory], ac_cv_openssldir, [
	for ssldir in $tryssldir "" $ssl_default_dirs; do 
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

])dnl AC_SLURM_WITH_SSL


