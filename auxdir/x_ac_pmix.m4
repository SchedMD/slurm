##*****************************************************************************
#  AUTHOR:
#    Artem Polyakov <artpol84@gmail.com>
#
#  SYNOPSIS:
#    X_AC_PMIX
#
#  DESCRIPTION:
#    Determine if the PMIx libraries exists. Derived from "x_ac_hwloc.m4".
##*****************************************************************************

AC_DEFUN([X_AC_PMIX],
[
  _x_ac_pmix_dirs="/usr /usr/local"
  _x_ac_pmix_libs="lib64 lib"

  AC_ARG_WITH(
    [pmix],
    AS_HELP_STRING(--with-pmix=PATH,Specify path to pmix installation),
    [_x_ac_pmix_dirs="$withval $_x_ac_pmix_dirs"])

  AC_CACHE_CHECK(
    [for pmix installation],
    [x_ac_cv_pmix_dir],
    [
      for d in $_x_ac_pmix_dirs; do
        test -d "$d" || continue
        test -d "$d/include" || continue
        test -f "$d/include/pmix/pmix_common.h" || continue
        test -f "$d/include/pmix_server.h" || continue
        for d1 in $_x_ac_pmix_libs; do
          test -d "$d/$d1" || continue
          _x_ac_pmix_cppflags_save="$CPPFLAGS"
          CPPFLAGS="-I$d/include $CPPFLAGS"
          _x_ac_pmix_libs_save="$LIBS"
          LIBS="-L$d/$d1 -lpmix $LIBS"
          AC_LINK_IFELSE(
            [AC_LANG_CALL([], PMIx_Get_version)],
            AS_VAR_SET(x_ac_cv_pmix_dir, $d))
          CPPFLAGS="$_x_ac_pmix_cppflags_save"
          LIBS="$_x_ac_pmix_libs_save"
          test -n "$x_ac_cv_pmix_dir" && break
        done
        test -n "$x_ac_cv_pmix_dir" && break
      done
    ])

  if test -z "$x_ac_cv_pmix_dir"; then
    AC_MSG_WARN([unable to locate pmix installation])
  else
    AC_CACHE_CHECK(
      [for pmix library directory],
      [x_ac_cv_pmix_libdir],
      [
        for d1 in $_x_ac_pmix_libs; do
          d="$x_ac_cv_pmix_dir/$d1"
          test -d "$d" || continue
          _x_ac_pmix_cppflags_save="$CPPFLAGS"
          CPPFLAGS="-I$x_ac_cv_pmix_dir/include $CPPFLAGS"
          _x_ac_pmix_libs_save="$LIBS"
          LIBS="-L$d -lpmix $LIBS"
          AC_LINK_IFELSE(
            [AC_LANG_CALL([], PMIx_Get_version)],
            AS_VAR_SET(x_ac_cv_pmix_libdir, $d))
          CPPFLAGS="$_x_ac_pmix_cppflags_save"
          LIBS="$_x_ac_pmix_libs_save"
          test -n "$x_ac_cv_pmix_libdir" && break
        done
    ])
    PMIX_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
    if test "$ac_with_rpath" = "yes"; then
      PMIX_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
    else
      PMIX_LDFLAGS="-L$x_ac_cv_pmix_libdir"
    fi
    PMIX_LIBS="-lpmix"
    AC_DEFINE(HAVE_PMIX, 1, [Define to 1 if pmix library found])
  fi

  AC_SUBST(PMIX_LIBS)
  AC_SUBST(PMIX_CPPFLAGS)
  AC_SUBST(PMIX_LDFLAGS)
  AM_CONDITIONAL(HAVE_PMIX, test -n "$x_ac_cv_pmix_dir")
])
