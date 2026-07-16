##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_PTRACE
#
#  DESCRIPTION:
#    Test argument count of ptrace function.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_PTRACE], [
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/reg.h>
    #include <sys/ptrace.h>
    #include <sys/ldr.h>]], [[ptrace(PT_TRACE_ME,0,0,0,0);]])],[AC_DEFINE(PTRACE_FIVE_ARGS, 1,
             [Define to 1 if ptrace takes five arguments.])],[])

  AC_CHECK_FUNCS(ptrace64, [], []) 
])

