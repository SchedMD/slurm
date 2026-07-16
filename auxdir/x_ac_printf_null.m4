##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_PRINTF_NULL
#
#  DESCRIPTION:
#    Test that printf("%s\n", NULL); does not result in invalid memory 
#    reference. This is a known issue in Open Solaris version 118 and  
#    some other operating systems. The potential for this problem exists 
#    in hundreds of places in the SLURM code, so the ideal place to 
#    address it is in the underlying print functions.
##*****************************************************************************

AC_DEFUN([X_AC_PRINTF_NULL], [
  AC_MSG_CHECKING([for support of printf("%s", NULL)])
  AC_RUN_IFELSE([AC_LANG_PROGRAM([
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
        char *n=NULL;],
	[[
	char tmp[16];
	char *expected = "test (null)";
	snprintf(tmp,sizeof(tmp),"test %s",n);
	if (strncmp(tmp, expected, sizeof(tmp)))
		exit(1);
	exit(0); ]])],
    printf_null_ok=yes,
    printf_null_ok=no,
    printf_null_ok=yes)

  if test "$printf_null_ok" = "no"; then
    AC_MSG_ERROR([printf("%s", NULL) results in abort])
  else
    AC_MSG_RESULT([yes])
  fi
])

