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
            _x_ac_s2n_cppflags_save="$CPPFLAGS"
	    S2N_DIR="$d"
	    S2N_LIBS="-ls2n"
	    S2N_CPPFLAGS="-I$d/include"
	    if test "$ac_with_rpath" = "yes"; then
	      S2N_LDFLAGS="-Wl,-rpath -Wl,$d/$bit "
	    fi
	    S2N_LDFLAGS+="-L$d/$bit"
            LIBS="$S2N_LDFLAGS $S2N_LIBS"
            CPPFLAGS="$S2N_CPPFLAGS $CPPFLAGS"
	    AC_RUN_IFELSE([AC_LANG_PROGRAM([#include <s2n.h>],[
		 s2n_init();
		 s2n_cleanup_final();
	    ])],
	    s2n_run_ok=yes)
            LIBS="$_x_ac_s2n_libs_save"
            CPPFLAGS="$_x_ac_s2n_cppflags_save"

            if test "$s2n_run_ok" = "yes"; then
	      break
	    fi
          done
          if test "$s2n_run_ok" = "yes"; then
	    break
	  fi
        done
      ])

    if test "$s2n_run_ok" = "yes"; then
      AC_DEFINE([HAVE_S2N], [1], [Define to 1 if s2n library found.])
      AC_SUBST(S2N_LIBS)
      AC_SUBST(S2N_CPPFLAGS)
      AC_SUBST(S2N_DIR)
      AC_SUBST(S2N_LDFLAGS)
    else
      if [test -z "$with_s2n"] ; then
        AC_MSG_WARN([unable to locate 1.5.7+ s2n library])
      else
        AC_MSG_ERROR([unable to locate 1.5.7+ s2n library])
      fi
    fi
  fi
  AM_CONDITIONAL(WITH_S2N, test "$s2n_run_ok" = "yes")
])
