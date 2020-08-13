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
])
