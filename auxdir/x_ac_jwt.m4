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
      x_ac_jwt_found="no";
      AS_IF([test -z "$with_jwt" || test "x$with_jwt" = "xyes"],
	[
	  AC_CHECK_HEADER([jwt.h], [ac_jwt_h=yes], [ac_jwt_h=no])
	  AC_CHECK_LIB([jwt], [jwt_add_header], [ac_jwt=yes], [ac_jwt=no])

	  AS_IF([test "$ac_jwt" = "yes" && test "$ac_jwt_h" = "yes"],
	    [
	      JWT_CPPFLAGS=""
	      JWT_LDFLAGS="-ljwt"
	      x_ac_jwt_found="yes"
	    ],
            [
              AS_UNSET([ac_cv_header_jwt_h])
              AS_UNSET([ac_cv_lib_jwt_jwt_add_header])
            ])

	])

      AS_IF([test "x$x_ac_jwt_found" != "xyes"],
	[
	  for d in $_x_ac_jwt_dirs; do
	    test -f "$d/include/jwt.h" || continue
	    for bit in $_x_ac_jwt_libs; do
	      JWT_CPPFLAGS="-I$d/include"
	      AS_IF([test "$ac_with_rpath" = "yes"],
		    [JWT_LDFLAGS="-Wl,-rpath -Wl,$d/$bit -L$d/$bit -ljwt"],
		    [JWT_LDFLAGS="-L$d/$bit -ljwt"])

	      _x_ac_jwt_ldflags_save="$LDFLAGS"
	      _x_ac_jwt_cppflags_save="$CPPFLAGS"
	      CPPFLAGS="$JWT_CPPFLAGS $CPPFLAGS"
	      LDFLAGS="$JWT_LDFLAGS $LIBS"

	      AC_CHECK_HEADER([jwt.h], [ac_jwt_h=yes], [ac_jwt_h=no])
	      AC_CHECK_LIB([jwt], [jwt_add_header], [ac_jwt=yes], [ac_jwt=no])

	      LDFLAGS="$_x_ac_jwt_ldflags_save"
	      CPPFLAGS="$_x_ac_jwt_cppflags_save"

	      AS_IF([test "$ac_jwt" = "yes" && test "$ac_jwt_h" = "yes"],
		    [x_ac_jwt_found="yes"; break 2],
		    [x_ac_jwt_found="no"]);

              AS_UNSET([ac_cv_header_jwt_h])
              AS_UNSET([ac_cv_lib_jwt_jwt_add_header])
	    done
	  done
	])

    if test "x$x_ac_jwt_found" = "xno"; then
      if test -z "$with_jwt"; then
        AC_MSG_WARN([unable to locate jwt library])
      else
        AC_MSG_ERROR([unable to locate jwt library])
      fi
    else
      AC_DEFINE([HAVE_JWT], [1], [Define if you are compiling with jwt.])
    fi

    AC_SUBST(JWT_CPPFLAGS)
    AC_SUBST(JWT_LDFLAGS)
  fi

  AM_CONDITIONAL(WITH_JWT, [test "x$x_ac_jwt_found" = "xyes"])
])
