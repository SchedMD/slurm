##*****************************************************************************
## $Id: config.m4 8863 2006-08-10 18:47:55Z da $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@llnl.gov>
#
#  DESCRIPTION:
#    Use to make the php slurm extension
##*****************************************************************************
PHP_ARG_ENABLE(slurm, whether to enable SLURM support,
[ --enable-slurm   Enable SLURM support])




#if test "$PHP_SLURM" = "yes"; then
AC_DEFINE(HAVE_SLURM_PHP, 1, [Whether you have SLURM])
PHP_NEW_EXTENSION(slurm_php, slurm_php.c, $ext_shared)
#fi
