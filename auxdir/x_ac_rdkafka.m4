##*****************************************************************************
#  AUTHOR:
#   Alejandro Sanchez <alex@schedmd.com>
#
#  SYNOPSIS:
#   X_AC_RDKAFKA
#
#  DESCRIPTION:
#   Determine if librdkafka development files are available
##*****************************************************************************

AC_DEFUN([X_AC_RDKAFKA],
[

  _x_ac_rdkafka_dirs="/usr"
  _x_ac_rdkafka_lib_dirs="lib/x86_64-linux-gnu lib"

  AC_ARG_WITH([rdkafka],
    [AS_HELP_STRING([--with-rdkafka=PATH],
      [Specify path to librdkafka installation])],
    [AS_IF([test "x$with_rdkafka" != xno && test "x$with_rdkafka" != xyes],
           [_x_ac_rdkafka_dirs="$with_rdkafka"])])

  if [test "x$with_rdkafka" = xno]; then
     AC_MSG_WARN([support for rdkafka disabled])
  else
    # Check for librdkafka header and library in the default location
    # or in the location specified during configure
    AC_CACHE_CHECK([for librdkafka installation], [_x_ac_cv_rdkafka_avail],
    [
      cppflags_save="$CPPFLAGS"
      ldflags_save="$LDFLAGS"

      for dir in $_x_ac_rdkafka_dirs; do
        RDKAFKA_CPPFLAGS="-I$dir/include"
        CPPFLAGS="$RDKAFKA_CPPFLAGS/librdkafka"
        AS_UNSET([ac_cv_header_rdkafka_h])
        AC_CHECK_HEADER([rdkafka.h], [], [continue])
        for lib_dir in $_x_ac_rdkafka_lib_dirs; do
          if test "$ac_with_rpath" = "yes"; then
            RDKAFKA_LDFLAGS="-Wl,-rpath -Wl,$dir/$lib_dir -L$dir/$lib_dir"
          else
            RDKAFKA_LDFLAGS="-L$dir/$lib_dir"
          fi
          LDFLAGS="$RDKAFKA_LDFLAGS"
          AS_UNSET([ac_cv_lib_rdkafka_rd_kafka_version])
          AC_CHECK_LIB([rdkafka], [rd_kafka_version],
            [_x_ac_cv_rdkafka_avail=yes],
            [_x_ac_cv_rdkafka_avail=no])
          if test "$_x_ac_cv_rdkafka_avail" = "yes"; then
            break 2;
          fi
        done
      done

      CPPFLAGS="$cppflags_save"
      LDFLAGS="$ldflags_save"
    ])

    if test "$_x_ac_cv_rdkafka_avail" != "yes"; then
      if test -z "$with_rdkafka"; then
        AC_MSG_WARN([unable to locate librdkafka.so and/or rdkafka.h])
      else
        AC_MSG_ERROR([unable to locate librdkafka.so and/or rdkafka.h])
      fi
    else
      AC_DEFINE(HAVE_RDKAFKA, 1, [Define to 1 if librdkafka library found])
      AC_SUBST(RDKAFKA_CPPFLAGS)
      RDKAFKA_LIBS="-lrdkafka"
      AC_SUBST(RDKAFKA_LIBS)
      AC_SUBST(RDKAFKA_LDFLAGS)
    fi
  fi

  AM_CONDITIONAL(WITH_RDKAFKA, test "$_x_ac_cv_rdkafka_avail" = "yes")
])
