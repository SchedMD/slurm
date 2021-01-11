##*****************************************************************************
#  AUTHOR:
#    Derived from x_ac_json.
#
#  SYNOPSIS:
#    X_AC_YAML()
#
#  DESCRIPTION:
#    Check for libyaml parser library.
#    Right now, just check for libyaml header and library.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and before AC_PROG_LIBTOOL.
##*****************************************************************************

AC_DEFUN([X_AC_YAML], [
	_x_ac_yaml_dirs="/usr /usr/local"
	_x_ac_yaml_libs="lib64 lib"

	AC_ARG_WITH(
		[yaml],
		AS_HELP_STRING(--with-yaml=PATH,Specify path to libyaml installation),
		[AS_IF([test "x$with_yaml" != xno && test "x$with_yaml" != xyes],
           [_x_ac_yaml_dirs="$with_yaml"])])

	if [test "x$with_yaml" = xno]; then
		AC_MSG_WARN([support for libyaml disabled])
	else
		AC_CACHE_CHECK(
		  [for libyaml installation],
		  [x_ac_cv_yaml_dir],
		  [
			for d in $_x_ac_yaml_dirs; do
			  test -d "$d" || continue
			  test -d "$d/include" || continue
			  test -f "$d/include/yaml.h" || continue
			  for bit in $_x_ac_yaml_libs; do
				_x_ac_yaml_libs_save="$LIBS"
				LIBS="-L$d/$bit -lyaml $LIBS"
				AC_LINK_IFELSE(
					[AC_LANG_CALL([], yaml_parser_load)],
					AS_VAR_SET(x_ac_cv_yaml_dir, $d))
				LIBS="$_x_ac_yaml_libs_save"
				test -n "$x_ac_cv_yaml_dir" && break
			  done
			  test -n "$x_ac_cv_yaml_dir" && break
			done
		  ])

		if test -z "$x_ac_cv_yaml_dir"; then
			if test -z "$with_yaml"; then
				AC_MSG_WARN([unable to locate libyaml parser library])
			else
				AC_MSG_ERROR([unable to locate libyaml parser library])
			fi
		else
			AC_DEFINE([HAVE_YAML], [1], [Define if you are compiling with libyaml parser.])
			YAML_CPPFLAGS="-I$x_ac_cv_yaml_dir/include"
			YAML_LDFLAGS="-L$x_ac_cv_yaml_dir/$bit -lyaml"
		fi

		AC_SUBST(YAML_CPPFLAGS)
		AC_SUBST(YAML_LDFLAGS)
	fi

	AM_CONDITIONAL(WITH_YAML, test -n "$x_ac_cv_yaml_dir")
])
