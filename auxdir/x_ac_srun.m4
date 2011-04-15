##*****************************************************************************
## $Id: x_ac_srun.m4 17616 2009-05-27 21:24:58Z jette $
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette@schedmd.com>
#
#  SYNOPSIS:
#    AC_SRUN
#
#  DESCRIPTION:
#    Adds support for --with-srun2aprun. If set then build srun-aprun wrapper
#    rather than native SLURM srun.
##*****************************************************************************

AC_DEFUN([X_AC_SRUN2APRUN],
[
  ac_with_srun2aprun="no"

  AC_MSG_CHECKING([for whether to include srun-aprun wrapper rather than native SLURM srun])
  AC_ARG_WITH([srun2aprun],
    AS_HELP_STRING(--with-srun2aprun,use aprun wrapper instead of native SLURM srun command),
      [ case "$withval" in
        yes) ac_with_srun2aprun=yes ;;
        no)  ac_with_srun2aprun=no ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --with-srun2aprun]) ;;
      esac
    ]
  )

  AM_CONDITIONAL(BUILD_SRUN2APRUN, test "x$ac_with_srun2aprun" = "xyes")
])
