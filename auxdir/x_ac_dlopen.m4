##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_DLOPEN
#
#  DESCRIPTION:
#    Test how to link with dlopen. Update LIBS as needed. libtool.m4 seems to
#    handle the dlopen linking on some systems and not others.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_DLOPEN],
[
  x_ac_link_dlopen="yes"

  AC_TRY_LINK([#include <dlfcn.h>],
              [void * x = dlopen("/dev/null", 0);],
              [x_ac_link_dlopen="yes"], [])

  if test "$x_ac_link_dlopen" = "no"; then
    _x_ac_dlopen_libs_save="$LIBS"
    LIBS="-ldl $LIBS"
    AC_TRY_LINK([#include <dlfcn.h>],
                [void * x = dlopen("/dev/null", 0);],
                [x_ac_link_dlopen="yes"], [])
  fi

  if test "$x_ac_link_dlopen" = "no"; then
    AC_MSG_ERROR(["Unable to link with dlopen()"])
  fi
])
