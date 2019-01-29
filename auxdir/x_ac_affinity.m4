##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_AFFINITY
#
#  DESCRIPTION:
#    Test for various task affinity functions and set the definitions.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_AFFINITY], [

# Test if sched_setaffinity function exists
  AC_CHECK_FUNCS(sched_setaffinity, [have_sched_setaffinity=yes])

#
# Test for NUMA memory afffinity functions and set the definitions
#
  AC_CHECK_LIB([numa],
        [numa_available],
        [ac_have_numa=yes; NUMA_LIBS="-lnuma"])

  AC_SUBST(NUMA_LIBS)
  AM_CONDITIONAL(HAVE_NUMA, test "x$ac_have_numa" = "xyes")
  if test "x$ac_have_numa" = "xyes"; then
    AC_DEFINE(HAVE_NUMA, 1, [define if numa library installed])
    CFLAGS="-DNUMA_VERSION1_COMPATIBILITY $CFLAGS"
  else
    AC_MSG_WARN([unable to locate NUMA memory affinity functions])
  fi

#
# Test for cpuset directory
#
  cpuset_default_dir="/dev/cpuset"
  AC_ARG_WITH([cpusetdir],
              AS_HELP_STRING(--with-cpusetdir=PATH,specify path to cpuset directory default is /dev/cpuset),
              [try_path=$withval])
  for cpuset_dir in $try_path "" $cpuset_default_dir; do
    if test -d "$cpuset_dir" ; then
      AC_DEFINE_UNQUOTED(CPUSET_DIR, "$cpuset_dir", [Define location of cpuset directory])
      have_sched_setaffinity=yes
      break
    fi
  done

#
# Set HAVE_SCHED_SETAFFINITY if any task affinity supported
AM_CONDITIONAL(HAVE_SCHED_SETAFFINITY, test "x$have_sched_setaffinity" = "xyes")
])

