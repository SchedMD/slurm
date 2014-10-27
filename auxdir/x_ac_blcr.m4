##*****************************************************************************
## $Id: x_ac_blcr.m4 0001 2009-01-10 16:06:05Z hjcao $
##*****************************************************************************
#  AUTHOR:
#    Copied from x_ac_munge.
#
#
#  SYNOPSIS:
#    X_AC_BLCR()
#
#  DESCRIPTION:
#    Check the usual suspects for an BLCR installation,
#    updating CPPFLAGS and LDFLAGS as necessary.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_BLCR], [

  _x_ac_blcr_dirs="/usr /usr/local /opt/freeware /opt/blcr"
  _x_ac_blcr_libs="lib64 lib"

  AC_ARG_WITH(
    [blcr],
    AS_HELP_STRING(--with-blcr=PATH,Specify path to BLCR installation),
    [_x_ac_blcr_dirs="$withval $_x_ac_blcr_dirs"])

  AC_CACHE_CHECK(
    [for blcr installation],
    [x_ac_cv_blcr_dir],
    [
      for d in $_x_ac_blcr_dirs; do
	test -d "$d" || continue
	test -d "$d/include" || continue
	test -f "$d/include/libcr.h" || continue
	for bit in $_x_ac_blcr_libs; do
	  test -d "$d/$bit" || continue

 	  _x_ac_blcr_libs_save="$LIBS"
	  LIBS="-L$d/$bit -lcr $LIBS"
	  AC_LINK_IFELSE(
	    [AC_LANG_CALL([], cr_get_restart_info)],
	    AS_VAR_SET(x_ac_cv_blcr_dir, $d))
	  LIBS="$_x_ac_blcr_libs_save"
	  test -n "$x_ac_cv_blcr_dir" && break
	done
	test -n "$x_ac_cv_blcr_dir" && break
      done
    ])

  if test -z "$x_ac_cv_blcr_dir"; then
    AC_MSG_WARN([unable to locate blcr installation])
  else
    BLCR_HOME="$x_ac_cv_blcr_dir"
    BLCR_LIBS="-lcr"
    BLCR_CPPFLAGS="-I$x_ac_cv_blcr_dir/include"
    BLCR_LDFLAGS="-L$x_ac_cv_blcr_dir/$bit"
  fi

  AC_DEFINE_UNQUOTED(BLCR_HOME, "$x_ac_cv_blcr_dir", [Define BLCR installation home])
  AC_SUBST(BLCR_HOME)

  AC_SUBST(BLCR_LIBS)
  AC_SUBST(BLCR_CPPFLAGS)
  AC_SUBST(BLCR_LDFLAGS)

  AM_CONDITIONAL(WITH_BLCR, test -n "$x_ac_cv_blcr_dir")
])
