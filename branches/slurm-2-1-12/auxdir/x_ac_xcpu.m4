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
#    Test for XCPU job launch support. 
#    If found define HAVE_XCPU, XCPU_DIR and HAVE_FRONT_END.
#    Explicitly set path with --with-xcpu=PATH, defaults to "/mnt".
#
#  NOTES:
#    SLURM still has no way to signal XCPU spawned processes.
#    SLURM is not confirming that all processes have completed prior
#    to marking a job/node as COMPLETED. For that it needs to check 
#    for subdirectories (not files) under /mnt/xcpu/<host>/xcpu.
##*****************************************************************************


AC_DEFUN([X_AC_XCPU],
[
   AC_MSG_CHECKING([whether XCPU is enabled])

   xcpu_default_dirs="/mnt"

   AC_ARG_WITH([xcpu],
    AS_HELP_STRING(--with-xcpu=PATH,specify path to XCPU directory),
    [ try_path=$withval ]
   )

   ac_xcpu=no
   for xcpu_dir in $try_path "" $xcpu_default_dirs; do
      if test -d "$xcpu_dir/xcpu" ; then
         ac_xcpu=yes
         AC_DEFINE(HAVE_XCPU, 1, [Define to 1 if using XCPU for job launch])
         AC_DEFINE_UNQUOTED(XCPU_DIR, "$xcpu_dir/xcpu", [Define location of XCPU directory])
         AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
         break
      fi
   done

   AC_MSG_RESULT($ac_xcpu)
])
