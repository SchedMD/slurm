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
  _x_ac_ucx_dirs="/usr /usr/local /opt/ucx"
  _x_ac_ucx_libs="lib64 lib"

  AC_ARG_WITH(
    [ucx],
    AS_HELP_STRING(--with-ucx=PATH,Build with Unified Communication X library support),
        [AS_IF([test "x$with_ucx" != xno && test "x$with_ucx" != xyes],[_x_ac_ucx_dirs="$with_ucx"])],
        [AS_VAR_SET(with_ucx,no)])

  if [test "x$with_ucx" = xno]; then
    AC_MSG_NOTICE([support for ucx disabled])
  else
        AC_MSG_CHECKING(for ucx installation)
        AC_MSG_RESULT($_x_ac_ucx_dirs)

        for d in $_x_ac_ucx_dirs; do
          test -f "$d/include/ucp/api/ucp.h" || continue
          test -f "$d/include/uct/api/version.h" || continue
          for bit in $_x_ac_ucx_libs; do

            x_ac_cv_ucx_dir=
            x_ac_cv_ucx_libdir=
            test -d "$d/$bit" || continue
            _x_ac_ucx_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_ucx_ldflags_save="$LDFLAGS"
            if test "$ac_with_rpath" = "yes"; then
              LDFLAGS="-Wl,-rpath -Wl,$d/$bit -L$d/$bit $LDFLAGS"
            else
              LDFLAGS="-L$d/$bit $LDFLAGS"
            fi
            _x_ac_ucx_libs_save="$LIBS"
            LIBS="-lucp $LIBS"

            AC_CHECK_LIB([ucp],[ucp_cleanup])

            if [ test "x$ac_cv_lib_ucp_ucp_cleanup" = xno ]; then
                continue
            fi

            AS_VAR_SET(x_ac_cv_ucx_dir, $d)
            AS_VAR_SET(x_ac_cv_ucx_libdir, $d/$bit)

            AC_MSG_NOTICE(ucx checking result: $x_ac_cv_ucx_libdir)

            CPPFLAGS="$_x_ac_ucx_cppflags_save"
            LDFLAGS="$_x_ac_ucx_ldflags_save"
            LIBS="$_x_ac_ucx_libs_save"
            test -n "$x_ac_cv_ucx_dir" && break
          done
          test -n "$x_ac_cv_ucx_dir" && break
        done

    if test -z "$x_ac_cv_ucx_dir"; then
      AC_MSG_ERROR([unable to locate ucx installation])
    else
      UCX_CPPFLAGS="-I$x_ac_cv_ucx_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        UCX_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_ucx_libdir"
      else
        UCX_CPPFLAGS=$UCX_CPPFLAGS" -DPMIXP_UCX_LIBPATH=\\\"$x_ac_cv_ucx_libdir\\\""
      fi
      AC_DEFINE(HAVE_UCX, 1, [Define to 1 if ucx library found])
    fi

    AC_SUBST(UCX_CPPFLAGS)
    AC_SUBST(UCX_LDFLAGS)
    AC_SUBST(UCX_LIBS)
  fi

  AM_CONDITIONAL(HAVE_UCX, test -n "$x_ac_cv_ucx_dir")
])
