##*****************************************************************************
#  AUTHOR:
#    Nathan Rini <nate@schedmd.com>
#
#  SYNOPSIS:
#    Control compiling of slurmrestd
#
##*****************************************************************************


AC_DEFUN([X_AC_SLURMRESTD],
[
  dnl
  dnl Check if slurmrestd is requested and define BUILD_SLURMRESTD
  dnl if it is.
  dnl
  AC_MSG_CHECKING([whether to compile slurmrestd])
  AC_ARG_ENABLE(
    [slurmrestd],
    AS_HELP_STRING(--disable-slurmrestd,disable slurmrestd support),
    [ case "$enableval" in
        yes) x_ac_slurmrestd=yes ;;
         no) x_ac_slurmrestd=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-slurmrested])
      esac
    ]
  )

  if test "x$x_ac_slurmrestd" != "xno"; then
    if test -n "$x_ac_cv_http_parser_dir" && test -n "$x_ac_cv_json_dir"; then
      x_ac_slurmrestd=yes
    else
      if test "x$x_ac_slurmrestd" = "xyes"; then
        AC_MSG_ERROR([unable to build slurmrestd without http-parser and json libraries])
      else
        AC_MSG_WARN([unable to build slurmrestd without http-parser and json libraries])
        x_ac_slurmrestd=no
      fi
    fi
  fi

  AC_MSG_RESULT([$x_ac_slurmrestd])
  AM_CONDITIONAL(WITH_SLURMRESTD, test "x$x_ac_slurmrestd" = "xyes")

  AC_MSG_CHECKING(for slurmrestd default port)
  AC_ARG_WITH(
    [slurmrestd-port],
    AS_HELP_STRING(--with-slurmrestd-port=N,set slurmrestd default port [[6820]]),
    [
      if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
        slurmrestdport="$withval"
      fi
    ],
    [
      slurmrestdport=6820
    ]
  )
  AC_MSG_RESULT(${slurmrestdport=$1})
  AC_DEFINE_UNQUOTED(SLURMRESTD_PORT, [$slurmrestdport],
                     [Define the default port number for slurmrestd])
  AC_SUBST(SLURMRESTD_PORT, $slurmrestdport)
])
