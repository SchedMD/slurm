##*****************************************************************************
#  AUTHOR:
#    Tim McMullan <mcmullan@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_PKGCONFIG
#
#  DESCRIPTION:
#    Check if installing pkg-config file is desired.
##*****************************************************************************

AC_DEFUN([X_AC_PKGCONFIG],
[
  PKG_PROG_PKG_CONFIG([0.9.0])
  PKG_INSTALLDIR()

  AC_MSG_CHECKING([whether to install pkg-config slurm.pc file])
  AC_ARG_ENABLE(
    [pkgconfig],
    AS_HELP_STRING([--enable-pkgconfig],
                   [Install the slurm.pc file]),
    [ case "$enableval" in
        yes) x_ac_pkgconfig_file=yes ;;
         no) x_ac_pkg_config_file=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-pkgconfig]) ;;
      esac
    ],
    [x_ac_pkgconfig_file=no]
  )
  AC_MSG_RESULT([${x_ac_pkgconfig_file}])

  AM_CONDITIONAL(WITH_PKG_CONFIG, test "x${x_ac_pkgconfig_file}" = "xyes")
])
