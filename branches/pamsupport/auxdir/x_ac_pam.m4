##*****************************************************************************
#  $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_PAM
#
#  DESCRIPTION:
#    Test for PAM (Pluggable Authentication Module) support.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_PAM], [

  AC_CHECK_LIB([pam],
        [pam_get_user],
        [ac_have_pam=yes; PAM_LIBS="-lpam"])

  AC_CHECK_LIB([pam_misc],
        [misc_conv],
        [ac_have_pam_misc=yes; PAM_LIBS="$PAM_LIBS -lpam_misc"])
                                                                                                     
  AC_SUBST(PAM_LIBS)
  AM_CONDITIONAL(HAVE_PAM, test "x$ac_have_pam" = "xyes")
  if test "x$ac_have_pam" = "xyes" && "x$ac_have_pam_misc" = "xyes"; then
    AC_DEFINE(HAVE_PAM, 1, [define if you have the PAM library])
  else
    AC_MSG_WARN([Unable to locate PAM library])
  fi
])

