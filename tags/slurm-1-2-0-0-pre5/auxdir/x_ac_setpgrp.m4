##*****************************************************************************
#  $Id: x_ac_setpgrp.m4 8192 2006-05-25 00:15:05Z morrone $
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_SETPGRP
#
#  DESCRIPTION:
#    Test argument count of setpgrp function.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_SETPGRP], [
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <unistd.h>]],
    [[setpgrp(0,0);]])],[AC_DEFINE(SETPGRP_TWO_ARGS, 1,
             [Define to 1 if setpgrp takes two arguments.])],[])
])

