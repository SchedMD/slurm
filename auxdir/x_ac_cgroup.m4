##*****************************************************************************
#  AUTHOR:
#    Tim Wickberg <tim@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_CGROUP
#
#  DESCRIPTION:
#    Test if we should build cgroup plugins.
#
##*****************************************************************************

AC_DEFUN([X_AC_CGROUP],
[
  case "$host" in
  *-darwin*)
    with_cgroup=no
    ;;
  *)
    with_cgroup=yes
    ;;
  esac
  AM_CONDITIONAL(WITH_CGROUP, test x$with_cgroup = xyes)
])
