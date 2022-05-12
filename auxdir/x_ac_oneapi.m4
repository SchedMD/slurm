##*****************************************************************************
#  AUTHOR:
#    Kemp Ke <kemp.ke@intel.com>
#
#  SYNOPSIS:
#    X_AC_ONEAPI
#
#  DESCRIPTION:
#    Determine if Intel's oneAPI library exists
##*****************************************************************************

AC_DEFUN([X_AC_ONEAPI],
[
  AC_ARG_WITH(
    [oneapi],
    AS_HELP_STRING(--with-oneapi=yes/no, Build oneAPI-related code),
    []
  )

  if [test "x$with_oneapi" = xno]; then
     AC_MSG_WARN([support for oneapi disabled])
  else
    # /usr/include/level_zero is the main location. Others are just in case
    oneapi_includes="-I/usr/include/level_zero -I/lib/x86_64-linux-gnu -I/usr/lib64"
    # Check for oneAPI header and library in the default locations
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    CPPFLAGS="$oneapi_includes $CPPFLAGS"
    AC_CHECK_HEADER([ze_api.h], [ac_oneapi_h=yes], [ac_oneapi_h=no])
    AC_CHECK_LIB([ze_loader], [zeInit], [ac_oneapi=yes], [ac_oneapi=no])
    CPPFLAGS="$cppflags_save"
    if test "$ac_oneapi" = "yes" && test "$ac_oneapi_h" = "yes"; then
      ONEAPI_LIBS="-lze_loader"
      ONEAPI_CPPFLAGS="$oneapi_includes"
      AC_DEFINE(HAVE_ONEAPI, 1, [Define to 1 if oneAPI library found])
    else
      if test -z "$with_oneapi"; then
        AC_MSG_WARN([unable to locate libze_loader.so and/or ze_api.h])
      else
        AC_MSG_ERROR([unable to locate libze_loader.so and/or ze_api.h])
      fi
    fi
    AC_SUBST(ONEAPI_LIBS)
    AC_SUBST(ONEAPI_CPPFLAGS)
  fi
  AM_CONDITIONAL(BUILD_ONEAPI, test "$ac_oneapi" = "yes" && test "$ac_oneapi_h" = "yes")
])
