##*****************************************************************************
#  AUTHOR:
#    Tim Wickberg <tim@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_SELINUX
#
#  DESCRIPTION:
#    Determine if Slurm's internal SELinux support should be enabled.
##*****************************************************************************
#
# Copyright 2021 SchedMD LLC. All rights reserved.
#

AC_DEFUN([X_AC_SELINUX],
[
  AC_MSG_CHECKING([whether Slurm internal SELinux support is enabled])
  AC_ARG_ENABLE(
    [selinux],
    AS_HELP_STRING(--enable-selinux, enable internal SELinux support),
    [ case "$enableval" in
        yes) x_ac_selinux=yes ;;
         no) x_ac_selinux=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-selinux])
      esac
    ]
  )
  AC_MSG_RESULT([$x_ac_selinux])
  if test "$x_ac_selinux" = yes; then
    AC_DEFINE(WITH_SELINUX, 1, [Using internal Slurm SELinux support])
    PKG_CHECK_MODULES([libselinux], [libselinux], ,
      [AC_MSG_ERROR(cannot locate libselinux)])
  fi
])
