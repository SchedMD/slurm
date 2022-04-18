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

AC_DEFUN([X_AC_BPF], [

  _x_ac_bpf_dirs="/usr"

  AC_ARG_WITH(
    [bpf],
    AS_HELP_STRING(--with-bpf=PATH,Specify path to bpf header),
    [AS_IF([test "x$with_bpf" != xno && test "x$with_bpf" != xyes],
           [_x_ac_bpf_dirs="$with_bpf"])])

  if [test "x$with_bpf" = xno]; then
    AC_MSG_WARN([support for bpf disabled])
  else
    AC_CACHE_CHECK(
      [for bpf installation],
      [x_ac_cv_bpf_dir],
      [
        for d in $_x_ac_bpf_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -d "$d/include/linux" || continue
          test -f "$d/include/linux/bpf.h" || continue
	  AS_VAR_SET(x_ac_cv_bpf_dir,$d)
          test -n "$x_ac_cv_bpf_dir" && break
        done
      ])

    if test -z "$x_ac_cv_bpf_dir"; then
      if test -z "$with_bpf"; then
        AC_MSG_WARN([unable to locate bpf header])
      else
        AC_MSG_ERROR([unable to locate bpf header])
      fi
    else
      AC_DEFINE([HAVE_BPF], [1], [Define if you are compiling with bpf.])
      BPF_CPPFLAGS="-I$x_ac_cv_bpf_dir/include"
    fi

    AC_SUBST(BPF_CPPFLAGS)
  fi

  AM_CONDITIONAL(WITH_BPF, test -n "$x_ac_cv_bpf_dir")
])

AC_DEFUN([X_AC_DBUS],
[
       PKG_CHECK_MODULES([dbus], [dbus-1],
                         [x_ac_have_dbus="yes"],
                         [x_ac_have_dbus="no"])
       AC_MSG_RESULT([$x_ac_have_dbus, ${x_ac_dbus_pkg_name} was found.])
       AM_CONDITIONAL(WITH_DBUS, test "x$x_ac_have_dbus" = "xyes")
       if test "x$x_ac_have_dbus" = "xno"; then
          AC_MSG_WARN([unable to link against dbus-1 libraries required for cgroup/v2])
       fi
])
