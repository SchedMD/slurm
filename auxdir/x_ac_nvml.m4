##*****************************************************************************
#  AUTHOR:
#    Michael Hinton <hinton@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_NVML
#
#  DESCRIPTION:
#    Determine if NVIDIA's NVML API library exists (CUDA provides stubs)
##*****************************************************************************


AC_DEFUN([X_AC_NVML],
[
  func_check_path ()
  {
      AC_CHECK_LIB([nvidia-ml], [nvmlInit], [ac_nvml=yes], [ac_nvml=no])

      if [ test "$ac_nvml" = "yes" ]; then
          # Check indirectly that CUDA 11.1+ was installed to see if we
	  # can build NVML MIG code. Do this by checking for the existence of
	  # gpuInstanceSliceCount in the nvmlDeviceAttributes_t struct.
	  AC_LINK_IFELSE(
	      [AC_LANG_PROGRAM(
		   [[
		     #include <nvml.h>
		   ]],
		   [[
		     nvmlDeviceAttributes_t attributes;
		     attributes.gpuInstanceSliceCount = 0;
		   ]],
	       )],
	      [ac_mig_support=yes],
	      [ac_mig_support=no])
      fi
  }

  _x_ac_nvml_dirs="/usr /usr/local/cuda /usr/cuda"
  _x_ac_nvml_lib_dirs="lib/x86_64-linux-gnu lib/stubs lib64/stubs"

  AC_ARG_WITH(
    [nvml],
    AS_HELP_STRING(--with-nvml=PATH, Specify path to NVML installation),
    [AS_IF([test "x$with_nvml" != xno && test "x$with_nvml" != xyes],
           [_x_ac_nvml_dirs="$with_nvml"])])

  if [test "x$with_nvml" = xno]; then
     AC_MSG_WARN([support for nvml disabled])
  else
    AC_MSG_CHECKING([whether NVML in installed in this system])
    # Check for nvml header and library in the default location
    # or in the location specified during configure
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    ldflags_save="$LDFLAGS"
    for _x_ac_nvml_dir in $_x_ac_nvml_dirs; do
      NVML_CPPFLAGS="-I$_x_ac_nvml_dir/include"
      CPPFLAGS="$NVML_CPPFLAGS"
      AC_CHECK_HEADER([nvml.h], [ac_nvml_h=yes], [ac_nvml_h=no])
      if test "$ac_nvml_h" = "no"; then
	continue
      fi
      for _x_ac_nvml_lib_dir in $_x_ac_nvml_lib_dirs; do
	NVML_LIB_DIR="$_x_ac_nvml_dir/$_x_ac_nvml_lib_dir"
	LDFLAGS="-L$NVML_LIB_DIR"

	func_check_path

        if test "$ac_nvml" = "yes"; then
          break;
        fi
      done
      if test "$ac_nvml" = "yes"; then
	break;
      fi
    done
  fi
  LDFLAGS="$ldflags_save"
  CPPFLAGS="$cppflags_save"

  AM_CONDITIONAL(BUILD_NVML, test "$ac_nvml" = "yes")

  if [ test "$ac_nvml" = "yes" ]; then
    NVML_CPPFLAGS="$nvml_includes"
    AC_DEFINE(HAVE_NVML, 1, [Define to 1 if NVML library found])
    AC_DEFINE_UNQUOTED(NVIDIA_NVML_LIB, "$NVML_LIB_DIR/libnvidia-ml.so", [Full path of libnvidia-ml.so])
    AC_SUBST(NVML_CPPFLAGS)

    if [ test "$ac_mig_support" = "yes" ]; then
      AC_DEFINE(HAVE_MIG_SUPPORT, 1, [Define to 1 if NVML library has MIG support])
    else
      AC_MSG_WARN([NVML was found, but can not support MIG. For MIG support both nvml.h and libnvidia-ml must be 11.1+. Please make sure they are both the same version as well.])
    fi
  else
    if test -z "$with_nvml"; then
      AC_MSG_WARN([unable to locate libnvidia-ml.so and/or nvml.h])
    else
      AC_MSG_ERROR([unable to locate libnvidia-ml.so and/or nvml.h])
    fi
  fi
])
