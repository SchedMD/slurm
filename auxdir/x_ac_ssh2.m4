##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_SSH2
#
#  DESCRIPTION:
#    Determine if the SSH2 libraries exists.
##*****************************************************************************
#
# Copyright 2017 SchedMD LLC. All rights reserved.
#


AC_DEFUN([X_AC_SSH2],
[
  _x_ac_ssh2_dirs="/usr /usr/local"
  _x_ac_ssh2_libs="lib64 lib"

  AC_ARG_WITH(
    [ssh2],
    AS_HELP_STRING(--with-ssh2=PATH,Specify path to ssh2 installation),
        [AS_IF([test "x$with_ssh2" != xno],[_x_ac_ssh2_dirs="$with_ssh2 $_x_ac_ssh2_dirs"])])

  if [test "x$with_ssh2" = xno]; then
    AC_MSG_WARN([support for ssh2 disabled])
  else
    AC_CACHE_CHECK(
      [for ssh2 installation],
      [x_ac_cv_ssh2_dir],
      [
        for d in $_x_ac_ssh2_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/libssh2.h" || continue
          for bit in $_x_ac_ssh2_libs; do
            test -d "$d/$bit" || continue
            _x_ac_ssh2_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_ssh2_libs_save="$LIBS"
            LIBS="-L$d/$bit -lssh2 $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], libssh2_init)],
              AS_VAR_SET(x_ac_cv_ssh2_dir, $d))
            CPPFLAGS="$_x_ac_ssh2_cppflags_save"
            LIBS="$_x_ac_ssh2_libs_save"
            test -n "$x_ac_cv_ssh2_dir" && break
          done
          test -n "$x_ac_cv_ssh2_dir" && break
        done
      ])

    if test -z "$x_ac_cv_ssh2_dir"; then
      AC_MSG_WARN([unable to locate ssh2 installation])
    else
      SSH2_CPPFLAGS="-I$x_ac_cv_ssh2_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        SSH2_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_ssh2_dir/$bit -L$x_ac_cv_ssh2_dir/$bit"
      else
        SSH2_LDFLAGS="-L$x_ac_cv_ssh2_dir/$bit"
      fi
      SSH2_LIBS="-lssh2"
      AC_DEFINE(HAVE_SSH2, 1, [Define to 1 if ssh2 library found])
    fi

    AC_SUBST(SSH2_CPPFLAGS)
    AC_SUBST(SSH2_LDFLAGS)
    AC_SUBST(SSH2_LIBS)
  fi
])
