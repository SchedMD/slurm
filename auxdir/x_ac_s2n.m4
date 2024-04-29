##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_munge.
#
#  SYNOPSIS:
#    X_AC_S2N()
#
#  DESCRIPTION:
#    Check for S2N libraries.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_S2N], [

  _x_ac_s2n_dirs="/usr /usr/local"
  _x_ac_s2n_libs="lib64 lib"

  AC_ARG_WITH(
    [s2n],
    AS_HELP_STRING(--with-s2n=PATH,Specify path to s2n installation),
    [AS_IF([test "x$with_s2n" != xno && test "x$with_s2n" != xyes],
	   [_x_ac_s2n_dirs="$with_s2n"])])

  if [test "x$with_s2n" = xno]; then
    AC_MSG_NOTICE([support for s2n disabled])
  else
    AC_CACHE_CHECK(
      [for s2n installation],
      [x_ac_cv_s2n_dir],
      [
        for d in $_x_ac_s2n_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/s2n.h" || continue
          for bit in $_x_ac_s2n_libs; do
            test -d "$d/$bit" || continue
            _x_ac_s2n_libs_save="$LIBS"
            LIBS="-L$d/$bit -ls2n $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], s2n_init)],
              AS_VAR_SET(x_ac_cv_s2n_dir, $d))
            LIBS="$_x_ac_s2n_libs_save"
            test -n "$x_ac_cv_s2n_dir" && break
          done
          test -n "$x_ac_cv_s2n_dir" && break
        done
      ])

    if test -z "$x_ac_cv_s2n_dir"; then
      if [test -z "$with_s2n"] ; then
        AC_MSG_WARN([unable to locate s2n library])
      else
        AC_MSG_ERROR([unable to locate s2n library])
      fi
    else
      AC_DEFINE([HAVE_S2N], [1], [Define to 1 if s2n library found.])
      S2N_LIBS="-ls2n"
      S2N_CPPFLAGS="-I$x_ac_cv_s2n_dir/include"
      S2N_DIR="$x_ac_cv_s2n_dir"
      if test "$ac_with_rpath" = "yes"; then
        S2N_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_s2n_dir/$bit -L$x_ac_cv_s2n_dir/$bit"
      else
        S2N_LDFLAGS="-L$x_ac_cv_s2n_dir/$bit"
      fi
    fi

    AC_SUBST(S2N_LIBS)
    AC_SUBST(S2N_CPPFLAGS)
    AC_SUBST(S2N_DIR)
    AC_SUBST(S2N_LDFLAGS)
  fi

  AM_CONDITIONAL(WITH_S2N, test -n "$x_ac_cv_s2n_dir")
])
