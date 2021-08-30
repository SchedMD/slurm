##*****************************************************************************
#  AUTHOR:
#    Daniel Pou <danielp@sgi.com>
#
#  SYNOPSIS:
#    X_AC_NETLOC
#
#  DESCRIPTION:
#    Determine if the NETLOC libraries exists
##*****************************************************************************

AC_DEFUN([X_AC_NETLOC],
[
  _x_ac_netloc_dirs="/usr /usr/local"
  _x_ac_netloc_libs="lib64 lib"
  x_ac_cv_netloc_nosub="no"

  AC_ARG_WITH(
    [netloc],
    AS_HELP_STRING(--with-netloc=PATH,Specify path to netloc installation),
    [AS_IF([test "x$with_netloc" != xno && test "x$with_netloc" != xyes],
           [_x_ac_netloc_dirs="$with_netloc"])])

  if [test "x$with_netloc" = xno]; then
    AC_MSG_WARN([support for netloc disabled])
  else
    AC_CACHE_CHECK(
      [for netloc installation],
      [x_ac_cv_netloc_dir],
      [
        for d in $_x_ac_netloc_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/netloc.h" || continue
          for bit in $_x_ac_netloc_libs; do
            test -d "$d/$bit" || continue
            _x_ac_netloc_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_netloc_libs_save="$LIBS"
            LIBS="-L$d/$bit -lnetloc $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_PROGRAM([#include <netloc.h>
                                #include <netloc/map.h>],
                                [netloc_map_t map;
                                netloc_map_create(&map);]) ],
              AS_VAR_SET(x_ac_cv_netloc_dir, $d))
              AC_LINK_IFELSE(
              [AC_LANG_PROGRAM([#include <netloc.h>
                                #include <netloc_map.h>],
                                [netloc_map_t map;
                                netloc_map_create(&map)]) ],
              AS_VAR_SET(x_ac_cv_netloc_dir, $d)
              x_ac_cv_netloc_nosub="yes"
              )
            CPPFLAGS="$_x_ac_netloc_cppflags_save"
            LIBS="$_x_ac_netloc_libs_save"
            test -n "$x_ac_cv_netloc_dir" && break
          done
          test -n "$x_ac_cv_netloc_dir" && break
        done
      ])

    if test -z "$x_ac_cv_netloc_dir"; then
      if test -z "$with_netloc"; then
        AC_MSG_WARN([unable to locate netloc installation])
      else
        AC_MSG_ERROR([unable to locate netloc installation])
      fi
    else
      NETLOC_CPPFLAGS="-I$x_ac_cv_netloc_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        NETLOC_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_netloc_dir/$bit -L$x_ac_cv_netloc_dir/$bit"
      else
        NETLOC_LDFLAGS="-L$x_ac_cv_netloc_dir/$bit"
      fi
      NETLOC_LIBS="-lnetloc"
      AC_DEFINE(HAVE_NETLOC, 1, [Define to 1 if netloc library found])
      if test "$x_ac_cv_netloc_nosub" = "yes"; then
        AC_DEFINE(HAVE_NETLOC_NOSUB, 1, [Define to 1 if netloc includes use underscore not subdirectory])
      fi
    fi

    AC_SUBST(NETLOC_LIBS)
    AC_SUBST(NETLOC_CPPFLAGS)
    AC_SUBST(NETLOC_LDFLAGS)
  fi

  AM_CONDITIONAL(HAVE_NETLOC, test -n "$x_ac_cv_netloc_dir")
])
