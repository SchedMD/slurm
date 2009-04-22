##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_SUN_CONST
#
#  DESCRIPTION:
#    Test for Sun Constellation system with 3-D interconect
##*****************************************************************************

AC_DEFUN([X_AC_SUN_CONST], [
  AC_MSG_CHECKING([for Sun Constellation system])
  AC_ARG_ENABLE(
    [sun-const],
    AS_HELP_STRING(--enable-sun-const,enable Sun Constellation system support),
    [ case "$enableval" in
        yes) x_ac_sun_const=yes ;;
         no) x_ac_sun_const=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-sun-const]) ;;
      esac
    ],
    [x_ac_sun_const=no]
  )

  if test "$x_ac_sun_const" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
    AC_DEFINE(HAVE_SUN_CONST,1,[define if Sun Constellation system])
  else
    AC_MSG_RESULT([no])
  fi
])

