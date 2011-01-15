##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CRAY
#
#  DESCRIPTION:
#    Test for Cray systems including XT with 3-D interconnect
#    Also test for the apbasil client (Cray's Batch Application Scheduler 
#    Interface Layer interface)
##*****************************************************************************

AC_DEFUN([X_AC_CRAY], [
  AC_MSG_CHECKING([whether this is a native Cray XT or XE system])
  AC_ARG_ENABLE(
    [cray-xt],
    AS_HELP_STRING(--enable-cray-xt,enable Cray XT system support),
    [ case "$enableval" in
        yes) x_ac_cray_xt=yes ;;
         no) x_ac_cray_xt=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-cray-xt]) ;;
      esac
    ],
    [x_ac_cray_xt=no]
  )

  if test "$x_ac_cray_xt" = yes; then
    AC_DEFINE(HAVE_CRAY, 1, [Define to 1 for basic support of Cray XT/XE systems])
    # Check whether we are on a native Cray host:
    #  * older XT systems use an /etc/xtrelease file
    #  * newer XT/XE systems use an /etc/opt/cray/release/xtrelease file
    #  * both have an /etc/xthostname
    if test -f /etc/xtrelease  || test -d /etc/opt/cray/release ; then
      AC_DEFINE(HAVE_NATIVE_CRAY,  1, [Define to 1 for native Cray XT/XE system])
      AC_DEFINE(HAVE_3D,           1, [Define to 1 if 3-dimensional architecture])
      AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
      AC_DEFINE(HAVE_FRONT_END,    1, [Define to 1 if running slurmd on front-end only])
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
  else
    AC_MSG_RESULT([no])
  fi
])

