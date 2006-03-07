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

AC_DEFUN([X_AC_NUMA], [

# Test if numa_available function exists
  save_LIBS="$LIBS"
  LIBS="-lnuma $LIBS"
  AC_CHECK_FUNCS(numa_available, [have_numa_available=yes], [LIBS="$save_LIBS"])
  AM_CONDITIONAL(HAVE_NUMA_AFFINITY, test "x$have_numa_available" = "xyes")
])

