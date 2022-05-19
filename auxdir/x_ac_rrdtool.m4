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
    [AS_IF([test "x$with_rrdtool" != xno && test "x$with_rrdtool" != xyes],
           [_x_ac_rrdtool_dirs="$with_rrdtool"])])

  if [test "x$with_rrdtool" = xno]; then
    AC_MSG_WARN([support for rrdtool disabled])
  else
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
            AC_LINK_IFELSE(
              [AC_LANG_PROGRAM(
                 [[
                   #include <rrd.h>
                 ]],
                 [[
                   rrd_value_t *rrd_data;
                   rrd_info_t *rrd_info;
                   rrd_test_error();
                 ]],
               )],
              [AS_VAR_SET(x_ac_cv_rrdtool_dir, $d)],
              [])
            CPPFLAGS="$_x_ac_rrdtool_cppflags_save"
            LIBS="$_x_ac_rrdtool_libs_save"
            test -n "$x_ac_cv_rrdtool_dir" && break
          done
          test -n "$x_ac_cv_rrdtool_dir" && break
        done
      ])

    # echo x_ac_cv_rrdtool_dir $x_ac_cv_rrdtool_dir
    if test -z "$x_ac_cv_rrdtool_dir"; then
      if test -z "$with_rrdtool"; then
        AC_MSG_WARN([unable to locate rrdtool installation])
      else
        AC_MSG_ERROR([unable to locate rrdtool installation])
      fi
    else
      RRDTOOL_CPPFLAGS="-I$x_ac_cv_rrdtool_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        RRDTOOL_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_rrdtool_dir/$bit -L$x_ac_cv_rrdtool_dir/$bit"
      else
        RRDTOOL_LDFLAGS="-L$x_ac_cv_rrdtool_dir/$bit"
      fi
      RRDTOOL_LIBS="-lrrd"
    fi

    AC_SUBST(RRDTOOL_LIBS)
    AC_SUBST(RRDTOOL_CPPFLAGS)
    AC_SUBST(RRDTOOL_LDFLAGS)
  fi

  AM_CONDITIONAL(BUILD_RRD, test -n "$x_ac_cv_rrdtool_dir")
])
