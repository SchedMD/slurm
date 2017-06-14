##*****************************************************************************
#  AUTHOR:
#    Boris Karasev <boriska@mellanox.com>
#
#  SYNOPSIS:
#    X_AC_PMIX
#
#  DESCRIPTION:
#    Determine if the UCX libraries exists.
##*****************************************************************************
#
# Copyright 2017 Mellanox Technologies. All rights reserved.
#


AC_DEFUN([X_AC_UCX],
[
  _x_ac_ucx_dirs="/usr /usr/local"
  _x_ac_ucx_libs="lib64 lib"

  AC_ARG_WITH(
    [ucx],
    AS_HELP_STRING(--with-ucx=PATH,Build with Unified Communication X library support),
    [AS_IF([test "x$with_ucx" != xno],[_x_ac_ucx_dirs="$with_ucx $_x_ac_ucx_dirs"])])

  if [test "x$with_ucx" = xno]; then
    AC_MSG_WARN([support for ucx disabled])
  else
    AC_CACHE_CHECK(
      [for ucx installation],
      [x_ac_cv_ucx_dir],
      [
        for d in $_x_ac_ucx_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -d "$d/include/ucp" || continue
          test -d "$d/include/ucp/api" || continue
          test -d "$d/include/uct" || continue
          test -d "$d/include/uct/api" || continue
          test -f "$d/include/ucp/api/ucp.h" || continue
          test -f "$d/include/ucp/api/ucp_version.h" || continue
          for bit in $_x_ac_ucx_libs; do
            test -d "$d/$bit" || continue
            _x_ac_ucx_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_ucx_libs_save="$LIBS"
            LIBS="-L$d/$bit -lrrd $LIBS"

            AC_COMPILE_IFELSE(
                [AC_LANG_PROGRAM([[#include <ucp/api/ucp_version.h>]],
                    [[
                    ]])],
                AS_VAR_SET(x_ac_cv_ucx_dir, $d)
                AS_VAR_SET(x_ac_cv_ucx_libdir, $d/$bit))

            if [test -z "$x_ac_cv_ucx_dir"] ||
               [test -z "$x_ac_cv_ucx_libdir"]; then
              AC_MSG_WARN([unable to locate ucx installation])
              continue
            fi

            CPPFLAGS="$_x_ac_ucx_cppflags_save"
            LIBS="$_x_ac_ucx_libs_save"
            test -n "$x_ac_cv_ucx_dir" && break
          done
          test -n "$x_ac_cv_ucx_dir" && break
        done
      ])

    if test -z "$x_ac_cv_ucx_dir"; then
      AC_MSG_WARN([unable to locate ucx installation])
    else
      UCX_CPPFLAGS="-I$x_ac_cv_ucx_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        UCX_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_ucx_libdir"
      else
        UCX_CPPFLAGS+=" -DPMIXP_UCX_LIBPATH=\\\"$x_ac_cv_ucx_libdir\\\""
      fi
      AC_DEFINE(HAVE_UCX, 1, [Define to 1 if ucx library found])
    fi

    AC_SUBST(UCX_CPPFLAGS)
    AC_SUBST(UCX_LDFLAGS)
    AC_SUBST(UCX_LIBS)
  fi

  AM_CONDITIONAL(HAVE_UCX, test -n "$x_ac_cv_ucx_dir")
])
