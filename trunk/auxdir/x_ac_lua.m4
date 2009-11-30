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

   if test "x$x_ac_have_lua" = "xyes"; then
      x_ac_lua_saved_LIBS="$LIBS"
      AC_SEARCH_LIBS([luaL_newstate], [lua lua5.1],
                            [x_ac_have_lua="yes"],
                                        [x_ac_have_lua="no"])
      LIBS="$x_ac_lua_saved_LIBS"
   fi
   AM_CONDITIONAL(HAVE_LUA, test "x$x_ac_have_lua" = "xyes")
])
