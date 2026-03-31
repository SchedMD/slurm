##*****************************************************************************
#  AUTHOR:
#    Advanced Micro Devices
#
#  SYNOPSIS:
#    X_AC_ASMI
#
#  DESCRIPTION:
#    Determine if AMD's AMD-SMI API library exists
##*****************************************************************************

AC_DEFUN([X_AC_ASMI],
[

  dnl /opt/rocm is the current default location.
  dnl Unless _x_ac_asmi_dirs is overwritten with --with-asmi
  _x_ac_asmi_dirs="/opt/rocm"

  AC_ARG_WITH(
    [asmi],
    AS_HELP_STRING([--with-asmi=PATH], [Specify path to AMD-SMI (amdsmi) installation]),
    [AS_IF([test "x$with_asmi" != xno && test "x$with_asmi" != xyes],
           [_x_ac_asmi_dirs="$with_asmi"])]
  )

  if test "x$with_asmi" = xno; then
    AC_MSG_NOTICE([support for AMD-SMI (asmi) disabled])
  else
    AC_MSG_CHECKING([whether AMD-SMI is installed in this system])
    dnl Check for AMD-SMI header and library in the default location
    dnl or in the location specified during configure
    dnl
    dnl NOTE: Even if we find the libraries here, they must be in the
    dnl runtime library path / ldconfig cache for the plugin to work.
    AC_MSG_RESULT([])

    ac_asmi=no
    ac_asmi_h=no
    ac_asmi_l=no

    for _x_ac_asmi_dir in $_x_ac_asmi_dirs; do
      cppflags_save="$CPPFLAGS"
      ldflags_save="$LDFLAGS"

      AMDSMI_CPPFLAGS="-I$_x_ac_asmi_dir/include"
      CPPFLAGS="$CPPFLAGS $AMDSMI_CPPFLAGS"

      AMDSMI_LIB_DIR="$_x_ac_asmi_dir/lib"
      LDFLAGS="$LDFLAGS -L$AMDSMI_LIB_DIR"

      AS_UNSET([ac_cv_header_amd_smi_amdsmi_h])
      AS_UNSET([ac_cv_lib_amd_smi_amdsmi_init])

      dnl Header: amd_smi/amdsmi.h
      AC_CHECK_HEADER([amd_smi/amdsmi.h],
                      [ac_asmi_h=yes],
                      [ac_asmi_h=no])

      dnl Library: libamd_smi.so, symbol: amdsmi_init
      AC_CHECK_LIB([amd_smi],
                   [amdsmi_init],
                   [ac_asmi_l=yes],
                   [ac_asmi_l=no])

      CPPFLAGS="$cppflags_save"
      LDFLAGS="$ldflags_save"

      if test "x$ac_asmi_l" = xyes && test "x$ac_asmi_h" = xyes; then
        ac_asmi=yes
        AC_DEFINE([HAVE_ASMI], [1],
                  [Define to 1 if AMD-SMI library found])
        AC_SUBST([AMDSMI_CPPFLAGS])
        AMDSMI_LIBS="-L$AMDSMI_LIB_DIR -lamd_smi"
        AC_SUBST([AMDSMI_LIBS])
        break
      fi
    done

    dnl Only print errors/warnings if _x_ac_asmi_dirs don't work
    if test "x$ac_asmi" != xyes; then
      if test -z "$with_asmi" || test "x$with_asmi" = xyes; then
        AC_MSG_WARN([unable to locate libamd_smi.so and/or amd_smi/amdsmi.h])
      else
        AC_MSG_ERROR([unable to locate libamd_smi.so and/or amd_smi/amdsmi.h in $with_asmi])
      fi
    fi
  fi

  AM_CONDITIONAL([BUILD_ASMI], [test "x$ac_asmi" = xyes])
])