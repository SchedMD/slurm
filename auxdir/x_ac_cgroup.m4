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
  case ${host_os} in
  darwin* | freebsd* | netbsd* )
    with_cgroup=no
    ;;
  *)
    with_cgroup=yes
    ;;
  esac
  AM_CONDITIONAL(WITH_CGROUP, test x$with_cgroup = xyes)
  if test x$with_cgroup = xyes; then
    AC_DEFINE(WITH_CGROUP, 1, [Building with Linux cgroup support])
  fi
])
