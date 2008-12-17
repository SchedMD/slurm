##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_ENV_LOGIC
#
#  DESCRIPTION:
#    Test for how user's environment should be loaded for sbatch's 
#    --get-user-env option (as used by Moab)
##*****************************************************************************

AC_DEFUN([X_AC_ENV_LOGIC], [
  AC_MSG_CHECKING([whether sbatch --get-user-env option should load .login])
  AC_ARG_ENABLE(
    [env_with_login],
    AS_HELP_STRING(--disable_env_with_login,disable loading .login with sbatch --get-user-env option),
    [ case "$enableval" in
        yes) x_ac_env_with_login=yes ;;
         no) x_ac_env_with_login=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable_env_with_login]) ;;
      esac
    ],
    [x_ac_env_with_login=yes]
  )

  if test "$x_ac_env_with_login" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(LOAD_ENV_WITH_LOGIN,,
	      [define whether sbatch --get-user-env option should load .login])
  else
    AC_MSG_RESULT([no])
  fi
])

