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

# TODO: Check for the "CUDA_​DEVICE_​ORDER=PCI_BUS_ID" environmental var
# If that is not set, emit a warning and point to the documentation
# saying that this needs to be set for CUDA device numbers to match Slurm/NVML
# device numbers, and that after setting, a reboot is required?

# TODO: Check to make sure that nvidia driver is at least r384.40, or else there
# is weirdness with PCI bus id lookups. See https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html#group__nvmlDeviceQueries_1g9dc7be8cb41b6c77552c0fa0c36557c4

AC_DEFUN([X_AC_NVML],
[
  _x_ac_nvml_dirs="/usr /usr/local /usr/local/cuda"
  _x_ac_nvml_libs="lib64 lib"

  AC_ARG_WITH(
    [nvml],
    AS_HELP_STRING(--with-nvml=PATH,Specify path to nvml installation),
    [AS_IF([test "x$with_nvml" != xno],[_x_ac_nvml_dirs="$with_nvml $_x_ac_nvml_dirs"])])

  if [test "x$with_nvml" = xno]; then
     AC_MSG_WARN([support for nvml disabled])
  else
    AC_CACHE_CHECK(
      [for nvml installation],
      [x_ac_cv_nvml_dir],
      [
        for d in $_x_ac_nvml_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/nvml.h" || continue
          for bit in $_x_ac_nvml_libs; do
            test -d "$d/$bit" || continue
            test -d "$d/$bit/stubs" || continue
            _x_ac_nvml_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_nvml_libs_save="$LIBS"
            LIBS="-L$d/$bit/stubs -lnvidia-ml $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], nvmlInit)],
              AS_VAR_SET(x_ac_cv_nvml_dir, $d))
            CPPFLAGS="$_x_ac_nvml_cppflags_save"
            LIBS="$_x_ac_nvml_libs_save"
            test -n "$x_ac_cv_nvml_dir" && break
          done
          test -n "$x_ac_cv_nvml_dir" && break
        done
      ])

    if test -z "$x_ac_cv_nvml_dir"; then
      AC_MSG_WARN([unable to locate nvml installation])
    else
      NVML_CPPFLAGS="-I$x_ac_cv_nvml_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        NVML_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_nvml_dir/$bit -L$x_ac_cv_nvml_dir/$bit/stubs"
      else
        NVML_LDFLAGS="-L$x_ac_cv_nvml_dir/$bit/stubs"
      fi
      NVML_LIBS="-lnvidia-ml"
      AC_DEFINE(HAVE_NVML, 1, [Define to 1 if nvml library found])
    fi

    AC_SUBST(NVML_LIBS)
    AC_SUBST(NVML_CPPFLAGS)
    AC_SUBST(NVML_LDFLAGS)
  fi
])
