##*****************************************************************************
#  AUTHOR:
#    Jim Nordby <james.nordby@hpe.com>
#
#  SYNOPSIS:
#    X_AC_HPE_SLINGSHOT
#
#  DESCRIPTION:
#    Test for HPE Slingshot network systems (via libcxi.h header)
#*****************************************************************************
#
# Copyright 2021 Hewlett Packard Enterprise Development LP
#

AC_DEFUN([X_AC_HPE_SLINGSHOT],
[

  # /usr is the current default location.
  # We will use a for loop to check for any.
  # Unless _x_ac_hpe_ss_dirs is overwritten with --with-hpe-slingshot
  _x_ac_hpe_ss_dirs="/usr"

  AC_ARG_WITH(
    [hpe-slingshot],
    AS_HELP_STRING(--with-hpe-slingshot=PATH, Specify path to HPE Slingshot installation dir),
    [AS_IF([test "x$with_hpe_slingshot" != xno && test "x$with_hpe_slingshot" != xyes],
           [_x_ac_hpe_ss_dirs="$with_hpe_slingshot"])])

  if [test "x$with_hpe_slingshot" = xno]; then
     AC_MSG_WARN([support for HPE Slingshot disabled])
  else
    AC_MSG_CHECKING([whether HPE Slingshot is installed in this system])
    # Check for HPE Slingshot header and library in the default location
    # or in the location specified during configure
    AC_MSG_RESULT([])
    for _x_ac_hpe_ss_dir in $_x_ac_hpe_ss_dirs; do
      cflags_save="$CFLAGS"
      HPE_SLINGSHOT_CFLAGS="-I$_x_ac_hpe_ss_dir/include"
      CFLAGS="$HPE_SLINGSHOT_CFLAGS"
      AC_CHECK_HEADER([libcxi/libcxi.h], [ac_hpe_ss_h=yes], [ac_hpe_ss_h=no])

      # We only care about the headers here.
      # This plugin is designed to work without the lib.

      # ldflags_save="$LDFLAGS"
      # HPE_SLINGSHOT_LIBS="-L$_x_ac_hpe_ss_dir/lib64"
      # LDFLAGS="$HPE_SLINGSHOT_LIBS"
      # AC_CHECK_LIB([cxi], [cxil_get_device_list], [ac_hpe_ss_l=yes], [ac_hpe_ss_l=no])
      # LDFLAGS="$ldflags_save"

      if test "$ac_hpe_ss_h" = "yes"; then
        ac_hpe_ss="yes"
        AC_CHECK_TYPES([struct cxi_rsrc_use], [], [],
                       [
			#include <string.h>
                        #include <stddef.h>
                        #include <sys/types.h>
                        #include <libcxi/libcxi.h>
                       ])
	AC_SUBST(HPE_SLINGSHOT_CFLAGS)
	AC_DEFINE_UNQUOTED(HPE_SLINGSHOT_LIB, "$_x_ac_hpe_ss_dir/lib64/libcxi.so", [Full path of libcxi.so])
	CFLAGS="$cflags_save"
	break;
      fi
      CFLAGS="$cflags_save"

    done

    AM_CONDITIONAL(WITH_SWITCH_HPE_SLINGSHOT, test x$ac_hpe_ss = xyes)
    if test -z "$ac_hpe_ss"; then
      if test -z "$with_hpe_slingshot"; then
        AC_MSG_WARN([HPE Slingshot: unable to locate libcxi/libcxi.h])
      else
	AC_MSG_ERROR([HPE Slingshot: unable to locate libcxi/libcxi.h])
      fi
    fi
  fi
])
