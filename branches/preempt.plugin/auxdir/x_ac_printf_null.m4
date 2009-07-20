##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_PRINTF_NULL
#
#  DESCRIPTION:
#    Test that printf("%s\n", NULL); does not result in invalid memory 
#    reference. This is a known issue in Open Solaris version 115 and earlier 
#    plus a few other operating systems.
##*****************************************************************************

AC_DEFUN([X_AC_PRINTF_NULL], [
  AC_MSG_CHECKING([for support of printf("%s", NULL)])
  AC_TRY_RUN([
	#include <stdio.h>
	#include <stdlib.h>
	int main() { char tmp[8]; snprintf(tmp,8,"%s",NULL); exit(0); } ],
    printf_null_ok=yes,
    printf_null_ok=no,
    printf_null_ok=no)

  if test "$printf_null_ok" == "yes"; then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_ERROR([printf("%s", NULL) results in abort. If using OpenSolaris, upgrade to release 116 or higher.])
  fi
])

