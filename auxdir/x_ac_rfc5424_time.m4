##*****************************************************************************
#  AUTHOR:
#    Janne Blomqvist <janne.blomqvist@aalto.fi>
#
#  SYNOPSIS:
#    X_AC_RFC5424_TIME
#
#  DESCRIPTION:
#    Test for RFC 5424 compliant time support.
##*****************************************************************************

AC_DEFUN([X_AC_RFC5424_TIME], [
  AC_MSG_CHECKING([whether to enable RFC 5424 time format support])
  AC_ARG_ENABLE(
    [rfc5424time],
    AS_HELP_STRING(--disable-rfc5424time, disable RFC 5424 time format support),
    [ case "$enableval" in
        yes) x_ac_rfc5424time=yes ;;
         no) x_ac_rfc5424time=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-rfc5424time]) ;;
      esac
    ],
    [x_ac_rfc5424time=yes]
  )

  if test "$x_ac_rfc5424time" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(USE_RFC5424_TIME,,[define if using RFC 5424 time format])
  else
    AC_MSG_RESULT([no])
  fi
])

