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
    [load-env-no-login],
    AS_HELP_STRING(--enable-load-env-no-login,
                   [enable --get-user-env option to load user environment without .login]),
    [ case "$enableval" in
        yes) x_ac_load_env_no_login=yes ;;
         no) x_ac_load_env_no_login=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-load-env-no-login]) ;;
      esac
    ],
    [x_ac_load_env_no_login=no]
  )

  if test "$x_ac_load_env_no_login" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(LOAD_ENV_NO_LOGIN, 1,
              [Define to 1 for --get-user-env to load user environment without .login])
  else
    AC_MSG_RESULT([no])
  fi
])

