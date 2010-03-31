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
	PKG_CHECK_EXISTS([lua5.1], [x_ac_lua_pkg_name=lua5.1], [])
	PKG_CHECK_MODULES([lua], ${x_ac_lua_pkg_name},
                [x_ac_have_lua="yes"],
                [x_ac_have_lua="no"])

	if test x"$x_ac_have_lua" = "xyes"; then
	  saved_CFLAGS="$CFLAGS"
	  saved_LDFLAGS="$LDFLAGS"
	  CFLAGS="$CFLAGS $lua_CFLAGS"
	  LDFLAGS="$LDFLAGS $lua_LIBS"
	  AC_MSG_CHECKING([for whether we can link to liblua])
	  AC_TRY_LINK(
		[#include <lua.h>
         #include <lauxlib.h>
		 #include <lualib.h>
		],
		[lua_State *L = luaL_newstate ();
		],
		[], [x_ac_have_lua="no"])

	  AC_MSG_RESULT([$x_ac_have_lua])
	  CFLAGS="$saved_CFLAGS"
	  LDFLAGS="$saved_LDFLAGS"
     fi

	AM_CONDITIONAL(HAVE_LUA, test "x$x_ac_have_lua" = "xyes")
])
