# This file is to be processed with autoconf to generate a configure script

dnl Prologue
dnl

AC_INIT(slurm, m4_esyscmd([perl -ne 'print,exit if s/^\s*VERSION:\s*(\d*.\d*).\S*/\1/i' ./META | sed 's/^v//' | tr '-' '_' | tr -d '\n']), [], [], [https://slurm.schedmd.com])
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([auxdir])
AC_CONFIG_MACRO_DIR([auxdir])
AC_CANONICAL_TARGET([])

dnl the is a generic flag to avoid building things
AM_CONDITIONAL(DONT_BUILD, test "1" = "0")

# Determine project/version from META file.
# Sets PACKAGE, VERSION, SLURM_VERSION
X_AC_SLURM_VERSION

dnl Initialize Automake
dnl
dnl If you ever change to use AM_INIT_AUTOMAKE(subdir-objects) edit
dnl auxdir/slurm.m4 to not define VERSION
dnl
AM_INIT_AUTOMAKE(no-define no-dist)
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([slurm/slurm_version.h])

X_AC_RPATH

dnl This sets the first compiler to be C instead of C++.
X_AC_DATABASES

dnl Check to see if this architecture should use slurm_* prefix function
dnl aliases for plugins.
dnl
case "$host" in
	*darwin*) AC_DEFINE(USE_ALIAS, 0,
			[Define slurm_ prefix function aliases for plugins]) ;;
	*)        AC_DEFINE(USE_ALIAS, 1,
			[Define slurm_ prefix function aliases for plugins]) ;;
esac

dnl Checks for programs.
dnl
AC_PROG_CC
AC_PROG_CXX
AC_PROG_MAKE_SET
LT_INIT
X_AC_PKGCONFIG

dnl Find objcopy and setup path
AC_CHECK_TARGET_TOOL([OBJCOPY], objcopy, [], [])

dnl Silence warning: ar: 'u' modifier ignored since 'D' is the default
dnl
AC_SUBST(AR_FLAGS, [cr])

AM_CONDITIONAL(WITH_CXX, test -n "$ac_ct_CXX")
AM_CONDITIONAL(WITH_GNU_LD, test "$with_gnu_ld" = "yes")

AC_PATH_PROG([SLEEP_CMD], [sleep], [/bin/sleep])
AC_DEFINE_UNQUOTED([SLEEP_CMD], ["$SLEEP_CMD"], [Define path to sleep command])

AC_PATH_PROG([SUCMD], [su], [/bin/su])
AC_DEFINE_UNQUOTED([SUCMD], ["$SUCMD"], [Define path to su command])

dnl Checks for libraries
dnl Includes workaround to disable -Werror=builtin-declaration-mismatch which
dnl will cause errors on newer GCC, and can then lead to incorrect results.
old_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-builtin-declaration-mismatch"
AC_SEARCH_LIBS([socket],        [socket])
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([hstrerror],     [resolv])
AC_SEARCH_LIBS([kstat_open],    [kstat])
AC_SEARCH_LIBS([ns_initparse],  [resolv])
AC_SEARCH_LIBS([log],		[m])
AC_SEARCH_LIBS([lrint],		[m])
AC_SEARCH_LIBS([timer_create],	[rt])
AC_SEARCH_LIBS([dlopen],	[dl svdl])
CFLAGS=$old_CFLAGS

dnl Checks for header files.
dnl
AC_CHECK_HEADERS(mcheck.h values.h socket.h sys/socket.h  \
		 stdbool.h sys/ipc.h sys/shm.h sys/sem.h errno.h \
		 stdlib.h dirent.h pthread.h sys/prctl.h \
		 sysint.h inttypes.h termcap.h netdb.h sys/socket.h  \
		 sys/systemcfg.h sys/dr.h sys/vfs.h \
		 pam/pam_appl.h security/pam_appl.h sys/sysctl.h \
		 pty.h utmp.h \
		 sys/syslog.h linux/sched.h \
		 kstat.h paths.h limits.h sys/statfs.h sys/ptrace.h \
		 float.h sys/statvfs.h
		)
AC_HEADER_SYS_WAIT

# Workaround for transition of autoconf/glibc issues in deprecation of the
# definitions major, minor, makedev by sys/types.h vs sys/sysmacros.h
old_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror"
AC_HEADER_MAJOR
CFLAGS=$old_CFLAGS

dnl check to see if glibc's program_invocation_name is available:
dnl
X_AC_SLURM_PROGRAM_INVOCATION_NAME

dnl Check if ptrace takes four or five arguments
dnl
X_AC_PTRACE

dnl Check of sched_getaffinity exists and it's argument count
dnl
X_AC_AFFINITY

dnl
dnl Check for PAM module support
X_AC_PAM

dnl
dnl Check to see how we link to libslurm
X_AC_LIBSLURM

dnl
dnl Check if we want to load .login with sbatch --get-user-env option
X_AC_ENV_LOGIC

dnl Checks for types.
dnl
X_AC_SLURM_BIGENDIAN

dnl Check for C99 compatibility
dnl
X_AC_C99

dnl Force -fno-omit-frame-pointer compiler flag if available
dnl
AX_CHECK_COMPILE_FLAG([-fno-omit-frame-pointer],
		      [CFLAGS="$CFLAGS -fno-omit-frame-pointer"])

dnl check for presumed size of uid_t and gid_t
X_AC_UID_GID_SIZE_CHECK

dnl Check for JSON parser
X_AC_JSON

dnl Check for JWT library
X_AC_JWT

dnl Check for HTTP parser
X_AC_HTTP_PARSER

dnl Check for yaml parser
X_AC_YAML

dnl Check for epoll support
AX_HAVE_EPOLL()
if test "x$ax_cv_have_epoll" = "xyes"; then
	AC_DEFINE(HAVE_EPOLL, 1, [Define to 1 if we have epoll(7) support])
fi
AM_CONDITIONAL(HAVE_EPOLL, [test "x$ax_cv_have_epoll" = "xyes" ])

dnl Checks for compiler characteristics.
dnl
AC_PROG_GCC_TRADITIONAL([])

AX_GCC_BUILTIN(__builtin_bswap64)
AX_GCC_BUILTIN(__builtin_clzll)
AX_GCC_BUILTIN(__builtin_ctzll)
AX_GCC_BUILTIN(__builtin_popcountll)


dnl checks for library functions.
dnl
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
   memfd_create \
   getrandom \
)

AC_CHECK_DECLS([hstrerror, strsignal, sys_siglist])

dnl Skip pthread checks on macOS as support is always enabled,
dnl and AX_PTHREAD will inject flags that will throw spurious warnings.
case "$host" in
*-darwin*)
	;;
*)
	AX_PTHREAD([], AC_MSG_ERROR([Error: Cannot figure out how to use pthreads!]))
	;;
esac

LDFLAGS="$LDFLAGS "
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"

X_AC_DIMENSIONS

X_AC_OFED

AX_LIB_HDF5()
AM_CONDITIONAL(BUILD_HDF5, test "$with_hdf5" = "yes")
# Some systems don't configure HDF5 with --with-default-api-version=v18
# which creates problems for slurm because slurm uses the 1.8 API.
# For hdf5 1.8, define H5_NO_DEPRECATED_SYMBOLS, otherwise use H5_USE_18_API
# to request the right api version.
AX_COMPARE_VERSION($[HDF5_VERSION], [eq0], [1.8],
	[AC_DEFINE([H5_NO_DEPRECATED_SYMBOLS], [1], [Use the 1.8 HDF5 API])],
	[AC_DEFINE([H5_USE_18_API], [1], [Make sure we get the 1.8 HDF5 API])])

X_AC_LZ4
X_AC_HWLOC
X_AC_NVML
X_AC_RSMI
X_AC_ONEAPI
X_AC_PMIX
X_AC_FREEIPMI
X_AC_UCX
X_AC_X11
X_AC_SELINUX
X_AC_RDKAFKA
X_AC_S2N

X_AC_CGROUP
X_AC_BPF
X_AC_DBUS

#
#  Tests for Check
#

PKG_CHECK_MODULES([CHECK], [check >= 0.9.8], [ac_have_check="yes"], [ac_have_check="no"])
AM_CONDITIONAL(HAVE_CHECK, test "x$ac_have_check" = "xyes")

X_AC_SVIEW
X_AC_HPE_SLINGSHOT

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

X_AC_SLURMRESTD([6820])

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
dnl Check for systemd presence, version and system unit dir
dnl
X_AC_SYSTEMD

dnl
dnl Check for compilation of Slurm auth modules:
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

savedLIBS="$LIBS"
LIBS="-lutil $LIBS"
AC_CHECK_LIB(util, openpty, [UTIL_LIBS="-lutil"], [])
AC_SUBST(UTIL_LIBS)
LIBS="$savedLIBS"

dnl
dnl Check for compilation of Slurm with CURL support:
dnl
LIBCURL_CHECK_CONFIG

dnl Check word size so we can deprecate 32-bit systems
AC_CHECK_SIZEOF([void *], 8)

dnl This needs to come last so it can detect deprecated options in use.
X_AC_DEPRECATED

dnl All slurm Makefiles:

AC_CONFIG_FILES([Makefile
		 auxdir/Makefile
		 contribs/Makefile
		 contribs/lua/Makefile
		 contribs/nss_slurm/Makefile
		 contribs/openlava/Makefile
		 contribs/pam/Makefile
		 contribs/pam_slurm_adopt/Makefile
		 contribs/perlapi/Makefile
		 contribs/perlapi/libslurm/Makefile
		 contribs/perlapi/libslurm/perl/Makefile.PL
		 contribs/perlapi/libslurmdb/Makefile
		 contribs/perlapi/libslurmdb/perl/Makefile.PL
		 contribs/pmi/Makefile
		 contribs/pmi2/Makefile
		 contribs/seff/Makefile
		 contribs/sgather/Makefile
		 contribs/sjobexit/Makefile
		 contribs/slurm_completion_help/Makefile
		 contribs/torque/Makefile
		 doc/Makefile
		 doc/html/Makefile
		 doc/html/configurator.easy.html
		 doc/html/configurator.html
		 doc/man/Makefile
		 doc/man/man1/Makefile
		 doc/man/man5/Makefile
		 doc/man/man8/Makefile
		 etc/Makefile
		 src/Makefile
		 src/api/Makefile
		 src/bcast/Makefile
		 src/common/Makefile
		 src/conmgr/Makefile
		 src/curl/Makefile
		 src/database/Makefile
		 src/interfaces/Makefile
		 src/lua/Makefile
		 src/plugins/Makefile
		 src/plugins/accounting_storage/Makefile
		 src/plugins/accounting_storage/common/Makefile
		 src/plugins/accounting_storage/ctld_relay/Makefile
		 src/plugins/accounting_storage/mysql/Makefile
		 src/plugins/accounting_storage/slurmdbd/Makefile
		 src/plugins/acct_gather_energy/Makefile
		 src/plugins/acct_gather_energy/gpu/Makefile
		 src/plugins/acct_gather_energy/ibmaem/Makefile
		 src/plugins/acct_gather_energy/ipmi/Makefile
		 src/plugins/acct_gather_energy/pm_counters/Makefile
		 src/plugins/acct_gather_energy/rapl/Makefile
		 src/plugins/acct_gather_energy/xcc/Makefile
		 src/plugins/acct_gather_filesystem/Makefile
		 src/plugins/acct_gather_filesystem/lustre/Makefile
		 src/plugins/acct_gather_interconnect/Makefile
		 src/plugins/acct_gather_interconnect/ofed/Makefile
		 src/plugins/acct_gather_interconnect/sysfs/Makefile
		 src/plugins/acct_gather_profile/Makefile
		 src/plugins/acct_gather_profile/hdf5/Makefile
		 src/plugins/acct_gather_profile/hdf5/sh5util/Makefile
		 src/plugins/acct_gather_profile/influxdb/Makefile
		 src/plugins/auth/Makefile
		 src/plugins/auth/jwt/Makefile
		 src/plugins/auth/munge/Makefile
		 src/plugins/auth/none/Makefile
		 src/plugins/auth/slurm/Makefile
		 src/plugins/burst_buffer/Makefile
		 src/plugins/burst_buffer/common/Makefile
		 src/plugins/burst_buffer/datawarp/Makefile
		 src/plugins/burst_buffer/lua/Makefile
		 src/plugins/certmgr/Makefile
		 src/plugins/certmgr/script/Makefile
		 src/plugins/cgroup/Makefile
		 src/plugins/cgroup/common/Makefile
		 src/plugins/cgroup/v1/Makefile
		 src/plugins/cgroup/v2/Makefile
		 src/plugins/cli_filter/Makefile
		 src/plugins/cli_filter/common/Makefile
		 src/plugins/cli_filter/lua/Makefile
		 src/plugins/cli_filter/syslog/Makefile
		 src/plugins/cli_filter/user_defaults/Makefile
		 src/plugins/cred/Makefile
		 src/plugins/cred/common/Makefile
		 src/plugins/cred/munge/Makefile
		 src/plugins/cred/none/Makefile
		 src/plugins/data_parser/Makefile
		 src/plugins/data_parser/v0.0.40/Makefile
		 src/plugins/data_parser/v0.0.41/Makefile
		 src/plugins/data_parser/v0.0.42/Makefile
		 src/plugins/gpu/Makefile
		 src/plugins/gpu/common/Makefile
		 src/plugins/gpu/generic/Makefile
		 src/plugins/gpu/nrt/Makefile
		 src/plugins/gpu/nvidia/Makefile
		 src/plugins/gpu/nvml/Makefile
		 src/plugins/gpu/oneapi/Makefile
		 src/plugins/gpu/rsmi/Makefile
		 src/plugins/gres/Makefile
		 src/plugins/gres/common/Makefile
		 src/plugins/gres/gpu/Makefile
		 src/plugins/gres/mps/Makefile
		 src/plugins/gres/nic/Makefile
		 src/plugins/gres/shard/Makefile
		 src/plugins/hash/Makefile
		 src/plugins/hash/common_xkcp/Makefile
		 src/plugins/hash/k12/Makefile
		 src/plugins/hash/sha3/Makefile
		 src/plugins/job_container/Makefile
		 src/plugins/job_container/tmpfs/Makefile
		 src/plugins/job_submit/Makefile
		 src/plugins/job_submit/all_partitions/Makefile
		 src/plugins/job_submit/defaults/Makefile
		 src/plugins/job_submit/logging/Makefile
		 src/plugins/job_submit/lua/Makefile
		 src/plugins/job_submit/partition/Makefile
		 src/plugins/job_submit/pbs/Makefile
		 src/plugins/job_submit/require_timelimit/Makefile
		 src/plugins/job_submit/throttle/Makefile
		 src/plugins/jobacct_gather/Makefile
		 src/plugins/jobacct_gather/cgroup/Makefile
		 src/plugins/jobacct_gather/common/Makefile
		 src/plugins/jobacct_gather/linux/Makefile
		 src/plugins/jobcomp/Makefile
		 src/plugins/jobcomp/common/Makefile
		 src/plugins/jobcomp/elasticsearch/Makefile
		 src/plugins/jobcomp/filetxt/Makefile
		 src/plugins/jobcomp/kafka/Makefile
		 src/plugins/jobcomp/lua/Makefile
		 src/plugins/jobcomp/mysql/Makefile
		 src/plugins/jobcomp/script/Makefile
		 src/plugins/mcs/Makefile
		 src/plugins/mcs/account/Makefile
		 src/plugins/mcs/group/Makefile
		 src/plugins/mcs/label/Makefile
		 src/plugins/mcs/user/Makefile
		 src/plugins/mpi/Makefile
		 src/plugins/mpi/cray_shasta/Makefile
		 src/plugins/mpi/pmi2/Makefile
		 src/plugins/mpi/pmix/Makefile
		 src/plugins/node_features/Makefile
		 src/plugins/node_features/helpers/Makefile
		 src/plugins/node_features/knl_generic/Makefile
		 src/plugins/preempt/Makefile
		 src/plugins/preempt/partition_prio/Makefile
		 src/plugins/preempt/qos/Makefile
		 src/plugins/prep/Makefile
		 src/plugins/prep/script/Makefile
		 src/plugins/priority/Makefile
		 src/plugins/priority/basic/Makefile
		 src/plugins/priority/multifactor/Makefile
		 src/plugins/proctrack/Makefile
		 src/plugins/proctrack/cgroup/Makefile
		 src/plugins/proctrack/linuxproc/Makefile
		 src/plugins/proctrack/pgid/Makefile
		 src/plugins/sched/Makefile
		 src/plugins/sched/backfill/Makefile
		 src/plugins/sched/builtin/Makefile
		 src/plugins/select/Makefile
		 src/plugins/select/cons_tres/Makefile
		 src/plugins/select/linear/Makefile
		 src/plugins/serializer/Makefile
		 src/plugins/serializer/json/Makefile
		 src/plugins/serializer/url-encoded/Makefile
		 src/plugins/serializer/yaml/Makefile
		 src/plugins/site_factor/Makefile
		 src/plugins/site_factor/example/Makefile
		 src/plugins/switch/Makefile
		 src/plugins/switch/hpe_slingshot/Makefile
		 src/plugins/switch/nvidia_imex/Makefile
		 src/plugins/task/Makefile
		 src/plugins/task/affinity/Makefile
		 src/plugins/task/cgroup/Makefile
		 src/plugins/tls/Makefile
		 src/plugins/tls/none/Makefile
		 src/plugins/tls/s2n/Makefile
		 src/plugins/topology/Makefile
		 src/plugins/topology/3d_torus/Makefile
		 src/plugins/topology/block/Makefile
		 src/plugins/topology/common/Makefile
		 src/plugins/topology/default/Makefile
		 src/plugins/topology/tree/Makefile
		 src/sacct/Makefile
		 src/sackd/Makefile
		 src/sacctmgr/Makefile
		 src/salloc/Makefile
		 src/sattach/Makefile
		 src/scrun/Makefile
		 src/sbatch/Makefile
		 src/sbcast/Makefile
		 src/scancel/Makefile
		 src/scontrol/Makefile
		 src/scrontab/Makefile
		 src/sdiag/Makefile
		 src/sinfo/Makefile
		 src/slurmctld/Makefile
		 src/slurmd/Makefile
		 src/slurmd/common/Makefile
		 src/slurmd/slurmd/Makefile
		 src/slurmd/slurmstepd/Makefile
		 src/slurmdbd/Makefile
		 src/slurmrestd/Makefile
		 src/slurmrestd/plugins/Makefile
		 src/slurmrestd/plugins/auth/Makefile
		 src/slurmrestd/plugins/auth/jwt/Makefile
		 src/slurmrestd/plugins/auth/local/Makefile
		 src/slurmrestd/plugins/openapi/Makefile
		 src/slurmrestd/plugins/openapi/slurmctld/Makefile
		 src/slurmrestd/plugins/openapi/slurmdbd/Makefile
		 src/sprio/Makefile
		 src/squeue/Makefile
		 src/sreport/Makefile
		 src/srun/Makefile
		 src/sshare/Makefile
		 src/sstat/Makefile
		 src/stepmgr/Makefile
		 src/strigger/Makefile
		 src/sview/Makefile
		 testsuite/Makefile
		 testsuite/testsuite.conf.sample
		 testsuite/expect/Makefile
		 testsuite/slurm_unit/Makefile
		 testsuite/slurm_unit/common/Makefile
		 testsuite/slurm_unit/common/bitstring/Makefile
		 testsuite/slurm_unit/common/hostlist/Makefile
		 testsuite/slurm_unit/common/slurm_protocol_defs/Makefile
		 testsuite/slurm_unit/common/slurm_protocol_pack/Makefile
		 testsuite/slurm_unit/common/slurmdb_defs/Makefile
		 testsuite/slurm_unit/common/slurmdb_pack/Makefile
		 ]
)

AC_OUTPUT
