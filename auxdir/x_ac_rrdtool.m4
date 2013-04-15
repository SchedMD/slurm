##*****************************************************************************
#  AUTHOR:
#    Written by Bull- Thomas Cadeau
#
#  SYNOPSIS:
#    X_AC_RRDTOOL
#
#  DESCRIPTION:
#    Determine if the RRDTOOL libraries exists
##*****************************************************************************

AC_DEFUN([X_AC_RRDTOOL],
[
  _x_ac_rrdtool_dirs="/usr /usr/local"
  _x_ac_rrdtool_libs="lib64 lib"

  AC_ARG_WITH(
    [rrdtool],
    AS_HELP_STRING(--with-rrdtool=PATH,Specify path to rrdtool-devel installation),
    [_x_ac_rrdtool_dirs="$withval $_x_ac_rrdtool_dirs"])

  AC_CACHE_CHECK(
    [for rrdtool installation],
    [x_ac_cv_rrdtool_dir],
    [
      for d in $_x_ac_rrdtool_dirs; do
        test -d "$d" || continue
        test -d "$d/include" || continue
        test -f "$d/include/rrd.h" || continue
	for bit in $_x_ac_rrdtool_libs; do
          test -d "$d/$bit" || continue
          _x_ac_rrdtool_cppflags_save="$CPPFLAGS"
          CPPFLAGS="-I$d/include $CPPFLAGS"
 	  _x_ac_rrdtool_libs_save="$LIBS"
          LIBS="-L$d/$bit -lrrd $LIBS"
          AC_TRY_LINK([#include <rrd.h>],
[rrd_value_t *rrd_data;]
[ rrd_test_error();],
AS_VAR_SET(x_ac_cv_rrdtool_dir, $d), [])
          CPPFLAGS="$_x_ac_rrdtool_cppflags_save"
          LIBS="$_x_ac_rrdtool_libs_save"
          test -n "$x_ac_cv_rrdtool_dir" && break
	done
        test -n "$x_ac_cv_rrdtool_dir" && break
      done
    ])

  if test -z "$x_ac_cv_rrdtool_dir"; then
    AC_MSG_WARN([unable to locate rrdtool installation])
  else
    RRDTOOL_CPPFLAGS="-I$x_ac_cv_rrdtool_dir/include"
    RRDTOOL_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_rrdtool_dir/$bit -L$x_ac_cv_rrdtool_dir/$bit"
    RRDTOOL_LIBS="-lrrd"
    AC_DEFINE(HAVE_RRDTOOL, 1, [Define to 1 if rrdtool library found])
  fi

  AC_SUBST(RRDTOOL_LIBS)
  AC_SUBST(RRDTOOL_CPPFLAGS)
  AC_SUBST(RRDTOOL_LDFLAGS)
  AM_CONDITIONAL(BUILD_RRD, test -n "$x_ac_cv_rrdtool_dir")

])
