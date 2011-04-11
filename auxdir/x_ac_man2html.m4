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
   AC_CHECK_PROG([MAN2HTML], [man2html], [yes], [$bindir:/usr/bin:/usr/local/bin])
   AM_CONDITIONAL(HAVE_MAN2HTML, test x"$MAN2HTML" == x"yes")
])
