# $Id$
# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl
AC_INIT
AC_PREREQ(2.52)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR(auxdir)
AC_CANONICAL_TARGET([])

AC_GPL_LICENSED

#
# Determine project/version from META file.
# Sets PACKAGE, VERSION, SLURM_VERSION
AC_SLURM_VERSION


dnl Initialize Automake
dnl
AM_INIT_AUTOMAKE(slurm, $VERSION)
AM_CONFIG_HEADER(config.h)
AM_CONFIG_HEADER(slurm/slurm.h)


dnl Checks for programs.
dnl
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL


dnl Checks for libraries
dnl


dnl Checks for header files.
dnl
AC_CHECK_HEADERS(popt.h mcheck.h values.h socket.h sys/socket.h  \
                 stdbool.h sys/ipc.h sys/shm.h sys/sem.h errno.h \
                 stdlib.h dirent.h pthread.h \
		 )
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_STDC

dnl Checks for structures.
dnl

dnl check to see if glibc's program_invocation_short_name is available:
dnl
AC_SLURM_PROGRAM_INVOCATION_SHORT_NAME


dnl Checks for types.
dnl
AC_SLURM_BIGENDIAN
  

dnl Checks for compiler characteristics.
dnl
AC_PROG_GCC_TRADITIONAL([])


dnl checks for library functions.
dnl
AC_FUNC_MALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS( \
   strerror  \
   mtrace    \
   strsignal \ 
   inet_aton \
   inet_ntop \
   inet_pton \
   setproctitle \
)


AC_CHECK_DECLS([strsignal, sys_siglist])

ACX_PTHREAD([], AC_MSG_ERROR([Error: Cannot figure out how to use pthreads!]))

# Always define WITH_PTHREADS if we make it this far
AC_DEFINE(WITH_PTHREADS,1,[Define if you have pthreads.])
LDFLAGS="$LDFLAGS "
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"

AC_SLURM_SEMAPHORE
AC_SLURM_USE_INCLUDED_POPT


dnl checks for system services.
dnl


dnl checks for system-specific stuff.
dnl

dnl check for how to emulate setproctitle
dnl
AC_SLURM_SETPROCTITLE

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
  CFLAGS="-g -Wall -Wpointer-arith $CFLAGS"
  AC_DEFINE(DEBUG_SYSTEM, 1, [Define for extra debug messages.])
else
  CFLAGS="-O2 -Wall -Wpointer-arith $CFLAGS"
  AC_DEFINE(NDEBUG, 1, [Define for production compile of code])
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
HAVE_ELAN=0
if test "$with_elan" = "yes"; then
  savedLIBS="$LIBS"
  AC_CHECK_LIB(elan3, elan3_create,
    [], 
    [AC_MSG_ERROR([unable to find the Elan3 library needed for Elan support.])], 
  )
  AC_CHECK_LIB(rmscall, rms_prgcreate, 
    [],
    [AC_MSG_ERROR([unable to find the RMS library needed for Elan support])],
  )
  HAVE_ELAN=1
  ELAN_LIBS="-lelan3 -lrmscall"
  LIBS="$savedLIBS" 
fi

AC_DEFINE_UNQUOTED(HAVE_ELAN, "$HAVE_ELAN", [Define to enable Elan support.])
AC_SUBST(HAVE_ELAN)
AC_SUBST(ELAN_LIBS)

dnl Check for whether to include readline support
dnl 
AC_MSG_CHECKING([whether to include readline support])
AC_ARG_ENABLE(readline,
  AC_HELP_STRING([--disable-readline], [disable readline support]),
  [ readline=no  ],
  [ readline=yes ]
)
AC_MSG_RESULT(${readline})

if test x$readline = xyes; then
   savedLIBS="$LIBS"
   READLINE_LIBS="-lreadline -lncurses"
   LIBS="$READLINE_LIBS $LIBS"
   AC_CHECK_LIB([readline], [readline], [], AC_MSG_ERROR([Cannot find libreadline!]))
   AC_DEFINE(HAVE_READLINE, 1, [Define if you are compiling with readline])
   LIBS="$savedLIBS"
else
   READLINE_LIBS=""
fi

AC_SUBST(READLINE_LIBS)

dnl OpenSSL location (taken from openssh configure.ac)
dnl
AC_SLURM_WITH_SSL


dnl
dnl Check for compilation of SLURM auth modules:
dnl
MUNGE_LIBS="-lmunge"
savedLIBS="$LIBS"
LIBS="$MUNGE_LIBS $LIBS"
AC_CHECK_LIB(munge, munge_encode, [have_munge=yes], [have_munge=no])
AC_SUBST(MUNGE_LIBS)
AM_CONDITIONAL(WITH_MUNGE, test "x$have_munge" = "xyes")
LIBS="$savedLIBS"

AUTHD_CFLAGS=""
AUTHD_LIBS="-lauth -le"
savedLIBS="$LIBS"
savedCFLAGS="$CFLAGS"
LIBS="$SSL_LIBS $AUTHD_LIBS $LIBS"
CFLAGS="$SSL_CPPFLAGS $CFLAGS $AUTHD_CFLAGS"
AC_CHECK_LIB(auth, auth_init_credentials, [have_authd=yes], [have_authd=no])
AC_SUBST(AUTHD_LIBS)
AC_SUBST(AUTHD_CFLAGS)
AM_CONDITIONAL(WITH_AUTHD, test "x$have_authd" = "xyes")
LIBS="$savedLIBS"
CFLAGS="$savedCFLAGS"


dnl check for whether to include TotalView support
dnl
AC_MSG_CHECKING([Whether to include TotalView support])
totalview=no
AC_ARG_WITH(totalview,
  AC_HELP_STRING([--with-totalview],[compile with TotalView support]),
  [ totalview=yes 
    AC_DEFINE(HAVE_TOTALVIEW, 1, [Define if you have TotalView support])
    TOTALVIEW_LIBS="-ltvmpich"
  ]
)
AC_MSG_RESULT(${totalview=no})
if test x$totalview = xyes; then
   savedLIBS="$LIBS"
   LIBS="$TOTALVIEW_LIBS $LIBS"
   AC_DEFINE(HAVE_TOTALVIEW, 1, [Define if you have TotalView support])
   LIBS="$savedLIBS"
   AC_SUBST(TOTALVIEW_LIBS)
fi
AM_CONDITIONAL(WITH_TOTALVIEW, test "x$with_totalview" = "xyes")


#
# Add system directories and config files to CPPFLAGS
#
CPPFLAGS="$CPPFLAGS -DSLURM_CONFIG_FILE=\\\"\$(sysconfdir)/slurm.conf\\\""
CPPFLAGS="$CPPFLAGS -DSLURM_PLUGIN_PATH=\\\"\$(pkglibdir)\\\""

dnl Add LSD-Tools defines:
AC_DEFINE(WITH_LSD_FATAL_ERROR_FUNC, 1, [Have definition of lsd_fatal_error()])
AC_DEFINE(WITH_LSD_NOMEM_ERROR_FUNC, 1, [Have definition of lsd_nomem_error()])

dnl All slurm Makefiles:

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
		 src/sinfo/Makefile
		 src/plugins/Makefile
		 src/plugins/auth/Makefile
		 doc/Makefile
		 doc/man/Makefile
		 testsuite/Makefile
		 testsuite/slurm_unit/Makefile
		 testsuite/slurm_unit/common/Makefile
		 testsuite/slurm_unit/slurmctld/Makefile
		 testsuite/slurm_unit/slurmd/Makefile
		 testsuite/slurm_unit/api/Makefile
		 testsuite/slurm_unit/api/manual/Makefile
		 slurm.spec
		 ]
)

AC_OUTPUT

