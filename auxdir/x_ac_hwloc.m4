##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_HWLOC
#
#  DESCRIPTION:
#    Determine if the HWLOC libraries exists and if they support PCI data. 
##*****************************************************************************

AC_DEFUN([X_AC_HWLOC],
[
  _x_ac_hwloc_dirs="/usr /usr/local"
  _x_ac_hwloc_libs="lib64 lib"

  AC_ARG_WITH(
    [hwloc],
    AS_HELP_STRING(--with-hwloc=PATH,Specify path to hwloc installation),
    [AS_IF([test "x$with_hwloc" != xno],[_x_ac_hwloc_dirs="$with_hwloc $_x_ac_hwloc_dirs"])])

  if [test "x$with_hwloc" = xno]; then
     AC_MSG_WARN([support for hwloc disabled])
  else
    AC_CACHE_CHECK(
      [for hwloc installation],
      [x_ac_cv_hwloc_dir],
      [
        for d in $_x_ac_hwloc_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/hwloc.h" || continue
          for bit in $_x_ac_hwloc_libs; do
            test -d "$d/$bit" || continue
            _x_ac_hwloc_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_hwloc_libs_save="$LIBS"
            LIBS="-L$d/$bit -lhwloc $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], hwloc_topology_init)],
              AS_VAR_SET(x_ac_cv_hwloc_dir, $d))
            CPPFLAGS="$_x_ac_hwloc_cppflags_save"
            LIBS="$_x_ac_hwloc_libs_save"
            test -n "$x_ac_cv_hwloc_dir" && break
          done
          test -n "$x_ac_cv_hwloc_dir" && break
        done
      ])

    if test -z "$x_ac_cv_hwloc_dir"; then
      AC_MSG_WARN([unable to locate hwloc installation])
    else
      HWLOC_CPPFLAGS="-I$x_ac_cv_hwloc_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        HWLOC_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_hwloc_dir/$bit -L$x_ac_cv_hwloc_dir/$bit"
      else
        HWLOC_LDFLAGS="-L$x_ac_cv_hwloc_dir/$bit"
      fi
      HWLOC_LIBS="-lhwloc"
      AC_DEFINE(HAVE_HWLOC, 1, [Define to 1 if hwloc library found])
    fi

    AC_SUBST(HWLOC_LIBS)
    AC_SUBST(HWLOC_CPPFLAGS)
    AC_SUBST(HWLOC_LDFLAGS)
  fi
])
