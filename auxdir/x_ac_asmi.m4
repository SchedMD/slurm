##*****************************************************************************
#  AUTHOR:
#    Advanced Micro Devices
#
#  SYNOPSIS:
#    X_AC_ASMI
#
#  DESCRIPTION:
#    Determine if AMD's AMD-SMI API library exists
#
#  NOTES:
#    - Always AC_SUBST() variables so @VAR@ never leaks into Makefiles
#    - BUILD_AMDSMI controls whether the asmi plugin is built
##*****************************************************************************

AC_DEFUN([X_AC_ASMI],
[
  dnl ------------------------------------------------------------------
  dnl Defaults — MUST always be substituted
  dnl ------------------------------------------------------------------
  ac_amdsmi=no
  AMDSMI_CPPFLAGS=""
  AMDSMI_LIBS=""

  AC_SUBST([AMDSMI_CPPFLAGS])
  AC_SUBST([AMDSMI_LIBS])

  dnl ------------------------------------------------------------------
  dnl Default search path
  dnl ------------------------------------------------------------------
  _x_ac_amdsmi_dirs="/opt/rocm"

  AC_ARG_WITH(
    [asmi],
    AS_HELP_STRING(
      [--with-asmi=PATH],
      [Specify path to AMD-SMI (amdsmi) installation]
    ),
    [
      AS_IF(
        [test "x$with_asmi" != xno && test "x$with_asmi" != xyes],
        [_x_ac_amdsmi_dirs="$with_asmi"]
      )
    ]
  )

  dnl ------------------------------------------------------------------
  dnl Handle --with-asmi=no explicitly
  dnl ------------------------------------------------------------------
  if test "x$with_asmi" = xno; then
    AC_MSG_NOTICE([support for AMD-SMI (asmi) disabled by user])
  else
    AC_MSG_CHECKING([for AMD-SMI (amdsmi) installation])
    AC_MSG_RESULT([])

    for _x_ac_amdsmi_dir in $_x_ac_amdsmi_dirs; do
      cppflags_save="$CPPFLAGS"
      ldflags_save="$LDFLAGS"

      AMDSMI_CPPFLAGS="-I$_x_ac_amdsmi_dir/include"
      CPPFLAGS="$CPPFLAGS $AMDSMI_CPPFLAGS"

      AMDSMI_LIB_DIR="$_x_ac_amdsmi_dir/lib"
      LDFLAGS="$LDFLAGS -L$AMDSMI_LIB_DIR"

      dnl Clear cached results so multiple paths work correctly
      AS_UNSET([ac_cv_header_amd_smi_amdsmi_h])
      AS_UNSET([ac_cv_lib_amd_smi_amdsmi_init])

      dnl --------------------------------------------------------------
      dnl Header check
      dnl --------------------------------------------------------------
      AC_CHECK_HEADER(
        [amd_smi/amdsmi.h],
        [ac_amdsmi_h=yes],
        [ac_amdsmi_h=no]
      )

      dnl --------------------------------------------------------------
      dnl Library + symbol check
      dnl --------------------------------------------------------------
      AC_CHECK_LIB(
        [amd_smi],
        [amdsmi_init],
        [ac_amdsmi_l=yes],
        [ac_amdsmi_l=no]
      )

      CPPFLAGS="$cppflags_save"
      LDFLAGS="$ldflags_save"

      if test "x$ac_amdsmi_h" = xyes && test "x$ac_amdsmi_l" = xyes; then
        ac_amdsmi=yes
        AMDSMI_LIBS="-L$AMDSMI_LIB_DIR -lamd_smi"

        AC_DEFINE(
          [HAVE_AMDSMI],
          [1],
          [Define to 1 if AMD-SMI (amdsmi) library is available]
        )

        break
      fi
    done

    dnl --------------------------------------------------------------
    dnl Diagnostics
    dnl --------------------------------------------------------------
    if test "x$ac_amdsmi" != xyes; then
      if test -z "$with_asmi" || test "x$with_asmi" = xyes; then
        AC_MSG_WARN(
          [AMD-SMI not found (libamd_smi.so and/or amd_smi/amdsmi.h missing)]
        )
      else
        AC_MSG_ERROR(
          [AMD-SMI not found in $with_asmi]
        )
      fi
    fi
  fi

  dnl ------------------------------------------------------------------
  dnl Automake conditional
  dnl ------------------------------------------------------------------
  AM_CONDITIONAL(
    [BUILD_AMDSMI],
    [test "x$ac_amdsmi" = xyes]
  )
])
