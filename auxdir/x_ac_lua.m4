##*****************************************************************************
#  AUTHOR:
#    Mark Grondona <mgrondona@llnl.gov>
#
#  SYNOPSIS:
#    AC_LUA
#
#  DESCRIPTION:
#    Check for presence of lua libs and headers
##*****************************************************************************


AC_DEFUN([X_AC_LUA],
[
	AC_MSG_CHECKING([whether to enable lua plugin support])
	AC_ARG_WITH([lua],
		    AS_HELP_STRING(--with-lua,enable lua plugin support),
		    [ case "$withval" in
			yes|"") x_ac_lua=yes ;;
			 no) x_ac_lua=no ;;
			  *) AC_MSG_RESULT([doh!])
			     AC_MSG_ERROR([bad value "$withval" for --with-lua]) ;;
			esac
		    ],
		    [x_ac_lua=yes]
		   )
	if test "x$with_lua" = "xno"; then
		AC_MSG_RESULT([no])
		AC_MSG_NOTICE([lua support is disabled])
	else
		x_ac_lua_pkg_name="lua"
		# First check for lua named package, then check for 5.4, 5.3, 5.2 and 5.1.
		# Modern RHEL uses "lua' for the primary version.
		PKG_CHECK_EXISTS([lua], [x_ac_lua_pkg_name="lua >= 5.1"],
			[PKG_CHECK_EXISTS([lua5.4], [x_ac_lua_pkg_name=lua5.4],
			[PKG_CHECK_EXISTS([lua-5.4], [x_ac_lua_pkg_name=lua-5.4],
			[PKG_CHECK_EXISTS([lua5.3], [x_ac_lua_pkg_name=lua5.3],
			[PKG_CHECK_EXISTS([lua-5.3], [x_ac_lua_pkg_name=lua-5.3],
			[PKG_CHECK_EXISTS([lua5.2], [x_ac_lua_pkg_name=lua5.2],
			[PKG_CHECK_EXISTS([lua-5.2], [x_ac_lua_pkg_name=lua-5.2],
			[PKG_CHECK_EXISTS([lua5.1], [x_ac_lua_pkg_name=lua5.1],
			[PKG_CHECK_EXISTS([lua-5.1], [x_ac_lua_pkg_name=lua-5.1],
			[x_ac_lua_pkg_name="lua >= 5.1"])])])])])])])])])
		PKG_CHECK_MODULES([lua], ${x_ac_lua_pkg_name},
			[x_ac_have_lua="yes"],
			[x_ac_have_lua="no"])

		if test "x$x_ac_have_lua" = "xyes"; then
		  saved_CFLAGS="$CFLAGS"
		  saved_LIBS="$LIBS"
		  lua_CFLAGS="$lua_CFLAGS"
		  CFLAGS="$CFLAGS $lua_CFLAGS"
		  LIBS="$LIBS $lua_LIBS"

		  AC_MSG_CHECKING([for whether we can link to liblua])
		  AC_LINK_IFELSE(
			  [AC_LANG_PROGRAM(
				   [[
				     #include <lua.h>
				     #include <lauxlib.h>
				     #include <lualib.h>
				   ]],
				   [[
				     lua_State *L = luaL_newstate();
				     luaL_openlibs(L);
				   ]],
			   )],
			  [],
			  [x_ac_have_lua="no"])

		  AC_MSG_RESULT([$x_ac_have_lua $x_ac_lua_pkg_name])
		  if test "x$x_ac_have_lua" = "xno"; then
		    AS_IF([test -z "$with_lua"],
			  [AC_MSG_WARN([unable to link against lua libraries])],
			  [AC_MSG_ERROR([unable to link against lua libraries])])
		  else
		    AC_DEFINE(HAVE_LUA, 1, [Define to 1 if we have the Lua library])
		    # We can not define something here to determine version for systems
		    # that use just liblua we will not know what version we are using.
		    # Use LUA_VERSION_NUM as in lua.h it will always be right.
		  fi
		  CFLAGS="$saved_CFLAGS"
		  LIBS="$saved_LIBS"
		else
		  AS_IF([test -z "$with_lua"],
			[AC_MSG_WARN([unable to locate lua package])],
			[AC_MSG_ERROR([unable to locate lua package])])
		fi
	fi

	AM_CONDITIONAL(HAVE_LUA, test "x$x_ac_have_lua" = "xyes")
])
