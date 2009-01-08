##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CRAY
#
#  DESCRIPTION:
#    Test for Cray systems including XT with 3-D interconect
#    Also test for the apbasil client (Cray's Batch Application Scheduler 
#    Interface Layer interface)
##*****************************************************************************

AC_DEFUN([X_AC_CRAY], [
  AC_MSG_CHECKING([for Cray XT])
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
    AC_MSG_RESULT([yes])
    AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
    AC_DEFINE(HAVE_CRAY,1,[Define if Cray system])
    AC_DEFINE(HAVE_CRAY_XT,1,[Define if Cray XT system])
    AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
  else
    AC_MSG_RESULT([no])
  fi

  AC_ARG_WITH(apbasil, AS_HELP_STRING(--with-apbasil=PATH,Specify path to apbasil command), [ try_apbasil=$withval ])
  apbasil_default_locs="/usr/apbasil"
  for apbasil_loc in $try_apbasil "" $apbasil_default_locs; do
    if test -z "$have_apbasil" -a -x "$apbasil_loc" ; then
      have_apbasil=$apbasil_loc
    fi
  done
  if test ! -z "$have_apbasil" ; then
    AC_DEFINE_UNQUOTED(APBASIL_LOC, "$have_apbasil", [Define the apbasil command location])
  fi
])

