##*****************************************************************************
## $Id: x_ac_aix.m4 8192 2006-05-25 00:15:05Z morrone $
##*****************************************************************************
#  AUTHOR:
#    Mark Grondona <mgrondona@llnl.gov>
#
#  SYNOPSIS:
#    AC_SGI_JOB
#
#  DESCRIPTION:
#    Check for presence of SGI job container support via libjob.so
##*****************************************************************************


AC_DEFUN([X_AC_SGI_JOB],
[
   AC_CHECK_LIB([job], [job_attachpid], [ac_have_sgi_job="yes"], [])
   AC_MSG_CHECKING([for SGI job container support])
   AC_MSG_RESULT([${ac_have_sgi_job=no}])
   AM_CONDITIONAL(HAVE_SGI_JOB, test "x$ac_have_sgi_job" = "xyes")
])
