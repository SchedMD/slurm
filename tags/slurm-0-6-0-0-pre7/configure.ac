# $Id$
# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl
AC_INIT
AC_PREREQ(2.52)
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
AM_CONFIG_HEADER(config.h)
AM_CONFIG_HEADER(slurm/slurm.h)

X_AC_AIX

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
                 sys/systemcfg.h ncurses.h curses.h sys/dr.h \
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
   hstrerror \
   strerror  \
   mtrace    \
   strsignal \ 
   inet_aton \
   inet_ntop \
   inet_pton \
   setproctitle \
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
X_AC_SLURM_SEMAPHORE

X_AC_NCURSES
AM_CONDITIONAL(HAVE_SOME_CURSES, test "x$ac_have_some_curses" = "xyes")
AC_SUBST(HAVE_SOME_CURSES)

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


dnl check for slurmd and slurmctld default ports
dnl
X_AC_SLURM_PORTS([6817], [6818])


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

dnl Check for whether to include readline support
dnl 
X_AC_READLINE

dnl 
dnl
X_AC_SLURM_WITH_SSL


dnl
dnl Check for compilation of SLURM auth modules:
dnl
X_AC_MUNGE

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
		 config.xml 
		 auxdir/Makefile
		 src/Makefile 
		 src/api/Makefile 
		 src/common/Makefile
		 src/sacct/Makefile
		 src/srun/Makefile 
		 src/slurmd/Makefile 
		 src/slurmctld/Makefile 
		 src/scontrol/Makefile
		 src/scancel/Makefile
		 src/squeue/Makefile
		 src/sinfo/Makefile
		 src/smap/Makefile
		 src/partition_allocator/Makefile
		 src/plugins/Makefile
		 src/plugins/auth/Makefile
		 src/plugins/auth/authd/Makefile
		 src/plugins/auth/munge/Makefile
		 src/plugins/auth/none/Makefile
		 src/plugins/checkpoint/Makefile
		 src/plugins/checkpoint/aix/Makefile
		 src/plugins/checkpoint/none/Makefile
		 src/plugins/jobacct/Makefile
		 src/plugins/jobacct/log/Makefile
		 src/plugins/jobacct/none/Makefile
		 src/plugins/jobcomp/Makefile
		 src/plugins/jobcomp/filetxt/Makefile
		 src/plugins/jobcomp/none/Makefile
		 src/plugins/jobcomp/script/Makefile
		 src/plugins/proctrack/Makefile
		 src/plugins/proctrack/aix/Makefile
		 src/plugins/proctrack/pgid/Makefile
		 src/plugins/proctrack/linuxproc/Makefile
		 src/plugins/proctrack/rms/Makefile
		 src/plugins/sched/Makefile
		 src/plugins/sched/backfill/Makefile
		 src/plugins/sched/builtin/Makefile
		 src/plugins/sched/hold/Makefile
		 src/plugins/sched/maui/Makefile
		 src/plugins/sched/maui/wiki/Makefile
		 src/plugins/select/Makefile
		 src/plugins/select/bluegene/Makefile
		 src/plugins/select/linear/Makefile
		 src/plugins/select/cons_res/Makefile
		 src/plugins/switch/Makefile
		 src/plugins/switch/elan/Makefile
		 src/plugins/switch/none/Makefile
		 src/plugins/switch/federation/Makefile
		 src/plugins/mpi/Makefile
		 src/plugins/mpi/mpichgm/Makefile
		 src/plugins/mpi/mvapich/Makefile
		 src/plugins/mpi/lam/Makefile
		 src/plugins/mpi/none/Makefile
		 doc/Makefile
		 doc/man/Makefile
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

