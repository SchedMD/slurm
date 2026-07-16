##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_X11
#
#  DESCRIPTION:
#    Determine if Slurm's internal X11 forwarding should be enabled.
##*****************************************************************************
#
# Copyright 2019 SchedMD LLC. All rights reserved.
#

AC_DEFUN([X_AC_X11],
[
  AC_MSG_CHECKING([whether Slurm internal X11 support is enabled])
  AC_ARG_ENABLE(
    [x11],
    AS_HELP_STRING(--disable-x11, disable internal X11 support),
    [ case "$enableval" in
        yes) x_ac_x11=yes ;;
         no) x_ac_x11=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-x11])
      esac
    ]
  )
  AC_MSG_RESULT([$x_ac_x11])
  if test "$x_ac_x11" != no; then
    AC_DEFINE(WITH_SLURM_X11, 1, [Using internal Slurm X11 support])
  fi
])
