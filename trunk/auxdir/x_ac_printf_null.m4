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
#
#    A good description of the problem can be found here: 
#       http://arc.opensolaris.org/caselog/PSARC/2008/403/20080625_darren.moffat
#
#    Here is an excerpt from that document:
#    "The current behavior of the printf(3C) family of functions in libc when
#     passed a NULL value for a string format is undefined and usually
#     results in a SEGV and crashed application.
#
#     The workaround to applications written to depend on this behavior is to
#     LD_PRELOAD=/usr/lib/0@0.so.1 (or the 64 bit equivalent).  The
#     workaround isn't always easy to apply (or it is too late data has been
#     lost or corrupted by that point)."
#
##*****************************************************************************

AC_DEFUN([X_AC_PRINTF_NULL], [
  AC_MSG_CHECKING([for support of printf("%s", NULL)])
  AC_TRY_RUN([
	#include <stdio.h>
	#include <stdlib.h>
	int main() { printf("%s", NULL); exit(0); } ],
    printf_null_ok=yes,
    printf_null_ok=no)

  if test "$printf_null_ok" == "no"; then
    AC_MSG_ERROR([printf("%s", NULL) results in abort. If using OpenSolaris, upgrade to release 119 or set LD_PRELOAD=/usr/lib/0@0.so.1.])
  fi
])

