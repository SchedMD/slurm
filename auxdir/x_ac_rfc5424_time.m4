##*****************************************************************************
#  AUTHOR:
#    Janne Blomqvist <janne.blomqvist@aalto.fi>
#
#  SYNOPSIS:
#    X_AC_RFC5424_TIME
#
#  DESCRIPTION:
#    Test for RFC 5424 compliant time support.
#    Test for time stamp resolution to the millisecond (default) or second
##*****************************************************************************

AC_DEFUN([X_AC_RFC5424_TIME], [
  AC_MSG_CHECKING([whether to enable RFC 5424 time format support])
  AC_ARG_ENABLE(
    [rfc5424time],
    AS_HELP_STRING(--enable-rfc5424time, enable RFC 5424 time format support),
    [ case "$enableval" in
        yes) x_ac_rfc5424time=yes ;;
         no) x_ac_rfc5424time=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-rfc5424time]) ;;
      esac
    ],
    [x_ac_rfc5424time=no]
  )

  if test "$x_ac_rfc5424time" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(USE_RFC5424_TIME,,[define if using RFC 5424 time format])
  else
    AC_MSG_RESULT([no])
  fi

  AC_MSG_CHECKING([log timestamps to millisecond resolution])
  AC_ARG_ENABLE(
    [log-time-msec],
    AS_HELP_STRING(--disable-log-time-msec, log timestamps to millisecond resolution),
    [ case "$enableval" in
        yes) x_ac_log_time_msec=yes ;;
         no) x_ac_log_time_msec=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --disable-log-time-msec]) ;;
      esac
    ],
    [x_ac_log_time_msec=yes]
  )

  if test "$x_ac_log_time_msec" = yes; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(LOG_TIME_MSEC,,[log timestamps to millisecond resolution])
  else
    AC_MSG_RESULT([no])
  fi
])

