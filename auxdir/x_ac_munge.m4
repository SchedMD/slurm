##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov> (originally for OpenSSL)
#    Modified for munge by Christopher Morrone <morrone2@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_MUNGE()
#
#  DESCRIPTION:
#    Check the usual suspects for an munge installation,
#    updating CPPFLAGS and LDFLAGS as necessary.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_MUNGE], [

  _x_ac_munge_dirs="/usr /usr/local /opt/freeware /opt/munge"
  _x_ac_munge_libs="lib64 lib"

  AC_ARG_WITH(
    [munge],
    AS_HELP_STRING(--with-munge=PATH,Specify path to munge installation),
    [_x_ac_munge_dirs="$withval $_x_ac_munge_dirs"])

  AC_CACHE_CHECK(
    [for munge installation],
    [x_ac_cv_munge_dir],
    [
      for d in $_x_ac_munge_dirs; do
        test -d "$d" || continue
        test -d "$d/include" || continue
        test -f "$d/include/munge.h" || continue
	for bit in $_x_ac_munge_libs; do
          test -d "$d/$bit" || continue

 	  _x_ac_munge_libs_save="$LIBS"
          LIBS="-L$d/$bit -lmunge $LIBS"
          AC_LINK_IFELSE(
            [AC_LANG_CALL([], munge_encode)],
            AS_VAR_SET(x_ac_cv_munge_dir, $d))
          LIBS="$_x_ac_munge_libs_save"
          test -n "$x_ac_cv_munge_dir" && break
	done
        test -n "$x_ac_cv_munge_dir" && break
      done
    ])

  if test -z "$x_ac_cv_munge_dir"; then
    AC_MSG_WARN([unable to locate munge installation])
  else
    MUNGE_LIBS="-lmunge"
    MUNGE_CPPFLAGS="-I$x_ac_cv_munge_dir/include"
    MUNGE_DIR="$x_ac_cv_munge_dir"
    if test "$ac_with_rpath" = "yes"; then
      MUNGE_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_munge_dir/$bit -L$x_ac_cv_munge_dir/$bit"
    else
      MUNGE_LDFLAGS="-L$x_ac_cv_munge_dir/$bit"
    fi
  fi

  AC_SUBST(MUNGE_LIBS)
  AC_SUBST(MUNGE_CPPFLAGS)
  AC_SUBST(MUNGE_LDFLAGS)
  AC_SUBST(MUNGE_DIR)

  AM_CONDITIONAL(WITH_MUNGE, test -n "$x_ac_cv_munge_dir")
])
