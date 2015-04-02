# $Id$
# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl

AC_INIT(slurm, m4_esyscmd([perl -ne 'print,exit if s/^\s*VERSION:\s*(\d*.\d*).\S*/\1/i' ./META | sed 's/^v//' | tr '-' '_' | tr -d '\n']), [slurm-dev@schedmd.com], [], [http://slurm.schedmd.com])
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([auxdir])
AC_CANONICAL_TARGET([])

dnl the is a generic flag to avoid building things
AM_CONDITIONAL(DONT_BUILD, test "1" = "0")

X_AC_GPL_LICENSED

# Determine project/version from META file.
# Sets PACKAGE, VERSION, SLURM_VERSION
X_AC_SLURM_VERSION

dnl Initialize Automake
dnl
dnl If you ever change to use AM_INIT_AUTOMAKE(subdir-objects) edit
dnl auxdir/slurm.m4 to not define VERSION
dnl
AM_INIT_AUTOMAKE(no-define)
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([slurm/slurm.h])

dnl This needs to be close to the front to set CFLAGS=-m64
X_AC_RPATH
X_AC_BGL

dnl we need to know if this is a bgl in the Makefile.am to do
dnl some things differently
AM_CONDITIONAL(BGL_LOADED, test "x$ac_bluegene_loaded" = "xyes")
AC_SUBST(BGL_LOADED)

X_AC_BGP

dnl ok now check if We have an L or P system, Q is handled differently
dnl so handle it later.
AM_CONDITIONAL(BG_L_P_LOADED, test "x$ac_bluegene_loaded" = "xyes")
AC_SUBST(BG_L_P_LOADED)

dnl ok now check if We are on a real L or P system, (test if to build srun
dnl or not.  If we are emulating things we should build it.
AM_CONDITIONAL(REAL_BG_L_P_LOADED, test "x$ac_real_bluegene_loaded" = "xyes")
AC_SUBST(REAL_BG_L_P_LOADED)

X_AC_BGQ

dnl We need to know if this is a Q system
AM_CONDITIONAL(BGQ_LOADED, test "x$ac_bgq_loaded" = "xyes")
AC_SUBST(BGQ_LOADED)

dnl ok now check if We are on a real L or P system, (test if to build srun
dnl or not.  If we are emulating things we should build it.
AM_CONDITIONAL(REAL_BGQ_LOADED, test "x$ac_real_bluegene_loaded" = "xyes")
AC_SUBST(REAL_BGQ_LOADED)


dnl ok now check if any bluegene was loaded.
AM_CONDITIONAL(BLUEGENE_LOADED, test "x$ac_bluegene_loaded" = "xyes")
AC_SUBST(BLUEGENE_LOADED)


X_AC_AIX

dnl
dnl Check to see if this architecture should use slurm_* prefix function
dnl aliases for plugins.
dnl
case "$host" in
	*-*-aix*) AC_DEFINE(USE_ALIAS, 0,
			[Define slurm_ prefix function aliases for plugins]) ;;
	*darwin*) AC_DEFINE(USE_ALIAS, 0,
			[Define slurm_ prefix function aliases for plugins]) ;;
	*)        AC_DEFINE(USE_ALIAS, 1,
			[Define slurm_ prefix function aliases for plugins]) ;;
esac

ac_have_cygwin=no
dnl
dnl add some flags for Solaris and cygwin
dnl
case "$host" in
     	*cygwin)   LDFLAGS="$LDFLAGS -no-undefined"
		   SO_LDFLAGS="$SO_LDFLAGS \$(top_builddir)/src/api/libslurmhelper.la"
		   AC_SUBST(SO_LDFLAGS)
		   ac_have_cygwin=yes
		   ;;
	*solaris*) CC="/usr/sfw/bin/gcc"
		   CFLAGS="$CFLAGS -D_POSIX_PTHREAD_SEMANTICS -I/usr/sfw/include"
		   LDFLAGS="$LDFLAGS -L/usr/sfw/lib"
		   ;;
esac
AM_CONDITIONAL(WITH_CYGWIN, test x"$ac_have_cygwin" = x"yes")

dnl Checks for programs.
dnl
AC_PROG_CC
AC_PROG_CXX
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG([0.9.0])

AM_CONDITIONAL(WITH_CXX, test -n "$ac_ct_CXX")
AM_CONDITIONAL(WITH_GNU_LD, test "$with_gnu_ld" = "yes")


dnl Checks for libraries
dnl
AC_SEARCH_LIBS([socket],        [socket])
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([hstrerror],     [resolv])
AC_SEARCH_LIBS([kstat_open],    [kstat])

dnl Checks for header files.
dnl
AC_CHECK_HEADERS(mcheck.h values.h socket.h sys/socket.h  \
		 stdbool.h sys/ipc.h sys/shm.h sys/sem.h errno.h \
		 stdlib.h dirent.h pthread.h sys/prctl.h \
		 sysint.h inttypes.h termcap.h netdb.h sys/socket.h  \
		 sys/systemcfg.h ncurses.h curses.h sys/dr.h sys/vfs.h \
		 pam/pam_appl.h security/pam_appl.h sys/sysctl.h \
		 pty.h utmp.h \
		 sys/syslog.h linux/sched.h \
		 kstat.h paths.h limits.h sys/statfs.h sys/ptrace.h \
		 sys/termios.h float.h sys/statvfs.h
		)
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_STDC


dnl Checks for structures.
dnl
X_AC__SYSTEM_CONFIGURATION

dnl Check for dlfcn
dnl
X_AC_DLFCN

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

dnl
dnl Check for ISO compliance
X_AC_ISO

dnl
dnl Check if we want to load .login with sbatch --get-user-env option
X_AC_ENV_LOGIC

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
   cfmakeraw \
   setresuid \
   get_current_dir_name \
   faccessat \
   eaccess \
   statvfs \
   statfs \
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

X_AC_SUN_CONST
X_AC_DIMENSIONS

X_AC_CFLAGS
X_AC_OFED

AX_LIB_HDF5()
AM_CONDITIONAL(BUILD_HDF5, test "$with_hdf5" = "yes")
# Some older systems (Debian/Ubuntu/...) configure HDF5 with
# --with-default-api-version=v16 which creates problems for slurm
# because slurm uses the 1.8 API. By defining this CPP macro we get
# the 1.8 API.
AC_DEFINE([H5_NO_DEPRECATED_SYMBOLS], [1], [Make sure we get the 1.8 HDF5 API])

X_AC_HWLOC
X_AC_FREEIPMI
X_AC_XCPU
X_AC_SLURM_SEMAPHORE
X_AC_RRDTOOL

X_AC_NCURSES
AM_CONDITIONAL(HAVE_SOME_CURSES, test "x$ac_have_some_curses" = "xyes")
AC_SUBST(HAVE_SOME_CURSES)

#
#  Tests for Check
#

PKG_CHECK_MODULES([CHECK], [check >= 0.9.8], [ac_have_check="yes"], [ac_have_check="no"])
AM_CONDITIONAL(HAVE_CHECK, test "x$ac_have_check" = "xyes")

#
#  Tests for GTK+
#

# use the correct libs if running on 64bit
if test -d "/usr/lib64/pkgconfig"; then
    PKG_CONFIG_PATH="/usr/lib64/pkgconfig/:$PKG_CONFIG_PATH"
fi

if test -d "/opt/gnome/lib64/pkgconfig"; then
    PKG_CONFIG_PATH="/opt/gnome/lib64/pkgconfig/:$PKG_CONFIG_PATH"
fi

AM_PATH_GLIB_2_0([2.7.1], [ac_glib_test="yes"], [ac_glib_test="no"], [gthread])

if test ${glib_config_minor_version=0} -ge 32 ; then
       AC_DEFINE([GLIB_NEW_THREADS], 1, [Define to 1 if using glib-2.32.0 or higher])
fi

AM_PATH_GTK_2_0([2.7.1], [ac_gtk_test="yes"], [ac_gtk_test="no"], [gthread])
if test ${gtk_config_minor_version=0} -ge 10 ; then
       AC_DEFINE([GTK2_USE_RADIO_SET], 1, [Define to 1 if using gtk+-2.10.0 or higher])
fi

if test ${gtk_config_minor_version=0} -ge 12 ; then
       AC_DEFINE([GTK2_USE_TOOLTIP], 1, [Define to 1 if using gtk+-2.12.0 or higher])
fi

if test ${gtk_config_minor_version=0} -ge 14 ; then
       AC_DEFINE([GTK2_USE_GET_FOCUS], 1, [Define to 1 if using gtk+-2.14.0 or higher])
fi

if test "x$ac_glib_test" != "xyes" -o "x$ac_gtk_test" != "xyes"; then
	AC_MSG_WARN([cannot build sview without gtk library]);
fi

AM_CONDITIONAL(BUILD_SVIEW, [test "x$ac_glib_test" = "xyes"] && [test "x$ac_gtk_test" = "xyes"])

X_AC_DATABASES

dnl Cray ALPS/Basil support depends on mySQL
X_AC_CRAY

dnl checks for system services.
dnl


dnl checks for system-specific stuff.
dnl

dnl check for how to emulate setproctitle
dnl
X_AC_SETPROCTITLE

dnl check for debug compilation, must follow X_AC_CRAY
dnl
X_AC_DEBUG
AM_CONDITIONAL(DEBUG_MODULES, test "x$ac_debug" = "xtrue")


dnl check for slurmctld, slurmd and slurmdbd default ports,
dnl and default number of slurmctld ports
dnl
X_AC_SLURM_PORTS([6817], [6818], [6819], [1])


dnl add SLURM_PREFIX to config.h
dnl
if test "x$prefix" = "xNONE" ; then
  AC_DEFINE_UNQUOTED(SLURM_PREFIX, "/usr/local", [Define Slurm installation prefix])
else
  AC_DEFINE_UNQUOTED(SLURM_PREFIX, "$prefix", [Define Slurm installation prefix])
fi
AC_SUBST(SLURM_PREFIX)

dnl check for whether to include IBM NRT (Network Resource Table) support
dnl
X_AC_NRT

dnl check for SGI job container support
dnl
X_AC_SGI_JOB

dnl check for lua library
dnl
X_AC_LUA

dnl check for presence of the man2html command
dnl
X_AC_MAN2HTML
AM_CONDITIONAL(HAVE_MAN2HTML, test "x$ac_have_man2html" = "xyes")
AC_SUBST(HAVE_MAN2HTML)

dnl check if we can use standard printf functions
dnl
X_AC_PRINTF_NULL

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

dnl
dnl Check for compilation of SLURM with BLCR support:
dnl
X_AC_BLCR

dnl
dnl Set some configuration based upon multiple configuration parameters
dnl
ac_build_smap="no"
if test "x$ac_have_some_curses" = "xyes" ; then
   ac_build_smap="yes"
fi
AM_CONDITIONAL(BUILD_SMAP, test "x$ac_build_smap" = "xyes")

dnl All slurm Makefiles:

AC_CONFIG_FILES([Makefile
		 config.xml
		 auxdir/Makefile
		 contribs/Makefile
		 contribs/cray/Makefile
		 contribs/lua/Makefile
		 contribs/mic/Makefile
		 contribs/pam/Makefile
		 contribs/perlapi/Makefile
		 contribs/perlapi/libslurm/Makefile
		 contribs/perlapi/libslurm/perl/Makefile.PL
		 contribs/perlapi/libslurmdb/Makefile
		 contribs/perlapi/libslurmdb/perl/Makefile.PL
		 contribs/torque/Makefile
		 contribs/phpext/Makefile
		 contribs/phpext/slurm_php/config.m4
		 contribs/sgather/Makefile
		 contribs/sjobexit/Makefile
		 contribs/slurmdb-direct/Makefile
		 contribs/pmi2/Makefile
		 doc/Makefile
		 doc/man/Makefile
		 doc/man/man1/Makefile
		 doc/man/man3/Makefile
		 doc/man/man5/Makefile
		 doc/man/man8/Makefile
		 doc/html/Makefile
		 doc/html/configurator.html
		 doc/html/configurator.easy.html
		 etc/cgroup.release_common.example
		 etc/init.d.slurm
		 etc/init.d.slurmdbd
		 etc/slurmctld.service
		 etc/slurmd.service
		 etc/slurmdbd.service
		 src/Makefile
		 src/api/Makefile
		 src/common/Makefile
		 src/db_api/Makefile
		 src/database/Makefile
		 src/sacct/Makefile
		 src/sacctmgr/Makefile
		 src/sreport/Makefile
		 src/salloc/Makefile
		 src/sbatch/Makefile
		 src/sbcast/Makefile
		 src/sattach/Makefile
		 src/scancel/Makefile
		 src/scontrol/Makefile
		 src/sdiag/Makefile
		 src/sinfo/Makefile
		 src/slurmctld/Makefile
		 src/slurmd/Makefile
		 src/slurmd/common/Makefile
		 src/slurmd/slurmd/Makefile
		 src/slurmd/slurmstepd/Makefile
		 src/slurmdbd/Makefile
		 src/smap/Makefile
		 src/smd/Makefile
		 src/sprio/Makefile
		 src/squeue/Makefile
		 src/srun/Makefile
		 src/srun/libsrun/Makefile
		 src/srun_cr/Makefile
		 src/sshare/Makefile
		 src/sstat/Makefile
		 src/strigger/Makefile
		 src/sview/Makefile
		 src/plugins/Makefile
		 src/plugins/accounting_storage/Makefile
		 src/plugins/accounting_storage/common/Makefile
		 src/plugins/accounting_storage/filetxt/Makefile
		 src/plugins/accounting_storage/mysql/Makefile
		 src/plugins/accounting_storage/none/Makefile
		 src/plugins/accounting_storage/slurmdbd/Makefile
		 src/plugins/auth/Makefile
		 src/plugins/auth/authd/Makefile
		 src/plugins/auth/munge/Makefile
		 src/plugins/auth/none/Makefile
		 src/plugins/checkpoint/Makefile
		 src/plugins/checkpoint/aix/Makefile
		 src/plugins/checkpoint/blcr/Makefile
		 src/plugins/checkpoint/blcr/cr_checkpoint.sh
		 src/plugins/checkpoint/blcr/cr_restart.sh
		 src/plugins/checkpoint/none/Makefile
		 src/plugins/checkpoint/ompi/Makefile
		 src/plugins/checkpoint/poe/Makefile
		 src/plugins/core_spec/Makefile
		 src/plugins/core_spec/cray/Makefile
		 src/plugins/core_spec/none/Makefile
		 src/plugins/crypto/Makefile
		 src/plugins/crypto/munge/Makefile
		 src/plugins/crypto/openssl/Makefile
		 src/plugins/ext_sensors/Makefile
		 src/plugins/ext_sensors/rrd/Makefile
		 src/plugins/ext_sensors/none/Makefile
		 src/plugins/route/Makefile
		 src/plugins/route/default/Makefile
		 src/plugins/route/topology/Makefile
		 src/plugins/gres/Makefile
		 src/plugins/gres/gpu/Makefile
		 src/plugins/gres/nic/Makefile
		 src/plugins/gres/mic/Makefile
		 src/plugins/jobacct_gather/Makefile
		 src/plugins/jobacct_gather/common/Makefile
		 src/plugins/jobacct_gather/linux/Makefile
		 src/plugins/jobacct_gather/aix/Makefile
		 src/plugins/jobacct_gather/cgroup/Makefile
		 src/plugins/jobacct_gather/none/Makefile
		 src/plugins/acct_gather_energy/Makefile
		 src/plugins/acct_gather_energy/rapl/Makefile
		 src/plugins/acct_gather_energy/ipmi/Makefile
		 src/plugins/acct_gather_energy/none/Makefile
		 src/plugins/acct_gather_infiniband/Makefile
		 src/plugins/acct_gather_infiniband/ofed/Makefile
		 src/plugins/acct_gather_infiniband/none/Makefile
		 src/plugins/acct_gather_filesystem/Makefile
		 src/plugins/acct_gather_filesystem/lustre/Makefile
		 src/plugins/acct_gather_filesystem/none/Makefile
		 src/plugins/acct_gather_profile/Makefile
		 src/plugins/acct_gather_profile/hdf5/Makefile
		 src/plugins/acct_gather_profile/hdf5/sh5util/Makefile
		 src/plugins/acct_gather_profile/none/Makefile
		 src/plugins/jobcomp/Makefile
		 src/plugins/jobcomp/filetxt/Makefile
		 src/plugins/jobcomp/none/Makefile
		 src/plugins/jobcomp/script/Makefile
		 src/plugins/jobcomp/mysql/Makefile
		 src/plugins/job_container/Makefile
		 src/plugins/job_container/cncu/Makefile
		 src/plugins/job_container/none/Makefile
		 src/plugins/job_submit/Makefile
		 src/plugins/job_submit/all_partitions/Makefile
		 src/plugins/job_submit/cnode/Makefile
		 src/plugins/job_submit/cray/Makefile
		 src/plugins/job_submit/defaults/Makefile
		 src/plugins/job_submit/logging/Makefile
		 src/plugins/job_submit/lua/Makefile
		 src/plugins/job_submit/partition/Makefile
		 src/plugins/job_submit/pbs/Makefile
		 src/plugins/job_submit/require_timelimit/Makefile
		 src/plugins/job_submit/throttle/Makefile
		 src/plugins/launch/Makefile
		 src/plugins/launch/aprun/Makefile
		 src/plugins/launch/poe/Makefile
		 src/plugins/launch/runjob/Makefile
		 src/plugins/launch/slurm/Makefile
		 src/plugins/preempt/Makefile
		 src/plugins/preempt/job_prio/Makefile
		 src/plugins/preempt/none/Makefile
		 src/plugins/preempt/partition_prio/Makefile
		 src/plugins/preempt/qos/Makefile
		 src/plugins/priority/Makefile
		 src/plugins/priority/basic/Makefile
		 src/plugins/priority/multifactor/Makefile
		 src/plugins/proctrack/Makefile
		 src/plugins/proctrack/aix/Makefile
		 src/plugins/proctrack/cray/Makefile
		 src/plugins/proctrack/cgroup/Makefile
		 src/plugins/proctrack/pgid/Makefile
		 src/plugins/proctrack/linuxproc/Makefile
		 src/plugins/proctrack/sgi_job/Makefile
		 src/plugins/proctrack/lua/Makefile
		 src/plugins/sched/Makefile
		 src/plugins/sched/backfill/Makefile
		 src/plugins/sched/builtin/Makefile
		 src/plugins/sched/hold/Makefile
		 src/plugins/sched/wiki/Makefile
		 src/plugins/sched/wiki2/Makefile
		 src/plugins/select/Makefile
		 src/plugins/select/alps/Makefile
		 src/plugins/select/alps/libalps/Makefile
		 src/plugins/select/alps/libemulate/Makefile
		 src/plugins/select/bluegene/Makefile
		 src/plugins/select/bluegene/ba/Makefile
		 src/plugins/select/bluegene/ba_bgq/Makefile
		 src/plugins/select/bluegene/bl/Makefile
		 src/plugins/select/bluegene/bl_bgq/Makefile
		 src/plugins/select/bluegene/sfree/Makefile
		 src/plugins/select/cons_res/Makefile
		 src/plugins/select/cray/Makefile
		 src/plugins/select/linear/Makefile
		 src/plugins/select/other/Makefile
		 src/plugins/select/serial/Makefile
		 src/plugins/slurmctld/Makefile
		 src/plugins/slurmctld/dynalloc/Makefile
		 src/plugins/slurmctld/nonstop/Makefile
		 src/plugins/slurmd/Makefile
		 src/plugins/switch/Makefile
		 src/plugins/switch/cray/Makefile
		 src/plugins/switch/generic/Makefile
		 src/plugins/switch/none/Makefile
		 src/plugins/switch/nrt/Makefile
		 src/plugins/switch/nrt/libpermapi/Makefile
		 src/plugins/mpi/Makefile
		 src/plugins/mpi/mpich1_p4/Makefile
		 src/plugins/mpi/mpich1_shmem/Makefile
		 src/plugins/mpi/mpichgm/Makefile
		 src/plugins/mpi/mpichmx/Makefile
		 src/plugins/mpi/mvapich/Makefile
		 src/plugins/mpi/lam/Makefile
		 src/plugins/mpi/none/Makefile
		 src/plugins/mpi/openmpi/Makefile
		 src/plugins/mpi/pmi2/Makefile
		 src/plugins/task/Makefile
		 src/plugins/task/affinity/Makefile
		 src/plugins/task/cgroup/Makefile
		 src/plugins/task/cray/Makefile
		 src/plugins/task/none/Makefile
		 src/plugins/topology/Makefile
		 src/plugins/topology/3d_torus/Makefile
		 src/plugins/topology/node_rank/Makefile
		 src/plugins/topology/none/Makefile
		 src/plugins/topology/tree/Makefile
		 testsuite/Makefile
		 testsuite/expect/Makefile
		 testsuite/slurm_unit/Makefile
		 testsuite/slurm_unit/api/Makefile
		 testsuite/slurm_unit/api/manual/Makefile
		 testsuite/slurm_unit/common/Makefile
		 ]
)

AC_OUTPUT
