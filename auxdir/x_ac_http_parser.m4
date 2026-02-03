##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_json.
#
#  SYNOPSIS:
#    X_AC_HTTP_PARSER()
#
#  DESCRIPTION:
#    Check for NodeJS HTTP parsing libraries.
#    Checks for libhttp_parser and llhttp.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_HTTP_PARSER], [

	_x_ac_libhttp_parser_dirs="/usr /usr/local"
	_x_ac_libhttp_parser_libs="lib64 lib"

	AC_ARG_WITH(
		[libhttp_parser],
		AS_HELP_STRING(--with-libhttp-parser=PATH,Specify path to libhttp_parser installation),
		[AS_IF([test "x$with_libhttp_parser" != xno && test "x$with_libhttp_parser" != xyes],
		       [_x_ac_libhttp_parser_dirs="$with_libhttp_parser"])])

	if [test "x$with_libhttp_parser" = xno]; then
		AC_MSG_NOTICE([support for libhttp_parser disabled])
	else
		AC_CACHE_CHECK(
		  [for libhttp_parser installation],
		  [x_ac_cv_libhttp_parser_dir],
		  [
			for d in $_x_ac_libhttp_parser_dirs; do
			  test -d "$d" || continue
			  test -d "$d/include" || continue
			  test -f "$d/include/http_parser.h" || continue
			  for bit in $_x_ac_libhttp_parser_libs; do
				_x_ac_libhttp_parser_libs_save="$LIBS"
				LIBS="-L$d/$bit -lhttp_parser $LIBS"
				AC_LINK_IFELSE(
					[AC_LANG_CALL([], http_parser_init)],
					AS_VAR_SET(x_ac_cv_libhttp_parser_dir, $d))
				LIBS="$_x_ac_libhttp_parser_libs_save"
				test -n "$x_ac_cv_libhttp_parser_dir" && break
			  done
			  test -n "$x_ac_cv_libhttp_parser_dir" && break
			done
		  ])

	if test -z "$x_ac_cv_libhttp_parser_dir"; then
		if test -z "$with_libhttp_parser"; then
			AC_MSG_WARN([unable to locate libhttp_parser library])
		else
			AC_MSG_ERROR([unable to locate libhttp_parser library])
		fi
	else
	  AC_DEFINE([HAVE_HTTP_PARSER], [1], [Define if you are compiling with libhttp_parser or llhttp.])
	  LIBHTTP_PARSER_CPPFLAGS="-I$x_ac_cv_libhttp_parser_dir/include"
	  LIBHTTP_PARSER_LDFLAGS="-L$x_ac_cv_libhttp_parser_dir/$bit -lhttp_parser"
	fi

	AC_SUBST(LIBHTTP_PARSER_CPPFLAGS)
	AC_SUBST(LIBHTTP_PARSER_LDFLAGS)
	fi

	AM_CONDITIONAL(WITH_LIBHTTP_PARSER, test -n "$x_ac_cv_libhttp_parser_dir")

	_x_ac_llhttp_parser_dirs="/usr /usr/local"
	_x_ac_llhttp_parser_libs="lib64 lib"

	AC_ARG_WITH(
		[llhttp_parser],
		AS_HELP_STRING(--with-llhttp-parser=PATH,Specify path to llhttp parser installation),
		[AS_IF([test "x$with_llhttp_parser" != xno && test "x$with_llhttp_parser" != xyes],
		       [_x_ac_llhttp_parser_dirs="$with_llhttp_parser"])])

	if [test "x$with_llhttp_parser" = xno]; then
		AC_MSG_NOTICE([support for llhttp parser disabled])
	else
		AC_CACHE_CHECK(
		  [for llhttp parser installation],
		  [x_ac_cv_llhttp_parser_dir],
		  [
			for d in $_x_ac_llhttp_parser_dirs; do
			  test -d "$d" || continue
			  test -d "$d/include" || continue
			  test -f "$d/include/llhttp.h" || continue
			  for bit in $_x_ac_llhttp_parser_libs; do
				_x_ac_llhttp_parser_libs_save="$LIBS"
				LIBS="-L$d/$bit -lllhttp $LIBS"
				AC_LINK_IFELSE(
					[AC_LANG_CALL([], llhttp_init)],
					AS_VAR_SET(x_ac_cv_llhttp_parser_dir, $d))
				LIBS="$_x_ac_llhttp_parser_libs_save"
				test -n "$x_ac_cv_llhttp_parser_dir" && break
			  done
			  test -n "$x_ac_cv_llhttp_parser_dir" && break
			done
		  ])

	if test -z "$x_ac_cv_llhttp_parser_dir"; then
		if test -z "$with_llhttp_parser"; then
			AC_MSG_WARN([unable to locate llhttp parser library])
		else
			AC_MSG_ERROR([unable to locate llhttp parser library])
		fi
	else
	  LLHTTP_PARSER_CPPFLAGS="-I$x_ac_cv_llhttp_parser_dir/include"
	  LLHTTP_PARSER_LDFLAGS="-L$x_ac_cv_llhttp_parser_dir/$bit -lllhttp"
	  AC_MSG_CHECKING([for llhttp major version >= 9])
	  save_CPPFLAGS="$CPPFLAGS"
	  CPPFLAGS="$CPPFLAGS $LLHTTP_PARSER_CPPFLAGS"
	  AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <llhttp.h>]],
		  [[
#if LLHTTP_VERSION_MAJOR < 9
#error llhttp major version is not supported
#endif
		  ]])],
		[AC_MSG_RESULT([yes])
		 AC_DEFINE([HAVE_HTTP_PARSER], [1],
			   [Define if you are compiling with HTTP parser.])],
		[AC_MSG_RESULT([no])
		 AS_IF([test -n "$with_llhttp_parser"],
		       [AC_MSG_ERROR([llhttp >= 9 is required but was not found])],
		       [AC_MSG_WARN([llhttp found but major version < 9; disabling llhttp parser support])
			x_ac_cv_llhttp_parser_dir=""
			LLHTTP_PARSER_CPPFLAGS=""
			LLHTTP_PARSER_LDFLAGS=""])])
	  CPPFLAGS="$save_CPPFLAGS"
	fi

	AC_SUBST(LLHTTP_PARSER_CPPFLAGS)
	AC_SUBST(LLHTTP_PARSER_LDFLAGS)
	fi

	AM_CONDITIONAL(WITH_LLHTTP_PARSER, test -n "$x_ac_cv_llhttp_parser_dir")
])
