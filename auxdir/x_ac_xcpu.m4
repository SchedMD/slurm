##*****************************************************************************
## $Id: x_ac_xcpu.m4 7443 2006-03-08 20:23:25Z da $
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_XCPU
#
#  DESCRIPTION:
#    Test for XCPU specific files. 
#    If found define HAVE_XCPU and HAVE_FRONT_END.
#    Explicitly disable with --enable-xcpu=no
##*****************************************************************************


AC_DEFUN([X_AC_XCPU],
[
   AC_MSG_CHECKING([whether XCPU is enabled])
   AC_ARG_ENABLE([xcpu],
    AC_HELP_STRING([--enable-xcpu], [enable XCPU job launch]),
    [ case "$enableval" in
        yes) ac_xcpu=yes ;;
        no)  ac_xcpu=no ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-xcpu]) ;;
      esac
    ]
   )

   if test "$ac_xcpu" != "no" ; then
      if test -d "/mnt/xcpu" ; then
         AC_DEFINE(HAVE_XCPU, 1, [Define to 1 if using XCPU for job launch])
         AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
      else
         ac_xcpu=no
      fi
   fi
   AC_MSG_RESULT($ac_xcpu)
])
