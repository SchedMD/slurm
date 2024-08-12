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
  AC_ARG_ENABLE(
    [cgroupv2],
    AS_HELP_STRING([--enable-cgroupv2],
		   [enable cgroupv2 support [default=auto]]),
    [ case "$enableval" in
        yes) x_ac_cgroupv2=yes ;;
         no) x_ac_cgroupv2=no ;;
          *) AC_MSG_ERROR([bad value "$enableval" for --enable-cgroupv2])
      esac
    ])

  case ${host_os} in
  darwin* | freebsd* | netbsd* )
    with_cgroup=no
    ;;
  *)
    with_cgroup=yes
    ;;
  esac
  AM_CONDITIONAL(LINUX_BUILD, test x$with_cgroup = xyes)
  AM_CONDITIONAL(WITH_CGROUP, test x$with_cgroup = xyes)
  if test x$with_cgroup = xyes; then
    AC_DEFINE(WITH_CGROUP, 1, [Building with Linux cgroup support])
  else
    if test x$x_ac_cgroupv2 = xyes; then
      AC_MSG_ERROR([Cgroupv2 not supported in this system])
    fi
    x_ac_cgroupv2=no
  fi
])

AC_DEFUN([X_AC_BPF], [

  _x_ac_bpf_dirs="/usr"

  AC_ARG_WITH(
    [bpf],
    AS_HELP_STRING(--with-bpf=PATH,Specify path to bpf header),
    [AS_IF([test "x$with_bpf" != xno && test "x$with_bpf" != xyes],
           [_x_ac_bpf_dirs="$with_bpf"])])

  if [test "x$with_bpf" = xno] || [test "x$x_ac_cgroupv2" = xno]; then
    if [test "x$x_ac_cgroupv2" = "xyes"]; then
      AC_MSG_ERROR([support for bpf disabled])
    else
      AC_MSG_NOTICE([support for bpf disabled])
    fi
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
      if [test -z "$with_bpf"] && [test "x$x_ac_cgroupv2" != "xyes"]; then
        AC_MSG_WARN([unable to locate bpf header])
      else
        AC_MSG_ERROR([unable to locate bpf header])
      fi
    else
      #Check for bpf defines existence added in kernel 5.7
      AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([[#include <linux/bpf.h>]],
	  [[int def_test;
	    def_test = BPF_DEVCG_DEV_BLOCK;
	    def_test = BPF_DEVCG_DEV_CHAR;
	    def_test = BPF_F_ALLOW_OVERRIDE;
	    def_test = BPF_OBJ_NAME_LEN;
	    def_test = BPF_PROG_TYPE_CGROUP_DEVICE;
	    def_test = BPF_PROG_ATTACH;]])],
	  [ac_bpf_define_presence=yes],
	  [ac_bpf_define_presence=no])
      if test "x$ac_bpf_define_presence" = "xyes"; then
        AC_DEFINE([HAVE_BPF], [1], [Define if you are compiling with bpf.])
        BPF_CPPFLAGS="-I$x_ac_cv_bpf_dir/include"
      else
        if test "x$x_ac_cgroupv2" = "xyes"; then
          AC_MSG_ERROR([unable to link against a bpf.h from kernel-headers, version >= 5.7 required for cgroup/v2])
	else
          AC_MSG_WARN([unable to link against a bpf.h from kernel-headers, version >= 5.7 required for cgroup/v2])
	fi
	BPF_CPPFLAGS=""
      fi
    fi

    AC_SUBST(BPF_CPPFLAGS)
  fi

  AM_CONDITIONAL(WITH_BPF, test -n "$x_ac_cv_bpf_dir" && test "$ac_bpf_define_presence" = "yes")
])

AC_DEFUN([X_AC_DBUS],
[
  if test "x$x_ac_cgroupv2" != "xno"; then
    PKG_CHECK_MODULES([dbus], [dbus-1 >= 1.11.16],
                      [x_ac_have_dbus="yes"],
                      [x_ac_have_dbus="no"])
    AM_CONDITIONAL(WITH_DBUS, test "x$x_ac_have_dbus" = "xyes")
    if test "x$x_ac_have_dbus" = "xno"; then
       if test "x$x_ac_cgroupv2" = "xdefault"; then
         AC_MSG_WARN([unable to locate dbus-1 development headers >= 1.11.16])
       elif test "x$x_ac_cgroupv2" = "xyes"; then
         AC_MSG_ERROR([unable to locate dbus-1 development headers >= 1.11.16])
       fi
    fi
  else
    AC_MSG_NOTICE([Skipping dbus check as cgroupv2 is disabled])
    x_ac_have_dbus="no"
    AM_CONDITIONAL(WITH_DBUS, test "x$x_ac_have_dbus" = "xyes")
  fi
])
