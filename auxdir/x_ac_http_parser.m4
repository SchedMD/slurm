##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_json.
#
#  SYNOPSIS:
#    X_AC_HTTP_PARSER()
#
#  DESCRIPTION:
#    Check for NodeJS HTTP Parser libraries.
#    Right now, just check for httpparser header and library.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_HTTP_PARSER], [

	_x_ac_http_parser_dirs="/usr /usr/local"
	_x_ac_http_parser_libs="lib64 lib"

	AC_ARG_WITH(
		[http_parser],
		AS_HELP_STRING(--with-http-parser=PATH,Specify path to HTTP Parser installation),
		[AS_IF([test "x$with_http_parser" != xno && test "x$with_http_parser" != xyes],
		       [_x_ac_http_parser_dirs="$with_http_parser"])])

	if [test "x$with_http_parser" = xno]; then
		AC_MSG_WARN([support for HTTP parser disabled])
	else
		AC_CACHE_CHECK(
		  [for http-parser installation],
		  [x_ac_cv_http_parser_dir],
		  [
			for d in $_x_ac_http_parser_dirs; do
			  test -d "$d" || continue
			  test -d "$d/include" || continue
			  test -f "$d/include/http_parser.h" || continue
			  for bit in $_x_ac_http_parser_libs; do
				_x_ac_http_parser_libs_save="$LIBS"
				LIBS="-L$d/$bit -lhttp_parser $LIBS"
				AC_LINK_IFELSE(
					[AC_LANG_CALL([], http_parser_init)],
					AS_VAR_SET(x_ac_cv_http_parser_dir, $d))
				LIBS="$_x_ac_http_parser_libs_save"
				test -n "$x_ac_cv_http_parser_dir" && break
			  done
			  test -n "$x_ac_cv_http_parser_dir" && break
			done
		  ])

	if test -z "$x_ac_cv_http_parser_dir"; then
		if test -z "$with_http_parser"; then
			AC_MSG_WARN([unable to locate HTTP Parser library])
		else
			AC_MSG_ERROR([unable to locate HTTP Parser library])
		fi
	else
	  AC_DEFINE([HAVE_HTTP_PARSER], [1], [Define if you are compiling with HTTP parser.])
	  HTTP_PARSER_CPPFLAGS="-I$x_ac_cv_http_parser_dir/include"
	  HTTP_PARSER_LDFLAGS="-L$x_ac_cv_http_parser_dir/$bit -lhttp_parser"
	fi

	AC_SUBST(HTTP_PARSER_CPPFLAGS)
	AC_SUBST(HTTP_PARSER_LDFLAGS)
	fi

	AM_CONDITIONAL(WITH_HTTP_PARSER, test -n "$x_ac_cv_http_parser_dir")
])
