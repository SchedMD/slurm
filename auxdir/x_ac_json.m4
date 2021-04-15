##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_munge.
#
#  SYNOPSIS:
#    X_AC_JSON()
#
#  DESCRIPTION:
#    Check for JSON parser libraries.
#    Right now, just check for json-c header and library.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_JSON], [

  _x_ac_json_dirs="/usr /usr/local"
  _x_ac_json_libs="lib64 lib"

  AC_ARG_WITH(
    [json],
    AS_HELP_STRING(--with-json=PATH,Specify path to json-c installation),
    [AS_IF([test "x$with_json" != xno && test "x$with_json" != xyes],
	   [_x_ac_json_dirs="$with_json"])])

  if [test "x$with_json" = xno]; then
    AC_MSG_WARN([support for json disabled])
  else
    AC_CACHE_CHECK(
      [for json installation],
      [x_ac_cv_json_dir],
      [
        for d in $_x_ac_json_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/json-c/json_object.h" || test -f "$d/include/json/json_object.h" || continue
          for bit in $_x_ac_json_libs; do
            test -d "$d/$bit" || continue
            _x_ac_json_libs_save="$LIBS"
            LIBS="-L$d/$bit -ljson-c $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], json_tokener_parse)],
              AS_VAR_SET(x_ac_cv_json_dir, $d))
            LIBS="$_x_ac_json_libs_save"
            test -n "$x_ac_cv_json_dir" && break
          done
          test -n "$x_ac_cv_json_dir" && break
        done
      ])

    if test -z "$x_ac_cv_json_dir"; then
      if [test -z "$with_json"] ; then
        AC_MSG_WARN([unable to locate json parser library])
      else
        AC_MSG_ERROR([unable to locate json parser library])
      fi
    else
      if test -f "$d/include/json-c/json_object.h" ; then
        AC_DEFINE([HAVE_JSON_C_INC], [1], [Define if headers in include/json-c.])
      fi
      if test -f "$d/include/json/json_object.h" ; then
        AC_DEFINE([HAVE_JSON_INC], [1], [Define if headers in include/json.])
      fi
      AC_DEFINE([HAVE_JSON], [1], [Define if you are compiling with json.])
      JSON_CPPFLAGS="-I$x_ac_cv_json_dir/include"
      JSON_LDFLAGS="-L$x_ac_cv_json_dir/$bit -ljson-c"
    fi

    AC_SUBST(JSON_CPPFLAGS)
    AC_SUBST(JSON_LDFLAGS)
  fi

  AM_CONDITIONAL(WITH_JSON_PARSER, test -n "$x_ac_cv_json_dir")
])
