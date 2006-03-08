##*****************************************************************************
#  $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_NUMA
#
#  DESCRIPTION:
#    Test for NUMA memory afffinity functions and set the definitions.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_NUMA],
[
  AC_CHECK_LIB([numa],
	[numa_available],
	[ac_have_numa=yes; NUMA_LIBS="-lnuma"])

  AC_SUBST(NUMA_LIBS)
  AM_CONDITIONAL(HAVE_NUMA, test "x$ac_have_numa" = "xyes")
  if test "x$ac_have_numa" = "xyes"; then
    AC_DEFINE(HAVE_NUMA, 1, [define if you have the numa library])
  else
    AC_MSG_WARN([Unable to locate NUMA memory affinity functions])
  fi
])

