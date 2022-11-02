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

  _x_ac_oneapi_dirs="/usr"
  _x_ac_oneapi_lib_dirs="x86_64-linux-gnu lib64"

  AC_ARG_WITH(
    [oneapi],
    AS_HELP_STRING(--with-oneapi=PATH, Specify path to oneAPI installation),
    [AS_IF([test "x$with_oneapi" != xno && test "x$with_oneapi" != xyes],
           [_x_ac_oneapi_dirs="$with_oneapi"])])

  if [test "x$with_oneapi" = xno]; then
     AC_MSG_WARN([support for oneapi disabled])
  else
    AC_MSG_CHECKING([whether oneAPI in installed in this system])
    # Check for oneAPI header and library in the default location
    # or in the location specified during configure
    #
    # NOTE: Just because this is where we are looking and finding the
    # libraries they must be in the ldcache when running as that is what the
    # card will be using.
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    ldflags_save="$LDFLAGS"
    for _x_ac_oneapi_dir in $_x_ac_oneapi_dirs; do
      ONEAPI_CPPFLAGS="-I$_x_ac_oneapi_dir/include/level_zero"
      CPPFLAGS="$ONEAPI_CPPFLAGS"
      AS_UNSET([ac_cv_header_ze_api_h])
      AC_CHECK_HEADER([ze_api.h], [ac_oneapi_h=yes], [ac_oneapi_h=no])
      if test "$ac_oneapi_h" = "no"; then
	continue
      fi
      for _x_ac_oneapi_lib_dir in $_x_ac_oneapi_lib_dirs; do
	ONEAPI_LIB_DIR="$_x_ac_oneapi_dir/$_x_ac_oneapi_lib_dir"
	LDFLAGS="-L$ONEAPI_LIB_DIR"
	AS_UNSET([ac_cv_lib_ze_loader_zeInit])
	AC_CHECK_LIB([ze_loader], [zeInit], [ac_oneapi=yes], [ac_oneapi=no])
        if test "$ac_oneapi" = "yes"; then
          AC_DEFINE(HAVE_ONEAPI, 1, [Define to 1 if oneAPI library found])
	  AC_SUBST(ONEAPI_CPPFLAGS)
          break;
        fi
      done
      if test "$ac_oneapi" = "yes"; then
	break;
      fi
    done

    CPPFLAGS="$cppflags_save"
    LDFLAGS="$ldflags_save"

    if test "$ac_oneapi" != "yes"; then
      if test -z "$with_oneapi"; then
	AC_MSG_WARN([unable to locate libze_loader.so and/or ze_api.h])
      else
        AC_MSG_ERROR([unable to locate libze_loader.so and/or ze_api.h])
      fi
    fi
  fi
  AM_CONDITIONAL(BUILD_ONEAPI, test "$ac_oneapi" = "yes")
])
