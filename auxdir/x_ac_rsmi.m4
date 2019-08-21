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
  AC_ARG_WITH(
    [rsmi],
    AS_HELP_STRING(--without-rsmi, Do not build AMD RSMI-related code),
    []
  )

  if [test "x$with_rsmi" = xno]; then
     AC_MSG_WARN([support for rsmi disabled])
  else
    AC_MSG_CHECKING([whether RSMI/ROCm in installed in this system])
    _x_ac_rsmi_dir="/opt/rocm/rocm_smi"
    AC_ARG_WITH(
      [rsmi_dir],
      AS_HELP_STRING(--with-rsmi_dir=PATH,Specify path to ROCm installation - /opt/rocm/rocm_smi by default),
      [AS_IF([test "x$with_rsmi_dir" != xno],[_x_ac_rsmi_dir="$with_rsmi_dir"])])
    # /opt/rocm/rocm_smi/ is the default location. Check for RSMI header and library in the default location
    # or in the location specified during configure
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    ldflags_save="$LDFLAGS"
    CPPFLAGS="-I$_x_ac_rsmi_dir/include/rocm_smi $CPPFLAGS"
    LDFLAGS="-L$_x_ac_rsmi_dir/lib $LDFLAGS"
    AC_CHECK_HEADER([rocm_smi.h], [ac_rsmi_h=yes], [ac_rsmi_h=no])
    AC_CHECK_LIB([rocm_smi64], [rsmi_init], [ac_rsmi_l=yes], [ac_rsmi_l=no])
    AC_CHECK_LIB([rocm_smi64], [rsmi_dev_drm_render_minor_get], [ac_rsmi_version=yes], [ac_rsmi_version=no])
    CPPFLAGS="$cppflags_save"
    LDFLAGS="$ldflags_save"
    if test "$ac_rsmi_l" = "yes" && test "$ac_rsmi_h" = "yes"; then
      if test "$ac_rsmi_version" = "yes"; then
        RSMI_LDFLAGS="-L$_x_ac_rsmi_dir/lib"
        RSMI_LIBS="-lrocm_smi64"
        RSMI_CPPFLAGS="-I$_x_ac_rsmi_dir/include/rocm_smi"
        ac_rsmi="yes"
        AC_DEFINE(HAVE_RSMI, 1, [Define to 1 if RSMI library found])
      else
        AC_MSG_WARN([upgrade to newer version of ROCm/rsmi])
      fi
    else
      AC_MSG_WARN([unable to locate librocm_smi64.so and/or rocm_smi.h])
    fi
    AC_SUBST(RSMI_LIBS)
    AC_SUBST(RSMI_CPPFLAGS)
    AC_SUBST(RSMI_LDFLAGS)
  fi
  AM_CONDITIONAL(BUILD_RSMI, test "$ac_rsmi" = "yes")
])
