# $Id$
# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl
AC_INIT
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([auxdir])
AC_CANONICAL_TARGET([])

X_AC_GPL_LICENSED

#
# Determine project/version from META file.
# Sets PACKAGE, VERSION, SLURM_VERSION
X_AC_SLURM_VERSION


dnl Initialize Automake
dnl
AM_INIT_AUTOMAKE(slurm, $VERSION)
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([slurm/slurm.h])

X_AC_AIX

dnl
dnl Check to see if this architecture should use slurm_* prefix function
dnl aliases for plugins.
dnl
case "$host" in
	*-*-aix*) AC_DEFINE(USE_ALIAS, 0, 
                 	[Define slurm_ prefix function aliases for plusins]) ;;
	*darwin*) AC_DEFINE(USE_ALIAS, 0,
                  	[Define slurm_ prefix function aliases for plusins]) ;;
	*)        AC_DEFINE(USE_ALIAS, 1, 
			[Define slurm_ prefix function aliases for plugins]) ;;
esac

dnl Checks for programs.
dnl
AC_PROG_CC
AC_PROG_CXX
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

AM_CONDITIONAL(WITH_GNU_LD, test "$with_gnu_ld" = "yes")


dnl Checks for libraries
dnl

dnl Checks for header files.
dnl
AC_CHECK_HEADERS(mcheck.h values.h socket.h sys/socket.h  \
                 stdbool.h sys/ipc.h sys/shm.h sys/sem.h errno.h \
                 stdlib.h dirent.h pthread.h sys/prctl.h \
                 sysint.h inttypes.h termcap.h netdb.h sys/socket.h  \
                 sys/systemcfg.h ncurses.h curses.h sys/dr.h sys/vfs.h \
                 pam/pam_appl.h security/pam_appl.h sys/sysctl.h \
                 pty.h utmp.h \
		 sys/syslog.h \
		)
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_STDC


dnl Checks for structures.
dnl
X_AC__SYSTEM_CONFIGURATION

dnl check to see if glibc's program_invocation_name is available:
dnl
X_AC_SLURM_PROGRAM_INVOCATION_NAME

dnl Check if ptrace takes four or five arguments
dnl
X_AC_PTRACE

dnl Check if setpgrp takes zero or two arguments
dnl
X_AC_SETPGRP

dnl Check of sched_getaffinity exists and it's argument count
dnl
X_AC_AFFINITY

dnl
dnl Check for PAM module support
X_AC_PAM

dnl Checks for types.
dnl
X_AC_SLURM_BIGENDIAN

dnl Checks for compiler characteristics.
dnl
AC_PROG_GCC_TRADITIONAL([])


dnl checks for library functions.
dnl
AC_FUNC_MALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS( \
   fdatasync \
   hstrerror \
   strerror  \
   mtrace    \
   strndup   \
   strlcpy   \
   strsignal \ 
   inet_aton \
   inet_ntop \
   inet_pton \
   setproctitle \
   sysctlbyname \
)

AC_CHECK_DECLS([hstrerror, strsignal, sys_siglist])

AC_CHECK_FUNCS(unsetenv, [have_unsetenv=yes])
AM_CONDITIONAL(HAVE_UNSETENV, test "x$have_unsetenv" = "xyes")

ACX_PTHREAD([], AC_MSG_ERROR([Error: Cannot figure out how to use pthreads!]))

# Always define WITH_PTHREADS if we make it this far
AC_DEFINE(WITH_PTHREADS,1,[Define if you have pthreads.])
LDFLAGS="$LDFLAGS "
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"

X_AC_BGL
X_AC_BGP
AM_CONDITIONAL(BLUEGENE_LOADED, test "x$ac_bluegene_loaded" = "xyes")
AC_SUBST(BLUEGENE_LOADED)

X_AC_CFLAGS
X_AC_XCPU
X_AC_SLURM_SEMAPHORE

X_AC_NCURSES
AM_CONDITIONAL(HAVE_SOME_CURSES, test "x$ac_have_some_curses" = "xyes")
AC_SUBST(HAVE_SOME_CURSES)

X_AC_GTK
AM_CONDITIONAL(HAVE_GTK, test "x$ac_have_gtk" = "xyes")

X_AC_DATABASES

dnl checks for system services.
dnl


dnl checks for system-specific stuff.
dnl

dnl check for how to emulate setproctitle
dnl
X_AC_SETPROCTITLE

dnl check for debug compilation
dnl
X_AC_DEBUG
AM_CONDITIONAL(DEBUG_MODULES, test "x$ac_debug" = "xtrue")


dnl check for slurmctld, slurmd and slurmdbd default ports
dnl
X_AC_SLURM_PORTS([6817], [6818], [6819])


dnl add SLURM_PREFIX to config.h
dnl
if test "x$prefix" == "xNONE" ; then
  AC_DEFINE_UNQUOTED(SLURM_PREFIX, "/usr/local", [Define Slurm installation prefix])
else
  AC_DEFINE_UNQUOTED(SLURM_PREFIX, "$prefix", [Define Slurm installation prefix])
fi
AC_SUBST(SLURM_PREFIX)

dnl check for whether to include Elan support
dnl
X_AC_ELAN
AM_CONDITIONAL(HAVE_ELAN, test "x$ac_have_elan" = "xyes")
AC_SUBST(HAVE_ELAN)

dnl check for whether to include Federation support
dnl
X_AC_FEDERATION
AM_CONDITIONAL(HAVE_FEDERATION, test "x$ac_have_federation" = "xyes")
AC_SUBST(HAVE_FEDERATION)

dnl check for SGI job container support
dnl
X_AC_SGI_JOB

dnl Check for whether to include readline support
dnl 
X_AC_READLINE

dnl 
dnl
X_AC_SLURM_WITH_SSL
AM_CONDITIONAL(HAVE_OPENSSL, test "x$ac_have_openssl" = "xyes")
AC_SUBST(HAVE_OPENSSL)

dnl
dnl Check for compilation of SLURM auth modules:
dnl
X_AC_MUNGE

dnl Check if srun --get-user-env (for Moab) should load user's environment 
dnl based upon a login. This may be temporary.
dnl
AC_MSG_CHECKING([if user env vars should be based upon login])
AC_ARG_ENABLE(
  [load-env-no-login],
  AS_HELP_STRING(--enable-load-env-no-login,
                 [enable --get-user-env option to load user environment without login]),
  [ case "$enableval" in
     yes) x_ac_load_env_no_login=yes ;;
      no) x_ac_load_env_no_login=no ;;
       *) AC_MSG_RESULT([doh!])
          AC_MSG_ERROR([bad value "$enableval" for --enable-load-env-no-login]) ;;
   esac
  ]
)
if test "$x_ac_load_env_no_login" = yes; then
  AC_DEFINE(LOAD_ENV_NO_LOGIN, 1, 
            [Define to 1 for --get-user-env to load user environment without login.])
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi

dnl
dnl Check if multiple-slurmd support is requested and define MULTIPLE_SLURMD
dnl if it is.
dnl
AC_MSG_CHECKING(whether to enable multiple-slurmd support)
AC_ARG_ENABLE([multiple-slurmd],
  AS_HELP_STRING(--enable-multiple-slurmd,enable multiple-slurmd support),
    [ case "$enableval" in
      yes) multiple_slurmd=yes ;;
      no)  multiple_slurmd=no ;;
      *)   AC_MSG_ERROR([bad value "$enableval" for --enable-multiple-slurmd]);;
    esac ]
)
if test "x$multiple_slurmd" = "xyes"; then
  AC_DEFINE([MULTIPLE_SLURMD], [1], [Enable multiple slurmd on one node])
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi


AUTHD_LIBS="-lauth -le"
savedLIBS="$LIBS"
savedCFLAGS="$CFLAGS"
LIBS="$SSL_LIBS $AUTHD_LIBS $LIBS"
CFLAGS="$SSL_CPPFLAGS $CFLAGS"
AC_CHECK_LIB(auth, auth_init_credentials, [have_authd=yes], [have_authd=no])
AC_SUBST(AUTHD_LIBS)
AC_SUBST(AUTHD_CFLAGS)
AM_CONDITIONAL(WITH_AUTHD, test "x$have_authd" = "xyes")
LIBS="$savedLIBS"
CFLAGS="$savedCFLAGS"

savedLIBS="$LIBS"
LIBS="-lutil $LIBS"
AC_CHECK_LIB(util, openpty, [UTIL_LIBS="-lutil"], [])
AC_SUBST(UTIL_LIBS)
LIBS="$savedLIBS"

dnl Add LSD-Tools defines:
AC_DEFINE(WITH_LSD_FATAL_ERROR_FUNC, 1, [Have definition of lsd_fatal_error()])
AC_DEFINE(WITH_LSD_NOMEM_ERROR_FUNC, 1, [Have definition of lsd_nomem_error()])

dnl All slurm Makefiles:

AC_CONFIG_FILES([Makefile
		 config.xml 
		 auxdir/Makefile
		 contribs/Makefile
		 contribs/perlapi/Makefile
		 contribs/perlapi/libslurm-perl/Makefile.PL
		 contribs/torque/Makefile
		 contribs/phpext/Makefile
		 contribs/phpext/slurm_php/config.m4
		 contribs/python/Makefile
		 contribs/python/hostlist/Makefile
		 contribs/python/hostlist/test/Makefile
		 src/Makefile 
		 src/api/Makefile 
		 src/common/Makefile
		 src/database/Makefile
		 src/sacct/Makefile
		 src/sacctmgr/Makefile
		 src/sreport/Makefile
		 src/sstat/Makefile
		 src/sshare/Makefile
		 src/salloc/Makefile 
		 src/sbatch/Makefile 
		 src/sattach/Makefile
		 src/srun/Makefile 
		 src/slurmd/Makefile 
		 src/slurmd/slurmd/Makefile 
		 src/slurmd/slurmstepd/Makefile 
		 src/slurmdbd/Makefile
		 src/slurmctld/Makefile
		 src/sbcast/Makefile 
		 src/scontrol/Makefile
		 src/scancel/Makefile
		 src/squeue/Makefile
		 src/sinfo/Makefile
		 src/smap/Makefile
		 src/strigger/Makefile
		 src/sview/Makefile
		 src/plugins/Makefile
		 src/plugins/accounting_storage/Makefile
		 src/plugins/accounting_storage/filetxt/Makefile
		 src/plugins/accounting_storage/mysql/Makefile
		 src/plugins/accounting_storage/pgsql/Makefile
		 src/plugins/accounting_storage/none/Makefile
		 src/plugins/accounting_storage/slurmdbd/Makefile
		 src/plugins/auth/Makefile
		 src/plugins/auth/authd/Makefile
		 src/plugins/auth/munge/Makefile
		 src/plugins/auth/none/Makefile
		 src/plugins/checkpoint/Makefile
		 src/plugins/checkpoint/aix/Makefile
		 src/plugins/checkpoint/none/Makefile
		 src/plugins/checkpoint/ompi/Makefile
		 src/plugins/checkpoint/xlch/Makefile
		 src/plugins/crypto/Makefile
		 src/plugins/crypto/munge/Makefile
		 src/plugins/crypto/openssl/Makefile
		 src/plugins/jobacct_gather/Makefile
		 src/plugins/jobacct_gather/linux/Makefile
		 src/plugins/jobacct_gather/aix/Makefile
		 src/plugins/jobacct_gather/none/Makefile
		 src/plugins/jobcomp/Makefile
		 src/plugins/jobcomp/filetxt/Makefile
		 src/plugins/jobcomp/none/Makefile
		 src/plugins/jobcomp/script/Makefile
		 src/plugins/jobcomp/mysql/Makefile
		 src/plugins/jobcomp/pgsql/Makefile
		 src/plugins/priority/Makefile
		 src/plugins/priority/basic/Makefile
		 src/plugins/priority/multifactor/Makefile
		 src/plugins/proctrack/Makefile
		 src/plugins/proctrack/aix/Makefile
		 src/plugins/proctrack/pgid/Makefile
		 src/plugins/proctrack/linuxproc/Makefile
		 src/plugins/proctrack/rms/Makefile
		 src/plugins/proctrack/sgi_job/Makefile
		 src/plugins/sched/Makefile
		 src/plugins/sched/backfill/Makefile
		 src/plugins/sched/builtin/Makefile
		 src/plugins/sched/gang/Makefile
		 src/plugins/sched/hold/Makefile
		 src/plugins/sched/wiki/Makefile
		 src/plugins/sched/wiki2/Makefile
		 src/plugins/select/Makefile
		 src/plugins/select/bluegene/Makefile
		 src/plugins/select/bluegene/block_allocator/Makefile
		 src/plugins/select/bluegene/plugin/Makefile
		 src/plugins/select/linear/Makefile
		 src/plugins/select/cons_res/Makefile
		 src/plugins/switch/Makefile
		 src/plugins/switch/elan/Makefile
		 src/plugins/switch/none/Makefile
		 src/plugins/switch/federation/Makefile
		 src/plugins/mpi/Makefile
		 src/plugins/mpi/mpich1_p4/Makefile
		 src/plugins/mpi/mpich1_shmem/Makefile
		 src/plugins/mpi/mpichgm/Makefile
		 src/plugins/mpi/mpichmx/Makefile
		 src/plugins/mpi/mvapich/Makefile
		 src/plugins/mpi/lam/Makefile
		 src/plugins/mpi/none/Makefile
		 src/plugins/mpi/openmpi/Makefile
		 src/plugins/task/Makefile
		 src/plugins/task/affinity/Makefile
		 src/plugins/task/none/Makefile
		 doc/Makefile
		 doc/man/Makefile
		 doc/html/Makefile
		 doc/html/configurator.html
		 testsuite/Makefile
		 testsuite/expect/Makefile
		 testsuite/slurm_unit/Makefile
		 testsuite/slurm_unit/api/Makefile
		 testsuite/slurm_unit/api/manual/Makefile
		 testsuite/slurm_unit/common/Makefile
		 testsuite/slurm_unit/slurmctld/Makefile
		 testsuite/slurm_unit/slurmd/Makefile
		 testsuite/slurm_unit/slurmdbd/Makefile
		 ]
)

AC_OUTPUT
