##*****************************************************************************
#  AUTHOR:
#    Tim McMullan <mcmullan@schedmd.com>
#
#  SYNOPSIS:
#    Control compiling of sview
#
##*****************************************************************************

AC_DEFUN([X_AC_SVIEW],
[
  AC_MSG_NOTICE([checking whether to compile sview])
  AC_ARG_ENABLE(
    [sview],
    AS_HELP_STRING(--disable-sview,disable sview support),
    [ case "$enableval" in
        yes) x_ac_sview=yes ;;
         no) x_ac_sview=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-sview])
      esac
    ]
  )

  if test "x$x_ac_sview" == "xno"; then
    AC_MSG_NOTICE([sview is disabled])
  else
    # use the correct libs if running on 64bit
    if test -d "/usr/lib64/pkgconfig"; then
      PKG_CONFIG_PATH="/usr/lib64/pkgconfig/:$PKG_CONFIG_PATH"
    fi

    if test -d "/opt/gnome/lib64/pkgconfig"; then
      PKG_CONFIG_PATH="/opt/gnome/lib64/pkgconfig/:$PKG_CONFIG_PATH"
    fi

    AM_PATH_GLIB_2_0([2.7.1], [ac_glib_test="yes"], [ac_glib_test="no"],
                     [gthread])

    if test ${glib_config_minor_version=0} -ge 32 ; then
      AC_DEFINE([GLIB_NEW_THREADS], 1,
                [Define to 1 if using glib-2.32.0 or higher])
   fi

    AM_PATH_GTK_2_0([2.7.1], [ac_gtk_test="yes"], [ac_gtk_test="no"], [gthread])
    if test ${gtk_config_minor_version=0} -ge 10 ; then
      AC_DEFINE([GTK2_USE_RADIO_SET], 1,
                [Define to 1 if using gtk+-2.10.0 or higher])
    fi

    if test ${gtk_config_minor_version=0} -ge 12 ; then
      AC_DEFINE([GTK2_USE_TOOLTIP], 1,
                [Define to 1 if using gtk+-2.12.0 or higher])
    fi

    if test ${gtk_config_minor_version=0} -ge 14 ; then
      AC_DEFINE([GTK2_USE_GET_FOCUS], 1,
                [Define to 1 if using gtk+-2.14.0 or higher])
    fi

    if test "x$ac_glib_test" != "xyes" -o "x$ac_gtk_test" != "xyes"; then
      if test -z "$x_ac_sview"; then
        AC_MSG_WARN([cannot build sview without gtk library])
      else
        AC_MSG_ERROR([cannot build sview without gtk library])
      fi
    fi
  fi

  AM_CONDITIONAL(BUILD_SVIEW, [test "x$ac_glib_test" = "xyes"] && [test "x$ac_gtk_test" = "xyes"])
])
