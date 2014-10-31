##*****************************************************************************
## $Id$
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
	x_ac_lua_pkg_name="lua"
	#check for 5.2 if that fails check for 5.1
	PKG_CHECK_EXISTS([lua5.2], [x_ac_lua_pkg_name=lua5.2],
		[PKG_CHECK_EXISTS([lua5.1], [x_ac_lua_pkg_name=lua5.1], [])])
	PKG_CHECK_MODULES([lua], ${x_ac_lua_pkg_name},
                [x_ac_have_lua="yes"],
                [x_ac_have_lua="no"])

	if test "x$x_ac_have_lua" = "xyes"; then
	  saved_CFLAGS="$CFLAGS"
	  saved_LIBS="$LIBS"
	  # -DLUA_COMPAT_ALL is needed to support lua 5.2
	  lua_CFLAGS="$lua_CFLAGS -DLUA_COMPAT_ALL"
	  CFLAGS="$CFLAGS $lua_CFLAGS"
	  LIBS="$LIBS $lua_LIBS"
	  AC_MSG_CHECKING([for whether we can link to liblua])
	  AC_TRY_LINK(
		[#include <lua.h>
                 #include <lauxlib.h>
		 #include <lualib.h>
		],
		[lua_State *L = luaL_newstate (); luaL_openlibs(L);
		],
		[], [x_ac_have_lua="no"])

	  AC_MSG_RESULT([$x_ac_have_lua $x_ac_lua_pkg_name])
	  if test "x$x_ac_have_lua" = "xno"; then
	    AC_MSG_WARN([unable to link against lua libraries])
	  fi
	  CFLAGS="$saved_CFLAGS"
	  LIBS="$saved_LIBS"
	else
	  AC_MSG_WARN([unable to locate lua package])
	fi

	AM_CONDITIONAL(HAVE_LUA, test "x$x_ac_have_lua" = "xyes")
])
