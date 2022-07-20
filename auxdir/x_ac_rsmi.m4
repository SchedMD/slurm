##*****************************************************************************
#  AUTHOR:
#    Advanced Micro Devices
#
#  SYNOPSIS:
#    X_AC_RSMI
#
#  DESCRIPTION:
#    Determine if AMD's RSMI API library exists
##*****************************************************************************

AC_DEFUN([X_AC_RSMI],
[

  # /opt/rocm is the current default location.
  # /opt/rocm/rocm_smi was the default location for before to 5.2.0
  # We will use a for loop to check for both.
  # Unless _x_ac_rsmi_dirs is overwritten with --with-rsmi
  declare -a _x_ac_rsmi_dirs=("/opt/rocm" "/opt/rocm/rocm_smi")

  AC_ARG_WITH(
    [rsmi],
    AS_HELP_STRING(--with-rsmi=PATH, Specify path to rsmi installation),
    [AS_IF([test "x$with_rsmi" != xno && test "x$with_rsmi" != xyes],
           [_x_ac_rsmi_dirs=("$with_rsmi")])])

  if [test "x$with_rsmi" = xno]; then
     AC_MSG_WARN([support for rsmi disabled])
  else
    AC_MSG_CHECKING([whether RSMI/ROCm in installed in this system])
    # Check for RSMI header and library in the default location
    # or in the location specified during configure
    AC_MSG_RESULT([])
    for _x_ac_rsmi_dir in "${_x_ac_rsmi_dirs@<:@@@:>@}"; do
      cppflags_save="$CPPFLAGS"
      ldflags_save="$LDFLAGS"
      CPPFLAGS="-I$_x_ac_rsmi_dir/include $CPPFLAGS"
      LDFLAGS="-L$_x_ac_rsmi_dir/lib $LDFLAGS"
      AC_CHECK_HEADER([rocm_smi/rocm_smi.h], [ac_rsmi_h=yes], [ac_rsmi_h=no])
      AC_CHECK_LIB([rocm_smi64], [rsmi_init], [ac_rsmi_l=yes], [ac_rsmi_l=no])
      AC_CHECK_LIB([rocm_smi64], [rsmi_dev_drm_render_minor_get], [ac_rsmi_version=yes], [ac_rsmi_version=no])
      CPPFLAGS="$cppflags_save"
      LDFLAGS="$ldflags_save"
      if test "$ac_rsmi_l" = "yes" && test "$ac_rsmi_h" = "yes"; then
        if test "$ac_rsmi_version" = "yes"; then
          RSMI_LDFLAGS="-L$_x_ac_rsmi_dir/lib"
          RSMI_LIBS="-lrocm_smi64"
          RSMI_CPPFLAGS="-I$_x_ac_rsmi_dir/include"
          ac_rsmi="yes"
          AC_DEFINE(HAVE_RSMI, 1, [Define to 1 if RSMI library found])
          break;
        fi
      fi
    done

    # Only print errors/wanrings if both _x_ac_rsmi_dirs don't work
    if test "$ac_rsmi_l" = "yes" && test "$ac_rsmi_h" = "yes"; then
      if test "$ac_rsmi_version" != "yes"; then
        if test -z "$with_rsmi"; then
          AC_MSG_WARN([upgrade to newer version of ROCm/rsmi])
        else
          AC_MSG_ERROR([upgrade to newer version of ROCm/rsmi])
        fi
      fi
    else
      if test -z "$with_rsmi"; then
        AC_MSG_WARN([unable to locate librocm_smi64.so and/or rocm_smi.h])
      else
        AC_MSG_ERROR([unable to locate librocm_smi64.so and/or rocm_smi.h])
      fi
    fi
    AC_SUBST(RSMI_LIBS)
    AC_SUBST(RSMI_CPPFLAGS)
    AC_SUBST(RSMI_LDFLAGS)
  fi
  AM_CONDITIONAL(BUILD_RSMI, test "$ac_rsmi" = "yes")
])
