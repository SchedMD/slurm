##*****************************************************************************
## $Id: config.m4 8863 2006-08-10 18:47:55Z da $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@llnl.gov>
#
#  DESCRIPTION:
#    Use to make the php slurm extension
##*****************************************************************************

AC_DEFINE(HAVE_SLURM, 1, [Whether you have SLURM])
PHP_NEW_EXTENSION(slurm, slurm_php.c, $ext_shared)

