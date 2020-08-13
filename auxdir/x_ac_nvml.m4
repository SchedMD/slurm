##*****************************************************************************
#  AUTHOR:
#    Michael Hinton <hinton@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_NVML
#
#  DESCRIPTION:
#    Determine if NVIDIA's NVML API library exists (comes with CUDA)
##*****************************************************************************

AC_DEFUN([X_AC_NVML],
[
  AC_ARG_WITH(
    [nvml],
    AS_HELP_STRING(--without-nvml, Do not build NVIDIA NVML-related code),
    []
  )

  if [test "x$with_nvml" = xno]; then
     AC_MSG_WARN([support for nvml disabled])
  else
    # /usr/local/cuda/include is the main location. Others are just in case
    nvml_includes="-I/usr/local/cuda/include -I/usr/cuda/include"
    # Check for NVML header and library in the default locations
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    CPPFLAGS="$nvml_includes $CPPFLAGS"
    AC_CHECK_HEADER([nvml.h], [ac_nvml_h=yes], [ac_nvml_h=no])
    AC_CHECK_LIB([nvidia-ml], [nvmlInit], [ac_nvml=yes], [ac_nvml=no])
    CPPFLAGS="$cppflags_save"
    if test "$ac_nvml" = "yes" && test "$ac_nvml_h" = "yes"; then
      NVML_LIBS="-lnvidia-ml"
      NVML_CPPFLAGS="$nvml_includes"
      AC_DEFINE(HAVE_NVML, 1, [Define to 1 if NVML library found])
    else
      AC_MSG_WARN([unable to locate libnvidia-ml.so and/or nvml.h])
    fi
    AC_SUBST(NVML_LIBS)
    AC_SUBST(NVML_CPPFLAGS)
  fi
  AM_CONDITIONAL(BUILD_NVML, test "$ac_nvml" = "yes" && test "$ac_nvml_h" = "yes")
])
