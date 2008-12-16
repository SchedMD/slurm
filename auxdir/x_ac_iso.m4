##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_ISO
#
#  DESCRIPTION:
#    Test for ISO compliant time support.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_ISO], [
  AC_MSG_CHECKING([whether to enable ISO 8601 time format support])
  AC_ARG_ENABLE(
    [iso8601],
    AS_HELP_STRING(--disable-iso8601,disable ISO 8601 time format support),
    [ case "$enableval" in
        yes) x_ac_iso8601=yes ;;
         no) x_ac_iso8601=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-iso8601]) ;;
      esac
    ],
    [x_ac_iso8601=yes]
  )

  if test "$x_ac_iso8601" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(USE_ISO_8601,,[define if using ISO 8601 time format])
  else
    AC_MSG_RESULT([no])
  fi
])

