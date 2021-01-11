##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_json.
#
#  SYNOPSIS:
#    X_AC_JWT()
#
#  DESCRIPTION:
#    Check for JWT libraries.
#    Right now, just check for jwt header and library.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_JWT], [

  _x_ac_jwt_dirs="/usr /usr/local"
  _x_ac_jwt_libs="lib64 lib"

  AC_ARG_WITH(
    [jwt],
    AS_HELP_STRING(--with-jwt=PATH,Specify path to jwt installation),
    [AS_IF([test "x$with_jwt" != xno && test "x$with_jwt" != xyes],
           [_x_ac_jwt_dirs="$with_jwt"])])

  if [test "x$with_jwt" = xno]; then
    AC_MSG_WARN([support for jwt disabled])
  else
    AC_CACHE_CHECK(
      [for jwt installation],
      [x_ac_cv_jwt_dir],
      [
        for d in $_x_ac_jwt_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/jwt.h" || continue
          for bit in $_x_ac_jwt_libs; do
            test -d "$d/$bit" || continue
            _x_ac_jwt_libs_save="$LIBS"
            LIBS="-L$d/$bit -ljwt $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], jwt_new)],
              AS_VAR_SET(x_ac_cv_jwt_dir, $d))
            LIBS="$_x_ac_jwt_libs_save"
            test -n "$x_ac_cv_jwt_dir" && break
          done
          test -n "$x_ac_cv_jwt_dir" && break
        done
      ])

    if test -z "$x_ac_cv_jwt_dir"; then
      if test -z "$with_jwt"; then
        AC_MSG_WARN([unable to locate jwt library])
      else
        AC_MSG_ERROR([unable to locate jwt library])
      fi
    else
      AC_DEFINE([HAVE_JWT], [1], [Define if you are compiling with jwt.])
      JWT_CPPFLAGS="-I$x_ac_cv_jwt_dir/include"
      JWT_LDFLAGS="-L$x_ac_cv_jwt_dir/$bit -ljwt"
    fi

    AC_SUBST(JWT_CPPFLAGS)
    AC_SUBST(JWT_LDFLAGS)
  fi

  AM_CONDITIONAL(WITH_JWT, test -n "$x_ac_cv_jwt_dir")
])
