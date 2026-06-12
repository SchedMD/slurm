##*****************************************************************************
#  AUTHOR:
#    Tim Wickberg <tim@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_C99
#
#  DESCRIPTION:
#    Slurm requires C99 support. Some older GCC versions - such as the
#    defaults in RHEL6/RHEL7 - need CFLAGS="-std=gnu99" to handle certain
#    newer syntactic elements.
##*****************************************************************************

AC_DEFUN([X_AC_C99], [
  AC_MSG_CHECKING([for C99 support])
  AC_COMPILE_IFELSE(
    [
      AC_LANG_PROGRAM([[]],[[
        for (int i = 0; i < 10; i++)
          ;
      ]])
    ],
    [c99_ok=yes],
    [
      AX_CHECK_COMPILE_FLAG(
        [-std=gnu99],
        [c99_ok=yes && CFLAGS="$CFLAGS -std=gnu99"],
        [c99_ok=no])
    ]
  )
  if test "$c99_ok" = "no"; then
    AC_MSG_ERROR([cannot build C99 programs correctly])
  else
    AC_MSG_RESULT([yes])
  fi
])
