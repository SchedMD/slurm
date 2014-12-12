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

  x_ac_json_dirs="/usr /usr/local"
  x_ac_json_libs="lib64 lib"

  AC_ARG_WITH(
    [json],
    AS_HELP_STRING(--with-json=PATH,Specify path to jansson json parser installation),
    [_x_ac_json_dirs="$withval $_x_ac_json_dirs"])

  AC_CACHE_CHECK(
    [for json installation],
    [x_ac_cv_json_dir],
    [
     for d in $x_ac_json_dirs; do
       test -d "$d" || continue
       test -d "$d/include" || continue
       test -f "$d/include/jansson.h" || continue
       for bit in $x_ac_json_libs; do
         test -d "$d/$bit" || continue

       _x_ac_json_libs_save="$LIBS"
       LIBS="-L$d/$bit -ljansson $LIBS"
       AC_LINK_IFELSE(
         [AC_LANG_CALL([], json_loads)],
         AS_VAR_SET(x_ac_cv_json_dir, $d))
       LIBS="$_x_ac_json_libs_save"
       test -n "$x_ac_cv_json_dir" && break
     done
     test -n "$x_ac_cv_json_dir" && break
  done
  ])

  if test -z "$x_ac_cv_json_dir"; then
    AC_MSG_WARN([unable to locate json parser library])
  else
    JSON_CPPFLAGS="-I$x_ac_cv_json_dir/include"
    JSON_LDFLAGS="-L$x_ac_cv_json_dir -ljansson"
  fi

  AC_SUBST(JSON_CPPFLAGS)
  AC_SUBST(JSON_LDFLAGS)
  AM_CONDITIONAL(WITH_JSON_PARSER, test -n "$x_ac_cv_json_dir")
])
