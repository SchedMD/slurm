##*****************************************************************************
#  $Id$
##*****************************************************************************
#  AUTHOR:
#    Mark Grondona <mgrondona@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_SETPROCTITLE
#
#  DESCRIPTION:
#    Check for setproctitle() system call or emulation.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************
dnl
dnl Perform checks related to setproctitle() emulation
dnl
AC_DEFUN([X_AC_SETPROCTITLE],
[
#
case "$host" in
*-*-aix*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_CLOBBER_ARGV)
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0')
     ;;
*-*-hpux*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_PSTAT)
     ;;
*-*-linux*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_CLOBBER_ARGV)
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0')
     ;;
*)
     AC_DEFINE(SETPROCTITLE_STRATEGY,PS_USE_NONE,
               [Define to the setproctitle() emulation type])
     AC_DEFINE(SETPROCTITLE_PS_PADDING, '\0',
               [Define if you need setproctitle padding])
     ;;
esac
])


