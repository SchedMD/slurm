##*****************************************************************************
#  AUTHOR:
#    Jim Nordby <james.nordby@hpe.com>
#
#  SYNOPSIS:
#    X_AC_HPE_SLINGSHOT
#
#  DESCRIPTION:
#    Test for HPE Slingshot network systems (via libcxi package)
#*****************************************************************************
#
# Copyright 2021 Hewlett Packard Enterprise Development LP
#

AC_DEFUN([X_AC_HPE_SLINGSHOT],
[
  PKG_CHECK_MODULES(
    [HPE_SLINGSHOT], [libcxi], [x_ac_have_libcxi="yes"], [x_ac_have_libcxi="no"]  )
  AM_CONDITIONAL(WITH_SWITCH_HPE_SLINGSHOT, test x$x_ac_have_libcxi = xyes)
  if test "x$x_ac_have_libcxi" = "xyes"; then
    saved_CPPFLAGS="$CPPFLAGS"
    saved_LIBS="$LIBS"
    CPPFLAGS="$HPE_SLINGSHOT_CPPFLAGS $saved_CPPFLAGS"
    LIBS="$HPE_SLINGSHOT_LDFLAGS $saved_LIBS"
  fi
])
