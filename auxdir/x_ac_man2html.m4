##*****************************************************************************
#  AUTHOR:
#    Don Lipari <lipari1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_MAN2HTML
#
#  DESCRIPTION:
#    Test for the presence of the man2html command.
#
##*****************************************************************************

AC_DEFUN([X_AC_MAN2HTML],
[
   AC_MSG_CHECKING([whether man2html is available])
   AC_CHECK_PROG(ac_have_man2html, man2html, [yes], [no], [$bindir:/usr/bin:/usr/local/bin])

   AM_CONDITIONAL(HAVE_MAN2HTML, test "x$ac_have_man2html" = "xyes")

   if test "x$ac_have_man2html" != "xyes" ; then
      AC_MSG_WARN([unable to build man page html files without man2html])
   fi
])
