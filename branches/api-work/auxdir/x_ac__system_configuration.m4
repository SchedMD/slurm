##*****************************************************************************
#  $Id$
##*****************************************************************************
#  AUTHOR:
#    Moe Jette <jette@llnl.gov>
#
#  SYNOPSIS:
#    X_AC__SYSTEM_CONFIGURATION
#
#  DESCRIPTION:
#    Tests for existence of the _system_configuration structure.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC__SYSTEM_CONFIGURATION], [
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/systemcfg.h>]], [[double x = _system_configuration.physmem;]])],[AC_DEFINE(HAVE__SYSTEM_CONFIGURATION, 1,
             [Define to 1 if you have the external variable,
              _system_configuration with a member named physmem.])],[])
])

