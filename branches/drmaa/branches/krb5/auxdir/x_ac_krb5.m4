##*****************************************************************************
## $Id: x_ac_krb5.m4 7071 2006-01-19 23:58:28Z da $
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov> (originally for OpenSSL)
#    Modified for Kerberos v5 by Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_KRB5()
#
#  DESCRIPTION:
#    Check the usual suspects for a Kerberos v5 installation,
#    updating CPPFLAGS and LDFLAGS as necessary.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_KRB5], [

  _x_ac_krb5_dirs="/usr /usr/local /usr/kerberos"
  _x_ac_krb5_libs="lib64 lib"

  AC_ARG_WITH(
    [krb5],
    AC_HELP_STRING(
      [--with-krb5=PATH],
      [Specify path to krb5 installation]),
    [_x_ac_krb5_dirs="$withval $_x_ac_krb5_dirs"])

  AC_CACHE_CHECK(
    [for krb5 installation],
    [x_ac_cv_krb5_dir],
    [
      for d in $_x_ac_krb5_dirs; do
        test -d "$d" || continue
        test -d "$d/include" || continue
        test -f "$d/include/krb5.h" || continue
	for bit in $_x_ac_krb5_libs; do
          test -d "$d/$bit" || continue
        
 	  _x_ac_krb5_libs_save="$LIBS"
          LIBS="-L$d/$bit -lkrb5 $LIBS"
          AC_LINK_IFELSE(
            AC_LANG_CALL([], krb5_os_localaddr),
            AS_VAR_SET(x_ac_cv_krb5_dir, $d))
          LIBS="$_x_ac_krb5_libs_save"
          test -n "$x_ac_cv_krb5_dir" && break
	done
        test -n "$x_ac_cv_krb5_dir" && break
      done
    ])

  if test -z "$x_ac_cv_krb5_dir"; then
    AC_MSG_WARN([unable to locate krb5 installation])
  else
    KRB5_LIBS="-lkrb5"
    KRB5_CPPFLAGS="-I$x_ac_cv_krb5_dir/include"
    KRB5_LDFLAGS="-L$x_ac_cv_krb5_dir/$bit"
  fi

  AC_SUBST(KRB5_LIBS)
  AC_SUBST(KRB5_CPPFLAGS)
  AC_SUBST(KRB5_LDFLAGS)

  AM_CONDITIONAL(WITH_KRB5, test -n "$x_ac_cv_krb5_dir")
])
